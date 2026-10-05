#!/bin/bash
# ab.sh DISPLAYNUM TAG RUNS SECS THREADS [FLAGS]: gdistress alternately on base (integ tip, wt/182-base-build) and fix, same seeds
cd /home/xl0/projects/wine
d=$1; tag=$2; runs=$3; secs=$4; n=$5; fl=$6
declare -A ok hang other
for b in base fix; do ok[$b]=0; hang[$b]=0; other[$b]=0; done
for s in $(seq 1 $runs); do
  for b in base fix; do
    o=$(taskset -c 20-59,80-119 inst/182/run.sh $b $d ab-$tag-$b-$d-s$s $((secs + 60)) tests/r182/gdistress.exe $secs $n $s $fl 2>&1 | grep -v "Killed\|debuginfod")
    case "$o" in
      *HANG*) hang[$b]=$((hang[$b]+1)); echo "  $o" ;;
      *"DONE gdistress"*) case "$o" in *ASSERT*|*"X errors"*) other[$b]=$((other[$b]+1)); echo "  $o";; *) ok[$b]=$((ok[$b]+1));; esac ;;
      *) other[$b]=$((other[$b]+1)); echo "  $o" ;;
    esac
  done
done
for b in base fix; do echo "gdistress $tag${fl:+ flags $fl} $b :$d threads $n ${secs}s: $runs runs: done ${ok[$b]}, hang ${hang[$b]}, other ${other[$b]}"; done
