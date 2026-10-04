#!/bin/bash
# geo.sh: geometry / lifetime scenarios with screenshots after each step (inst/132/m1/geo-N.png)
. "$(dirname "$0")/env.sh"
export WINEDEBUG=-all WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-renderer=gl}
O=${R132_OUT:-inst/132/m1}/geo
shot() { sleep ${2:-1.5}; x/wshot.sh $O-$1.png >/dev/null; echo "--- $1"; python3 tests/r132/pix.py $O-$1.png | grep -v "white\|black"; }
ctl() { $W tests/wl_winctl.exe "$@" >/dev/null 2>&1; }
(WINEDEBUG=${HDEBUG:--all} setsid nohup $W tests/r132/xp.exe host secs=90 > $O-host.out 2>&1 &)
sleep 3
TOP=$(sed -n 's/.*top=0*\([0-9A-Fa-f]*\).*/\1/p' $O-host.out); P1=$(sed -n 's/.*p1=0*\([0-9A-Fa-f]*\).*/\1/p' $O-host.out); P2=$(sed -n 's/.*p2=0*\([0-9A-Fa-f]*\).*/\1/p' $O-host.out)
(setsid nohup $W tests/r132/xp.exe child $P1 frames=2500 follow=1 quiet=1 > $O-foreign.out 2>&1 &)
(WINEDEBUG=${RDEBUG:--all} setsid nohup $W tests/r132/xp.exe child $P2 color=00ffff frames=2500 follow=1 quiet=1 > $O-child.out 2>&1 &)
shot 1-start 4
ctl $P2 move 360 60 320 120;  ctl $P1 move 20 100 200 150; shot 2-moved-resized
ctl $P2 hide; shot 3-p2-hidden
ctl $P2 show; shot 4-p2-shown
ctl $P1 hide; shot 5-p1-hidden
ctl $P1 show; shot 6-p1-shown
ctl $TOP hide; shot 7-top-hidden
ctl $TOP show; shot 8-top-shown 2.5
ctl $TOP move 300 200 760 400; shot 9-top-resized
R=$(ps -eo pid,args | awk '/xp.exe child '$P2'/ && !/awk/ {print $1; exit}'); kill -9 $R; shot 10-p2-renderer-killed
ctl $P1 close; shot 11-p1-destroyed
ctl $TOP close; sleep 1
ps -eo pid,args | awk '/xp.exe (foreign|child|host)/ && !/awk/ {print $1}' | xargs -r kill 2>/dev/null
grep -v "libEGL\|pci id\|^$" $O-host.out $O-foreign.out $O-child.out | tail -6
