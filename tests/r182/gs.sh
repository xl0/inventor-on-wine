#!/bin/bash
# gs.sh BUILD DISPLAYNUM TAG RUNS SECS THREADS [FLAGS]: gdistress.exe SECS THREADS SEED [FLAGS] for seeds 1..RUNS; summary line
# (WINEDEBUG is passed through: use WINEDEBUG=err+all,+synchronous for synchronous mode)
cd /home/xl0/projects/wine
b=$1; d=$2; tag=$3; runs=$4; secs=$5; n=$6; fl=$7; ok=0; ab=0; hang=0; xe=0; other=0; ops=0
for s in $(seq 1 $runs); do
  o=$(taskset -c 20-59,80-119 inst/182/run.sh $b $d gs-$tag-$b-$d-s$s $((secs + 60)) tests/r182/gdistress.exe $secs $n $s $fl 2>&1 | grep -v "Killed\|debuginfod")
  case "$o" in
    *ASSERT*) ab=$((ab+1)); echo "  $o" ;;
    *HANG*) hang=$((hang+1)); echo "  $o" ;;
    *"X errors"*) xe=$((xe+1)); echo "  $o" ;;
    *"DONE gdistress"*) ok=$((ok+1)); ops=$((ops + $(echo "$o" | sed 's/.*: \([0-9]*\) operations.*/\1/'))) ;;
    *) other=$((other+1)); echo "  $o" ;;
  esac
done
echo "gdistress $tag${fl:+ flags $fl} $b :$d threads $n ${secs}s: $runs runs: done $ok ($ops operations), _XAllocID assertion $ab, hang $hang, X error $xe, other $other"
