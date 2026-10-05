#!/bin/bash
# cost.sh ROUNDS: single-threaded CreateCompatibleDC/DeleteDC cost, base (integ b8f013d4fbb, wt/182-base-build) vs fix, alternating, one core
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
for r in $(seq 1 $1); do
  for b in base fix; do
    case $b in base) B=$PWD/wt/182-base-build; d=1486 ;; fix) B=$PWD/wt/182-build; d=1485 ;; esac
    n=$(DISPLAY=:$d WINEPREFIX=$PWD/inst/182/pfx-$b-$d WINEDEBUG=-all taskset -c 33 $B/wine inst/173-review/iconrace.exe dcchurn 5 1 2>/dev/null | sed -n 's/.*: \([0-9]*\) DCs.*/\1/p' | tr -d '\r')
    echo "$b $n DCs in 5 s = $(echo "scale=3; 5000000 / $n" | bc) us"
  done
done
