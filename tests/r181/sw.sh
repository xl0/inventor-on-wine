#!/bin/bash
# tests/r181/sw.sh TAG RENDERER MODE..: gpuchild.exe on Xvfb :1470 (openbox, no compositor) with lavapipe / llvmpipe (181)
cd /home/xl0/projects/wine
T=$1 R=$2; shift 2
. tests/r181/env.sh $T 1470
export LIBGL_ALWAYS_SOFTWARE=1 VK_ICD_FILENAMES="$(ls /usr/share/vulkan/icd.d/lvp_icd*.json "${SYSROOT_ENV:-/nonexistent}"/usr/share/vulkan/icd.d/lvp_icd*.json 2>/dev/null | head -1)"
xdotool mousemove 1900 1060
WINE_D3D_CONFIG=renderer=$R timeout 300 "$B/wine" tests/r181/gpuchild.exe "$@" 2>>inst/181/r/stderr-sw-$T.log </dev/null | grep -E "RESULT|BAD" | sort | uniq -c | cut -c1-150 | tr '\n' ';'
echo
