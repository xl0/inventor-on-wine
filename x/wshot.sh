#!/usr/bin/env bash
# Wayland session (x/wayland.sh) screenshot + input.
#   x/wshot.sh [out.png]        full-screen PNG (default /tmp/wl-xdg/shot.png); prints the path
#   x/wshot.sh move X Y | rel DX DY | click|down|up [BTN] | scroll N | key SYM | keydown/keyup SYM | type TEXT
# Screenshot: gnome-shell only answers org.gnome.Shell.Screenshot for allow-listed bus names,
# so we own org.gnome.Screenshot on our private bus while calling. Input: winj.py (RemoteDesktop).
set -euo pipefail
X=/tmp/wl-xdg
case ${1:-} in
move|rel|click|down|up|scroll|key|keydown|keyup|type)
  python3 - "$X/winj.sock" "$*" <<'EOF2'
import socket, sys
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1])
s.sendall((sys.argv[2] + '\n').encode()); r = s.makefile().readline().strip()
print(r); sys.exit(r != 'ok')
EOF2
  ;;
*)
  out=$(realpath -m "${1:-$X/shot.png}")
  . $X/dbus.env
  python3 - "$out" <<'EOF2'
import sys
from gi.repository import Gio, GLib
c = Gio.bus_get_sync(Gio.BusType.SESSION)
c.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
            GLib.Variant('(su)', ('org.gnome.Screenshot', 4)), None, 0, -1)
ok, path = c.call_sync('org.gnome.Shell.Screenshot', '/org/gnome/Shell/Screenshot', 'org.gnome.Shell.Screenshot',
                       'Screenshot', GLib.Variant('(bbs)', (False, False, sys.argv[1])), None, 0, -1).unpack()
assert ok; print(path)
EOF2
  ;;
esac
