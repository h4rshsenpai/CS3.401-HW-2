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

The checker runs the sequential executable once and the MPI executable with the requested process count.
It passes only when their output matches, ignoring blank lines and extra whitespace.

## Run

```bash
mpirun -np 4 ./weather_report data.txt
```

## Smoke Test

```bash
python3 generate_weather.py test_data.txt -n 10000 -k 5 -s 50 --seed 1
mpirun -np 1 ./weather_report test_data.txt
mpirun -np 2 ./weather_report test_data.txt
mpirun -np 4 ./weather_report test_data.txt
```

After adding the sequential implementation and final report output, run `check_weather.py` with the two executables as shown above.


## Implementation Status

`weather_report.cpp` is currently an unfinished naive approach: Rank 0 reads the entire input and scatters the records.
No sequential implementation as of now. The implementation and the sequential reference must be completed before using `check_weather.py`.

## Design and Complexity Roadmap

### Stage 1: Naive approach - Centralized reading and reduction

Rank 0 reads all `N` records and uses `MPI_Scatterv` to distribute contiguous
groups of `Measurement` values. Each rank computes scalar aggregates and
maintains two local hashmaps:

1. `station_id -> {count, temperature_sum, rainfall_sum}` for Top-K stations.
2. `timestamp / 60 -> count` for busiest intervals.

Scalar values are combined with `MPI_Reduce`. Each rank also provides one local
hottest and coldest measurement candidate; rank 0 selects the global candidate
using the required tie rules. The maps are flattened into entry arrays and sent
to rank 0, which merges and evaluates them.


- Rank-0 input work -> `O(N)`
- Per-rank local record work -> `O(N / P)`
- Root-to-rank scatter -> `O(N)`
- Root map merge -> `O(sum of local unique keys)` -> worst case `O(N)`
- Root Top-K selection after merge -> `O(S log S)`

This version is straightforward but rank 0 will bottleck the system.

### Stage 2: Parallel file reading

Remove the full root-side record vector and `MPI_Scatterv`. Rank 0 reads and
broadcasts only the header (`N`, `K`, `S`), the byte offset after the header,
and file size. Each rank reads a non-overlapping byte range of the records
section from the shared input file.

Because input is newline-delimited text, a rank starting inside a line must discard that partial line.
This ensures each record is processed exactly once. Parsing and record storage become approximately `O(N / P)`
per rank, with no `O(N)` requirement for root memory and distribution.

### Stage 3: Redistribute and merge partial aggregates

Each rank first combines its own records, but a station or interval may still
have partial statistics on multiple ranks. Assign every station and interval
key to one owner rank:

```text
owner(key) = key % P
```

Flatten the populated local map entries. First exchange the number of entries
for each destination using `MPI_Alltoall`, then exchange the entries using
`MPI_Alltoallv`. Each owner merges the entries it receives and now holds the
complete statistics for its assigned keys.

Each rank owns about `S / P` stations. Intervals remain sparse because Q8 does
not bound the timestamp range. This removes the root-side merge of every
station and interval aggregate.

### Stage 4: Final Gather

Each station owner selects its local Top-K from its complete station statistics.
Each interval owner selects its one busiest interval. Rank 0 gathers at most
`K` station candidates and one interval candidate from every rank.

Rank 0 therefore selects the global Top-K from at most `P * K` stations and
the busiest interval from `P` interval candidates. This is correct because a
station in the global Top-K must also be in the local Top-K of its owner rank.
Sorting the gathered station candidates costs `O(P * K * log(P * K))` at rank
0. For equal busiest counts, ties are deterministic.

Scalar aggregates still use `MPI_Reduce`. Hottest and coldest remain one
`Measurement` candidate per rank, gathered to rank 0. The root receives only
these small final candidate sets, rather than every aggregate entry.
