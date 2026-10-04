#!/bin/bash
# sinkstress.sh BUILD TAG [N] [SEED]: lockstress with client surfaces of another process (132) in its windows: xp.exe sources
# with their own child in A (main window), F (the popup that flips managed <-> unmanaged = role change with live sinks) and D.
# Host Wayland session (x/wayland.sh start), prefix inst/157/pfx-<build>, output inst/157/r132/TAG/sink-*.out.
# JITTER=1: relative pointer motion and key presses during the run (tests/r157/jitter.py).
# LSFLAGS=noxulw: extra lockstress flags (noxulw avoids issue 171).
# STRESSENV="WAYLAND_DEBUG=1" / STRESSDEBUG=+seh: environment / WINEDEBUG of lockstress only; NOSRC=1: no sources.
# Watchdog: lockstress must print DONE within 300 s, else gdb backtraces -> sink-hang.txt.
cd "$(dirname "$0")/../.."
B=$1; TAG=$2; N=${3:-15000}; SEED=${4:-1}
export W=$PWD/$B/wine R132_PREFIX=$PWD/inst/157/pfx-$(basename $B)   # one prefix per build
O=inst/157/r132/$TAG; mkdir -p $O
. tests/r132/env.sh
export WINEDEBUG=-all WINE_D3D_CONFIG=renderer=gl
WINEDLLOVERRIDES="mscoree,mshtml=" $W wineboot -u >$O/wineboot.log 2>&1
env ${STRESSENV:-} WINEDEBUG=${STRESSDEBUG:--all} setsid nohup $W inst/157/lockstress.exe $N $SEED ${LSFLAGS:-} >$O/sink-stress.out 2>&1 </dev/null &
pid=$!
for i in $(seq 100); do grep -a -q "ops per thread" $O/sink-stress.out && break; sleep 0.1; done
get() { grep -a "ops per thread" $O/sink-stress.out | sed -n "s/.* $1=0*\([0-9A-Fa-f]*\).*/\1/p"; }
i=0; for h in ${NOSRC:+none} $(get A) $(get F) $(get D); do i=$((i + 1)); [ $h = none ] && break
  (setsid nohup $W tests/r132/xp.exe child $h frames=100000 sleep=5 follow=1 quiet=1 color=$([ $i = 2 ] && echo 00ffff || echo ff00ff) >$O/sink-src$i.out 2>&1 </dev/null &)
done
[ -z "${JITTER:-}" ] || { python3 tests/r157/jitter.py host 3000 900 500 >$O/sink-jitter.out 2>&1 & jp=$!; }
for ((s = 0; s < 300; s++)); do kill -0 $pid 2>/dev/null || break; sleep 1; done
[ -z "${jp:-}" ] || kill $jp 2>/dev/null
if kill -0 $pid 2>/dev/null; then
  gdb -p $pid -batch -ex 'source tools/gdb/winesyms.py' -ex 'thread apply all bt' \
      -ex "p 'dce.c'::surfaces_lock.__data" -ex "p 'window.c'::surfaces_lock.__data" -ex "p win_data_mutex.__data" \
      -ex "p user_mutex.__data" -ex "p source_mutex.__data" -ex 'info threads' \
      -ex 'source tools/gdb/sehbt.py' -ex 'sehbt' -ex 'thread 1' -ex 'x/2000a $sp' >$O/sink-hang.txt 2>&1
  kill -9 $pid; res="HANG (sink-hang.txt)"
else res=$(grep -a -o "DONE.*" $O/sink-stress.out | tr -d '\r'); fi
ps -eo pid,args | awk '/xp.exe child/ && !/awk/ {print $1}' | xargs -r kill 2>/dev/null; sleep 1
$PWD/$B/server/wineserver -k
echo "$TAG sinkstress N=$N seed=$SEED: ${res:-exited without DONE}; sinks created: $(grep -a -c 'child: hwnd' $O/sink-src*.out | tr '\n' ' ');" \
     "protocol errors $(cat $O/sink-*.out | grep -a -c -iE 'wl_display[@#]1\.error|[a-z_0-9]+[@#][0-9]+: error [0-9]+:|protocol error|Lost connection')," \
     "LOCKORDER reports $(cat $O/sink-*.out | grep -a -c '^LOCKORDER.*->')"
