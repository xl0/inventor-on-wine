#!/usr/bin/env bash
# Baseline in the current session: notepad, wl_xowner self (+ click into owner A), wl_xswap. Usage: vmwl/probes.sh NAME
# Screenshots vmwl/shots/NAME-*.png; fresh prefix ~/wp-NAME in the guest (deleted before).
set -euo pipefail
cd "$(dirname "$0")"; n=$1; mkdir -p shots
W="export WINEPREFIX=\$HOME/wp-$n WINEDLLOVERRIDES='mscoree,mshtml=' WINEDEBUG=-all; B=/host/wt/wayland-build; T=/host/tests"
bg() { ./wl.sh "$W; setsid nohup \$B/wine $1 >~/$2.log 2>&1 </dev/null &"; }
stop() { ./wl.sh "$W; \$B/server/wineserver -k; sleep 1; true"; }
./wl.sh "rm -rf ~/wp-$n"; ./wl.sh "$W; \$B/wine wineboot -i >/dev/null 2>&1"
bg notepad np; sleep 6; ./shot.sh $PWD/shots/$n-notepad.png; stop
bg "\$T/wl_xowner.exe self 4000" xo; sleep 9; ./shot.sh $PWD/shots/$n-xowner-1.png
./input.py click 1100 600; sleep 2; ./shot.sh $PWD/shots/$n-xowner-2-after-click.png; ./wl.sh 'cat ~/xo.log | grep -v libEGL || true'; stop
bg "\$T/wl_xswap.exe" xsA; sleep 4; h=$(./wl.sh 'grep child= ~/xsA.log' | cut -d= -f2)
bg "\$T/wl_xswap.exe $h" xsB; sleep 8; ./shot.sh $PWD/shots/$n-xswap.png; ./wl.sh 'grep -hv libEGL ~/xsA.log ~/xsB.log || true'; stop
