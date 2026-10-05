#!/bin/bash
# tests/r181/xmove.sh TAG: host process moves a panel that holds another process' D3D11 child (xp.exe), which has
# stopped presenting; screen colour at the panel's old and new place (181)
cd /home/xl0/projects/wine
. tests/r181/env.sh $1 98
export WINE_D3D_CONFIG=renderer=vulkan
O=inst/181/r/xmove-$1
xdotool mousemove 1900 1060
"$B/wine" tests/r132/xp.exe host secs=25 >$O.host 2>/dev/null </dev/null &
sleep 4
P1=$(grep -o 'p1=[0-9A-Fa-fx]*' $O.host | head -1 | cut -d= -f2)
"$B/wine" tests/r132/xp.exe child $P1 frames=20 sleep=50 hold=15 quiet=1 color=ff00ff >$O.child 2>/dev/null </dev/null &
sleep 5
WID=$(xdotool search --name '^xp' 2>/dev/null | head -1)
eval $(xwininfo -id $WID | awk '/Absolute upper-left X/{print "X="$4}/Absolute upper-left Y/{print "Y="$4}')
x/shot.sh $O-before.png 98 >/dev/null
"$B/wine" tests/wl_winctl.exe $P1 move 20 60 300 200 >/dev/null 2>&1    # p1: 20,20 -> 20,60, same size
sleep 1.5
x/shot.sh $O-after.png 98 >/dev/null
python3 - $O $X $Y <<'PY'
import sys
from PIL import Image
o, x, y = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
for n in ('before', 'after'):
    im = Image.open('%s-%s.png' % (o, n)).convert('RGB')
    print(n, 'top-left quadrant at old place', im.getpixel((x + 20 + 40, y + 20 + 15)), "bottom-left quadrant at new place", im.getpixel((x + 20 + 40, y + 60 + 185)))
PY
wait
"$B/server/wineserver" -k 2>/dev/null; sleep 1
