#!/bin/bash
# idlecpu.sh PREFIX_DIR [SECS=60] [threads]: CPU per process of a Wine prefix (its wineserver and
# every process with that WINEPREFIX) over SECS, in % of one core; "threads" also lists threads
# >= 0.5 %. Reads /proc only (utime+stime), so it doesn't disturb what it measures (088).
WP=$(realpath "${1:?usage: $0 PREFIX_DIR [SECS] [threads]}") S=${2:-60} THR=${3:-}
d=$(printf '/tmp/.wine-%d/server-%s-%x' $UID "$(stat -c %D "$WP")" "$(stat -c %i "$WP")")
sp=$(for p in $(pgrep -x wineserver); do [ "$(readlink /proc/$p/cwd)" = "$d" ] && echo $p; done)
pids=$(echo $sp $(grep -lxsz "WINEPREFIX=$WP" /proc/[0-9]*/environ | cut -d/ -f3) | tr ' ' '\n' | sort -u)
snap() { for p in $pids; do for t in /proc/$p/task/*; do
	awk -v p=$p -v t=${t##*/} '{ sub(/.*\) /, ""); print p, t, $12 + $13 }' $t/stat 2>/dev/null; done; done; }
a=$(mktemp); snap >$a; sleep $S; b=$(mktemp); snap >$b
hz=$(getconf CLK_TCK)
awk -v S=$S -v hz=$hz -v thr=$THR 'NR == FNR { a[$2] = $3; next }
	{ d = ($3 - a[$2]) * 100 / hz / S; P[$1] += d; if (thr && d >= 0.5) T[$1 " " $2] = d }
	END { for (p in P) if (P[p] >= 0.1) { c = "cat /proc/" p "/comm"; c | getline n; close(c); printf "%-16s %8s %6.1f%%\n", n, p, P[p] }
	      for (t in T) { split(t, x, " "); c = "cat /proc/" x[1] "/task/" x[2] "/comm"; c | getline n; close(c)
	                     printf "  thread %-16s %s/%s %6.1f%%\n", n, x[1], x[2], T[t] } }' $a $b | sort -k1,1
rm -f $a $b
