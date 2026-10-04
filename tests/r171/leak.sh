#!/bin/bash
# Window surfaces left in win32u's list after "ulwrace MODE N 1 hold" (leaked = entries of destroyed windows).
#   leak.sh BUILD MODE N      BUILD project-relative; uses run.sh's display :1400 and prefix inst/171/pfx-BUILD
root=$(cd "$(dirname "$0")/../.." && pwd); cd $root; eval "$(tools/sysroot.sh env)"
B=$1; MODE=$2; N=$3
export DISPLAY=:1400 WINEPREFIX=$root/inst/171/pfx-$(basename $B) WINEDEBUG=-all
log=inst/171/leak-$(basename $B)-$MODE.log
setsid nohup $root/$B/wine tests/r171/ulwrace.exe $MODE $N 1 hold >$log 2>&1 </dev/null &
pid=$!
for i in $(seq 600); do grep -q -E '^(DONE|HANG|FAULT)' $log && break; kill -0 $pid 2>/dev/null || break; sleep 0.5; done
tail -n 1 $log
gdb -p $pid -batch -ex "source $root/tools/gdb/winesyms.py" -ex "source $root/tests/r171/surfaces.py" 2>&1 | grep -E "^surface|window_surfaces"
kill -9 $pid
