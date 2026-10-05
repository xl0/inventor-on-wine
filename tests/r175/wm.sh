#!/bin/bash
# wm.sh DISPLAY awesome|openbox [picom]: replace the window manager of DISPLAY (kills that display's
# openbox/awesome/picom by PID), optionally start picom with the user's options (175).
# awesome runs on a private D-Bus session; `ac.sh DISPLAY LUA` talks to it (awesome-client).
D=$1; WM=$2; COMP=$3
W=$(cd "$(dirname "$0")/../.." && pwd); S=$W/inst/175; mkdir -p $S
eval "$($W/tools/sysroot.sh env)"
n=${D#:}
for p in $(ps -eo pid,comm | awk '$2 ~ /^(openbox|awesome|picom)$/ {print $1}'); do
  [ "$(tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -x "DISPLAY=$D")" ] && kill $p
done
sleep 1
export DISPLAY=$D
case $WM in
awesome) setsid nohup dbus-run-session --config-file=$W/x/dbus-session.conf -- sh -c "echo \$DBUS_SESSION_BUS_ADDRESS > $S/dbus.$n; exec awesome -c $W/x/awesome-rc.lua" > $S/awesome.$n.log 2>&1 < /dev/null & ;;
openbox) setsid nohup openbox > $S/openbox.$n.log 2>&1 < /dev/null & ;;
esac
sleep 2
# openbox keeps the number of desktops the previous WM left on the root window (awesome: 9 tags); its own default is 4
[ "$WM" = openbox ] && xdotool set_num_desktops ${DESKTOPS:-4}
if [ "$COMP" = picom ]; then
  setsid nohup picom --config /dev/null --backend glx ${PICOM_VSYNC---vsync} --no-use-damage > $S/picom.$n.log 2>&1 < /dev/null &
  sleep 2
fi
xprop -root _NET_SUPPORTING_WM_CHECK _NET_NUMBER_OF_DESKTOPS _NET_CURRENT_DESKTOP 2>&1 | tr '\n' ' '; echo
xprop -id $(xprop -root _NET_SUPPORTING_WM_CHECK | awk '{print $NF}') _NET_WM_NAME 2>&1
ps -eo pid,comm | awk '$2 == "picom"' | while read p c; do [ "$(tr '\0' '\n' < /proc/$p/environ | grep -x "DISPLAY=$D")" ] && echo "picom $p"; done
