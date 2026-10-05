#!/bin/bash
# batch.sh BUILD DISPLAYNUM TAG RUNS TIMEOUT EXE ARGS...: RUNS runs under the watchdog, one summary line
cd /home/xl0/projects/wine
b=$1; d=$2; tag=$3; n=$4; t=$5; shift 5
ok=0; hang=0; other=0
for ((i = 1; i <= n; i++)); do
  r=$(inst/177/run.sh $b $d $tag-$b-$d-$i $t "$@" 2>&1 | grep -v "Killed\|debuginfod")
  case "$r" in *HANG*) hang=$((hang+1)); echo "  $r";; *"X errors"*|*ASSERT*) other=$((other+1)); echo "  $r";; *DONE*) ok=$((ok+1));; *) other=$((other+1)); echo "  $r";; esac
done
echo "$tag $b :$d: ok $ok hang $hang other $other of $n  [$(basename $1) ${*:2}]"
