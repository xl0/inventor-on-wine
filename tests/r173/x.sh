#!/bin/bash
# x.sh start|stop: Xvfb :1410 (no WM) and :1411 (openbox) for issue 173; pids in inst/173/x.pids
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
P=inst/173/x.pids
case $1 in
start)
  : > $P
  for n in 1410 1411; do
    setsid nohup Xvfb :$n -screen 0 1280x1024x24 -nolisten tcp > inst/173/out/xvfb-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  sleep 2
  DISPLAY=:1411 setsid nohup openbox > inst/173/out/openbox.log 2>&1 </dev/null &
  echo $! >> $P
  sleep 1; DISPLAY=:1410 xdpyinfo | grep -c dimensions; DISPLAY=:1411 xdpyinfo | grep -c dimensions ;;
stop) for p in $(cat $P); do kill $p 2>/dev/null; done; : > $P ;;
esac
