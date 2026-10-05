#!/bin/bash
# dcchurn.sh BUILD DISPLAYNUM THREADS RUNS [SECS]: inst/173-review/iconrace.exe dcchurn SECS THREADS, RUNS times (182's check); summary line
cd /home/xl0/projects/wine
b=$1; d=$2; n=$3; runs=$4; secs=${5:-10}; done=0; ab=0; hang=0; other=0
for r in $(seq 1 $runs); do
  o=$(taskset -c 20-59,80-119 inst/177/run.sh $b $d dcchurn-$b-t$n-r$r $((secs + 30)) inst/173-review/iconrace.exe dcchurn $secs $n 2>&1 | grep -v "Killed\|debuginfod")
  case "$o" in
    *ASSERT*) ab=$((ab+1)) ;;
    *HANG*) hang=$((hang+1)) ;;
    *"DONE dcchurn"*) done=$((done+1)) ;;
    *) other=$((other+1)); echo "  $o" ;;
  esac
done
echo "RESULT dcchurn $b threads $n: $runs runs: done $done, _XAllocID assertion $ab, hang $hang, other $other"
