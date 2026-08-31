#!/bin/bash

echo "=== Running Random Graph Tests ==="
for i in 1 2 3 4 5 6 7 8; do
  f="rand_$i.txt"
  if [ ! -f "$f" ]; then
    echo "File $f not found, skipping..."
    continue
  fi
  
  V=$(head -1 $f | cut -d' ' -f1)
  E=$(head -1 $f | cut -d' ' -f2)
  expected=$(python3 brute_force.py < $f)
  
  for p in 1 2 3 5 7 9; do
    got=$(mpirun --allow-run-as-root --oversubscribe -np $p ../src/triangle_count_distributed < $f)
    status="OK"
    if [ "$expected" != "$got" ]; then status="MISMATCH"; fi
    echo "test $i (V=$V E=$E) np=$p: expected=$expected got=$got -> $status"
  done
done

echo ""
echo "=== Running Topology Edge-Case Tests ==="
# We bypass brute_force.py here because V=100,000 will hang a Python brute force script.
# Format: "filename:expected_answer"
custom_tests=(
  "test_complete.txt:166167000"
  "test_star.txt:0"
  "test_wheel.txt:99999"
  "test_disjoint.txt:33333"
)

for test_data in "${custom_tests[@]}"; do
  # Split the string by colon to get filename and expected value
  f="${test_data%%:*}"
  expected="${test_data##*:}"
  
  if [ ! -f "$f" ]; then
    echo "File $f not found (did you run the python generator?), skipping..."
    continue
  fi

  V=$(head -1 $f | cut -d' ' -f1)
  E=$(head -1 $f | cut -d' ' -f2)
  
  for p in 1 2 3 5 7 9; do
    got=$(mpirun --allow-run-as-root --oversubscribe -np $p ../src/triangle_count_distributed < $f)
    status="OK"
    if [ "$expected" != "$got" ]; then status="MISMATCH"; fi
    echo "$f (V=$V E=$E) np=$p: expected=$expected got=$got -> $status"
  done
done