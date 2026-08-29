#!/bin/bash

cd "$(dirname "$0")"
failed=0

for p in 1 2 4 8; do
    for multiplier in 1 2 4; do
        n=$((p * multiplier))
        echo "Running P=$p N=$n"
        output="$(mpirun --use-hwthread-cpus -np "$p" ./bitonic "$n" 12345)" || {
            echo "FAIL: program did not run for P=$p N=$n"
            failed=1
            continue
        }

        mapfile -t lines < <(printf '%s\n' "$output" | awk 'NF')
        if [[ ${#lines[@]} -ne 3 ]]; then
            echo "FAIL: expected seed array, sorted array, and time"
            echo "Program output:"
            printf '%s\n' "$output"
            failed=1
            continue
        fi

        expected="$(printf '%s\n' "${lines[0]}" | tr ' ' '\n' | sort -n | xargs)"
        actual="$(printf '%s\n' "${lines[1]}" | xargs)"
        if [[ "$actual" == "$expected" ]]; then
            echo "PASS: P=$p N=$n"
        else
            echo "FAIL: sorted array is incorrect for P=$p N=$n"
            echo "Generated array: ${lines[0]}"
            echo "Expected sorted array: $expected"
            echo "Program sorted array: ${lines[1]}"
            failed=1
        fi

    done
done

exit "$failed"
