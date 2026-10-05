#!/bin/bash
# tests/r181/quick.sh TAG: 061 / 078 checks and the cross-process probes on :98 as it is now (181)
cd /home/xl0/projects/wine
. tests/r181/env.sh $1 98
xdotool mousemove 1900 1060
for r in vulkan gl; do
  export WINE_D3D_CONFIG=renderer=$r
  for t in "expose_present.exe" "expose_present.exe nopump" "expose_present.exe show" "present_lag.exe 60 1" "present_lag.exe 30 0" "xproc_hidden_present.exe" "xproc_swapchain_expose.exe" "layered_child_gpu.exe"; do
    set -- $t
    timeout 90 "$B/wine" tests/$1 ${@:2} >inst/181/r/quick.out 2>/dev/null </dev/null; rc=$?; out=$(tail -1 inst/181/r/quick.out)
    echo "$TAG $r $t: rc=$rc ${out:0:170}"
  done
done
"$B/server/wineserver" -k 2>/dev/null; sleep 1
