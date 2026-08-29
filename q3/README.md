# Q3: MPI Bitonic Sort

Run all commands in this directory (`q3/`).

## Build and run locally

```bash
mpicxx -O2 -std=c++17 -o bitonic bitonic.cpp
mpirun -np P ./bitonic N [seed]
```

The program generates `N` integers using the seed (default is 12345 if not given), then 
prints the generated array, sorted array, and execution time. For Slurm benchmarking, 
append `--benchmark` specifying the program to print only the execution time.

```bash
mpirun -np P ./bitonic N [seed] --benchmark
```

## Local correctness check

```bash
mpicxx -O2 -std=c++17 -o bitonic bitonic.cpp
./check_correctness.sh
```

The checker runs `P=1,2,4,8` and verifies the sorted output against the
generated input. It is intended for local use.

## RCE benchmark

The Slurm job compiles the program on RCE itself.
For a one-repeat plumbing test, run:

```bash
BENCHMARK_REPEATS=1 sbatch benchmark_bitonic.slurm
```

For benchmark measurements, use the default five repeats:

```bash
sbatch benchmark_bitonic.slurm
```

Check a submitted job and read its results with:

```bash
squeue -j <job-id>
cat bitonic_benchmark_<job-id>.err
cat results_<job-id>/report.md
```

Each report contains speedup, efficiency, and runtime tables. Every reported
runtime is the median of five runs; individual timings are stored in
`results_<job-id>/samples/`.
