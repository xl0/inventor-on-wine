#!/bin/bash
# x.sh start|stop: Xvfb :1770-:1773 (no WM), :1774-:1777 (openbox) for issue 177; pids in inst/177/x.pids (:1778-:1781, no WM, were added by hand)
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=inst/177; P=$R/x.pids
NOWM="1770 1771 1772 1773"; OB="1774 1775 1776 1777"
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
