# Q4. Distributed Triangle Counting in an Undirected Graph

## 1. Problem Statement

The objective is to implement a distributed algorithm to count the number of triangles in a given undirected graph using MPI.

### Constraints

* Number of vertices: `3 <= V <= 100000`
* Number of edges: `3 <= E <= 1000000`
* Vertices are `0`-indexed.
* Each process stores a distinct subset of the graph's edges.
* The workload must be distributed across MPI processes.
* The final output must contain only the total number of triangles.

### Input Format

```text
V E
u1 v1
u2 v2
...
uE vE
```

where each `(ui, vi)` represents an undirected edge.

### Output Format

```text
number_of_triangles
```

Only the final global triangle count is printed by rank 0.

---

## 2. Algorithm

The implementation uses a **degree-based forward orientation algorithm** combined with MPI-based distributed communication.

### Step 1: Distribute the edges

Rank 0 reads the input graph and distributes the edges approximately equally among all MPI processes using `MPI_Scatterv`.

Therefore, each process initially receives its own distinct subset of edges.

If there are `E` edges and `P` processes, each process receives approximately:

```text
E / P
```

edges.

---

### Step 2: Calculate global vertex degrees

Each process calculates the degree contribution of vertices from the edges it owns.

These local degree arrays are combined using:

```text
MPI_Allreduce
```

This produces the global degree of every vertex on every process.

---

### Step 3: Orient the graph

Each undirected edge `{u,v}` is converted into one directed edge.

The orientation is determined using the ordering:

```text
(degree(u), u) < (degree(v), v)
```

More specifically, `u -> v` if:

```text
degree(u) < degree(v)
```

or, if the degrees are equal:

```text
degree(u) == degree(v) && u < v
```

This produces a consistent acyclic orientation of the graph.

---

## 3. Avoiding Double Counting

Consider a triangle consisting of vertices `a`, `b`, and `c`.

After applying the degree/id ordering, the vertices have a unique order:

```text
a < b < c
```

with respect to the orientation ordering.

The triangle contains the directed edges:

```text
a -> b
a -> c
b -> c
```

When processing the edge:

```text
a -> b
```

both `a` and `b` have `c` as a common outgoing neighbour.

Therefore, the triangle is counted exactly once.

The same triangle cannot be counted while processing `a -> c` or `b -> c`, because there is no common outgoing vertex that satisfies the required forward orientation.

Thus, every triangle contributes exactly one to the final count.

---

## 4. Distributed Graph Storage

After the initial edge distribution, the oriented edges are routed according to their source vertex.

The owner of vertex `v` is defined as:

```text
owner(v) = v % P
```

The oriented edges are exchanged using:

```text
MPI_Alltoallv
```

After this operation, each process stores the outgoing edges belonging to the vertices it owns.

This avoids replicating the complete edge list on every MPI process.

---

## 5. Remote Neighbour Communication

For an oriented edge:

```text
a -> b
```

the process that owns `a` needs the outgoing neighbour list of `b` to determine whether `a` and `b` have a common outgoing neighbour.

If `b` belongs to another process, its neighbour list is requested using MPI communication.

The implementation performs:

1. Request exchange using `MPI_Alltoallv`
2. Exchange of neighbour-list lengths
3. Exchange of the actual neighbour data using `MPI_Alltoallv`

This allows each process to obtain the remote adjacency information required for its assigned edges without storing the complete graph.

---

## 6. Triangle Counting

All outgoing neighbour lists are sorted.

For every locally owned oriented edge:

```text
a -> b
```

the program computes:

```text
N+(a) ∩ N+(b)
```

using a two-pointer intersection algorithm.

If a common neighbour is found, one triangle is counted.

Because the graph has already been consistently oriented, each triangle is counted exactly once.

---

## 7. Global Reduction

Each MPI process produces a local triangle count:

```text
local_count
```

The local counts are combined using:

```text
MPI_Reduce
```

with the `MPI_SUM` operation.

Rank 0 receives the final global count and prints it.

The program does not print any debugging information.

---

# 8. Complexity

Let `E` be the number of edges and `P` the number of MPI processes.

### Edge distribution

The initial edge distribution requires approximately:

```text
O(E/P)
```

edge storage per process after scattering.

### Degree calculation

Each process processes its local edges:

```text
O(E/P)
```

followed by an `MPI_Allreduce` over the `V` vertex degrees.

### Sorting

The outgoing adjacency lists are sorted locally.

The total sorting work depends on the distribution of outgoing edges, with the overall cost bounded by the sorting performed on the distributed adjacency lists.

### Triangle intersection

For each oriented edge, the two outgoing adjacency lists are intersected using a linear two-pointer scan.

The forward orientation significantly reduces the amount of work compared with checking all possible vertex triples.

### Communication

The algorithm uses MPI collective communication operations including:

```text
MPI_Scatterv
MPI_Allreduce
MPI_Alltoall
MPI_Alltoallv
MPI_Reduce
```

The communication overhead becomes increasingly important when the number of MPI processes is increased on a single machine.

---

# 9. Compilation

The source code is:

```text
src/triangle_count_distributed.c
```

Compile using:

```bash
mpicc -O3 -o triangle_count_distributed src/triangle_count_distributed.c
```

---

# 10. Execution

The program reads the graph from standard input.

For example:

```bash
mpirun --allow-run-as-root --oversubscribe -np 4 ./triangle_count_distributed < tests/sample1.txt
```

The required output is a single integer.

For the sample graph:

```text
4 5
0 1
1 2
2 0
2 3
3 0
```

the output is:

```text
2
```

The two triangles are:

```text
{0,1,2}
{0,2,3}
```

---

# 11. Correctness Verification

The implementation was tested using both randomly generated graphs and specific graph topologies.

The tests were executed with multiple MPI process counts:

```text
P = 1, 2, 3, 5, 7, 9
```

The expected answers were independently calculated using a brute-force Python implementation.

## Random Graph Tests

All eight random graph test cases passed for every tested process count.

Representative results:

```text
test 1 (V=202 E=1924) np=3: expected=1145 got=1145 -> OK
test 1 (V=202 E=1924) np=7: expected=1145 got=1145 -> OK

test 2 (V=25 E=157) np=5: expected=327 got=327 -> OK
test 2 (V=25 E=157) np=9: expected=327 got=327 -> OK

test 6 (V=116 E=2183) np=5: expected=8772 got=8772 -> OK
test 7 (V=76 E=1230) np=9: expected=5574 got=5574 -> OK

test 8 (V=76 E=464) np=7: expected=289 got=289 -> OK
```

All remaining combinations also produced `OK`.

---

# 12. Topology-Based Tests

Four additional graph structures were tested to verify important edge cases.

### Complete Graph

```text
V = 1000
E = 499500
Expected triangles = 166167000
```

The result matched the mathematical value:

```text
1000 C 3 = 166167000
```

The test passed for the tested MPI process counts.

---

### Star Graph

```text
V = 100000
E = 99999
Expected triangles = 0
```

A star graph contains no triangles.

The implementation correctly produced:

```text
0
```

---

### Wheel Graph

```text
V = 100000
E = 199998
Expected triangles = 99999
```

Each pair of adjacent vertices on the outer cycle forms one triangle with the central vertex.

The implementation correctly produced:

```text
99999
```

---

### Disjoint Triangle Graph

```text
V = 99999
E = 99999
Expected triangles = 33333
```

The graph consists of independent triangles.

The implementation correctly produced:

```text
33333
```

These topology tests passed with the tested MPI process counts.

---

# 13. Performance Evaluation

Performance was measured on:

```text
Environment: WSL / Ubuntu on Windows
MPI: OpenMPI
```

A complete graph was used for the benchmark:

```text
V = 1000
E = 499500
Expected triangles = 166167000
```

The number of MPI processes was varied while keeping the input graph fixed.

## Strong Scaling Results

| MPI Processes | Execution Time | Speedup | Parallel Efficiency |
| ------------: | -------------: | ------: | ------------------: |
|             1 |         0.63 s |   1.00x |              100.0% |
|             2 |         0.62 s |   1.02x |               50.8% |
|             4 |         0.58 s |   1.09x |               27.2% |
|             8 |         0.83 s |   0.76x |                9.5% |

Speedup is calculated as:

```text
Speedup(P) = T1 / TP
```

and parallel efficiency as:

```text
Efficiency(P) = Speedup(P) / P
```

---

## Performance Observations

The benchmark shows only a small speedup when increasing the number of MPI processes.

The main reason is that the experiment was performed on a single WSL machine rather than on a multi-node cluster.

The distributed implementation performs several collective communication operations, including:

```text
MPI_Allreduce
MPI_Alltoall
MPI_Alltoallv
MPI_Reduce
```

These communication operations introduce synchronization and data-transfer overhead.

For the tested graph, the computation is not large enough for the additional processes to completely compensate for this communication cost.

The execution time improves slightly from 1 to 4 processes:

```text
0.63 s → 0.58 s
```

but increases at 8 processes:

```text
0.58 s → 0.83 s
```

This is consistent with the overhead of running more MPI processes on a single machine, particularly when the available CPU resources are limited.

Therefore, the benchmark demonstrates that increasing the number of MPI processes does not automatically improve performance for relatively small inputs. The distributed approach is primarily beneficial when the graph and computational workload are sufficiently large to justify the communication overhead.

---

# 14. Final Summary

The submitted implementation satisfies the distributed triangle-counting requirements by:

* Using MPI to distribute the edge workload.
* Giving each process a distinct subset of edges.
* Computing global vertex degrees using `MPI_Allreduce`.
* Orienting edges using a deterministic degree/id ordering.
* Routing edges using `MPI_Alltoallv`.
* Fetching required remote adjacency lists through MPI communication.
* Counting triangles using sorted-neighbour intersection.
* Guaranteeing that each triangle is counted exactly once.
* Combining local results using `MPI_Reduce`.
* Supporting multiple MPI process counts.
* Producing only the required integer as program output.

The implementation passed all random and topology-based correctness tests performed, and its performance was evaluated using multiple MPI process counts.
