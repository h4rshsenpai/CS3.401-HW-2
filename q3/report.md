# Q3 Bitonic Sort Benchmark Report

## Results Table

Speed-up $S(P) = T_1 / T_P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small | 1.00 | 0.77 | 0.84 | 0.07 |
| Medium | 1.00 | 1.51 | 2.32 | 2.16 |
| Large | 1.00 | 1.56 | 1.62 | 2.44 |
| Very large | 1.00 | 1.58 | 2.46 | 2.58 |

## Efficiency

Efficiency $E(P) = S(P) / P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small | 1.00 | 0.39 | 0.21 | 0.01 |
| Medium | 1.00 | 0.76 | 0.58 | 0.27 |
| Large | 1.00 | 0.78 | 0.41 | 0.30 |
| Very large | 1.00 | 0.79 | 0.61 | 0.32 |

## Runtime Table (seconds)

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small | 0.000318809 | 0.000411775 | 0.000378707 | 0.00480859 |
| Medium | 0.00628942 | 0.00415696 | 0.00271119 | 0.00291831 |
| Large | 0.122845 | 0.0787064 | 0.0756183 | 0.0502605 |
| Very large | 0.537972 | 0.340454 | 0.218581 | 0.208455 |

## Analysis of Communication versus Computation

The speedup is low for small inputs because MPI communication overhead is larger than the sorting work.  
This is especially visible for P=8 , where the small input becomes slower than sequential execution. As 
the input size increases, the computation cost becomes large enough to benefit from parallelism. For the
case of very large input, P=8 gives a speedup of 2.58×, but efficiency is only 0.32 because Bitonic Sort 
requires multiple communication and synchronization stages between processes. Overall, using more processes
helps for large inputs, but communication limits scalability.

## Implementation notes

- Command: `mpirun --bind-to none --mca coll_hcoll_enable 0 -np P ./bitonic N 12345 --benchmark`
- Each value is the median of 5 timing-only runs.
- Seed: 12345. Raw timing samples are in `samples/`.
- Correctness is checked separately with `./check_correctness.sh`.
