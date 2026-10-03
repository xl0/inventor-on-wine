#!/usr/bin/env bash
# Wayland test VM (Ubuntu 26.04 guest). Usage: vmwl/run.sh   (first run creates disk + seed from dl/)
#   SSH 127.0.0.1:2223 (user xl0, key vmwl/id_ed25519), VNC 127.0.0.1:5911, HMP vmwl/mon.sock, QMP vmwl/qmp.sock.
#   Project subset exported read-only over virtio-fs (tag "host", mounted at /host in the guest):
#   wt/wayland-build, tests, wine-src/{nls,fonts} (the build's symlinks point there) + $VMWL_BIND
#   (space-separated project-relative paths, same layout in the guest). virtiofsd exits with QEMU.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(realpath ..)

if [ ! -f disk.qcow2 ]; then
  ./fetch.sh
  [ -f id_ed25519 ] || ssh-keygen -q -t ed25519 -N '' -C vmwl -f id_ed25519
  qemu-img convert -O qcow2 dl/ubuntu-26.04-server-cloudimg-amd64.img disk.qcow2
  qemu-img resize -q disk.qcow2 120G
  t=$(mktemp -d)
  sed "s|@PUBKEY@|$(cat id_ed25519.pub)|" user-data.in > $t/user-data
  printf 'instance-id: vmwl-1\nlocal-hostname: %s\n' "$(hostname)" > $t/meta-data
  xorriso -as mkisofs -quiet -volid cidata -J -r -o seed.iso $t/user-data $t/meta-data
  rm -f $t/user-data $t/meta-data; rmdir $t
fi

# virtiofsd inside bwrap: only the listed paths are visible under /mnt, read-only.
rm -f vfs.sock
binds=(); for p in wt/wayland-build tests wine-src/nls wine-src/fonts ${VMWL_BIND:-}; do
  binds+=(--ro-bind "$ROOT/$p" "/mnt/$p"); done
setsid bwrap --ro-bind / / --tmpfs /mnt --bind "$PWD" "$PWD" --dev-bind /dev /dev "${binds[@]}" \
  ../deps/virtiofsd-v1.14.0/target/x86_64-unknown-linux-musl/release/virtiofsd \
  --socket-path="$PWD/vfs.sock" --shared-dir=/mnt --sandbox=none --readonly --log-level=${VFS_LOG:-warn} >vfs.log 2>&1 </dev/null &
until [ -S vfs.sock ]; do sleep 0.1; done

# GL=1: virgl-accelerated guest GL through a host render node (egl-headless; GL_NODE overrides). Default: llvmpipe in the guest.
gpu=(-device virtio-vga,xres=1280,yres=800)
[ -z "${GL:-}" ] || gpu=(-device virtio-vga-gl,xres=1280,yres=800 -display egl-headless,rendernode=${GL_NODE:-/dev/dri/renderD130})

qemu-system-x86_64 \
  -name vmwl -machine q35,accel=kvm -cpu host -smp 8 -m 16G \
  -object memory-backend-memfd,id=mem,size=16G,share=on -numa node,memdev=mem \
  -chardev socket,id=vfs,path=vfs.sock -device vhost-user-fs-pci,chardev=vfs,tag=host \
  -drive file=disk.qcow2,if=none,id=disk0,cache=unsafe,discard=unmap -device virtio-blk-pci,drive=disk0,bootindex=1 \
  -drive file=seed.iso,media=cdrom,if=none,id=cd0,readonly=on -device ide-cd,drive=cd0,bus=ide.0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2223-:22 -device virtio-net-pci,netdev=net0 \
  -device qemu-xhci -device usb-tablet \
  "${gpu[@]}" \
  -vnc 127.0.0.1:11 -monitor unix:mon.sock,server,nowait -qmp unix:qmp.sock,server,nowait \
  -daemonize -pidfile qemu.pid
