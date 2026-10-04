#!/bin/bash
# (copy of inst/130/sm.sh without the exported WINEDLLOVERRIDES) Usage: inst/173/sm.sh aw|ob 0|1(picom) DISPLAYNUM PREFIX BUILD OUTDIR [SCENARIOS]
# Own Xvfb + WM (+ picom xrender), then tests/sizemove_scen.sh once per scenario (077 cases).
cd "$(dirname "$0")/../.."; eval "$(tools/sysroot.sh env)"
WM=$1 PIC=$2 N=$3; export WINEPREFIX=$(realpath -m $4) WINEBUILD=$(realpath $5); OUT=$6

S=${7:-}
mkdir -p $OUT
Xvfb :$N -screen 0 1600x1000x24 >/dev/null 2>&1 & XP=$!
until DISPLAY=:$N xdpyinfo >/dev/null 2>&1; do sleep 0.2; done
if [ $WM = aw ]; then DISPLAY=:$N awesome -c x/awesome-rc.lua >$OUT/wm.log 2>&1 & MOD=super; : ${S:="modmove modresize caption click quick modfirst selfmove dblclick stale"}
else DISPLAY=:$N openbox --config-file $PWD/inst/130/sm/ob-rc.xml >$OUT/wm.log 2>&1 & MOD=alt; : ${S:="modmove modresize caption click quick kbmove kbresize selfmove dblclick stale"}; fi
WP=$!
sleep 1
PP=; if [ $PIC = 1 ]; then DISPLAY=:$N picom --config /dev/null --backend xrender >$OUT/picom.log 2>&1 & PP=$!; sleep 1; fi
DISPLAY=:$N WINEDLLOVERRIDES="mscoree,mshtml=" $WINEBUILD/wine wineboot -u >$OUT/wineboot.log 2>&1 </dev/null
for s in $S; do
	tests/sizemove_scen.sh :$N $OUT/$s.log $MOD $s </dev/null >/dev/null 2>&1
	echo "$WM picom=$PIC $s: $(grep -E '^summary|^exit' $OUT/$s.log | paste -sd' ')"
done
$WINEBUILD/server/wineserver -k
kill $PP $WP 2>/dev/null; sleep 0.5; kill $XP
