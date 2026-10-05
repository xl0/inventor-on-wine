#!/bin/bash
# tests/r181/all.sh LABEL: acceptance on :98 as it is now: 174's probes (30 resizes each) + gpuchild.exe, fix build, both renderers (181)
cd /home/xl0/projects/wine
L=$1
for r in vulkan gl; do
  tests/r181/accept.sh fix $r $L
  tests/r181/accept-wpf.sh $r $L
  for m in "grow 30" "move 30" "move 30 d3d9" "movesib 30" "partial 30 nudge" "partial 30"; do
    echo "gpuchild fix $r $L $m: $(tests/r181/run.sh fix $r $m | grep -E 'RESULT|BAD' | sort | uniq -c | tr '\n' ' ' | cut -c1-300)"
  done
  ( . tests/r181/env.sh fix; "$B/server/wineserver" -k 2>/dev/null ); sleep 1
done
