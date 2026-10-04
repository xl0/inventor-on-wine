#!/bin/bash
# Guest side (through vmwl/wl.sh): run conformance test units under Wayland, RUNS times each.
#   g-utest.sh BUILD RUNS DLL:UNIT...     output ~/r157/ut-<build>/<dll>-<unit>-N.txt (+ summary line per run)
# A unit that does not finish in 900 s is killed by PID (wineserver -k afterwards) and reported as TIMEOUT.
B=/host/$1; RUNS=$2; shift 2
export WINEPREFIX=$HOME/wp-ut-$(basename $B) WINEDEBUG=-all
OUT=$HOME/r157/ut-$(basename $B); mkdir -p $OUT
WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u >$OUT/wineboot.log 2>&1
for u in "$@"; do
  dll=${u%%:*}; unit=${u##*:}
  for i in $(seq $RUNS); do
    log=$OUT/$dll-$unit-$i.txt
    setsid nohup $B/wine $B/dlls/$dll/tests/x86_64-windows/${dll}_test.exe $unit >$log 2>&1 </dev/null &
    pid=$!; res=
    for ((s = 0; s < 900; s++)); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    if kill -0 $pid 2>/dev/null; then kill -9 $pid; res=TIMEOUT; $B/server/wineserver -k; sleep 2; fi
    echo "$(basename $B) $u run $i: $res $(grep -a -c 'Test failed' $log) failed lines; $(grep -a -E 'tests executed' $log | tail -n 1 | tr -d '\r')"
  done
done
