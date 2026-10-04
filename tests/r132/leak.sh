#!/bin/bash
# leak.sh MODE(child|foreign): 200 create/destroy cycles of a remote surface; owner resources before / after
. "$(dirname "$0")/env.sh"
export WINEDEBUG=-all WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-renderer=gl}
M=$1; O=inst/132/m1/leak-$M
# make sure the prefix runs before WAYLAND_DEBUG is set for the host only
${W%/wine}/server/wineserver -p 2>/dev/null; $W cmd /c "echo warm" >/dev/null 2>&1; sleep 2
(WAYLAND_DEBUG=1 setsid nohup $W tests/r132/xp.exe host secs=120 > $O-host.out 2>&1 &)
sleep 3
P=$(sed -n 's/.*p1=0*\([0-9A-Fa-f]*\).*/\1/p' $O-host.out)
H=$(ps -eo pid,args | awk '/xp.exe host/ && !/awk/ {print $1; exit}')
res() { echo "$1: owner fds $(ls /proc/$H/fd | wc -l) maps $(wc -l < /proc/$H/maps) rss $(awk '/VmRSS/ {print $2}' /proc/$H/status) kB; $(grep 'host:.*handles' $O-host.out | tail -n 1)"; }
# warm up: one remote surface comes and goes, so one-time allocations are in the baseline
$W tests/r132/xp.exe $M $P frames=20 quiet=1 > /dev/null 2>&1; sleep 6; res baseline
(setsid nohup $W tests/r132/xp.exe $M $P cycle=${CYCLES:-200} frames=100 quiet=1 > $O-render.out 2>&1 &)
until grep -q "cycles done" $O-render.out; do sleep 1; done; sleep 1
R=$(ps -eo pid,args | awk '/xp.exe '$M'/ && !/awk/ {print $1; exit}')
echo "renderer after cycles: fds $(ls /proc/$R/fd | wc -l) maps $(wc -l < /proc/$R/maps); $(grep 'cycles done' $O-render.out)"
res "after cycles (renderer presenting)"
until ! kill -0 $R 2>/dev/null; do sleep 1; done; sleep 6
res "after renderer exit"
for o in wl_compositor#[0-9]*.create_surface wl_surface#[0-9]*.destroy get_subsurface wl_subsurface#[0-9]*.destroy get_viewport wp_viewport#[0-9]*.destroy create_pool wl_shm_pool#[0-9]*.destroy create_buffer wl_buffer#[0-9]*.destroy create_region wl_region#[0-9]*.destroy; do printf "%s=%s " "${o##*.}${o%%#*}" "$(grep -c -- "-> $o\|-> .*\.$o" $O-host.out)"; done; echo
grep -c "error" $O-host.out
kill $H
