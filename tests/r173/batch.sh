#!/bin/bash
# batch.sh BUILD TAG RUNS TIMEOUT EXE ARGS...: RUNS runs without a WM (:1410) and RUNS with openbox (:1411); summary line per display
cd /home/xl0/projects/wine
b=$1; tag=$2; n=$3; t=$4; shift 4
for d in ${D:-1410 1411}; do
  ok=0; hang=0; other=0
  for ((i = 1; i <= n; i++)); do
    r=$(inst/173/run.sh $b $d $tag-$b-$d-$i $t "$@" 2>&1 | grep -v Killed)
    case "$r" in *HANG*) hang=$((hang+1));; *DONE*"X errors 0"*) ok=$((ok+1));; *) other=$((other+1)); echo "  $r";; esac
  done
  echo "$tag $b :$d ($(case $d in 1411|1415) echo openbox;; *) echo no WM;; esac)): ok $ok hang $hang other $other of $n"
done
