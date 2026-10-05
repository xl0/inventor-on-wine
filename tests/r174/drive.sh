#!/bin/bash
# drive.sh DISPLAYNUM OUT N [MOD=super] [STEPS=10] [STEP_MS=20] [HOLD_MS=0] -- PROBE ARGS...
# Start frame.exe (WINEPREFIX, WINEBUILD set), let the WM resize it N times with MOD + right-button
# drags of a corner (CORNER=br|tl|tr|bl, default br; bigger by a varying amount, then back), and after each one
# compare the screen with the layout for the X window's size (check.py) and print the probe's STATE
# line (174). OUT.log = probe log, OUT.res = one line per resize, OUT-bad-N.png = failing screens.
# HOLD_MS: pause between the last motion and the release. BTN=1 moves instead (control).
# DXMUL=0: only the height changes (default: the width by twice as much). DMUL=N: N times bigger drags
# (default +-40..130 px high). START_SLEEP=0 POLL=0.02: first drag as soon as the window exists. XSIZE=1: resize with xdotool windowsize steps instead (no grab, no size-move).
N=$1; OUT=$2; CNT=$3; MOD=${4:-super}; STEPS=${5:-10}; STEP_MS=${6:-20}; HOLD_MS=${7:-0}
shift 7; [ "$1" = -- ] && shift
W=$(cd "$(dirname "$0")/../.." && pwd)
eval "$($W/tools/sysroot.sh env)"
export DISPLAY=:$N WINEDEBUG=${WINEDEBUG:--all}
: "${WINEPREFIX:?}" "${WINEBUILD:?}"
GPU=gpu; case " $* " in *" nogpu "*) GPU=;; esac
case " $* " in *" plain "*|*" thread "*) export BORDER=0;; esac
: >$OUT.log
$WINEBUILD/wine $W/tests/r174/${PROBE:-frame.exe} "$@" >>$OUT.log 2>$OUT.err </dev/null &
P=$!
for _ in $(seq 50); do WID=$(xdotool search --name "^${TITLE:-r174_frame}\$" 2>/dev/null | head -1); [ -n "$WID" ] && break; sleep ${POLL:-0.2}; done
sleep ${START_SLEEP:-1.5}
geo() { set -- $(xwininfo -id $WID | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}/Width/{w=$2}/Height/{h=$2}END{print x,y,w,h}'); X=$1 Y=$2 GW=$3 GH=$4; }
ms() { awk -v m=$1 'BEGIN{printf "%.3f", m/1000}'; }
: >$OUT.res; bad=0
for k in $(seq 1 $CNT); do
	geo
	if [ $((k % 2)) = 1 ]; then d=$(( ((k * 37) % 90 + 40) * ${DMUL:-1} )); else d=$((-d)); fi   # +40..130 px, then back
	dx=$((d * ${DXMUL:-2})); dy=$d
	# the corner the WM resizes is the one nearest to the pointer; sx / sy: which way makes the window bigger
	case ${CORNER:-br} in br) cx=$((X+GW)) cy=$((Y+GH)) sx=1 sy=1;; tl) cx=$X cy=$Y sx=-1 sy=-1;;
		tr) cx=$((X+GW)) cy=$Y sx=1 sy=-1;; bl) cx=$X cy=$((Y+GH)) sx=-1 sy=1;; esac
	cmd="mousemove $((cx-sx*20)) $((cy-sy*20)) sleep 0.15 keydown $MOD sleep 0.05 mousedown ${BTN:-3} sleep 0.15"
	for i in $(seq 1 $STEPS); do cmd+=" mousemove $((cx+sx*dx*i/STEPS)) $((cy+sy*dy*i/STEPS)) sleep $(ms $STEP_MS)"; done
	cmd+=" sleep $(ms $HOLD_MS) mouseup ${BTN:-3} sleep 0.05 keyup $MOD"
	if [ -n "$XSIZE" ]; then  # no WM grab: another client asks the WM for each size
		cmd=; for i in $(seq 1 $STEPS); do cmd+=" windowsize $WID $((GW+dx*i/STEPS)) $((GH+dy*i/STEPS)) sleep $(ms $STEP_MS)"; done
	fi
	echo "#### resize $k d=$dx,$dy from ${GW}x$GH" >>$OUT.log
	xdotool $cmd
	sleep 1.6
	geo
	$W/x/shot.sh $OUT-shot.png $N >/dev/null
	pix=$(python3 $W/tests/r174/${CHECK:-check.py} $OUT-shot.png $X $Y $GW $GH $GPU) || { bad=$((bad+1)); cp $OUT-shot.png $OUT-bad-$k.png; }
	echo "$k x11 ${GW}x$GH+$X+$Y | $pix | $(grep STATE $OUT.log | tail -1 | cut -c8-)" >>$OUT.res
done
echo "RESULT $bad bad of $CNT" >>$OUT.res
kill $P 2>/dev/null; wait $P 2>/dev/null
tail -1 $OUT.res
