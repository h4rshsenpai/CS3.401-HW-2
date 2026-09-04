#!/usr/bin/env python3
"""
generate_dataset.py — Reproducible weather-station measurement dataset generator.

Output format:
    Line 1:            N K S
    Next N lines:      timestamp station_id temperature humidity pressure rainfall wind_speed

Where:
    N = number of measurement records
    K = "top-K stations" parameter used by the analysis program
    S = number of distinct station IDs (stations are numbered 1..S)

Field ranges (chosen to be realistic and to reliably exercise the
EXTREME_TEMPERATURE_EVENTS rule, i.e. temperature >= 40.0 or <= 0.0):
    timestamp     : integer, monotonically non-decreasing "seconds since epoch"
                    starting at TIMESTAMP_START, average gap TIMESTAMP_STEP_MEAN,
                    with random jitter and occasional "burst" clusters so that
                    BUSIEST_INTERVAL is a meaningful, non-trivial computation.
    station_id    : uniform random int in [1, S]      (skewed via zipf-like weights
                    so TOP_STATIONS is non-trivial: a few stations report far more often)
    temperature   : float in [-15.0, 48.0], 1 decimal place, normally distributed
                    around 22.0 with a long tail so extreme events occur naturally
    humidity      : float in [0.0, 100.0], 1 decimal place
    pressure      : float in [950.0, 1050.0], 1 decimal place
    rainfall      : float in [0.0, 80.0], 1 decimal place (mostly 0, occasional spikes)
    wind_speed    : float in [0.0, 40.0], 1 decimal place

Reproducibility:
    All randomness is driven by Python's `random.Random(seed)`. The same
    (n, k, s, seed) tuple always produces byte-identical output, regardless
    of machine, OS, or Python minor version (only stdlib `random` is used,
    whose algorithm — Mersenne Twister — is stable across CPython versions).

Usage:
    python3 generate_dataset.py --n 1000000 --k 10 --s 500 --seed 42 -o data_1M.txt

Batch-generate the standard benchmark suite:
    python3 generate_dataset.py --suite -o-dir ./datasets
"""

import argparse
import bisect
import random
import sys
import os

# ---- Fixed default parameters (documented for reproducibility) ----
DEFAULT_SEED = 42
TIMESTAMP_START = 1_700_000_000     # arbitrary fixed epoch start (2023-11-14T22:13:20Z)
TIMESTAMP_STEP_MEAN = 2             # avg seconds between consecutive records
BURST_PROBABILITY = 0.05            # probability a record starts a "burst" cluster
BURST_LEN_RANGE = (5, 40)           # number of records fired within same/near-same second
TEMP_MEAN, TEMP_SD = 22.0, 9.0
TEMP_MIN, TEMP_MAX = -15.0, 48.0
HUMIDITY_MIN, HUMIDITY_MAX = 0.0, 100.0
PRESSURE_MEAN, PRESSURE_SD = 1000.0, 12.0
PRESSURE_MIN, PRESSURE_MAX = 950.0, 1050.0
RAIN_ZERO_PROB = 0.75               # most records report no rainfall
RAIN_MAX = 80.0
WIND_MEAN, WIND_SD = 8.0, 6.0
WIND_MIN, WIND_MAX = 0.0, 40.0
EXTREME_TEMP_TARGET_FRACTION = 0.01  # ~1% of records forced into extreme range


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def zipf_station_weights(rng: random.Random, s: int):
    """A few stations report much more often than others (skewed, but deterministic)."""
    weights = []
    for i in range(1, s + 1):
        weights.append(1.0 / i)  # station 1 heaviest, tapering off
    rng.shuffle(weights)  # so it's not just station 1 that's busiest
    return weights


def generate(n: int, k: int, s: int, seed: int, out_path: str):
    rng = random.Random(seed)
    station_weights = zipf_station_weights(rng, s)
    station_ids = list(range(1, s + 1))

    # Precompute cumulative weights once so each station pick is O(log S)
    # instead of O(S) (rng.choices() rebuilds cumulative weights every call).
    total_w = sum(station_weights)
    cum_weights = []
    running = 0.0
    for w in station_weights:
        running += w
        cum_weights.append(running)

    def pick_station():
        x = rng.random() * total_w
        idx = bisect.bisect_right(cum_weights, x)
        if idx >= s:
            idx = s - 1
        return station_ids[idx]

    ts = TIMESTAMP_START
    i = 0
    buf = []
    BUF_FLUSH = 200_000

    with open(out_path, "w") as f:
        f.write(f"{n} {k} {s}\n")
        while i < n:
            # Occasionally emit a burst of records within the same short interval
            if rng.random() < BURST_PROBABILITY:
                burst_len = min(rng.randint(*BURST_LEN_RANGE), n - i)
            else:
                burst_len = 1

            for _ in range(burst_len):
                if i >= n:
                    break
                station = pick_station()

                # Force a small fraction of records to be extreme temperature events
                if rng.random() < EXTREME_TEMP_TARGET_FRACTION:
                    if rng.random() < 0.5:
                        temp = rng.uniform(40.0, TEMP_MAX)
                    else:
                        temp = rng.uniform(TEMP_MIN, 0.0)
                else:
                    temp = rng.gauss(TEMP_MEAN, TEMP_SD)
                    temp = clamp(temp, TEMP_MIN, TEMP_MAX)
                temp = round(temp, 1)

                humidity = round(clamp(rng.gauss(55.0, 20.0), HUMIDITY_MIN, HUMIDITY_MAX), 1)
                pressure = round(clamp(rng.gauss(PRESSURE_MEAN, PRESSURE_SD), PRESSURE_MIN, PRESSURE_MAX), 1)

                if rng.random() < RAIN_ZERO_PROB:
                    rainfall = 0.0
                else:
                    rainfall = round(rng.uniform(0.0, RAIN_MAX), 1)

                wind = round(clamp(abs(rng.gauss(WIND_MEAN, WIND_SD)), WIND_MIN, WIND_MAX), 1)

                buf.append(f"{ts} {station} {temp:.1f} {humidity:.1f} {pressure:.1f} {rainfall:.1f} {wind:.1f}")
                i += 1

                if len(buf) >= BUF_FLUSH:
                    f.write("\n".join(buf) + "\n")
                    buf = []

            # advance timestamp (records within a burst share timestamps close together)
            step = max(0, int(rng.expovariate(1.0 / TIMESTAMP_STEP_MEAN)))
            ts += step

        if buf:
            f.write("\n".join(buf) + "\n")

    return out_path


STANDARD_SUITE = [
    # (name, n, k, s)
    ("tiny",    1_000,       5,   20),
    ("small",   10_000,      5,   50),
    ("medium",  100_000,     10,  200),
    ("large",   1_000_000,   10,  500),
    ("xlarge",  5_000_000,   15,  1000),
    ("xxlarge", 20_000_000,  20,  2000),
]


def main():
    ap = argparse.ArgumentParser(description="Generate reproducible weather-station benchmark datasets.")
    ap.add_argument("--n", type=int, help="number of measurement records")
    ap.add_argument("--k", type=int, default=10, help="top-K stations parameter (default 10)")
    ap.add_argument("--s", type=int, help="number of distinct station IDs (default: max(10, n // 2000))")
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED, help=f"RNG seed (default {DEFAULT_SEED})")
    ap.add_argument("-o", "--output", type=str, help="output file path (single-dataset mode)")
    ap.add_argument("--suite", action="store_true", help="generate the standard benchmark suite instead")
    ap.add_argument("-o-dir", dest="out_dir", type=str, default="./datasets",
                     help="output directory for --suite mode (default ./datasets)")
    args = ap.parse_args()

    if args.suite:
        os.makedirs(args.out_dir, exist_ok=True)
        manifest = []
        for name, n, k, s in STANDARD_SUITE:
            path = os.path.join(args.out_dir, f"weather_{name}_n{n}.txt")
            generate(n, k, s, DEFAULT_SEED, path)
            size_bytes = os.path.getsize(path)
            manifest.append((name, n, k, s, DEFAULT_SEED, path, size_bytes))
            print(f"[ok] {name:8s} N={n:>10,} K={k:<3} S={s:<5} seed={DEFAULT_SEED} "
                  f"-> {path} ({size_bytes/1e6:.2f} MB)")
        # Write manifest
        man_path = os.path.join(args.out_dir, "MANIFEST.txt")
        with open(man_path, "w") as f:
            f.write("name,n,k,s,seed,path,size_bytes\n")
            for row in manifest:
                f.write(",".join(str(x) for x in row) + "\n")
        print(f"[ok] manifest written to {man_path}")
        return

    if args.n is None or args.output is None:
        ap.error("either --suite, or both --n and --output, are required")

    s = args.s if args.s is not None else max(10, args.n // 2000)
    path = generate(args.n, args.k, s, args.seed, args.output)
    size_bytes = os.path.getsize(path)
    print(f"[ok] N={args.n:,} K={args.k} S={s} seed={args.seed} -> {path} ({size_bytes/1e6:.2f} MB)")


if __name__ == "__main__":
    sys.exit(main())
