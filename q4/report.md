# Q4 Distributed Triangle Counting Benchmark Report

## Results Table

Speed-up $S(P) = T_1 / T_P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small (`rand_1.txt`) | 1.00 | 0.26 | 0.26 | 0.01 |
| Medium (`test_star.txt`) | 1.00 | 0.70 | 0.50 | 0.06 |
| Large (`test_wheel.txt`) | 1.00 | 1.05 | 0.44 | 0.13 |
| Very large (`test_complete.txt`) | 1.00 | 0.80 | 1.43 | 1.30 |

## Efficiency

Efficiency $E(P) = S(P) / P$

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small (`rand_1.txt`) | 1.00 | 0.13 | 0.06 | 0.00 |
| Medium (`test_star.txt`) | 1.00 | 0.35 | 0.12 | 0.01 |
| Large (`test_wheel.txt`) | 1.00 | 0.52 | 0.11 | 0.02 |
| Very large (`test_complete.txt`) | 1.00 | 0.40 | 0.36 | 0.16 |

## Runtime Table (seconds)

| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |
|------------|:-----:|:-----:|:-----:|:-----:|
| Small (`rand_1.txt`) | 0.000296 | 0.001140 | 0.001151 | 0.047866 |
| Medium (`test_star.txt`) | 0.003860 | 0.005501 | 0.007744 | 0.065354 |
| Large (`test_wheel.txt`) | 0.010598 | 0.010104 | 0.024326 | 0.082955 |
| Very large (`test_complete.txt`) | 0.248137 | 0.309840 | 0.172984 | 0.190791 |

## Analysis of Communication versus Computation

The speedup is consistently low (less than 1.0) for small and medium inputs because the raw computational workload takes only fractions of a millisecond. Consequently, the fixed MPI overhead—specifically the heavy `MPI_Alltoallv` edge-sharding phases and network synchronization—vastly exceeds the time spent actually counting triangles. For the Very large dense graph (`test_complete.txt`), the two-pointer intersection phase requires enough computation to finally outpace the communication penalty, achieving a peak speedup of 1.43× at $P=4$. However, efficiency remains bounded at 0.36 because distributing graph adjacencies inherently requires global communication, limiting linear scalability across higher core counts.

## Implementation notes

- Command: `mpirun --oversubscribe --bind-to none -mca coll_hcoll_enable 0 -np P ./triangle_count_distributed < [input_file]`
- Performance timings capture only the algorithmic execution (excluding initial MPI setup) via `MPI_Wtime()`.
- Correctness is verified across 8 random graphs and 4 edge-case structural topologies (Complete, Star, Wheel, Disjoint) using the independent `brute_force.py` implementation. 
- All expected outputs achieved a 100% `[OK]` validation match across all process scales.