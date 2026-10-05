#!/bin/bash
# x.sh start|stop: Xvfb :1800-:1801 (no WM), :1802-:1809 (openbox) for issue 191; pids in inst/191/x.pids
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=inst/191; P=$R/x.pids
NOWM="1800 1801"; OB="1802 1803 1804 1805 1806 1807 1808 1809"
case $1 in
start)
  : > $P
  for n in $NOWM $OB; do
    [ -e /tmp/.X11-unix/X$n ] && { echo "display :$n is in use"; exit 1; }
  done
  for n in $NOWM $OB; do
    setsid nohup Xvfb :$n -screen 0 1280x1024x24 -nolisten tcp > $R/out/xvfb-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  sleep 2
  for n in $OB; do
    DISPLAY=:$n setsid nohup openbox > $R/out/openbox-$n.log 2>&1 </dev/null &
    echo $! >> $P
  done
  sleep 1; for n in $NOWM $OB; do DISPLAY=:$n xdpyinfo | grep -c dimensions; done | tr '\n' ' ' ;;
stop) for p in $(cat $P); do kill $p 2>/dev/null; done; : > $P ;;
esac
