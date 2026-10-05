#!/bin/bash
# tests/r181/fps.sh: d3d11_present.exe fps (top-level swapchain) and ms per Present of a child swapchain in another
# process' window (xp.exe child, offscreen client surface), base / fix interleaved, 3 runs each (181)
cd /home/xl0/projects/wine
for i in 1 2 3; do for t in base fix; do (
  . tests/r181/env.sh $t 98; export WINE_D3D_CONFIG=renderer=vulkan
  xdotool mousemove 1900 1060
  f=$("$B/wine" tests/d3d11_present.exe 2>/dev/null </dev/null | grep fps)
  "$B/wine" tests/r132/xp.exe host secs=14 >inst/181/r/fps.host 2>/dev/null </dev/null &
  sleep 3
  P1=$(grep -o 'p1=[0-9A-Fa-fx]*' inst/181/r/fps.host | head -1 | cut -d= -f2)
  c=$("$B/wine" tests/r132/xp.exe child $P1 frames=3000 sleep=0 quiet=1 2>/dev/null </dev/null | tail -2 | tr '\n' ' ')
  wait
  echo "$t run $i: $f | child: ${c:0:150}"
  "$B/server/wineserver" -k 2>/dev/null; sleep 1
) done; done
