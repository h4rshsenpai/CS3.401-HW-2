#!/bin/bash
#SBATCH --job-name=bitonic-benchmark
#SBATCH --nodes=1
#SBATCH --ntasks=8
#SBATCH --cpus-per-task=1
#SBATCH --time=01:00:00
#SBATCH --partition=debug
#SBATCH --output=bitonic_benchmark_%j.log
#SBATCH --error=bitonic_benchmark_%j.err

set -euo pipefail

cd "${SLURM_SUBMIT_DIR:?Submit this job from the q3 directory}"
[[ -f bitonic.cpp && -f run_benchmarks.sh ]] || {
    echo "Submit this job from the directory containing bitonic.cpp and run_benchmarks.sh" >&2
    exit 1
}
MPI_MODULE="${MPI_MODULE:-hpcx-2.7.0/hpcx-ompi}"

if [[ "$MPI_MODULE" != "none" ]]; then
    module load "$MPI_MODULE"
fi

mpicxx -O2 -std=c++17 -o bitonic bitonic.cpp
bash ./run_benchmarks.sh
