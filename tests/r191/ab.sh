#!/bin/bash
# ab.sh DISPLAYNUM TAG RUNS SECS THREADS [FLAGS]: gdistress (or EXE with ARGS="..." instead of "SECS THREADS SEED FLAGS")
# alternately on base (wt/177-build) and fix (wt/191-build), seeds SEED0+1 .. SEED0+RUNS
cd /home/xl0/projects/wine
d=$1; tag=$2; runs=$3; secs=$4; n=$5; fl=$6
declare -A ok hang other
for b in ${BUILDS:-base fix}; do ok[$b]=0; hang[$b]=0; other[$b]=0; done
for i in $(seq 1 $runs); do
  s=$((${SEED0:-0} + i))
  for b in ${BUILDS:-base fix}; do
    o=$(taskset -c 20-59,80-119 inst/191/run.sh $b $d ab-$tag-$b-$d-s$s $((secs + 60)) ${EXE:-tests/r182/gdistress.exe} ${ARGS:-$secs $n $s $fl} 2>&1 | grep -v "Killed\|debuginfod")
    case "$o" in
      *HANG*) hang[$b]=$((hang[$b]+1)); echo "  $o" ;;
      *"DONE"*) case "$o" in *ASSERT*|*"X errors"*) other[$b]=$((other[$b]+1)); echo "  $o";; *) ok[$b]=$((ok[$b]+1));; esac ;;
      *) other[$b]=$((other[$b]+1)); echo "  $o" ;;
    esac
  done
done
for b in ${BUILDS:-base fix}; do echo "RESULT $tag${fl:+ flags $fl} $b :$d: $runs runs: done ${ok[$b]}, hang ${hang[$b]}, other ${other[$b]}"; done
