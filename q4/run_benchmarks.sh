#!/bin/bash

echo "=== Running Random Graph Tests ==="
for i in {1..8}; do
  f="tests/rand_$i.txt"  # <--- Updated path to tests/ folder
  if [ ! -f "$f" ]; then
    echo "File $f not found, skipping..."
    continue
  fi
  
  V=$(head -1 $f | cut -d' ' -f1)
  E=$(head -1 $f | cut -d' ' -f2)
  expected=$(python3 brute_force.py < $f)
  
  for p in 1 2 4 8; do
    # <--- Updated binary path (now in current folder ./triangle_count_distributed)
    got=$(mpirun --bind-to none -mca coll_hcoll_enable 0 -np $p ./triangle_count_distributed < $f)
    status="OK"
    if [ "$expected" != "$got" ]; then status="MISMATCH"; fi
    echo "test $i (V=$V E=$E) np=$p: expected=$expected got=$got -> $status"
  done
done

echo ""
echo "=== Running Topology Edge-Case Tests ==="
declare -A custom_tests=(
  ["tests/test_complete.txt"]="166167000"
  ["tests/test_star.txt"]="0"
  ["tests/test_wheel.txt"]="99999"
  ["tests/test_disjoint.txt"]="33333"
)

for f in "${!custom_tests[@]}"; do
  expected="${custom_tests[$f]}"
  
  if [ ! -f "$f" ]; then
    echo "File $f not found, skipping..."
    continue
  fi

  V=$(head -1 $f | cut -d' ' -f1)
  E=$(head -1 $f | cut -d' ' -f2)
  
  for p in 1 2 4 8; do
    got=$(mpirun --bind-to none -mca coll_hcoll_enable 0 -np $p ./triangle_count_distributed < $f)
    status="OK"
    if [ "$expected" != "$got" ]; then status="MISMATCH"; fi
    echo "$f (V=$V E=$E) np=$p: expected=$expected got=$got -> $status"
  done
done