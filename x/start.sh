#!/usr/bin/env bash
# Headless NVIDIA Xorg. Usage: x/start.sh [DISPLAY_NUM [BUSID]]
# Default :98 on GPU ca:00.0 (PCI:202:0:0); :99 uses 34:00.0 (PCI:52:0:0).
# Clients also need DRI_PRIME=pci-0000_<bus>_00_0 (ca / 34) so Vulkan picks the
# GPU that owns the X screen.
set -euo pipefail
cd "$(dirname "$0")"
N=${1:-98} BUS=${2:-PCI:202:0:0}
conf=xorg-nvidia.conf
if [ "$BUS" != PCI:202:0:0 ]; then
  conf=xorg-$N.conf
  sed "s/PCI:202:0:0/$BUS/" xorg-nvidia.conf >$conf
fi
/usr/lib/xorg/Xorg :$N -config "$PWD/$conf" \
  -modulepath /usr/lib/x86_64-linux-gnu/nvidia/xorg,/usr/lib/xorg/modules \
  -logfile "$PWD/Xorg.$N.log" -nolisten tcp -noreset -novtswitch -sharevts >xorg.$N.out 2>&1 &
until DISPLAY=:$N xset q >/dev/null 2>&1; do sleep 0.2; done
DISPLAY=:$N xset s off -dpms  # blanking loses GPU-drawn window content (issue 005)
DISPLAY=:$N setxkbmap -rules evdev -model pc105 -layout us  # Wine expects evdev keycodes
# A window manager: Wine relies on it (WM_STATE) to unmap hidden managed windows (issue 029).
DISPLAY=:$N setsid nohup openbox >openbox.$N.log 2>&1 < /dev/null &
