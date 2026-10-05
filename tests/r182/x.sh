#!/bin/bash
# x.sh start|stop: Xvfb :1480 :1481 :1485 :1486 (no WM), :1482 :1483 (openbox), :1484 (awesome) for issues 182/184; pids in inst/182/x.pids
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=inst/182; P=$R/x.pids
ALL="1480 1481 1482 1483 1484 1485 1486"
case $1 in
start)
  : > $P
  for n in $ALL; do
    setsid nohup Xvfb :$n -screen 0 1280x1024x24 -nolisten tcp > $R/out/xvfb-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  sleep 2
  for n in 1482 1483; do
    DISPLAY=:$n setsid nohup openbox > $R/out/openbox-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  DISPLAY=:1484 setsid nohup awesome -c x/awesome-rc.lua > $R/out/awesome-1484.log 2>&1 </dev/null &
  echo $! >> $P
  sleep 1; for n in $ALL; do DISPLAY=:$n xdpyinfo | grep -c dimensions; done | tr '\n' ' ' ;;
stop) for p in $(cat $P); do kill $p 2>/dev/null; done; : > $P ;;
esac
