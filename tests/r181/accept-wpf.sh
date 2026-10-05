#!/bin/bash
# tests/r181/accept-wpf.sh RENDERER LABEL: 174's hardware WPF probes on the fix build (.NET prefix inst/181/pfx-net48), :98 as it is now (181)
# moved pane (defect 2): xdotool windowsize, height only; dirty rectangle present (defect 3): Mod4 resizes
cd /home/xl0/projects/wine
. tests/r181/env.sh fix 98
export WINEPREFIX=$W/inst/181/pfx-net48 WINE_D3D_CONFIG=renderer=$1 SCALE=1.5 PROBE=wpf.exe
O=inst/181/r/wpf-$1-$2
XSIZE=1 DXMUL=0 tests/r174/drive.sh 98 $O-move ${N:-30} super 3 50 0 -- 600 100 100 600 400 nogpu hosted partial >/dev/null 2>&1
echo "wpf $1 $2 move: $(tail -1 $O-move.res) | $(grep -o 'PIX BAD[^|]*' $O-move.res | sed 's/ [0-9]* px.*//' | sort | uniq -c | tr '\n' ' ')"
"$B/server/wineserver" -k 2>/dev/null; sleep 1
DXMUL=0 tests/r174/drive.sh 98 $O-partial ${N:-30} super 10 20 0 -- 600 100 100 600 400 nogpu hosted partial >/dev/null 2>&1
echo "wpf $1 $2 partial: $(tail -1 $O-partial.res) | $(grep -o 'PIX BAD[^|]*' $O-partial.res | sed 's/ [0-9]* px.*//' | sort | uniq -c | tr '\n' ' ')"
"$B/server/wineserver" -k 2>/dev/null; sleep 1
