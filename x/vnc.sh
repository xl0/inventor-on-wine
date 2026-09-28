#!/usr/bin/env bash
# (Re)start x11vnc for :N (default 98) on 127.0.0.1:59(N-96) (:98 → 5902,
# :99 → 5903), detached. Usage: x/vnc.sh [DISPLAY_NUM]. -noxdamage: poll the
# framebuffer instead of trusting DAMAGE (GPU-presented content may not report
# damage). Also disables screen blanking, which shows as a black screen.
set -euo pipefail
cd "$(dirname "$0")"
N=${1:-98}
DISPLAY=:$N xset s off -dpms
pkill -f "^x11vnc -display :$N " || true
setsid nohup x11vnc -display :$N -localhost -rfbport $((5904 + N - 100)) -forever -shared -nopw \
  -noxdamage -quiet -o "$PWD/x11vnc.$N.log" >/dev/null 2>&1 < /dev/null &
