#!/bin/bash
# run.sh BUILD DISPLAYNUM TAG TIMEOUT EXE ARGS...: run EXE on winex11 under a watchdog; a run that is still alive after
# TIMEOUT s gets gdb backtraces of all threads (out/TAG-hang.txt) and is killed by PID.
# BUILD: integ (build/), master (wt/regress-master-build-h26), fix (wt/173-build), dbg (wt/173-dbg-build); one prefix per build.
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=$PWD/inst/173
case $1 in
  integ)  B=$PWD/build ;;
  master) B=$PWD/wt/regress-master-build-h26 ;;
  fix)    B=$PWD/wt/173-build ;;
  dbg)    B=$PWD/wt/173-dbg-build ;;
  *) echo "run.sh integ|master|fix|dbg DISPLAYNUM TAG TIMEOUT EXE ARGS..."; exit 2 ;;
esac
unset WAYLAND_DISPLAY; export DISPLAY=:$2 WINEPREFIX=$R/pfx-$1
TAG=$3; T=$4; shift 4
# one wineserver (and its explorer) must not serve two displays: X_UnmapWindow BadWindow on the other display's window (077)
if [ "$(cat $WINEPREFIX/.display 2>/dev/null)" != "$DISPLAY" ]; then
  $B/server/wineserver -k 2>/dev/null; $B/server/wineserver -w 2>/dev/null; mkdir -p $WINEPREFIX; echo $DISPLAY > $WINEPREFIX/.display
fi
if [ ! -e $WINEPREFIX/system.reg ]; then
  WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u > $R/out/wineboot-$(basename $WINEPREFIX).log 2>&1
  $B/server/wineserver -w
fi
WINEDEBUG=${WINEDEBUG:-err+all} setsid nohup $B/wine "$@" > $R/out/$TAG.out 2>&1 </dev/null &
pid=$!
for ((s = 0; s < T * 5; s++)); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
res=$(grep -a -o "DONE.*" $R/out/$TAG.out | tr -d '\r' | tail -n 1)
if kill -0 $pid 2>/dev/null; then
  gdb -p $pid -batch -ex 'source tools/gdb/winesyms.py' -ex 'thread apply all bt' \
      -ex "p 'dce.c'::surfaces_lock.__data" -ex "p 'window.c'::surfaces_lock.__data" -ex "p user_mutex.__data" -ex "p win_data_mutex.__data" > $R/out/$TAG-hang.txt 2>&1
  kill -9 $pid; res="HANG ($TAG-hang.txt)"
fi
xerr=$(grep -a -c "X Error of failed request" $R/out/$TAG.out)
echo "$TAG: ${res:-exited without DONE}${xerr:+; X errors $xerr}"
