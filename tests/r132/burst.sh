#!/bin/bash
# burst.sh [TAG]: backpressure test of the event loop: the compositor is stopped (SIGSTOP, our own headless session)
# while burst.exe queues megabytes of requests and then stops pumping; after SIGCONT the window must turn red.
. "$(dirname "$0")/env.sh"
export WINEDEBUG=-all
O=inst/132/m1/burst-${1:-run}
G=$(ps -eo pid,args | awk '/gnome-shell --headless/ && !/awk/ {print $1; exit}')
(setsid nohup $W tests/r132/burst.exe ${N:-2000} ${LEN:-3000} > $O.out 2>&1 &)
sleep 3; kill -STOP $G; echo "compositor stopped"
# STRACE=1: the poll and sendmsg calls of the process (the event thread polls the display fd for POLLOUT while a flush is pending)
[ -z "${STRACE:-}" ] || { B=$(ps -eo pid,args | awk '/burst.exe/ && !/awk/ {print $1; exit}'); (timeout 20 strace -f -tt -e trace=poll,ppoll,sendmsg -p $B -o $O.strace >/dev/null 2>&1 &); }
sleep 5; cat $O.out | grep burst
kill -CONT $G; echo "compositor continued"
for t in 2 6 12; do sleep $((t == 2 ? 2 : t == 6 ? 4 : 6)); x/wshot.sh $O-$t.png >/dev/null
  python3 -c "
from PIL import Image
im=Image.open('$O-$t.png').convert('RGB'); px=im.load()
red=sum(1 for y in range(40,1080,4) for x in range(0,1920,4) if px[x,y]==(255,0,0)); white=sum(1 for y in range(40,1080,4) for x in range(0,1920,4) if px[x,y]==(255,255,255))
print('$t s after continue: red %d white %d' % (red, white))"; done
ps -eo pid,args | awk '/burst.exe/ && !/awk/ {print $1}' | xargs -r kill
