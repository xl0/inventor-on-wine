#!/bin/bash
# backpressure.sh [TAG]: a source presents 12000 frames while the compositor is stopped (SIGSTOP, own headless
# session): the owner's event thread fills the socket with attach/commit requests. With STRACE=1 its polls are
# logged: it must wait for POLLOUT while a flush is pending. After SIGCONT the last frame must be on screen
# (bar of frame 11999 at x 296 of the 300 px panel).
. "$(dirname "$0")/env.sh"
export WINEDEBUG=-all WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-renderer=gl}
O=inst/132/m1/backpressure-${1:-run}
G=$(ps -eo pid,args | awk '/gnome-shell --headless/ && !/awk/ {print $1; exit}')
# TITLES=N: the owner's UI thread also queues N title changes of 3000 bytes 1.5 s after the stop, more than the socket takes
(WAYLAND_DEBUG=${HOST_WAYLAND_DEBUG:-} setsid nohup $W tests/r132/xp.exe host secs=60 titles=${TITLES:-0} > $O-host.out 2>&1 &)
sleep 3
P1=$(sed -n 's/.*p1=0*\([0-9A-Fa-f]*\).*/\1/p' $O-host.out)
H=$(ps -eo pid,args | awk '/xp.exe host/ && !/awk/ {print $1; exit}')
(setsid nohup $W tests/r132/xp.exe child $P1 frames=12000 sleep=0 hold=40 quiet=1 > $O-child.out 2>&1 &)
[ -z "${STRACE:-}" ] || (timeout 40 strace -f -tt -e trace=poll,ppoll,sendmsg -p $H -o $O.strace >/dev/null 2>&1 &)
sleep 2.5; kill -STOP $G; echo "compositor stopped"
c0=$(awk '{print $14+$15}' /proc/$H/stat)
sleep 14
c1=$(awk '{print $14+$15}' /proc/$H/stat)
grep presents $O-child.out; echo "owner CPU while the compositor was stopped: $((c1-c0)) ticks in 14 s"
kill -CONT $G; echo "compositor continued"
sleep 3; x/wshot.sh $O.png >/dev/null; python3 tests/r132/pix.py $O.png | grep "black\|magenta\|green"
grep "host:" $O-host.out | tail -n 2
ps -eo pid,args | awk '/xp.exe/ && !/awk/ {print $1}' | xargs -r kill
