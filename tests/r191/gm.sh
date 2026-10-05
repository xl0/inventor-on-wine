#!/bin/bash
# gm.sh BUILD DISPLAYNUM TAG RUNS ARGS...: thrwin.exe ARGS under tests/r191/guardmalloc.so on a fresh wineserver per run;
# stops at the first GUARDMALLOC report (out/gm-TAG.out)
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=$PWD/inst/191
case $1 in fix) B=$PWD/wt/191-build ;; base) B=$PWD/wt/177-build ;; esac
b=$1; d=$2; tag=$3; runs=$4; shift 4
unset WAYLAND_DISPLAY; export DISPLAY=:$d WINEPREFIX=$R/pfx-$b-$d
for i in $(seq 1 $runs); do
  $B/server/wineserver -k 2>/dev/null; $B/server/wineserver -w 2>/dev/null
  o=$R/out/gm-$tag.out
  GUARDMALLOC_ONLY=thrwin.exe LD_PRELOAD=$PWD/tests/r191/guardmalloc.so WINEDEBUG=${WINEDEBUG:-err+all} setsid nohup $B/wine tests/r191/thrwin.exe "$@" > $o 2>&1 </dev/null &
  pid=$!
  for ((s = 0; s < 300; s++)); do kill -0 $pid 2>/dev/null || break; grep -aq "GUARDMALLOC" $o && break; sleep 0.2; done
  if grep -aq "GUARDMALLOC" $o; then
    sleep 1; echo "run $i: caught"; 
    for p in $(grep -a "GUARDMALLOC: fault" $o | sed 's/.*pid \([0-9]*\) .*/\1/' | sort -u); do
      gdb -p $p -batch -ex 'source tools/gdb/winesyms.py' -ex 'thread apply all bt 25' > $R/out/gm-$tag-gdb-$p.txt 2>&1; cp /proc/$p/maps $R/out/gm-$tag-maps-$p.txt; kill -9 $p
    done
    kill -9 $pid 2>/dev/null; $B/server/wineserver -k 2>/dev/null; exit 0
  fi
  kill -0 $pid 2>/dev/null && { echo "run $i: timeout"; kill -9 $pid; } || echo "run $i: $(grep -a -o 'DONE.*\|corrupt.*\|free().*' $o | head -1)"
done
$B/server/wineserver -k 2>/dev/null
