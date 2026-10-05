#!/bin/bash
# fresh.sh BUILD DISPLAYNUM TAG RUNS [THREADS]: thrwin.exe on a freshly started wineserver each run (every thread then
# also starts its own explorer.exe /desktop); on a hang or death: symbol of each swallowed fault ip, gdb stacks
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
R=$PWD/inst/191
case $1 in fix) B=$PWD/wt/191-build ;; base) B=$PWD/wt/177-build ;; *) B=$PWD/$1 ;; esac
b=$(basename $1); d=$2; tag=$3; runs=$4; n=${5:-16}; ok=0; hang=0; died=0
unset WAYLAND_DISPLAY; export DISPLAY=:$d WINEPREFIX=$R/pfx-$b-$d
[ -n "$X11LIB" ] && export LD_LIBRARY_PATH=$X11LIB:$LD_LIBRARY_PATH
if [ ! -e $WINEPREFIX/system.reg ]; then
  WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u > $R/out/wineboot-$(basename $WINEPREFIX).log 2>&1
fi
for i in $(seq 1 $runs); do
  $B/server/wineserver -k 2>/dev/null; $B/server/wineserver -w 2>/dev/null
  o=$R/out/fresh-$tag-$b-$d-r$i
  WINEDEBUG=${WINEDEBUG:-err+all,+seh} setsid nohup $B/wine tests/r191/thrwin.exe $n ${ARGS:-1} > $o.out 2>&1 </dev/null &
  pid=$!
  for ((s = 0; s < 100; s++)); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
  if kill -0 $pid 2>/dev/null; then
    hang=$((hang+1))
    ips=$(grep -a "handle_syscall_fault code" $o.out | grep -v "ip=0$" | sed 's/.* ip=\([0-9a-f]*\).*/\1/' | sort -u | head -5)
    gdb -p $pid -batch -ex 'source tools/gdb/winesyms.py' $(for ip in $ips; do echo "-ex"; echo "info symbol 0x$ip"; echo "-ex"; echo "x/6i 0x$ip"; done) -ex 'thread apply all bt' -ex 'source inst/182/mowner.py' > $o-hang.txt 2>&1
    cp /proc/$pid/maps $o-maps.txt 2>/dev/null
    kill -9 $pid
    echo "fresh-$tag-$b-$d-r$i: HANG; fault ips: $(for ip in $ips; do grep -a -A1 "^Line\|in section\|No symbol matches" $o-hang.txt | head -0; echo -n "$ip "; done)"
    grep -a "in section\|No symbol matches" $o-hang.txt | sort | uniq -c
  elif grep -aq "DONE" $o.out; then ok=$((ok+1)); tools/del -f $o.out
  else died=$((died+1)); echo "fresh-$tag-$b-$d-r$i: died: $(grep -a 'free()\|double free\|realloc()\|corrupt\|Assertion\|invalid frame' $o.out | cut -c1-80 | sort | uniq -c | head -3)"
  fi
done
$B/server/wineserver -k 2>/dev/null
echo "RESULT fresh thrwin $tag $b :$d threads $n: $runs runs: done $ok, hang $hang, died $died"
