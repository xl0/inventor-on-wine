#!/bin/bash
# cost.sh ROUNDS [SECS] [TAG]: tests/r177/cost.exe alternately on base (build-next) and fix (FIXBUILD, default wt/177-build),
# same Xvfb :1772 (pinned to CPU 35), app on CPU 33, wineserver on CPU 37; prints min / median us per line
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
xpid=$(sed -n 3p inst/177/x.pids); taskset -pc 35 $xpid > /dev/null
out=inst/177/out/cost-${3:-run}.txt; : > $out
for r in $(seq 1 $1); do
  for b in base fix; do
    case $b in base) B=$PWD/build-next ;; fix) B=$PWD/${FIXBUILD:-wt/177-build} ;; esac
    export DISPLAY=:1772 WINEPREFIX=$PWD/inst/177/pfx-cost-$b
    if [ ! -e $WINEPREFIX/system.reg ]; then
      WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -u > inst/177/out/wineboot-$(basename $WINEPREFIX).log 2>&1; $B/server/wineserver -w
    fi
    WINEDEBUG=-all taskset -c 37 $B/server/wineserver -p 2>/dev/null
    WINEDEBUG=-all taskset -c 33 $B/wine tests/r177/cost.exe ${2:-2} 2>/dev/null | tr -d '\r' | sed "s/^/$b /" >> $out
    $B/server/wineserver -k 2>/dev/null; $B/server/wineserver -w 2>/dev/null
  done
done
python3 - $out <<'PY'
import sys, collections, statistics
d = collections.defaultdict(list)
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) < 4: continue
    d[(' '.join(p[1:-2]), p[0])].append(float(p[-2]))
names = []
for (n, b) in d:
    if n not in names: names.append(n)
print("%-22s %18s %18s" % ("us per call", "base min / median", "fix min / median"))
for n in names:
    print("%-22s %8.2f / %7.2f %8.2f / %7.2f" % (n, min(d[(n,'base')]), statistics.median(d[(n,'base')]), min(d[(n,'fix')]), statistics.median(d[(n,'fix')])))
PY
