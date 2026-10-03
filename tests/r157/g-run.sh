#!/bin/bash
# Guest side (run through vmwl/wl.sh): repeat a probe under a watchdog, gdb backtraces on a hang.
#   g-run.sh BUILD TAG RUNS TIMEOUT EXE ARGS...      (BUILD and EXE are project-relative, under /host)
# A run is ok when the process exits within TIMEOUT s and its log contains the line "DONE" or "rapid end";
# on a timeout all thread backtraces go to hang-N.txt and the process is killed by PID.
B=/host/$1; TAG=$2; RUNS=$3; T=$4; EXE=/host/$5; shift 5
export WINEPREFIX=$HOME/wp-$(basename $B) WINEDEBUG=${WINEDEBUG:--all}
OUT=$HOME/r157/$TAG; mkdir -p $OUT
sudo sysctl -q kernel.yama.ptrace_scope=0   # gdb attaches to non-children (lost at guest reboot)
if [ ! -d $WINEPREFIX ]; then WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -i >$OUT/wineboot.log 2>&1; fi
ok=0; hang=0; bad=0
for i in $(seq $RUNS); do
  log=$OUT/run-$i.log
  setsid nohup $B/wine $EXE "$@" >$log 2>&1 </dev/null &
  pid=$!
  for ((s = 0; s < T * 5; s++)); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
  if kill -0 $pid 2>/dev/null; then
    hang=$((hang + 1))
    gdb -p $pid -batch -ex 'source /host/tools/gdb/winesyms.py' -ex 'thread apply all bt' >$OUT/hang-$i.txt 2>&1
    kill -9 $pid
    echo "run $i: HANG (pid $pid, $(grep -a -c . $log) log lines) -> $OUT/hang-$i.txt"
  elif grep -a -q -E 'DONE|rapid end' $log; then ok=$((ok + 1))
  else bad=$((bad + 1)); echo "run $i: exited without finishing: $(tail -n 2 $log | tr '\r\n' '  ')"
  fi
done
echo "$TAG: ok=$ok hang=$hang bad=$bad of $RUNS; wayland errors: $(cat $OUT/run-*.log | grep -a -c -iE 'wl_display@1\.error|protocol error|Lost connection')"
