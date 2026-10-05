#!/bin/bash
# scen.sh [N]: owned.exe on $D with build $B (see env.sh): move the owner between desktops 0 and 1 N times each way
# (xdotool set_desktop_for_window = _NET_WM_DESKTOP client message, any EWMH WM; MOVE=awesome: c:move_to_tag like
# Mod4+Shift+N), after each move: _NET_WM_DESKTOP of every managed window, and what is on screen on the desktop the
# owner left / arrived at (pix.py). Then: dialog created while the owner is on the other desktop, minimize / restore,
# (ShowWindow and WM_SYSCOMMAND), focus + stacking of the dialogs, close. SETTLE=2.5 under picom on the headless
# NVIDIA Xorg (the composited screen lags the X state by up to 2 s). Prints one line per check, "BAD" where an owned window is not with its owner.
. "$(dirname "$0")/env.sh"
N=${1:-10}; n=${DISPLAY#:}; bad=0
desk() { xprop -id $1 _NET_WM_DESKTOP 2>/dev/null | awk '/=/{print $NF}'; }
wid() { xdotool search --name "^r175 $1\$" | head -1; }
shot() { $W/x/shot.sh $S/scen.png $n > /dev/null; $T/pix.py $S/scen.png; }
view() { xdotool set_desktop $1; sleep ${SETTLE:-0.7}; }
move() { if [ "$MOVE" = awesome ]; then $T/movetag.sh $DISPLAY "r175 owner" $(($1 + 1)) > /dev/null; else xdotool set_desktop_for_window $(wid owner) $1; fi; sleep ${SETTLE:-0.7}; }
state() { # state WANT: desktops of all windows; BAD if a managed owned window is elsewhere
  local s="" d
  for w in owner bar dlg tool early sub late xproc noact plain; do
    id=$(wid $w); [ "$id" ] || continue; d=$(desk $id); s="$s $w=${d:-u}"
    case $d in ""|found.) ;; $1) ;; *) bad=$((bad + 1)); s="$s(BAD)";; esac
  done; echo "$s"; }
probe 600; sleep 7
view 0
echo "start: $(state 0) | screen: $(shot)"
for i in $(seq $N); do
  move 1; echo "move $i 0->1: $(state 1) | left on desktop 0: $(shot)"
  view 1; echo "          on desktop 1: $(shot)"
  move 0; echo "move $i 1->0: $(state 0) | left on desktop 1: $(shot)"
  view 0; echo "          on desktop 0: $(shot)"
done
move 1; cmd late xproc; sleep 2; echo "dialog created with the owner on desktop 1, viewing 0: $(state 1) | screen: $(shot)"
view 1; echo "          on desktop 1: $(shot)"
move 0; view 0; echo "back: $(state 0) | screen: $(shot)"
# stacking and focus after the moves: real X stacking order (bottom to top; awesome's _NET_CLIENT_LIST_STACKING
# is not it), then a click on the
# owner and on the dialog: the clicked window must become the active one (X and Win32)
stack() { local o="" n; for f in $(xwininfo -root -children | awk '/^ +0x/{print $1}'); do n=$(xwininfo -id $f -children 2>/dev/null | grep -o '"r175 [a-z]*"' | head -1 | sed 's/"r175 //; s/"//'); [ "$n" ] && o="$n $o"; done; echo " $o"; }
click() { set -- $1 $(xwininfo -id $(wid $1) | awk '/Absolute upper-left X/{x=$4}/Absolute upper-left Y/{y=$4}END{print x,y}'); xdotool mousemove $(($2 + $3)) $(($3 + $3)) 2>/dev/null; xdotool mousemove $(($2 + 30)) $(($3 + 30)) click 1; sleep 1
  cmd report; echo "click on $1: X active $(xprop -id $(xdotool getactivewindow) WM_NAME 2>/dev/null | sed 's/.*= //'), Win32 $(grep '^report' $S/owned.log | tail -1 | sed 's/report cmd: //') | stacking:$(stack)"; }
echo "stacking (bottom to top):$(stack)"
click owner; click dlg; click tool
win32() { cmd report; awk '/^report cmd/ {s = ""} /^  [a-z]+ +[0-9A-F]+ visible/ {s = s sprintf("%s=%s%s ", $1, $4, $6 == 1 ? "(iconic)" : "")} END {printf "%s", s}' $S/owned.log; }
for m in min:restore sysmin:sysrestore; do
  cmd ${m%:*}; sleep ${SETTLE:-1}; echo "${m%:*}: screen: $(shot) | Win32 visible: $(win32)"
  cmd ${m#*:}; sleep ${SETTLE:-1}; echo "${m#*:}: $(state 0) | screen: $(shot) | Win32 visible: $(win32)"
done
cmd close; sleep 2; echo "after close: $(xwininfo -root -tree | grep -c '"r175 ') X windows left, log: $(tail -1 $S/owned.log)"
echo "end"
