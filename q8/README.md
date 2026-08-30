# Large Scale Weather/Environment Data Analytics

## Build

Run these commands from the `q8` directory:

```bash
mpicxx -std=c++17 -O2 -Wall -Wextra weather_report.cpp -o weather_report
```

## Generate Input Data

The generator creates reproducible, whitespace-separated input in the required format:

```text
N K S
timestamp station_id temperature humidity pressure rainfall wind_speed
...
```

Generate one million records for 1,000 stations:

```bash
python3 generate_weather.py data.txt -n 1000000 -k 10 -s 1000 --seed 401
```

Arguments:

- `-n` / `--records`: number of measurements (default: `1000000`)
- `-k` / `--top-k`: requested number of top stations (default: `10`)
- `-s` / `--stations`: number of station IDs, numbered `0` through `S-1` (default: `1000`)
- `--seed`: random seed for reproducible datasets (default: `401`)

## Correctness Check

Once both the sequential and MPI report programs produce the final required Q8 output, compare them on the same generated input:

```bash
python3 check_weather.py test_data.txt \
  --sequential ./weather_sequential \
  --mpi ./weather_report \
  --processes 4
```

The checker runs the sequential executable once and the MPI executable with the requested process count. It passes only when their output matches, ignoring blank lines and extra whitespace.

## Run

```bash
mpirun -np 4 ./weather_report data.txt
```

The current implementation has rank 0 read the input and uses `MPI_Scatterv` to distribute `Measurement` structs evenly across ranks. When `N` is not divisible by the process count, the first `N % P` ranks each receive one additional record.

## Smoke Test

```bash
python3 generate_weather.py test_data.txt -n 10000 -k 5 -s 50 --seed 1
mpirun -np 1 ./weather_report test_data.txt
mpirun -np 2 ./weather_report test_data.txt
mpirun -np 4 ./weather_report test_data.txt
```

After adding the sequential implementation and final report output, run `check_weather.py` with the two executables as shown above.



## Design Choices and Implementation 

#### Trivial Computations (doesn't affect scalability)

- Totals
- Aggregatess
- 

#### Non-trivial Computations


### 1. Naive Approach

Have Rank 0 be the master rank that reads all input and distributes to nodes. Each node calculates its own
scalar measurements (sum, average, minimum, maximum) and 2 local maps :

1. { count : timestamp } for busiest interval 
2. { station_id : (temperature, count, rainfall_sum)} for top K station measurements

Reduce scalar values to 0 towards Rank 0. 
Each node communicates their map to Rank 0 --> Rank 0 merges the maps and prints top K stations and
busiest interval.

- Centralized approach and easy to implement but Rank 0 becomes a bottleneck on the systems efficiency.

### 2. Distributed I/O + 

Each node reads non-overlapping `N / P` sections. If N is not divisible by P, rank 0 can take that work.

Instead of merging maps, have each node be in charge of one station ID. Once 

Smarter approach would be to have each node keep their own minimum and maximum,
and shift both towards Rank 0. 

- Top K stations 

**naive approch:** A node must be 


Each node keeps maintains a local copy of top K stations and sends to rank 0 at the end of
local computations. Rank 0 collates and computes - O(K.P). 

Necessarily, if a station is the top K 
in the entire list, it must also be in the top K of the shelters 



## Naive approach
