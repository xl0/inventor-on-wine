#!/bin/bash
# sizemove_scen.sh DISPLAY OUT MODKEY [SCENARIOS]: run sizemove_log.exe (WINEPREFIX, WINEBUILD set)
# and drive WM moves/resizes with xdotool; the probe's log goes to OUT (077).
# Scenarios: modmove (MOD+B1 drag), modresize (MOD+B3 drag), caption (drag the app-drawn caption:
# Wine's own move loop), click (plain clicks), quick (5 short MOD drags), modfirst (MOD released
# before the button), kbmove / kbresize (openbox with A-F7 Move / A-F8 Resize keybinds + arrows),
# selfmove (right button held in the client: the app moves itself), dblclick (caption double-click,
# second press held: maximize), stale (focus another client with FOCUSKEY, default super+j, then
# move the window from outside; needs another client, an xev is started). Expected 0/0 for the last
# three apart from win32u's own caption-click pair.
# MODKEY: super for awesome, alt for openbox.
D=$1; OUT=$2; MOD=${3:-super}; SCENS=${4:-"modmove modresize caption"}
W=$(cd "$(dirname "$0")/.." && pwd)
export DISPLAY=$D WINEDEBUG=${WINEDEBUG:--all}
: "${WINEPREFIX:?}" "${WINEBUILD:?}"
$WINEBUILD/wine $W/tests/sizemove_log.exe 25 200 200 400 300 > $OUT 2>$OUT.err &
P=$!
sleep 4
WID=$(xdotool search --name '^sizemove_log$' | head -1)
geo() { X=$(xwininfo -id $WID | awk '/Absolute upper-left X/{print $4}'); Y=$(xwininfo -id $WID | awk '/Absolute upper-left Y/{print $4}'); }; geo
echo "== wid $WID at $X,$Y" >> $OUT.drv
cx=$((X+200)); cy=$((Y+150))
for s in $SCENS; do
  echo "#### $s" >> $OUT.drv
  case $s in
  modmove)
    xdotool mousemove $cx $cy; sleep 0.3; xdotool keydown $MOD; sleep 0.1; xdotool mousedown 1; sleep 0.3
    for i in $(seq 1 10); do xdotool mousemove $((cx+i*10)) $cy; sleep 0.05; done
    sleep 0.3; xdotool mouseup 1; sleep 0.1; xdotool keyup $MOD; sleep 1 ;;
  modresize)
    xdotool mousemove $cx $cy; sleep 0.3; xdotool keydown $MOD; sleep 0.1; xdotool mousedown 3; sleep 0.3
    for i in $(seq 1 10); do xdotool mousemove $((cx+150+i*5)) $((cy+150+i*5)); sleep 0.05; done
    sleep 0.3; xdotool mouseup 3; sleep 0.1; xdotool keyup $MOD; sleep 1 ;;
  caption)
    geo
    xdotool mousemove $((X+150)) $((Y+20)); sleep 0.3; xdotool mousedown 1; sleep 0.3
    for i in $(seq 1 10); do xdotool mousemove $((X+150+i*10)) $((Y+20+i*3)); sleep 0.05; done
    sleep 0.3; xdotool mouseup 1; sleep 1 ;;
  kbmove)
    xdotool mousemove $cx $cy; sleep 0.2; xdotool click 1; sleep 0.3
    xdotool key alt+F7; sleep 0.5
    for i in 1 2 3 4 5; do xdotool key Right; sleep 0.1; done
    xdotool key Return; sleep 1 ;;
  click)
    for i in 1 2 3; do xdotool mousemove $((cx+i*5)) $cy; sleep 0.2; xdotool mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 0.3; done ;;
  quick)
    for k in 1 2 3 4 5; do
      xdotool mousemove $cx $cy; xdotool keydown $MOD; xdotool mousedown 1
      for i in 1 2 3; do xdotool mousemove $((cx+i*7)) $cy; sleep 0.01; done
      xdotool mouseup 1; xdotool keyup $MOD; cx=$((cx+21)); sleep 0.05
    done; sleep 1 ;;
  modfirst)
    xdotool mousemove $cx $cy; sleep 0.3; xdotool keydown $MOD; sleep 0.1; xdotool mousedown 1; sleep 0.3
    for i in $(seq 1 5); do xdotool mousemove $((cx+i*10)) $cy; sleep 0.05; done
    xdotool keyup $MOD; sleep 0.3
    for i in $(seq 6 10); do xdotool mousemove $((cx+i*10)) $cy; sleep 0.05; done
    sleep 0.3; xdotool mouseup 1; sleep 1 ;;
  selfmove)
    xdotool mousemove $cx $cy; sleep 0.3; xdotool mousedown 3; sleep 1.5; xdotool mouseup 3; sleep 1 ;;
  dblclick)
    geo; xdotool mousemove $((X+150)) $((Y+20)); sleep 0.3; xdotool click 1; sleep 0.08; xdotool mousedown 1
    sleep 0.6; xdotool mouseup 1; sleep 1.5 ;;
  stale)
    xev -geometry 200x200+1100+600 >/dev/null & XE=$!; sleep 1
    xdotool mousemove $cx $cy click 1; sleep 0.5; xdotool key ${FOCUSKEY:-super+j}; sleep 0.8
    echo "focus: $(xdotool getwindowfocus getwindowname)" >> $OUT.drv
    xdotool windowmove $WID $((X+90)) $((Y+30)); sleep 1
    xdotool mousemove 1200 700 click 1; sleep 0.5; kill $XE ;;
  kbresize)
    xdotool mousemove $cx $cy; sleep 0.2; xdotool click 1; sleep 0.3
    xdotool key alt+F8; sleep 0.5
    for i in 1 2 3 4 5; do xdotool key Right; sleep 0.1; done
    xdotool key Return; sleep 1 ;;
  esac
  echo "#### $s done" >> $OUT.drv
done
wait $P; echo "exit $?" >> $OUT
