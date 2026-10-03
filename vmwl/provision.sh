#!/bin/bash
# Run in the guest as root (vmwl/ssh.sh 'sudo bash -s' < vmwl/provision.sh). Ubuntu archive packages only.
set -euxo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
# Wine runtime (winewayland.drv + wined3d GL on llvmpipe + lavapipe), tools, native build deps.
apt-get install -y --no-install-recommends \
  sudo rsync dbus-user-session libpam-systemd seatd xwayland \
  wayland-utils grim slurp wl-clipboard \
  libwayland-client0 libwayland-egl1 libxkbcommon0 libxkbregistry0 libfreetype6 libfontconfig1 \
  libvulkan1 mesa-vulkan-drivers libgl1 libegl1 libgl1-mesa-dri libglx-mesa0 libgnutls30t64 libx11-6 \
  libasound2t64 libpulse0 fonts-dejavu-core fonts-liberation \
  build-essential pkg-config flex bison gcc-mingw-w64-x86-64-posix libwayland-dev libxkbcommon-dev libxkbregistry-dev \
  libfreetype-dev libfontconfig-dev libvulkan-dev libgnutls28-dev libegl-dev libgl-dev strace gdb
apt-get install -y --no-install-recommends sway swaybg foot xdg-desktop-portal-wlr
apt-get install -y --no-install-recommends gnome-session gnome-shell mutter gnome-settings-daemon gnome-terminal \
  gnome-control-center nautilus xdg-desktop-portal-gnome adwaita-icon-theme gnome-session-bin
apt-get install -y --no-install-recommends plasma-desktop plasma-workspace kwin-wayland konsole \
  breeze plasma-workspace-wayland || apt-get install -y --no-install-recommends plasma-desktop kwin-wayland konsole
