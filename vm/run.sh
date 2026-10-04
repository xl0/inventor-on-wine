#!/usr/bin/env bash
# Windows reference VM. Usage: vm/run.sh [install]
#   VNC 127.0.0.1:5901, SSH 127.0.0.1:2222 (user dev, key vm/id_ed25519),
#   HMP monitor vm/mon.sock, QMP vm/qmp.sock (vm/input.py). `install` attaches the Windows + unattend ISOs.
#   vm/share/ is exported over virtio-fs (tag "share"); virtiofsd exits with QEMU.
set -euo pipefail
cd "$(dirname "$0")"
# Big images live outside /home (no redundancy there: RAID0); everything else stays here.
DATA=${WINE_DATA:-/data/users/xl0/wine} DISK=$DATA/win.qcow2

[ -f "$DISK" ] || qemu-img create -f qcow2 "$DISK" 160G
[ -f vars.fd ] || cp /usr/share/OVMF/OVMF_VARS_4M.ms.fd vars.fd
mkdir -p tpm
# Ubuntu 26.04's AppArmor profile for /usr/bin/swtpm only lets it talk to libvirt-labelled peers (qemu fails with
# "tpm-emulator: Failed to send CMD_SET_DATAFD"). Profiles attach by path: run a copy of the binary instead.
[ -x ../deps/swtpm ] || cp /usr/bin/swtpm ../deps/swtpm
../deps/swtpm socket --tpm2 --tpmstate dir=tpm --ctrl type=unixio,path=tpm/sock --daemon --terminate

# Unprivileged virtiofsd: no uid switching, bwrap already confines it.
mkdir -p share; rm -f vfs.sock
../deps/virtiofsd-v1.14.0/target/x86_64-unknown-linux-musl/release/virtiofsd \
  --socket-path=vfs.sock --shared-dir=share --sandbox=none --log-level=${VFS_LOG:-warn} >vfs.log 2>&1 &
until [ -S vfs.sock ]; do sleep 0.1; done

extra=()
if [ "${1:-}" = install ]; then
  [ -f id_ed25519 ] || ssh-keygen -q -t ed25519 -N '' -C winref -f id_ed25519
  xorriso -as mkisofs -quiet -J -o unattend.iso -graft-points \
    autounattend.xml=autounattend.xml setup.ps1=setup.ps1 authorized_keys=id_ed25519.pub
  extra=(
    -drive file="$DATA/iso/Win11_25H2_English_x64_v2.iso",media=cdrom,if=none,id=cd0,readonly=on
    -device ide-cd,drive=cd0,bus=ide.0,bootindex=0
    -drive file=unattend.iso,media=cdrom,if=none,id=cd1,readonly=on
    -device ide-cd,drive=cd1,bus=ide.1
  )
fi

qemu-system-x86_64 \
  -name winref -machine q35,accel=kvm,smm=on \
  -global driver=cfi.pflash01,property=secure,value=on \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.ms.fd \
  -drive if=pflash,format=raw,file=vars.fd \
  -cpu host,hv_relaxed,hv_vapic,hv_spinlocks=0x1fff,hv_time,hv_vpindex,hv_synic,hv_stimer \
  -smp 16 -m 32G \
  -object memory-backend-memfd,id=mem,size=32G,share=on -numa node,memdev=mem \
  -chardev socket,id=vfs,path=vfs.sock -device vhost-user-fs-pci,chardev=vfs,tag=share \
  -chardev socket,id=tpm,path=tpm/sock -tpmdev emulator,id=tpm0,chardev=tpm -device tpm-crb,tpmdev=tpm0 \
  -drive file="$DISK",if=none,id=disk0,cache=unsafe,discard=unmap \
  -device nvme,drive=disk0,serial=winref0,bootindex=1 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 -device e1000e,netdev=net0 \
  -device qemu-xhci -device usb-tablet -vga std \
  -vnc 127.0.0.1:1 -monitor unix:mon.sock,server,nowait -qmp unix:qmp.sock,server,nowait \
  -daemonize -pidfile qemu.pid "${extra[@]}"

# Windows ISO's UEFI loader waits for "press any key to boot from CD".
if [ "${1:-}" = install ]; then
  python3 - <<'EOF'
import socket, time
s = socket.socket(socket.AF_UNIX); s.connect('mon.sock')
for _ in range(20):
    s.send(b'sendkey ret\n'); time.sleep(1)
EOF
fi
