#!/bin/bash
# gpu.sh BUILD TAG RENDERER EXE ARGS...: run on the headless NVIDIA Xorg :101 (inv4's display, leased) with an own prefix per build
cd /home/xl0/projects/wine
case $1 in integ) B=$PWD/build ;; b173) B=$PWD/wt/173-build ;; fix) B=$PWD/wt/182-build ;; dbg) B=$PWD/wt/182-dbg-build ;; esac
b=$1; tag=$2; r=$3; shift 3
export WINEPREFIX=$PWD/inst/182/pfx-$b-101
if [ ! -e $WINEPREFIX/system.reg ]; then
  tools/sysroot.sh run env DISPLAY=:101 DRI_PRIME=pci-0000_ac_00_0 WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u > inst/182/out/wineboot-$b-101.log 2>&1
  $B/server/wineserver -w
fi
tools/sysroot.sh run env DISPLAY=:101 DRI_PRIME=pci-0000_ac_00_0 WINE_D3D_CONFIG=renderer=$r WINEDEBUG=${WINEDEBUG:-err+all} timeout 300 $B/wine "$@" > inst/182/out/$tag.out 2>&1
echo "$tag: exit $?; $(grep -a -c 'XIDALLOC pid' inst/182/out/$tag.out) unlocked allocation reports; $(grep -a -c 'X Error\|XERROR pid' inst/182/out/$tag.out) X errors"
