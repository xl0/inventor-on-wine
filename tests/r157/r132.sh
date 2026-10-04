#!/bin/bash
# r132.sh BUILD TAG: issue 132's probes (clip.sh, geo.sh, role flip of the top-level with an idle and a live source) on BUILD
# in the host Wayland session (x/wayland.sh start). Prefix inst/157/pfx-<build>, outputs in inst/157/r132/TAG/:
# pix.py lines per step in clip.txt / geo.txt / flip.txt, probe output in *.out.
cd "$(dirname "$0")/../.."
B=$1; TAG=$2
export W=$PWD/$B/wine R132_PREFIX=$PWD/inst/157/pfx-$(basename $B) R132_OUT=inst/157/r132/$TAG
mkdir -p $R132_OUT
. tests/r132/env.sh
WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all $W wineboot -u >$R132_OUT/wineboot.log 2>&1
# no WAYLAND_DEBUG: the trace shares the output file with the probe and breaks the handle lines the scripts parse;
# libwayland still prints protocol errors
fin() { ps -eo pid,args | awk '/xp.exe (foreign|child|host)/ && !/awk/ {print $1}' | xargs -r kill 2>/dev/null; sleep 2; }
tests/r132/clip.sh >$R132_OUT/clip.txt 2>&1; fin
tests/r132/geo.sh >$R132_OUT/geo.txt 2>&1; fin
export WINEDEBUG=-all
SECS=30 SHOT=5 tests/r132/run2.sh flip "flip=8|frames=40 hold=120 quiet=1|frames=2500 quiet=1" >$R132_OUT/flip.txt 2>&1
sleep 9; x/wshot.sh $R132_OUT/flip-after.png >/dev/null; echo "--- after the role change" >>$R132_OUT/flip.txt
python3 tests/r132/pix.py $R132_OUT/flip-after.png >>$R132_OUT/flip.txt; fin
$PWD/$B/server/wineserver -k
echo "$TAG: protocol errors $(cat $R132_OUT/*.out | grep -a -c -iE 'wl_display[@#]1\.error|[a-z_0-9]+[@#][0-9]+: error [0-9]+:|protocol error|Lost connection')," \
     "LOCKORDER reports $(cat $R132_OUT/*.out | grep -a -c '^LOCKORDER.*->')"
