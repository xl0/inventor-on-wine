#!/usr/bin/env bash
# Headless Wayland session for winewayland.drv tests (notes/wine/wayland.md).
#   x/wayland.sh start    gnome-shell/mutter --headless, 1920x1080 virtual monitor, own D-Bus + pipewire,
#                         plus the winj.py input injector (x/wshot.sh talks to both)
#   x/wayland.sh stop     kill all of it (by process group)
#   x/wayland.sh env      print exports; use: eval "$(x/wayland.sh env)"
#   x/wayland.sh status
# Private runtime dir /tmp/wl-xdg, socket wayland-wine. Never uses the user's real session bus.
set -euo pipefail
X=/tmp/wl-xdg W=wayland-wine
here=$(cd "$(dirname "$0")" && pwd)
pgid() { [ -f $X/pgid ] && kill -0 "$(cat $X/pgid)" 2>/dev/null; }
case ${1:-} in
start)
  pgid && { echo "already running"; exit 0; }
  rm -rf $X; mkdir -p $X; chmod 700 $X
  cat >$X/inner.sh <<EOS
echo "export DBUS_SESSION_BUS_ADDRESS='\$DBUS_SESSION_BUS_ADDRESS'" >$X/dbus.env
pipewire >$X/pipewire.log 2>&1 &
gnome-shell --headless --wayland --no-x11 --wayland-display $W --virtual-monitor 1920x1080 --sm-disable >$X/shell.log 2>&1 &
until [ -S $X/$W ]; do sleep 0.2; done; sleep 3
python3 $here/winj.py >$X/winj.log 2>&1 &
wait
EOS
  env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR=$X \
    setsid -f bash -c 'echo $$ >'$X'/pgid; exec dbus-run-session -- bash '$X'/inner.sh' >$X/dbus.log 2>&1
  until [ -S $X/winj.sock ]; do sleep 0.3; done
  "$here/wshot.sh" key Escape >/dev/null  # Ubuntu's shell starts in the Activities overview
  sleep 1
  eval "$("$0" env)"; echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR" ;;
stop)
  pgid || { echo "not running"; exit 0; }
  g=$(ps -eo pid,pgid,args | awk -v g="$(cat $X/pgid)" '$2==g && /gnome-shell --headless/{print $1}')
  [ -n "$g" ] && pkill -P "$g" || true   # ibus-daemon etc. run in their own process group
  kill -- -"$(cat $X/pgid)"; rm -f $X/pgid ;;
env)
  cat $X/dbus.env; echo "export XDG_RUNTIME_DIR=$X WAYLAND_DISPLAY=$W; unset DISPLAY" ;;
status) pgid && echo running || echo stopped ;;
*) sed -n 2,8p "$0"; exit 1 ;;
esac
