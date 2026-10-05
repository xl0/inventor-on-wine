#!/bin/bash
# ibus.sh start|stop DISPLAYNUM: ibus-daemon --xim on a private D-Bus session for that display (pids in inst/191/ibus-N.pids)
cd /home/xl0/projects/wine
S=/home/xl0/projects/wine/inst/191/ir
d=$2
case $1 in
start)
  mkdir -p $S/h/.config $S/h/.cache; chmod 700 $S
  DISPLAY=:$d HOME=$S/h XDG_CONFIG_HOME=$S/h/.config XDG_CACHE_HOME=$S/h/.cache XDG_RUNTIME_DIR=/home/xl0/projects/wine/inst/191/ir TMPDIR=/home/xl0/projects/wine/inst/191/ir \
    setsid nohup dbus-run-session -- ibus-daemon --xim --panel disable --config disable > inst/191/out/ibus-$d.log 2>&1 < /dev/null &
  echo $! > inst/191/ibus-$d.pids
  for i in $(seq 50); do DISPLAY=:$d xprop -root XIM_SERVERS 2>/dev/null | grep -q ibus && break; sleep 0.2; done; sleep 1 ;;
stop)
  # the session leader and everything in its session (dbus-daemon, ibus-daemon, ibus-x11, ...)
  l=$(cat inst/191/ibus-$d.pids 2>/dev/null); [ -n "$l" ] || exit 0
  for p in $(ps -eo pid,sid | awk -v s=$l '$2 == s {print $1}'); do kill $p 2>/dev/null; done
  : > inst/191/ibus-$d.pids; sleep 1 ;;
esac
