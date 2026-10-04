#!/bin/bash
# clip.sh: owner-side geometry and clipping of remote surfaces whose sources do NOT present any more
# (issue 132 M2): every step is a change made by the owner process only. Screenshots inst/132/m1/clip-N.png.
. "$(dirname "$0")/env.sh"
export WINEDEBUG=-all WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-renderer=gl}
O=${R132_OUT:-inst/132/m1}/clip
shot() { sleep ${2:-1.2}; x/wshot.sh $O-$1.png >/dev/null; echo "--- $1"; python3 tests/r132/pix.py $O-$1.png | grep -v "white\|black\|grey"; }
ctl() { $W tests/wl_winctl.exe "$@" >/dev/null 2>&1; }
(WINEDEBUG=${HDEBUG:--all} setsid nohup $W tests/r132/xp.exe host secs=100 > $O-host.out 2>&1 &)
sleep 3
get() { sed -n "s/.*$1=0*\([0-9A-Fa-f]*\).*/\1/p" $O-host.out; }
TOP=$(get top); P1=$(get p1); P2=$(get p2); C=$(get cover)
(setsid nohup $W tests/r132/xp.exe child $P1 frames=40 hold=120 quiet=1 > $O-foreign.out 2>&1 &)
(setsid nohup $W tests/r132/xp.exe child $P2 color=00ffff frames=40 hold=120 quiet=1 > $O-child.out 2>&1 &)
shot 1-start 5
ctl $P2 move 360 60 300 200; shot 2-p2-moved
ctl $P2 move 360 60 150 100; shot 3-p2-smaller-than-child
ctl $P1 move 20 20 200 150; shot 4-p1-resized
ctl $P1 move 20 20 300 200; ctl $C top; ctl $C move 170 0 200 300; ctl $C show; shot 5-cover-right-half-of-p1
ctl $C move 0 0 340 300; shot 6-cover-all-of-p1
ctl $P1 top; shot 7-p1-raised
ctl $C hide; ctl $P1 move 600 20 300 200; shot 8-p1-over-the-edge
ctl $P1 move 20 20 300 200; ctl $P2 hide; shot 9-p2-hidden
ctl $P2 show; shot 10-p2-shown
ctl $TOP hide; shot 11-top-hidden
ctl $TOP show; shot 12-top-shown 2.5
ctl $P2 close; shot 13-p2-destroyed
ctl $TOP close; sleep 1
ps -eo pid,args | awk '/xp.exe (foreign|child|host)/ && !/awk/ {print $1}' | xargs -r kill 2>/dev/null
