#!/bin/bash
# tests/r181/accept.sh TAG RENDERER LABEL: 174's frame.exe probe (defect 1), 30 WM resizes on :98 as it is now (181)
cd /home/xl0/projects/wine
. tests/r181/env.sh $1 98
export WINE_D3D_CONFIG=renderer=$2 SCALE=1.5
O=inst/181/r/acc-$1-$2-$3
tests/r174/drive.sh 98 $O ${N:-30} super 15 10 0 -- 600 100 100 900 700 dpi plain >/dev/null 2>&1
echo "$1 $2 $3: $(tail -1 $O.res) view-bad $(grep -o 'PIX[^|]*' $O.res | grep -c 'view [0-9]') other-bad $(grep -o 'PIX[^|]*' $O.res | grep -c 'ribbon [0-9]\|browser [0-9]\|status [0-9]')"
"$B/server/wineserver" -k 2>/dev/null; sleep 1
