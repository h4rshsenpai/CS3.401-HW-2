# Q4: Distributed Triangle Counting using MPI

Run all commands in this directory (`q4/`).

## Build and run locally

```bash
mpicc -O3 -o triangle_count_distributed triangle_count_distributed.c
mpirun -np P ./triangle_count_distributed < tests/sample1.txt
```

The program reads an undirected graph from standard input and prints the
total number of triangles.

For example:

```bash
mpirun -np 4 ./triangle_count_distributed < tests/sample1.txt
```

If required on a local/root MPI setup:

```bash
mpirun --allow-run-as-root --oversubscribe -np 4 \
./triangle_count_distributed < tests/sample1.txt
```

## Local correctness check

The implementation was verified against an independent brute-force Python
triangle counter.

Run the sequential checker with:

```bash
python3 brute_force.py tests/rand_1.txt
```

Run the MPI implementation with:

```bash
mpirun -np 4 ./triangle_count_distributed < tests/rand_1.txt
```

The implementation was tested across process scales:

```text
P = 1, 2, 4, 8
```

Random graphs were tested across these process counts, and all
computed triangle counts matched the brute-force results.

Additional topology-based tests were also verified:

```text
Complete graph       V=1000    Expected=166167000    PASS
Star graph           V=100000  Expected=0            PASS
Wheel graph          V=100000  Expected=99999        PASS
Disjoint triangles   V=99999   Expected=33333        PASS
```

To test multiple process counts manually:

```bash
for p in 1 2 4 8
do
    echo "P=$p"
    mpirun --oversubscribe --bind-to none -np $p ./triangle_count_distributed < tests/rand_1.txt
done
```

## Automated testing & RCE Benchmark

For automated cluster performance scaling and correctness testing in a Slurm environment:

```bash
sbatch traingle_benchmark.slurm
```

Check a submitted job and read its results with:

```bash
squeue -u $USER
cat timed_tests_<job-id>.log
```