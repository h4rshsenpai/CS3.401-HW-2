#!/usr/bin/env python3
"""Compare the sequential Q8 output with the MPI Q8 output."""

import argparse
import subprocess
import sys


def run(command):
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        raise RuntimeError("command failed: " + " ".join(command))
    return result.stdout


def normalize(output):
    # Ignore trailing spaces and blank lines, but preserve output order and values.
    return "\n".join(" ".join(line.split()) for line in output.splitlines() if line.split())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="generated weather input file")
    parser.add_argument("--sequential", required=True, help="sequential executable")
    parser.add_argument("--mpi", required=True, help="MPI executable")
    parser.add_argument("-p", "--processes", type=int, default=4)
    parser.add_argument("--mpirun", default="mpirun")
    args = parser.parse_args()

    sequential_output = run([args.sequential, args.input])
    mpi_output = run(args.mpirun.split() + ["-np", str(args.processes), args.mpi, args.input]) 
    if normalize(sequential_output) != normalize(mpi_output):
        print("FAIL: sequential and MPI output differ", file=sys.stderr)
        print("\n--- sequential output ---\n" + sequential_output, file=sys.stderr)
        print("\n--- MPI output ---\n" + mpi_output, file=sys.stderr)
        sys.exit(1)

    print(f"PASS: sequential and MPI output match with {args.processes} processes")


if __name__ == "__main__":
    main()
