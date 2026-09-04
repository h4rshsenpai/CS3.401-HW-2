#!/bin/bash
#SBATCH --job-name=tri_timed_tests
#SBATCH --nodes=2                  
#SBATCH --ntasks-per-node=4        
#SBATCH --cpus-per-task=1          
#SBATCH --time=00:25:00            
#SBATCH --output=timed_tests_%j.log        
#SBATCH --partition=debug          

module load hpcx-2.7.0/hpcx-ompi

echo "=== Compiling Timed Code ==="
mpicc -O3 -o triangle_count_distributed triangle_count_distributed.c

echo ""
echo "=== Running Random Graph Tests (Timed) ==="
for i in {1..8}; do
  f="tests/rand_$i.txt"
  if [ ! -f "$f" ]; then continue; fi
  
  V=$(head -1 $f | cut -d' ' -f1)
  E=$(head -1 $f | cut -d' ' -f2)
  expected=$(python3 brute_force.py < $f)
  
  for p in 1 2 4 8; do
    # Added --oversubscribe and --bind-to none to prevent slot allocation crashes
    mpirun --oversubscribe --bind-to none -mca coll_hcoll_enable 0 -np $p ./triangle_count_distributed < $f > out.tmp 2> err.tmp
    got=$(cat out.tmp | tr -d '[:space:]')
    perf=$(cat err.tmp | grep "\[PERF\]")
    
    status="OK"
    if [ "$expected" != "$got" ]; then status="MISMATCH"; fi
    echo "tests/rand_$i.txt (V=$V, E=$E) np=$p -> expected=$expected got=$got [$status] | $perf"
  done
done

echo ""
echo "=== Running Topology Edge-Case Tests (Timed) ==="
declare -A custom_tests=(
  ["tests/test_complete.txt"]="166167000"
  ["tests/test_star.txt"]="0"
  ["tests/test_wheel.txt"]="99999"
  ["tests/test_disjoint.txt"]="33333"
)

for f in "${!custom_tests[@]}"; do
  expected="${custom_tests[$f]}"
  if [ ! -f "$f" ]; then continue; fi

  V=$(head -1 $f | cut -d' ' -f1)
  E=$(head -1 $f | cut -d' ' -f2)
  
  for p in 1 2 4 8; do
    mpirun --oversubscribe --bind-to none -mca coll_hcoll_enable 0 -np $p ./triangle_count_distributed < $f > out.tmp 2> err.tmp
    got=$(cat out.tmp | tr -d '[:space:]')
    perf=$(cat err.tmp | grep "\[PERF\]")
    
    status="OK"
    if [ "$expected" != "$got" ]; then status="MISMATCH"; fi
    echo "$f (V=$V, E=$E) np=$p -> expected=$expected got=$got [$status] | $perf"
  done
done

rm -f out.tmp err.tmp
echo ""
echo "=== All Timed Tests Completed ==="