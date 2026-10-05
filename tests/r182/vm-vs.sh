#!/bin/bash
# vm-vs.sh BUILDDIR TAG RUNS: in the vmwl guest, vstate.exe loop 60 on winex11 through the session's Xwayland (run by vmwl/ssh.sh 'bash -s' < this)
B=/host/$1; tag=$2; runs=$3
export XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 WINEPREFIX=$HOME/wp-184-$tag
a=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1); [ -n "$a" ] && export XAUTHORITY=$a
a=$(ls /run/user/1000/xauth_* 2>/dev/null | head -1); [ -n "$a" ] && export XAUTHORITY=$a
unset WAYLAND_DISPLAY
xdpyinfo 2>/dev/null | grep -m1 "vendor string" || { echo "no X display"; exit 1; }
if [ ! -e $WINEPREFIX/system.reg ]; then WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u >/dev/null 2>&1; $B/server/wineserver -w; fi
for r in $(seq 1 $runs); do
  WINEDEBUG=err+all timeout 300 $B/wine /host/tests/r184/vstate.exe loop 60 2>&1 | grep -a "DONE\|X Error\|err:" | tr -d '\r' | sed "s/^/$tag r$r: /"
done
$B/server/wineserver -k
