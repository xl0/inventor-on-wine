#!/bin/bash
# barpix.sh: screen pixels across owned.exe's bar (3 px left of its WM frame ... 3 px right, mid height), the frame's
# and the client's _NET_WM_WINDOW_OPACITY (175: awesome's border around the invisible popup).
. "$(dirname "$0")/env.sh"
id=$(xdotool search --name '^r175 bar$' | head -1)
frame=$(xwininfo -id $id -children | awk '/Parent window id/{print $4}')
set -- $(xwininfo -id $id | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width/{w=$2}/Height/{h=$2}END{print x,y,w,h}')
$W/x/shot.sh $S/barpix.png ${DISPLAY#:} > /dev/null
python3 -c "
from PIL import Image
im = Image.open('$S/barpix.png').convert('RGB')
print('bar at $1,$2:', ' '.join('%02x%02x%02x' % im.getpixel((x, $2 + $4 // 2)) for x in range($1 - 4, $1 + $3 + 4)))"
echo "frame $frame: $(xprop -id $frame _NET_WM_WINDOW_OPACITY 2>&1 | sed 's/.*= //; s/.*not found.*/no opacity/') | client: $(xprop -id $id _NET_WM_WINDOW_OPACITY | sed 's/.*= //; s/.*not found.*/no opacity/')"
