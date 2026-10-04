#!/bin/bash
# x.sh start|stop: Xvfb :1410, :1414 (no WM) and :1411, :1415 (openbox), :1416 (spare, no WM) for issue 173; pids in inst/173/x.pids
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
P=inst/173/x.pids
case $1 in
start)
  : > $P
  for n in 1410 1411 1414 1415 1416; do
    setsid nohup Xvfb :$n -screen 0 1280x1024x24 -nolisten tcp > inst/173/out/xvfb-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  sleep 2
  for n in 1411 1415; do
    DISPLAY=:$n setsid nohup openbox > inst/173/out/openbox-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  sleep 1; for n in 1410 1411 1414 1415 1416; do DISPLAY=:$n xdpyinfo | grep -c dimensions; done | tr '\n' ' ' ;;
stop) for p in $(cat $P); do kill $p 2>/dev/null; done; : > $P ;;
esac
