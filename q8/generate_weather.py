#!/usr/bin/env python3
"""Generate reproducible Q8 weather input data."""

import argparse
import random


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="output text file")
    parser.add_argument("-n", "--records", type=int, default=1_000_000)
    parser.add_argument("-k", "--top-k", type=int, default=10)
    parser.add_argument("-s", "--stations", type=int, default=1_000)
    parser.add_argument("--seed", type=int, default=401)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    start_timestamp = 1_700_000_000

    with open(args.output, "w", encoding="utf-8") as output:
        output.write(f"{args.records} {args.top_k} {args.stations}\n")

        for _ in range(args.records):
            timestamp = start_timestamp + rng.randrange(30 * 24 * 60 * 60)
            station_id = rng.randrange(args.stations)
            temperature = rng.uniform(-10.0, 48.0)
            humidity = rng.uniform(0.0, 100.0)
            pressure = rng.uniform(950.0, 1050.0)
            rainfall = rng.uniform(0.0, 50.0)
            wind_speed = rng.uniform(0.0, 45.0)

            output.write(
                f"{timestamp} {station_id} {temperature:.2f} {humidity:.2f} "
                f"{pressure:.2f} {rainfall:.2f} {wind_speed:.2f}\n"
            )


if __name__ == "__main__":
    main()
