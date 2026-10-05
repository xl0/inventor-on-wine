#!/bin/bash
# run.sh BUILD DISPLAYNUM TAG TIMEOUT EXE ARGS...: run EXE on winex11 under a watchdog; a run still alive after TIMEOUT s
# gets gdb backtraces of all threads (out/TAG-hang.txt) and is killed by PID. One prefix per build AND display
# (a wineserver must not serve two X displays), so runs on different displays can go in parallel.
# BUILD: base (wt/177-build = fix/177), fix (wt/191-build). X11LIB=DIR runs with another libX11.
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=$PWD/inst/191

[ -n "$X11LIB" ] && export LD_LIBRARY_PATH=$X11LIB:$LD_LIBRARY_PATH
case $1 in
  fix)   B=$PWD/wt/191-build ;;
  base)  B=$PWD/wt/177-build ;;
  *) echo "run.sh base|fix DISPLAYNUM TAG TIMEOUT EXE ARGS..."; exit 2 ;;
esac
unset WAYLAND_DISPLAY; export DISPLAY=:$2 WINEPREFIX=$R/pfx-$1-$2
TAG=$3; T=$4; shift 4
if [ ! -e $WINEPREFIX/system.reg ]; then
  WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u > $R/out/wineboot-$(basename $WINEPREFIX).log 2>&1
  $B/server/wineserver -w
fi
WINEDEBUG=${WINEDEBUG:-err+all} setsid nohup ${PIN:+taskset -c $PIN} $B/wine "$@" > $R/out/$TAG.out 2>&1 </dev/null &
pid=$!
for ((s = 0; s < T * 5; s++)); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
res=$(grep -a -o "DONE.*" $R/out/$TAG.out | tr -d '\r' | tail -n 1)
if kill -0 $pid 2>/dev/null; then
  gdb -p $pid -batch -ex 'source tools/gdb/winesyms.py' -ex 'thread apply all bt' -ex 'source inst/182/mowner.py' > $R/out/$TAG-hang.txt 2>&1
  kill -9 $pid; res="HANG ($TAG-hang.txt)"
fi
xerr=$(grep -a -c "X Error of failed request" $R/out/$TAG.out)
ab=$(grep -a -c "Assertion" $R/out/$TAG.out)
[ "$xerr" = 0 ] && xerr=; [ "$ab" = 0 ] && ab=
echo "$TAG: ${res:-exited without DONE}${xerr:+; X errors $xerr}${ab:+; ASSERT $ab}"
