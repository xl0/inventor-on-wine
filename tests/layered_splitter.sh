#!/bin/bash
# Drive tests/layered_splitter.exe with real X input (062). Needs DISPLAY, WINEPREFIX and
# WINE (path of the wine binary) set; a WM on the display is optional.
#   layered_splitter.sh [drag|move|cycle]
#     drag:  drag the bar 60 px, click its alpha 0 rows, the tooltip's shadow and body
#     move:  move the main window (MOVE=title: drag the WM title bar; MOVE=super: Mod4+drag), drag the bar
#     cycle: hide/show the bar (right click), resize the main window, drag the bar
# Expect in the log: "bar ... screen 00c800" (pane colour, not 030303), "bar down 2,N", "bar up, split N"
# with N changed by the drag, "main down" for the alpha 0 and shadow clicks, "tip down" for the body.
here=$(dirname "$0"); out=$(mktemp); mode=${1:-drag}
(WINEDEBUG=-all "$WINE" "$here/layered_splitter.exe" 16 > $out 2>/dev/null &)
for i in $(seq 60); do grep -q '^bar ' $out && break; sleep 0.5; done
bar() { read _ pos _ <<<$(grep '^bar [0-9]' $out | tail -1); bx=${pos%,*}; by=${pos#*,}; }
drag() { xdotool mousemove $1 $2; sleep 0.3; xdotool mousedown 1; sleep 0.2
         for i in 1 2 3 4 5 6; do xdotool mousemove $(($1 + $3 * i)) $(($2 + $4 * i)); sleep 0.1; done; xdotool mouseup 1; sleep 1.5; }
bar
case $mode in
drag)
    drag $((bx + 2)) $((by + 100)) 10 0
    xdotool mousemove $((bx + 62)) $((by + 10)) click 1; sleep 0.5
    tip=$(grep -o 'tip [0-9]*,[0-9]*' $out | head -1); tip=${tip#tip }
    xdotool mousemove $((${tip%,*} + 4)) $((${tip#*,} + 40)) click 1; sleep 0.5
    xdotool mousemove $((${tip%,*} + 40)) $((${tip#*,} + 40)) click 1 ;;
move)
    if [ "${MOVE:-title}" = super ]; then xdotool keydown super; drag $((bx + 300)) $((by + 300)) 16 8; xdotool keyup super
    else drag $((bx + 100)) $((by - 12)) 16 8; fi
    bar; drag $((bx + 2)) $((by + 100)) -8 0 ;;
cycle)
    xdotool mousemove $((bx + 300)) $((by + 300)) click 3; sleep 2
    bar; drag $((bx + 2)) $((by + 100)) -8 0
    xdotool windowsize $(xdotool search --name "layered splitter" | head -1) 700 640; sleep 2
    bar; drag $((bx + 2)) $((by + 500)) 8 0 ;;
esac
for i in $(seq 60); do grep -q '^bar down [0-9]* up' $out && break; sleep 0.5; done
uniq $out; rm -f $out
