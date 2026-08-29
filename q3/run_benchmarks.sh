#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BIN="${BITONIC_BIN:-./bitonic}"
SEED="${BITONIC_SEED:-12345}"
REPEATS="${BENCHMARK_REPEATS:-5}"
RESULT_DIR="${RESULT_DIR:-results_${SLURM_JOB_ID:-local}}"
PROCESSES=(1 2 4 8)
LABELS=(Small Medium Large Very_large)
SIZES=(4096 65536 1048576 4194304)

[[ -x "$BIN" ]] || { echo "Missing executable: $BIN" >&2; exit 1; }
[[ "$REPEATS" =~ ^[1-9][0-9]*$ ]] || { echo "BENCHMARK_REPEATS must be positive" >&2; exit 1; }
mkdir -p "$RESULT_DIR/samples"

median() {
    sort -n | awk 'NR == int((n + 1) / 2) { print; exit }' n="$1"
}

declare -A TIMES SPEEDUPS EFFICIENCIES
for index in "${!SIZES[@]}"; do
    label="${LABELS[$index]}"
    n="${SIZES[$index]}"
    for p in "${PROCESSES[@]}"; do
        samples="$RESULT_DIR/samples/${label}_P${p}.seconds"
        : > "$samples"
        echo "Benchmarking N=$n P=$p ($REPEATS repetitions)"
        for ((run = 1; run <= REPEATS; run++)); do
            mpirun --bind-to none --mca coll_hcoll_enable 0 -np "$p" "$BIN" "$n" "$SEED" --benchmark >> "$samples"
            time_value="$(tail -n 1 "$samples")"
            [[ "$time_value" =~ ^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$ ]] || {
                echo "Invalid benchmark output for N=$n P=$p: $time_value" >&2
                exit 1
            }
        done
        TIMES["$label,$p"]="$(median "$REPEATS" < "$samples")"
    done

    base="${TIMES[$label,1]}"
    for p in "${PROCESSES[@]}"; do
        elapsed="${TIMES[$label,$p]}"
        SPEEDUPS["$label,$p"]="$(awk -v base="$base" -v elapsed="$elapsed" 'BEGIN { printf "%.2f", base / elapsed }')"
        EFFICIENCIES["$label,$p"]="$(awk -v speedup="${SPEEDUPS[$label,$p]}" -v p="$p" 'BEGIN { printf "%.2f", speedup / p }')"
    done
done

REPORT="$RESULT_DIR/report.md"
{
    echo '# Q3 Bitonic Sort Benchmark Report'
    echo
    echo '## Results Table'
    echo
    echo 'Speed-up $S(P) = T_1 / T_P$'
    echo
    echo '| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |'
    echo '|------------|:-----:|:-----:|:-----:|:-----:|'
    for label in "${LABELS[@]}"; do
        echo "| ${label//_/ } | ${SPEEDUPS[$label,1]} | ${SPEEDUPS[$label,2]} | ${SPEEDUPS[$label,4]} | ${SPEEDUPS[$label,8]} |"
    done
    echo
    echo '## Efficiency'
    echo
    echo 'Efficiency $E(P) = S(P) / P$'
    echo
    echo '| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |'
    echo '|------------|:-----:|:-----:|:-----:|:-----:|'
    for label in "${LABELS[@]}"; do
        echo "| ${label//_/ } | ${EFFICIENCIES[$label,1]} | ${EFFICIENCIES[$label,2]} | ${EFFICIENCIES[$label,4]} | ${EFFICIENCIES[$label,8]} |"
    done
    echo
    echo '## Runtime Table (seconds)'
    echo
    echo '| Input size | $P=1$ | $P=2$ | $P=4$ | $P=8$ |'
    echo '|------------|:-----:|:-----:|:-----:|:-----:|'
    for label in "${LABELS[@]}"; do
        echo "| ${label//_/ } | ${TIMES[$label,1]} | ${TIMES[$label,2]} | ${TIMES[$label,4]} | ${TIMES[$label,8]} |"
    done
    echo
    echo '## Implementation Notes'
    echo
    echo "- Command: \`mpirun --bind-to none --mca coll_hcoll_enable 0 -np P $BIN N $SEED --benchmark\`"
    echo "- Each value is the median of $REPEATS timing-only runs."
    echo "- Seed: $SEED. Raw timing samples are in \`samples/\`."
    echo '- Correctness is checked separately with `./check_correctness.sh`.'
} > "$REPORT"

echo "Benchmark complete. Wrote $REPORT"
