#!/bin/bash
# Repeat a probe on an own Xvfb under a watchdog (issue 171).
#   run.sh BUILD TAG RUNS TIMEOUT EXE ARGS...     e.g. run.sh build b 200 60 tests/r171/ulwrace.exe race 3000 2
# BUILD and EXE are project-relative. Display ${DISP:-:1400} (Xvfb started here if it isn't up, left running:
# kill it by the PID in inst/171/xvfb-N.pid), prefix inst/171/pfx-BUILD, logs in inst/171/TAG/.
# Per run: ok = a line DONE (lockstress: "TICKS DONE"); FAULT / HANG as printed by the probe (exit 3 / 2); a run that outlives
# TIMEOUT s gets gdb backtraces of all threads (stuck-N.txt) and is killed by PID.
# DEBUG=+seh sets WINEDEBUG. KEEP=1 keeps the logs of good runs.
root=$(cd "$(dirname "$0")/../.." && pwd); cd "$root"
eval "$(tools/sysroot.sh env)"
B=$root/$1; TAG=$2; RUNS=$3; T=$4; EXE=$root/$5; shift 5
export DISPLAY=${DISP:-:1400}
n=${DISPLAY#:}
OUT=$root/inst/171/$TAG; mkdir -p "$OUT"
if ! xdpyinfo >/dev/null 2>&1; then
  setsid nohup Xvfb $DISPLAY -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 </dev/null &
  echo $! > "$root/inst/171/xvfb-$n.pid"
  for i in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done
  xdpyinfo >/dev/null 2>&1 || { echo "Xvfb $DISPLAY failed"; exit 1; }
fi
export WINEPREFIX=$root/inst/171/pfx-$(basename "$B") WINEDEBUG=${DEBUG:--all}
WINEDLLOVERRIDES="mscoree,mshtml=" "$B/wine" wineboot -u >"$OUT/wineboot.log" 2>&1
ok=0; fault=0; hang=0; stuck=0; bad=0
for i in $(seq $RUNS); do
  log=$OUT/run-$i.log
  setsid nohup "$B/wine" "$EXE" "$@" >"$log" 2>&1 </dev/null &
  pid=$!
  for ((s = 0; s < T * 5; s++)); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
  if kill -0 $pid 2>/dev/null; then
    stuck=$((stuck + 1))
    gdb -p $pid -batch -ex "source $root/tools/gdb/winesyms.py" -ex 'thread apply all bt' \
        -ex "print 'dce.c'::surfaces_lock" >"$OUT/stuck-$i.txt" 2>&1
    kill -9 $pid
    echo "run $i: STUCK (pid $pid): $(grep -a -E 'FAULT|HANG' "$log" | tr '\r\n' '  ') -> $OUT/stuck-$i.txt"
  elif grep -a -q '^FAULT' "$log"; then fault=$((fault + 1))
  elif grep -a -q '^HANG' "$log"; then hang=$((hang + 1))
  elif grep -a -q -E '(^|[0-9] )DONE' "$log"; then ok=$((ok + 1)); [ -n "$KEEP" ] || "$root/tools/del" -f "$log"
  else bad=$((bad + 1)); echo "run $i: exited without finishing: $(tail -n 2 "$log" | tr '\r\n' '  ')"
  fi
done
"$B/server/wineserver" -k 2>/dev/null
echo "$TAG ($(basename "$B"), $*): ok=$ok fault=$fault hang=$hang stuck=$stuck bad=$bad of $RUNS, loadavg $(cut -d' ' -f1-3 /proc/loadavg)"
