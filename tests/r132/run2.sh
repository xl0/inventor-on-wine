#!/bin/bash
# run2.sh TAG [host args] -- [foreign args] -- [child args]: xp host + topology A on p1 + topology B on p2; screenshot after 6 s
. "$(dirname "$0")/env.sh"
O=inst/132/m1/$1; shift
IFS='|' read -r HA FA CA <<<"$*"
export WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-renderer=gl}
(WINEDEBUG=${HDEBUG:--all} setsid nohup $W tests/r132/xp.exe host secs=${SECS:-20} $HA > $O-host.out 2>&1 &)
sleep 3
P1=$(sed -n 's/.*p1=\([0-9A-Fa-f]*\).*/\1/p' $O-host.out); P2=$(sed -n 's/.*p2=\([0-9A-Fa-f]*\).*/\1/p' $O-host.out)
echo "p1=$P1 p2=$P2"
(WINEDEBUG=${RDEBUG:--all} setsid nohup $W tests/r132/xp.exe foreign $P1 $FA > $O-foreign.out 2>&1 &)
(WINEDEBUG=${RDEBUG:--all} setsid nohup $W tests/r132/xp.exe child $P2 color=00ffff $CA > $O-child.out 2>&1 &)
sleep ${SHOT:-6}
x/wshot.sh $O.png >/dev/null && python3 tests/r132/pix.py $O.png
