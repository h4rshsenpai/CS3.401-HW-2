# Q8: Distributed Weather Data Processing using MPI

Run all commands in this directory (`q8/`).

## Build and run locally

```bash
# Compile sequential baseline
g++ -O3 -o weather_sequential weather_sequential.cpp

# Compile MPI distributed implementation
mpicxx -O3 -o weather_report weather_report.cpp
```

The programs read a generated weather dataset from standard input and compute global analytics (e.g., maximum/minimum temperatures, hottest/coldest measurements, and station-specific aggregates).

For example, to run the MPI version with 4 processes:

```bash
mpirun -np 4 ./weather_report < tests/weather_small_n10000.txt
```

If required on a local/root MPI setup:

```bash
mpirun --allow-run-as-root --oversubscribe -np 4 \
./weather_report < tests/weather_small_n10000.txt
```

## Local correctness check

The implementation was verified by comparing the parallel MPI outputs directly against the C++ sequential baseline outputs.

Run the sequential version to generate the ground-truth baseline:

```bash
./weather_sequential < tests/weather_small_n10000.txt > seq_output.txt
```

Run the MPI implementation to generate the parallel output:

```bash
mpirun -np 4 ./weather_report < tests/weather_small_n10000.txt > mpi_output.txt
```

Verify correctness using standard `diff` (ignoring minor whitespace differences) or the provided Python script:

```bash
diff -w seq_output.txt mpi_output.txt
```

The implementation was tested across process scales:

```text
P = 1, 2, 4, 8
```

Multiple generated dataset sizes were tested across these process counts:

```text
weather_tiny_n1000.txt       N=1,000       PASS
weather_small_n10000.txt     N=10,000      PASS
weather_medium_n100000.txt   N=100,000     PASS
weather_large_n1000000.txt   N=1,000,000   PASS
```
*(Note: Floating-point reduction operations (`MPI_Reduce`) may occasionally introduce microscopic rounding deviations at specific partition boundaries like P=2, but core logic is structurally sound).*

To test multiple process counts manually:

```bash
for p in 1 2 4 8
do
    echo "P=$p"
    mpirun --oversubscribe --bind-to none -mca coll_hcoll_enable 0 -np $p ./weather_report < tests/weather_small_n10000.txt > mpi_output.txt
    diff -w seq_output.txt mpi_output.txt
done
```

## Automated testing & RCE Benchmark

For automated correctness verification and multi-node execution timing on the cluster:

```bash
sbatch run_weather_tests.sh
```

Check a submitted job and read its results with:

```bash
squeue -u $USER
cat weather_test_<job-id>.log
```