#!/bin/bash
# tests/r181/rep.sh TAG RENDERER N PROBE ARGS..: exit codes of N runs of a tests/ probe (181)
cd /home/xl0/projects/wine
T=$1 R=$2 N=$3; shift 3
. tests/r181/env.sh $T 98
export WINE_D3D_CONFIG=renderer=$R
xdotool mousemove 1900 1060
ok=0; first=
for i in $(seq $N); do
  timeout 90 "$B/wine" tests/$1 ${@:2} >inst/181/r/rep.out 2>/dev/null </dev/null && ok=$((ok+1)) || first="${first:-$(tail -1 inst/181/r/rep.out | cut -c1-110)}"
done
echo "$T $R $*: $ok of $N ok ${first:+| first failure: $first}"
"$B/server/wineserver" -k 2>/dev/null; sleep 1
