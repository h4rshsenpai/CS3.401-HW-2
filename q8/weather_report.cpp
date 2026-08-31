#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <math.h>
#include <climits>

using namespace std;

#define INF_VAL 1e18

// structs for data and stats
struct raw_data {
    long long ts, s_id;
    double t, h, p, w;
    long long r;
};

struct glob_st {
    long long cnt = 0, extr = 0;
    double s_t = 0, s_h = 0, s_p = 0, s_w = 0;
    long long s_r = 0;
    double min_t = INF_VAL, max_t = -INF_VAL;
    double min_h = INF_VAL, max_h = -INF_VAL;
    double min_p = INF_VAL, max_p = -INF_VAL;
    double max_w = -INF_VAL;
    long long max_r = LLONG_MIN;
};

struct st_val { 
    long long c = 0; 
    double t_sum = 0.0; 
    long long r_sum = 0; 
};

struct st_packet { 
    long long id, c; 
    double t_sum;
    long long r_sum; 
};

struct int_packet { 
    long long id, c; 
};

// heap comparison logic
struct cmp_heap {
    bool operator()(const st_packet& x, const st_packet& y) const {
        if (x.c != y.c) return x.c > y.c;
        return x.id < y.id;
    }
};

// make sure we dont read partial lines at boundaries
void align_file_ptrs(MPI_File f, MPI_Offset& ptr, MPI_Offset sz, bool is_start, MPI_Offset d_start) {
    if (is_start && ptr <= d_start) return;
    if (!is_start && ptr >= sz) return;
    
    MPI_Offset p_len = 4096; 
    while (1) {
        MPI_Offset r_start = (ptr - 1 < (is_start ? d_start : 0)) ? (is_start ? d_start : 0) : (ptr - 1);
        int len = (p_len < sz - r_start) ? p_len : (sz - r_start);
        if (len <= 0) return;

        vector<char> buf(len);
        MPI_Status st;
        MPI_File_read_at(f, r_start, buf.data(), len, MPI_BYTE, &st);

        int off = ptr - r_start;
        if (off > 0 && buf[off - 1] == '\n') return;

        for (int i = off; i < len; ++i) {
            if (buf[i] == '\n') {
                ptr = r_start + i + 1;
                return;
            }
        }
        if (r_start + len >= sz) { 
            ptr = sz; 
            return; 
        }
        p_len *= 2; 
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, sz_comm = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &sz_comm);

    char* fname = (argc > 1) ? argv[1] : (char*)"data.txt";
    int n = 0, k = 0, s = 0;

    // only root reads the first line
    if (rank == 0) {
        FILE* fp = fopen(fname, "r");
        if (!fp || fscanf(fp, "%d %d %d", &n, &k, &s) != 3) {
            printf("file error on root\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(fp);
    }
    
    // share metadata
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&k, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&s, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (n <= 0) { 
        MPI_Finalize(); 
        return 1; 
    }

    // read parallel file
    MPI_File mf;
    if (MPI_File_open(MPI_COMM_WORLD, fname, MPI_MODE_RDONLY, MPI_INFO_NULL, &mf) != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Offset fsize = 0;
    MPI_File_get_size(mf, &fsize);

    // skip headers
    int probe = (4096 < fsize) ? 4096 : fsize;
    vector<char> head_buf(probe);
    MPI_Status mpi_st;
    MPI_File_read_at(mf, 0, head_buf.data(), probe, MPI_BYTE, &mpi_st);
    
    MPI_Offset data_start = -1;
    for (int i = 0; i < probe; ++i) {
        if (head_buf[i] == '\n') { 
            data_start = i + 1; 
            break; 
        }
    }
    if (data_start < 0) MPI_Abort(MPI_COMM_WORLD, 1);

    // find out what chunk this rank should read
    MPI_Offset b_start = data_start + ((fsize - data_start) * rank) / sz_comm;
    MPI_Offset b_end = (rank == sz_comm - 1) ? fsize : data_start + ((fsize - data_start) * (rank + 1)) / sz_comm;

    align_file_ptrs(mf, b_start, fsize, true, data_start);
    align_file_ptrs(mf, b_end, fsize, false, data_start);
    if (b_end < b_start) b_end = b_start;

    size_t chunk_bytes = b_end - b_start;
    vector<char> txt(chunk_bytes);

    // load file chunk in memory blocks
    size_t d = 0;
    while (d < chunk_bytes) {
        int amt = (chunk_bytes - d > 67108864) ? 67108864 : (chunk_bytes - d); 
        MPI_File_read_at(mf, b_start + d, txt.data() + d, amt, MPI_BYTE, &mpi_st);
        d += amt;
    }
    MPI_File_close(&mf);

    // tracker variables
    glob_st mystat;
    unordered_map<long long, st_val> st_map;
    unordered_map<long long, long long> int_map;

    raw_data h_loc{}, c_loc{};
    bool got_any = false;

    char* ptr = txt.data();
    char* eof = ptr + chunk_bytes;

    // start parsing
    while (ptr < eof) {
        while (ptr < eof && *ptr <= 32) ptr++; 
        if (ptr >= eof) break;
        
        raw_data curr;

        // read timestamp fast
        int neg = 0;
        if (*ptr == '-') { neg = 1; ptr++; } else if (*ptr == '+') ptr++;
        curr.ts = 0;
        while (ptr < eof && *ptr >= '0' && *ptr <= '9') { 
            curr.ts = curr.ts * 10 + (*ptr - '0'); 
            ptr++; 
        }
        if (neg) curr.ts = -curr.ts;
        
        while (ptr < eof && *ptr <= 32) ptr++;
        
        // read station id fast
        neg = 0;
        if (*ptr == '-') { neg = 1; ptr++; } else if (*ptr == '+') ptr++;
        curr.s_id = 0;
        while (ptr < eof && *ptr >= '0' && *ptr <= '9') { 
            curr.s_id = curr.s_id * 10 + (*ptr - '0'); 
            ptr++; 
        }
        if (neg) curr.s_id = -curr.s_id;

        // rely on stdlib for floating point
        char* tmp = nullptr;
        curr.t = strtod(ptr, &tmp); ptr = tmp;
        curr.h = strtod(ptr, &tmp); ptr = tmp;
        curr.p = strtod(ptr, &tmp); ptr = tmp;
        
        // read rain and save as integer to fix float precision diffs in MPI sum
        double tmp_r = strtod(ptr, &tmp); ptr = tmp;
        curr.r = llround(tmp_r * 100);
        
        curr.w = strtod(ptr, &tmp); ptr = tmp;

        // update local calculations
        mystat.cnt++;
        if (curr.t >= 40.0 || curr.t <= 0.0) mystat.extr++;
        
        mystat.s_t += curr.t; 
        mystat.s_h += curr.h; 
        mystat.s_p += curr.p; 
        mystat.s_r += curr.r; 
        mystat.s_w += curr.w;
        
        if (curr.t < mystat.min_t) mystat.min_t = curr.t;
        if (curr.t > mystat.max_t) mystat.max_t = curr.t;
        if (curr.h < mystat.min_h) mystat.min_h = curr.h;
        if (curr.h > mystat.max_h) mystat.max_h = curr.h;
        if (curr.p < mystat.min_p) mystat.min_p = curr.p;
        if (curr.p > mystat.max_p) mystat.max_p = curr.p;
        if (curr.r > mystat.max_r) mystat.max_r = curr.r;
        if (curr.w > mystat.max_w) mystat.max_w = curr.w;

        // track hottest and coldest points
        if (!got_any) {
            h_loc = curr; 
            c_loc = curr; 
            got_any = true;
        } else {
            bool is_h = (curr.t != h_loc.t) ? (curr.t > h_loc.t) : ((curr.ts != h_loc.ts) ? (curr.ts < h_loc.ts) : (curr.s_id < h_loc.s_id));
            if (is_h) h_loc = curr;
            
            bool is_c = (curr.t != c_loc.t) ? (curr.t < c_loc.t) : ((curr.ts != c_loc.ts) ? (curr.ts < c_loc.ts) : (curr.s_id < c_loc.s_id));
            if (is_c) c_loc = curr;
        }

        st_map[curr.s_id].c++;
        st_map[curr.s_id].t_sum += curr.t;
        st_map[curr.s_id].r_sum += curr.r;
        int_map[curr.ts / 60]++;

        while (ptr < eof && *ptr != '\n') ptr++;
        if (ptr < eof) ptr++;
    }

    // collect double variables for reduction
    double lsum[4] = { mystat.s_t, mystat.s_h, mystat.s_p, mystat.s_w };
    double gsum[4] = {0};
    MPI_Reduce(lsum, gsum, 4, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    // collect integer variables for exact sums without float drift
    long long lrsum[2] = { mystat.s_r, mystat.extr };
    long long grsum[2] = {0};
    MPI_Reduce(lrsum, grsum, 2, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    double lmax[4] = { mystat.max_t, mystat.max_h, mystat.max_p, mystat.max_w };
    double gmax[4] = {0};
    MPI_Reduce(lmax, gmax, 4, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    long long lrmax = mystat.max_r, grmax = 0;
    MPI_Reduce(&lrmax, &grmax, 1, MPI_LONG_LONG, MPI_MAX, 0, MPI_COMM_WORLD);

    double lmin[3] = { mystat.min_t, mystat.min_h, mystat.min_p };
    double gmin[3] = {0};
    MPI_Reduce(lmin, gmin, 3, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

    // custom types to send structs over mpi
    MPI_Datatype t_data;
    int bl1[3] = {1, 1, 1};
    MPI_Aint ol1[3] = { offsetof(raw_data, ts), offsetof(raw_data, s_id), offsetof(raw_data, t) };
    MPI_Datatype dt1[3] = { MPI_LONG_LONG, MPI_LONG_LONG, MPI_DOUBLE };
    MPI_Datatype raw1;
    MPI_Type_create_struct(3, bl1, ol1, dt1, &raw1);
    MPI_Type_create_resized(raw1, 0, sizeof(raw_data), &t_data);
    MPI_Type_commit(&t_data); 
    MPI_Type_free(&raw1);

    MPI_Datatype t_st;
    int bl2[4] = {1, 1, 1, 1};
    MPI_Aint ol2[4] = { offsetof(st_packet, id), offsetof(st_packet, c), offsetof(st_packet, t_sum), offsetof(st_packet, r_sum) };
    MPI_Datatype dt2[4] = { MPI_LONG_LONG, MPI_LONG_LONG, MPI_DOUBLE, MPI_LONG_LONG };
    MPI_Datatype raw2;
    MPI_Type_create_struct(4, bl2, ol2, dt2, &raw2);
    MPI_Type_create_resized(raw2, 0, sizeof(st_packet), &t_st);
    MPI_Type_commit(&t_st); 
    MPI_Type_free(&raw2);

    MPI_Datatype t_int;
    int bl3[2] = {1, 1};
    MPI_Aint ol3[2] = { offsetof(int_packet, id), offsetof(int_packet, c) };
    MPI_Datatype dt3[2] = { MPI_LONG_LONG, MPI_LONG_LONG };
    MPI_Datatype raw3;
    MPI_Type_create_struct(2, bl3, ol3, dt3, &raw3);
    MPI_Type_create_resized(raw3, 0, sizeof(int_packet), &t_int);
    MPI_Type_commit(&t_int); 
    MPI_Type_free(&raw3);

    // find true hot and cold
    raw_data hsnd{}, csnd{};
    hsnd.t = -INF_VAL; csnd.t = INF_VAL;
    if (got_any) { 
        hsnd = h_loc; 
        csnd = c_loc; 
    }

    vector<raw_data> h_all(rank ? 0 : sz_comm), c_all(rank ? 0 : sz_comm);
    MPI_Gather(&hsnd, 1, t_data, h_all.data(), 1, t_data, 0, MPI_COMM_WORLD);
    MPI_Gather(&csnd, 1, t_data, c_all.data(), 1, t_data, 0, MPI_COMM_WORLD);

    // group stations properly across ranks
    vector<int> sc(sz_comm, 0), rc(sz_comm, 0);
    for (auto& k : st_map) {
        long long ow = k.first % sz_comm; 
        if (ow < 0) ow += sz_comm;
        sc[ow]++;
    }
    MPI_Alltoall(sc.data(), 1, MPI_INT, rc.data(), 1, MPI_INT, MPI_COMM_WORLD);

    vector<int> sd(sz_comm, 0), rd(sz_comm, 0);
    for (int i = 1; i < sz_comm; ++i) {
        sd[i] = sd[i - 1] + sc[i - 1];
        rd[i] = rd[i - 1] + rc[i - 1];
    }
    
    vector<st_packet> s_pbuf(sd.back() + sc.back()), r_pbuf(rd.back() + rc.back());
    vector<int> tracker = sd;
    for (auto& k : st_map) {
        long long ow = k.first % sz_comm; 
        if (ow < 0) ow += sz_comm;
        s_pbuf[tracker[ow]++] = {k.first, k.second.c, k.second.t_sum, k.second.r_sum};
    }
    MPI_Alltoallv(s_pbuf.data(), sc.data(), sd.data(), t_st, r_pbuf.data(), rc.data(), rd.data(), t_st, MPI_COMM_WORLD);

    unordered_map<long long, st_val> merge_st;
    for (auto& rx : r_pbuf) {
        merge_st[rx.id].c += rx.c; 
        merge_st[rx.id].t_sum += rx.t_sum; 
        merge_st[rx.id].r_sum += rx.r_sum;
    }

    // group intervals properly
    fill(sc.begin(), sc.end(), 0);
    for (auto& k : int_map) {
        long long ow = k.first % sz_comm; 
        if (ow < 0) ow += sz_comm;
        sc[ow]++;
    }
    MPI_Alltoall(sc.data(), 1, MPI_INT, rc.data(), 1, MPI_INT, MPI_COMM_WORLD);

    fill(sd.begin(), sd.end(), 0); 
    fill(rd.begin(), rd.end(), 0);
    for (int i = 1; i < sz_comm; ++i) {
        sd[i] = sd[i - 1] + sc[i - 1];
        rd[i] = rd[i - 1] + rc[i - 1];
    }
    
    vector<int_packet> si_pbuf(sd.back() + sc.back()), ri_pbuf(rd.back() + rc.back());
    tracker = sd;
    for (auto& k : int_map) {
        long long ow = k.first % sz_comm; 
        if (ow < 0) ow += sz_comm;
        si_pbuf[tracker[ow]++] = {k.first, k.second};
    }
    MPI_Alltoallv(si_pbuf.data(), sc.data(), sd.data(), t_int, ri_pbuf.data(), rc.data(), rd.data(), t_int, MPI_COMM_WORLD);

    unordered_map<long long, long long> merge_int;
    for (auto& rx : ri_pbuf) merge_int[rx.id] += rx.c;

    // compute best stations locally
    priority_queue<st_packet, vector<st_packet>, cmp_heap> pq;
    for (auto& kv : merge_st) {
        pq.push({kv.first, kv.second.c, kv.second.t_sum, kv.second.r_sum});
        if ((int)pq.size() > k) pq.pop();
    }

    st_packet junk = {-1, -1, 0, 0};
    vector<st_packet> loc_pq(k, junk);
    int p = 0;
    while (!pq.empty() && p < k) { 
        loc_pq[p++] = pq.top(); 
        pq.pop(); 
    }

    vector<st_packet> all_pq(rank ? 0 : sz_comm * k);
    MPI_Gather(loc_pq.data(), k, t_st, all_pq.data(), k, t_st, 0, MPI_COMM_WORLD);

    // find highest interval count locally
    int_packet bst_int = {-1, -1};
    for (auto& kv : merge_int) {
        if (kv.second > bst_int.c || (kv.second == bst_int.c && (bst_int.id == -1 || kv.first < bst_int.id))) {
            bst_int = {kv.first, kv.second};
        }
    }
    vector<int_packet> all_ints(rank ? 0 : sz_comm);
    MPI_Gather(&bst_int, 1, t_int, all_ints.data(), 1, t_int, 0, MPI_COMM_WORLD);

    // let root finish up
    if (rank == 0) {
        raw_data glob_h = h_all[0], glob_c = c_all[0];
        for (int i = 1; i < sz_comm; ++i) {
            bool is_h = (h_all[i].t != glob_h.t) ? (h_all[i].t > glob_h.t) : ((h_all[i].ts != glob_h.ts) ? (h_all[i].ts < glob_h.ts) : (h_all[i].s_id < glob_h.s_id));
            if (is_h) glob_h = h_all[i];
            
            bool is_c = (c_all[i].t != glob_c.t) ? (c_all[i].t < glob_c.t) : ((c_all[i].ts != glob_c.ts) ? (c_all[i].ts < glob_c.ts) : (c_all[i].s_id < glob_c.s_id));
            if (is_c) glob_c = c_all[i];
        }

        // sort final best stations
        priority_queue<st_packet, vector<st_packet>, cmp_heap> glob_pq;
        for (auto& r : all_pq) {
            if (r.id == -1) continue;
            glob_pq.push(r);
            if ((int)glob_pq.size() > k) glob_pq.pop();
        }
        
        vector<st_packet> final_k;
        while (!glob_pq.empty()) { 
            final_k.push_back(glob_pq.top()); 
            glob_pq.pop(); 
        }
        sort(final_k.begin(), final_k.end(), [](const st_packet& x, const st_packet& y){
            return x.c != y.c ? x.c > y.c : x.id < y.id;
        });

        long long f_int = -1, f_intc = -1;
        for (auto& c : all_ints) {
            if (c.id < 0) continue;
            if (c.c > f_intc || (c.c == f_intc && (f_int == -1 || c.id < f_int))) {
                f_int = c.id; 
                f_intc = c.c;
            }
        }

        // print outputs directly
        printf("TOTAL_MEASUREMENTS %d\n", n);
        printf("AVERAGE_TEMPERATURE %.6f\n", gsum[0] / n);
        printf("MIN_TEMPERATURE %.6f\n", gmin[0]);
        printf("MAX_TEMPERATURE %.6f\n", gmax[0]);
        printf("AVERAGE_HUMIDITY %.6f\n", gsum[1] / n);
        printf("MIN_HUMIDITY %.6f\n", gmin[1]);
        printf("MAX_HUMIDITY %.6f\n", gmax[1]);
        printf("AVERAGE_PRESSURE %.6f\n", gsum[2] / n);
        printf("MIN_PRESSURE %.6f\n", gmin[2]);
        printf("MAX_PRESSURE %.6f\n", gmax[2]);
        
        // fix the float precision drift by dividing our exact integer sum
        printf("TOTAL_RAINFALL %.6f\n", (double)grsum[0] / 100.0);
        printf("MAX_RAINFALL %.6f\n", (double)grmax / 100.0);
        
        printf("AVERAGE_WIND_SPEED %.6f\n", gsum[3] / n);
        printf("MAX_WIND_SPEED %.6f\n", gmax[3]);
        printf("EXTREME_TEMPERATURE_EVENTS %lld\n", grsum[1]);
        printf("HOTTEST_MEASUREMENT %.6f %lld %lld\n", glob_h.t, glob_h.s_id, glob_h.ts);
        printf("COLDEST_MEASUREMENT %.6f %lld %lld\n", glob_c.t, glob_c.s_id, glob_c.ts);
        printf("BUSIEST_INTERVAL %lld %lld\n", f_int, f_intc);
        printf("TOP_STATIONS\n");

        for (auto& r : final_k) {
            printf("%lld %lld %.6f %.6f\n", r.id, r.c, r.t_sum / r.c, (double)r.r_sum / 100.0);
        }
    }

    MPI_Type_free(&t_data); 
    MPI_Type_free(&t_st); 
    MPI_Type_free(&t_int);
    
    MPI_Finalize();
    return 0;
}