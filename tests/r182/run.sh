#!/bin/bash
# run.sh BUILD DISPLAYNUM TAG TIMEOUT EXE ARGS...: run EXE on winex11 under a watchdog; a run still alive after TIMEOUT s
# gets gdb backtraces of all threads (out/TAG-hang.txt) and is killed by PID. One prefix per build AND display
# (a wineserver must not serve two X displays), so runs on different displays can go in parallel.
# BUILD: integ (build/), b173 (wt/173-build), fix (wt/182-build), dbg (wt/182-dbg-build), base (build-next = integ d8e4d0f72d2)
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=$PWD/inst/182
[ -z "$X182_NOPRESYNC" ] && unset X182_NOPRESYNC
[ -n "$X11LIB" ] && export LD_LIBRARY_PATH=$X11LIB:$LD_LIBRARY_PATH
case $1 in
  integ) B=$PWD/build ;;
  b173)  B=$PWD/wt/173-build ;;
  fix)   B=$PWD/wt/182-build ;;
  base)  B=$PWD/build-next ;;
  dbg)   B=$PWD/wt/182-dbg-build ;;
  *) echo "run.sh integ|b173|fix|dbg DISPLAYNUM TAG TIMEOUT EXE ARGS..."; exit 2 ;;
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
