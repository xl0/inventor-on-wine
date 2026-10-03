#!/usr/bin/env bash
# Run a command in the guest inside the current Wayland session's environment (X unset, so Wine picks winewayland).
# Usage: vmwl/wl.sh CMD [ARGS...]   (args are joined and re-parsed by the guest shell)
exec "$(dirname "$0")/ssh.sh" "export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=\$(cd /run/user/1000 && ls -t wayland-? | head -1) DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus; unset DISPLAY; $*"
