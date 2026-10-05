#!/bin/bash
# vs.sh BUILD DISPLAYNUM RUNS [N]: vstate.exe loop N (default 60), RUNS times; one line per run + total of moved windows
cd /home/xl0/projects/wine
b=$1; d=$2; runs=$3; n=${4:-60}; tot=0; bad=0
for r in $(seq 1 $runs); do
  o=$(inst/182/run.sh $b $d vs-$b-$d-r$r 300 tests/r184/vstate.exe loop $n 2>&1 | grep -v Killed)
  echo "  $o"
  m=$(echo "$o" | sed -n 's/.*moved \([0-9]*\),.*/\1/p')
  if [ -n "$m" ]; then tot=$((tot + m)); else bad=$((bad + 1)); fi
done
echo "vstate loop $n $b :$d: $runs runs, moved $tot of $((runs * n * 2)), runs without a result $bad"
