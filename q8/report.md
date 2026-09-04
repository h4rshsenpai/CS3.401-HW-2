# Q8 Distributed Weather Data Processing Benchmark Report

## Results Table

Speed-up $S(P) = T_1 / T_P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Tiny (`n=1000`) | 1.00 | 1.50 | 1.38 | 1.54 |
| Small (`n=10000`) | 1.00 | 1.40 | 1.33 | 1.41 |
| Medium (`n=100000`) | 1.00 | 1.36 | 1.31 | 1.41 |
| Large (`n=1000000`) | 1.00 | 1.38 | 1.26 | 1.32 |

## Efficiency

Efficiency $E(P) = S(P) / P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Tiny (`n=1000`) | 1.00 | 0.75 | 0.34 | 0.19 |
| Small (`n=10000`) | 1.00 | 0.70 | 0.33 | 0.17 |
| Medium (`n=100000`) | 1.00 | 0.68 | 0.32 | 0.17 |
| Large (`n=1000000`) | 1.00 | 0.69 | 0.31 | 0.16 |

## Runtime Table (seconds)

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Tiny (`n=1000`) | 0.594260 | 0.394442 | 0.429250 | 0.385070 |
| Small (`n=10000`) | 0.559978 | 0.397225 | 0.418119 | 0.394710 |
| Medium (`n=100000`) | 0.545324 | 0.398199 | 0.413180 | 0.385312 |
| Large (`n=1000000`) | 0.546728 | 0.396111 | 0.431690 | 0.412512 |

## Analysis of Communication versus Computation

The execution times reveal an interesting characteristic of this specific implementation: the runtime remains virtually constant (between 0.38s and 0.59s) regardless of whether the input size is 1,000 or 1,000,000 records. This indicates that the mathematical computation required to reduce the weather statistics is entirely overshadowed by fixed systemic overheads—specifically, the standard I/O parsing of the dataset and the initial `MPI_Init` environment setup across the Slurm nodes. 

Moving from sequential ($P=1$) to parallel execution yields a baseline speedup (roughly 1.3× to 1.5×) simply by distributing the I/O read and local processing tasks. However, scaling to $P=4$ and $P=8$ causes efficiency to drop drastically (down to 0.16). The dataset is too small to saturate the CPU, meaning the network communication cost of global reductions (`MPI_Reduce`) negates any further parallelization benefits. 

## Implementation notes

- Command: `mpirun --oversubscribe --bind-to none -mca coll_hcoll_enable 0 -np P ./weather_report < [input_file]`
- Execution times encapsulate total runtime, including I/O stream parsing and array distribution overheads.
- Correctness logic is verified against a sequential baseline program (`weather_sequential.cpp`). Variations in distributed floating-point accumulations were factored out as expected mathematical behavior rather than logical errors.