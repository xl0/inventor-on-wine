#!/usr/bin/env bash
# Headless NVIDIA Xorg on :98 (GPU ca:00.0). Clients also need
# DRI_PRIME=pci-0000_ca_00_0 so Vulkan picks the GPU that owns the X screen.
set -euo pipefail
cd "$(dirname "$0")"
/usr/lib/xorg/Xorg :98 -config "$PWD/xorg-nvidia.conf" \
  -modulepath /usr/lib/x86_64-linux-gnu/nvidia/xorg,/usr/lib/xorg/modules \
  -logfile "$PWD/Xorg.98.log" -nolisten tcp -noreset -novtswitch -sharevts >xorg.out 2>&1 &
