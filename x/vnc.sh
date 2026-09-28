#!/usr/bin/env bash
# (Re)start x11vnc for :98 on 127.0.0.1:5902, detached. -noxdamage: poll the
# framebuffer instead of trusting DAMAGE (GPU-presented content may not report
# damage). Also disables screen blanking, which shows as a black screen.
set -euo pipefail
cd "$(dirname "$0")"
DISPLAY=:98 xset s off -dpms
pkill -f '^x11vnc -display :98' || true
setsid nohup x11vnc -display :98 -localhost -rfbport 5902 -forever -shared -nopw \
  -noxdamage -quiet -o "$PWD/x11vnc.log" >/dev/null 2>&1 < /dev/null &
