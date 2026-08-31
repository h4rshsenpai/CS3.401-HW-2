#!/bin/bash

# We will use the Complete Graph for the stress test
f="test_complete.txt"

echo "Benchmarking Performance on $f (V=1000, E=499500)"
echo "---------------------------------------------------"

for p in 1 2 4 8; do
  echo "Running with $p process(es)..."
  # /usr/bin/time -f "%e" prints only the real time in seconds
  /usr/bin/time -f "Time: %e seconds" mpirun --allow-run-as-root --oversubscribe -np $p ../src/triangle_count_distributed < $f > /dev/null
  echo ""
done