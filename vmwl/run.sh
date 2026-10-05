#!/usr/bin/env bash
# Wayland test VM (Ubuntu 26.04 guest). Usage: vmwl/run.sh   (first run creates disk + seed from dl/)
#   SSH 127.0.0.1:2223 (user xl0, key vmwl/id_ed25519), VNC 127.0.0.1:5911, HMP vmwl/mon.sock, QMP vmwl/qmp.sock.
#   Project subset exported read-only over virtio-fs, mounted under /host in the guest: tests, wine-src/{nls,fonts}
#   (the symlinks of build/ point there) + $VMWL_BIND (space-separated project-relative paths, same layout in the
#   guest; default "build" = the integ build). The virtiofsd processes exit with QEMU.
set -euo pipefail
cd "$(dirname "$0")"
# One instance only: a second start used to take the sockets, pid file and TPM state away from the running VM.
# Workers share this VM: never kill it because "it isn't yours"; shut it down from inside the guest when all are done.
if [ -f qemu.pid ] && tr '\0' ' ' < "/proc/$(cat qemu.pid)/cmdline" 2>/dev/null | grep -q -- "-name vmwl "; then
  echo "vmwl VM already running (pid $(cat qemu.pid))"; exit 0
fi
DISK=${WINE_DATA:-/data/users/xl0/wine}/vmwl-disk.qcow2  # big image outside /home
ROOT=$(realpath ..)

if [ ! -f "$DISK" ]; then
  ./fetch.sh
  [ -f id_ed25519 ] || ssh-keygen -q -t ed25519 -N '' -C vmwl -f id_ed25519
  qemu-img convert -O qcow2 dl/ubuntu-26.04-server-cloudimg-amd64.img "$DISK"
  qemu-img resize -q "$DISK" 120G
  t=$(mktemp -d)
  sed "s|@PUBKEY@|$(cat id_ed25519.pub)|" user-data.in > $t/user-data
  printf 'instance-id: vmwl-1\nlocal-hostname: %s\n' "$(hostname)" > $t/meta-data
  xorriso -as mkisofs -quiet -volid cidata -J -r -o seed.iso $t/user-data $t/meta-data
  rm -f $t/user-data $t/meta-data; rmdir $t
fi

# One read-only virtiofsd per exported path (user namespaces, and so bwrap, are not available in the sandbox on this host):
# tag "host" is an empty skeleton holding the mount points, mounted at /host by the guest's fstab; the paths are tags
# h0, h1, ... mounted below it over ssh once the guest is up.
VFSD=../deps/virtiofsd-v1.14.0/target/x86_64-unknown-linux-musl/release/virtiofsd
vfsd() { rm -f "$1"; setsid $VFSD --socket-path="$PWD/$1" --shared-dir="$2" --sandbox=none --readonly --log-level=${VFS_LOG:-warn} >>vfs.log 2>&1 </dev/null &
         until [ -S "$1" ]; do sleep 0.1; done; }
: >vfs.log
fsdev=(-chardev socket,id=vfs,path=vfs.sock -device vhost-user-fs-pci,chardev=vfs,tag=host); mounts=; n=0
for p in tests wine-src/nls wine-src/fonts ${VMWL_BIND:-build}; do
  mkdir -p "skel/$p"; vfsd vfs-$n.sock "$ROOT/$p"
  fsdev+=(-chardev socket,id=vfs$n,path=vfs-$n.sock -device vhost-user-fs-pci,chardev=vfs$n,tag=h$n)
  mounts+="mount -t virtiofs -o ro h$n /host/$p; "; n=$((n + 1))
done
vfsd vfs.sock "$PWD/skel"

# GL=1: virgl-accelerated guest GL through a host render node (egl-headless; GL_NODE overrides). Default: llvmpipe in the guest.
gpu=(-device virtio-vga,xres=1280,yres=800)
[ -z "${GL:-}" ] || gpu=(-device virtio-vga-gl,xres=1280,yres=800 -display egl-headless,rendernode=${GL_NODE:-/dev/dri/renderD130})

qemu-system-x86_64 \
  -name vmwl -machine q35,accel=kvm -cpu host -smp 8 -m 16G \
  -object memory-backend-memfd,id=mem,size=16G,share=on -numa node,memdev=mem \
  "${fsdev[@]}" \
  -drive file="$DISK",if=none,id=disk0,cache=unsafe,discard=unmap -device virtio-blk-pci,drive=disk0,bootindex=1 \
  -drive file=seed.iso,media=cdrom,if=none,id=cd0,readonly=on -device ide-cd,drive=cd0,bus=ide.0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2223-:22 -device virtio-net-pci,netdev=net0 \
  -device qemu-xhci -device usb-tablet \
  "${gpu[@]}" \
  -vnc 127.0.0.1:11 -monitor unix:mon.sock,server,nowait -qmp unix:qmp.sock,server,nowait \
  -daemonize -pidfile qemu.pid
for _ in $(seq 90); do ./ssh.sh true 2>/dev/null && break; sleep 2; done
echo "$mounts" >skel/.mounts   # after a guest reboot: vmwl/ssh.sh sudo sh /host/.mounts
./ssh.sh "sudo sh /host/.mounts"
