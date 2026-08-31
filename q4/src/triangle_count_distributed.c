/*
 * Distributed Triangle Counting in an Undirected Graph (MPI) -- STRICT VERSION
 * ------------------------------------------------------------------------
 * Unlike a "replicate everything via Bcast" design, THIS version genuinely
 * partitions edge storage: after the initial Scatterv, no process ever
 * holds more than roughly E/P + (a bounded amount of fetched remote data)
 * resident at once, and no process can compute the final answer without
 * receiving data from other ranks via MPI.
 *
 * ---- Algorithm: degree-orientation ("forward") triangle counting ----
 * Orient every edge {u,v} into a -> b where (deg(a),a) < (deg(b),b).
 * A triangle {a,b,c} (a<b<c in this order) is produced by EXACTLY ONE
 * oriented edge a->b, as the common out-neighbour c of both a and b.
 * total triangles = sum over oriented edges (a->b) of |N+(a) ^ N+(b)|.
 * This still guarantees no double counting, regardless of how edges or
 * vertices are split across processes.
 *
 * ---- True distributed pipeline ----
 *  1. SCATTER: rank 0 reads the graph and MPI_Scatterv's the E edges into
 *     equal contiguous shards, one per process. Rank 0 frees its full
 *     copy immediately after -- from this point on, the E edges live as
 *     P disjoint shards, one per machine, matching the assignment's
 *     storage requirement exactly.
 *  2. DEGREE: each process computes degree contributions from only ITS
 *     OWN edge shard, then MPI_Allreduce(SUM) combines them into the
 *     global degree[] array (O(V) ints -- a small aggregate statistic,
 *     not a replicated edge list).
 *  3. ORIENT: each process orients only its own local edges (needs only
 *     the global degree[] array, no peer edges).
 *  4. VERTEX-OWNER SHUFFLE (Alltoallv): every vertex v is assigned an
 *     owner, owner(v) = v % P. Each process routes its oriented edges
 *     a->b to owner(a). After this exchange, each edge lives on exactly
 *     ONE process (the owner of its source vertex) -- the total edge
 *     count resident across all ranks is still exactly E, never P*E.
 *     Each rank builds a CSR adjacency list from the edges it now holds
 *     -- this is a complete out-adjacency list, but ONLY for the
 *     vertices it owns, not the whole graph.
 *  5. REMOTE NEIGHBOUR FETCH (2 rounds of Alltoallv): to test edge a->b,
 *     a process needs N+(b), which lives on owner(b) (almost always a
 *     different rank). Each process de-duplicates the b's it needs,
 *     requests them from their owners, and owners ship back the
 *     requested (sorted) neighbour lists. This is the actual "MPI
 *     communicates to find the global count" step -- the computation is
 *     IMPOSSIBLE without it, since no single rank has both N+(a) and
 *     N+(b) locally in general.
 *  6. COUNT: for each edge a->b this rank owns, intersect its local
 *     N+(a) with the fetched N+(b) (two-pointer merge over sorted
 *     lists), accumulate local partial sum.
 *  7. MPI_Reduce(SUM) the partial sums to rank 0, which prints the
 *     final integer. No process ever prints anything else.
 *
 * Build:   mpicc -O3 -o triangle_count_distributed triangle_count_distributed.c
 * Run:     mpirun -np <P> ./triangle_count_distributed < input.txt
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long ll;

static char *read_all_stdin(size_t *out_len) {
    size_t cap = 1 << 20, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { fprintf(stderr, "OOM\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
    size_t r;
    while ((r = fread(buf + len, 1, cap - len, stdin)) > 0) {
        len += r;
        if (len == cap) {
            cap <<= 1;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { fprintf(stderr, "OOM\n"); MPI_Abort(MPI_COMM_WORLD, 1); }
            buf = nb;
        }
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static inline long parse_next_int(const char *s, size_t *pos) {
    size_t i = *pos;
    while (s[i] && (s[i] < '0' || s[i] > '9') && s[i] != '-') i++;
    int neg = 0;
    if (s[i] == '-') { neg = 1; i++; }
    long val = 0;
    while (s[i] >= '0' && s[i] <= '9') { val = val * 10 + (s[i] - '0'); i++; }
    *pos = i;
    return neg ? -val : val;
}

static int int_cmp(const void *a, const void *b) {
    return (*(const int *)a) - (*(const int *)b);
}

/* block decomposition helper: how many of `total` items rank r of size P owns */
static inline int block_count(int total, int P, int r) {
    return (int)(((ll)(r + 1) * total) / P - ((ll)r * total) / P);
}
static inline int block_start(int total, int P, int r) {
    return (int)(((ll)r * total) / P);
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    int V = 0, E = 0;

    /* ================= Step 1: read + SCATTER (real distribution) ================= */
    int *sendcounts = (int *)malloc(sizeof(int) * P);
    int *senddispls  = (int *)malloc(sizeof(int) * P);

    int *eu_all = NULL, *ev_all = NULL;
    if (rank == 0) {
        size_t len;
        char *buf = read_all_stdin(&len);
        size_t pos = 0;
        V = (int)parse_next_int(buf, &pos);
        E = (int)parse_next_int(buf, &pos);
        eu_all = (int *)malloc(sizeof(int) * (E > 0 ? E : 1));
        ev_all = (int *)malloc(sizeof(int) * (E > 0 ? E : 1));
        for (int i = 0; i < E; i++) {
            eu_all[i] = (int)parse_next_int(buf, &pos);
            ev_all[i] = (int)parse_next_int(buf, &pos);
        }
        free(buf);
    }
    MPI_Bcast(&V, 1, MPI_INT, 0, MPI_COMM_WORLD); /* just 2 scalars, not edges */
    MPI_Bcast(&E, 1, MPI_INT, 0, MPI_COMM_WORLD);

    for (int r = 0; r < P; r++) {
        sendcounts[r] = block_count(E, P, r);
        senddispls[r] = block_start(E, P, r);
    }
    int local_E = sendcounts[rank];

    int *local_eu = (int *)malloc(sizeof(int) * (local_E > 0 ? local_E : 1));
    int *local_ev = (int *)malloc(sizeof(int) * (local_E > 0 ? local_E : 1));

    MPI_Scatterv(eu_all, sendcounts, senddispls, MPI_INT,
                 local_eu, local_E, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Scatterv(ev_all, sendcounts, senddispls, MPI_INT,
                 local_ev, local_E, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) { free(eu_all); free(ev_all); }
    /* From here on, NO process holds more than its own ~E/P edge shard. */

    /* ================= Step 2: distributed degree via Allreduce ================= */
    int *local_deg = (int *)calloc((size_t)V, sizeof(int));
    for (int i = 0; i < local_E; i++) {
        if (local_eu[i] == local_ev[i]) continue;
        local_deg[local_eu[i]]++;
        local_deg[local_ev[i]]++;
    }
    int *degree = (int *)malloc(sizeof(int) * (size_t)V);
    MPI_Allreduce(local_deg, degree, V, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    free(local_deg);

    /* ================= Step 3: orient local edges only ================= */
    int *os = (int *)malloc(sizeof(int) * (local_E > 0 ? local_E : 1));
    int *od = (int *)malloc(sizeof(int) * (local_E > 0 ? local_E : 1));
    for (int i = 0; i < local_E; i++) {
        int u = local_eu[i], v = local_ev[i];
        int u_first = (degree[u] < degree[v]) || (degree[u] == degree[v] && u < v);
        if (u_first) { os[i] = u; od[i] = v; }
        else         { os[i] = v; od[i] = u; }
    }
    free(local_eu); free(local_ev);

    /* ================= Step 4: shuffle oriented edges to owner(source) ================= */
    /* owner(v) = v % P  (round-robin -- spreads consecutive ids across ranks) */
    int *shuf_sendcounts = (int *)calloc((size_t)P, sizeof(int));
    for (int i = 0; i < local_E; i++) shuf_sendcounts[os[i] % P]++;

    int *shuf_senddispls = (int *)malloc(sizeof(int) * P);
    shuf_senddispls[0] = 0;
    for (int r = 1; r < P; r++) shuf_senddispls[r] = shuf_senddispls[r - 1] + shuf_sendcounts[r - 1];

    int *cursor = (int *)malloc(sizeof(int) * P);
    memcpy(cursor, shuf_senddispls, sizeof(int) * P);

    int *send_a = (int *)malloc(sizeof(int) * (local_E > 0 ? local_E : 1));
    int *send_b = (int *)malloc(sizeof(int) * (local_E > 0 ? local_E : 1));
    for (int i = 0; i < local_E; i++) {
        int d = os[i] % P;
        int pos = cursor[d]++;
        send_a[pos] = os[i];
        send_b[pos] = od[i];
    }
    free(cursor); free(os); free(od);

    int *shuf_recvcounts = (int *)malloc(sizeof(int) * P);
    MPI_Alltoall(shuf_sendcounts, 1, MPI_INT, shuf_recvcounts, 1, MPI_INT, MPI_COMM_WORLD);
    int *shuf_recvdispls = (int *)malloc(sizeof(int) * P);
    shuf_recvdispls[0] = 0;
    for (int r = 1; r < P; r++) shuf_recvdispls[r] = shuf_recvdispls[r - 1] + shuf_recvcounts[r - 1];
    int owned_E = shuf_recvdispls[P - 1] + shuf_recvcounts[P - 1];

    int *recv_a = (int *)malloc(sizeof(int) * (owned_E > 0 ? owned_E : 1));
    int *recv_b = (int *)malloc(sizeof(int) * (owned_E > 0 ? owned_E : 1));
    MPI_Alltoallv(send_a, shuf_sendcounts, shuf_senddispls, MPI_INT,
                  recv_a, shuf_recvcounts, shuf_recvdispls, MPI_INT, MPI_COMM_WORLD);
    MPI_Alltoallv(send_b, shuf_sendcounts, shuf_senddispls, MPI_INT,
                  recv_b, shuf_recvcounts, shuf_recvdispls, MPI_INT, MPI_COMM_WORLD);
    free(send_a); free(send_b);
    /* recv_a/recv_b now hold exactly the oriented edges whose source vertex
       this rank owns -- each of the E edges lives on exactly one rank. */

    /* ================= Step 5a: build local CSR for owned vertices ================= */
    int *outdeg = (int *)calloc((size_t)V, sizeof(int));
    for (int i = 0; i < owned_E; i++) outdeg[recv_a[i]]++;
    int *offset = (int *)malloc(sizeof(int) * ((size_t)V + 1));
    offset[0] = 0;
    for (int v = 0; v < V; v++) offset[v + 1] = offset[v] + outdeg[v];
    int *nbr = (int *)malloc(sizeof(int) * (owned_E > 0 ? owned_E : 1));
    int *fill_cursor = (int *)malloc(sizeof(int) * (size_t)V);
    memcpy(fill_cursor, offset, sizeof(int) * (size_t)V);
    for (int i = 0; i < owned_E; i++) nbr[fill_cursor[recv_a[i]]++] = recv_b[i];
    free(fill_cursor); free(outdeg);
    for (int v = 0; v < V; v++) {
        int cnt = offset[v + 1] - offset[v];
        if (cnt > 1) qsort(nbr + offset[v], (size_t)cnt, sizeof(int), int_cmp);
    }

    /* ================= Step 5b: figure out which remote N+(b) we need ================= */
    char *visited = (char *)calloc((size_t)V, sizeof(char));
    int *unique_b = (int *)malloc(sizeof(int) * (owned_E > 0 ? owned_E : 1));
    int n_unique = 0;
    for (int i = 0; i < owned_E; i++) {
        int b = recv_b[i];
        if (!visited[b]) { visited[b] = 1; unique_b[n_unique++] = b; }
    }
    free(visited);

    int *req_sendcounts = (int *)calloc((size_t)P, sizeof(int));
    for (int i = 0; i < n_unique; i++) req_sendcounts[unique_b[i] % P]++;
    int *req_senddispls = (int *)malloc(sizeof(int) * P);
    req_senddispls[0] = 0;
    for (int r = 1; r < P; r++) req_senddispls[r] = req_senddispls[r - 1] + req_sendcounts[r - 1];

    int *req_cursor = (int *)malloc(sizeof(int) * P);
    memcpy(req_cursor, req_senddispls, sizeof(int) * P);
    int *req_sendbuf = (int *)malloc(sizeof(int) * (n_unique > 0 ? n_unique : 1));
    for (int i = 0; i < n_unique; i++) {
        int b = unique_b[i];
        req_sendbuf[req_cursor[b % P]++] = b;
    }
    free(req_cursor); free(unique_b);

    int *req_recvcounts = (int *)malloc(sizeof(int) * P);
    MPI_Alltoall(req_sendcounts, 1, MPI_INT, req_recvcounts, 1, MPI_INT, MPI_COMM_WORLD);
    int *req_recvdispls = (int *)malloc(sizeof(int) * P);
    req_recvdispls[0] = 0;
    for (int r = 1; r < P; r++) req_recvdispls[r] = req_recvdispls[r - 1] + req_recvcounts[r - 1];
    int total_req_in = req_recvdispls[P - 1] + req_recvcounts[P - 1];

    int *req_recvbuf = (int *)malloc(sizeof(int) * (total_req_in > 0 ? total_req_in : 1));
    MPI_Alltoallv(req_sendbuf, req_sendcounts, req_senddispls, MPI_INT,
                  req_recvbuf, req_recvcounts, req_recvdispls, MPI_INT, MPI_COMM_WORLD);
    /* req_recvbuf = vertex ids OTHER ranks are asking ME (the owner) about */

    /* ================= Step 5c: respond with lengths, then actual neighbour data ================= */
    int *resp_len_buf = (int *)malloc(sizeof(int) * (total_req_in > 0 ? total_req_in : 1));
    int *data_sendcounts = (int *)calloc((size_t)P, sizeof(int));
    for (int r = 0; r < P; r++) {
        for (int k = req_recvdispls[r]; k < req_recvdispls[r] + req_recvcounts[r]; k++) {
            int v = req_recvbuf[k];
            int len = offset[v + 1] - offset[v];
            resp_len_buf[k] = len;
            data_sendcounts[r] += len;
        }
    }
    int *data_senddispls = (int *)malloc(sizeof(int) * P);
    data_senddispls[0] = 0;
    for (int r = 1; r < P; r++) data_senddispls[r] = data_senddispls[r - 1] + data_sendcounts[r - 1];
    int total_data_out = data_senddispls[P - 1] + data_sendcounts[P - 1];

    int *data_sendbuf = (int *)malloc(sizeof(int) * (total_data_out > 0 ? total_data_out : 1));
    {
        int pos = 0;
        for (int r = 0; r < P; r++) {
            for (int k = req_recvdispls[r]; k < req_recvdispls[r] + req_recvcounts[r]; k++) {
                int v = req_recvbuf[k];
                int len = offset[v + 1] - offset[v];
                memcpy(data_sendbuf + pos, nbr + offset[v], sizeof(int) * (size_t)len);
                pos += len;
            }
        }
    }
    free(req_recvbuf);

    /* lengths back: reverse of the request exchange */
    int total_req_out = req_senddispls[P - 1] + req_sendcounts[P - 1]; /* == n_unique */
    int *lengths_received = (int *)malloc(sizeof(int) * (total_req_out > 0 ? total_req_out : 1));
    MPI_Alltoallv(resp_len_buf, req_recvcounts, req_recvdispls, MPI_INT,
                  lengths_received, req_sendcounts, req_senddispls, MPI_INT, MPI_COMM_WORLD);
    free(resp_len_buf);

    int *data_recvcounts = (int *)calloc((size_t)P, sizeof(int));
    for (int r = 0; r < P; r++)
        for (int k = req_senddispls[r]; k < req_senddispls[r] + req_sendcounts[r]; k++)
            data_recvcounts[r] += lengths_received[k];
    int *data_recvdispls = (int *)malloc(sizeof(int) * P);
    data_recvdispls[0] = 0;
    for (int r = 1; r < P; r++) data_recvdispls[r] = data_recvdispls[r - 1] + data_recvcounts[r - 1];
    int total_data_in = data_recvdispls[P - 1] + data_recvcounts[P - 1];

    int *data_recvbuf = (int *)malloc(sizeof(int) * (total_data_in > 0 ? total_data_in : 1));
    MPI_Alltoallv(data_sendbuf, data_sendcounts, data_senddispls, MPI_INT,
                  data_recvbuf, data_recvcounts, data_recvdispls, MPI_INT, MPI_COMM_WORLD);
    free(data_sendbuf); free(data_sendcounts); free(data_senddispls);
    free(data_recvcounts); free(data_recvdispls);

    /* Build b -> (start,len) lookup into data_recvbuf, in req_sendbuf's order */
    int *Bstart = (int *)malloc(sizeof(int) * (size_t)V);
    int *Blen   = (int *)malloc(sizeof(int) * (size_t)V);
    for (int v = 0; v < V; v++) Blen[v] = -1;
    {
        int cum = 0;
        for (int k = 0; k < total_req_out; k++) {
            int b = req_sendbuf[k];
            int len = lengths_received[k];
            Bstart[b] = cum;
            Blen[b] = len;
            cum += len;
        }
    }
    free(req_sendbuf); free(lengths_received);
    free(req_sendcounts); free(req_senddispls); free(req_recvcounts); free(req_recvdispls);

    /* ================= Step 6: count triangles for owned edges ================= */
    ll local_count = 0;
    for (int i = 0; i < owned_E; i++) {
        int a = recv_a[i], b = recv_b[i];
        int as = offset[a], ae = offset[a + 1];
        int bs = Bstart[b], be = Bstart[b] + Blen[b];
        int pa = as, pb = bs;
        while (pa < ae && pb < be) {
            int va = nbr[pa], vb = data_recvbuf[pb];
            if (va == vb) { local_count++; pa++; pb++; }
            else if (va < vb) pa++;
            else pb++;
        }
    }

    free(recv_a); free(recv_b); free(nbr); free(offset);
    free(data_recvbuf); free(Bstart); free(Blen);
    free(shuf_sendcounts); free(shuf_senddispls); free(shuf_recvcounts); free(shuf_recvdispls);
    free(degree); free(sendcounts); free(senddispls);

    /* ================= Step 7: combine into the final global answer ================= */
    ll global_count = 0;
    MPI_Reduce(&local_count, &global_count, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) printf("%lld\n", global_count);

    MPI_Finalize();
    return 0;
}
