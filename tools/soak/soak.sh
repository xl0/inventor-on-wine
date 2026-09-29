#!/bin/sh
# Usage: tools/soak/soak.sh OUT HOURS
# Soak test of one Inventor session in prefixes/inv on :98 (run.sh restarts Inventor
# only after a crash). Loops `tools/invscen/run.sh all` until HOURS have passed;
# every 3rd iteration also `samples`, every 10th (from the 1st) and after the last a
# tools/uilat rubber + orbit measurement (OUT/uilat, tag iterN).
# Per iteration: OUT/iterN/ (run.sh table + results/*.txt [+ samples.txt]), a line in
# OUT/iters.csv, events (crash, licensing error, Inventor restart) in OUT/events.txt.
# Background sampler, every 30 s: OUT/mon.csv (Linux side: Inventor, its wineserver,
# the :98 Xorg) and OUT/probe-PID.txt (tools/soak/resprobe.c inside the prefix:
# kernel handles, GDI objects, windows). After the suite of every odd iteration: combase stub
# managers / proxies per apartment (tools/soak/stubs.sh, gdb stalls Inventor ~s) in OUT/stubs.txt.
# Plots: tools/soak/plot.py OUT.
set -u
cd "$(dirname "$0")/../.."
OUT=${1:?usage: $0 OUT HOURS}; H=${2:?}
WP=$PWD/prefixes/inv; T=$WP/drive_c/users/xl0/AppData/Local/Temp; CL=$WP/drive_c/winedbg-crash.log
export WINEPREFIX=$WP DISPLAY=:98 DRI_PRIME=pci-0000_ca_00_0 WINE_D3D_CONFIG=renderer=vulkan WINEDEBUG=-all
mkdir -p $OUT
[ -e $OUT/resprobe.exe ] || x86_64-w64-mingw32-gcc -O2 -o $OUT/resprobe.exe tools/soak/resprobe.c -lpsapi -lntdll
inpfx() { tr '\0' '\n' </proc/$1/environ 2>/dev/null | grep -qx "WINEPREFIX=$WP"; }
invpid() { for p in $(pgrep -x Inventor.exe); do inpfx $p && echo $p && return; done; }
ev() { echo "$(date '+%F %T') iter=$I $*" | tee -a $OUT/events.txt; }
st() { awk '{ print $14 + $15 }' /proc/$1/stat 2>/dev/null; }   # utime + stime, ticks
kb() { awk -v k=$2 '$1 == k":" { print $2 }' /proc/$1/status 2>/dev/null; }

I=0; echo 0 >$OUT/.iter
(   # sampler
	[ -s $OUT/mon.csv ] || echo t,iter,inv,inv_rss,inv_vsz,inv_thr,inv_fds,inv_maps,inv_cpu,ws_rss,ws_cpu,ws_fds,xorg_rss,xorg_cpu,tmp_files >$OUT/mon.csv
	X=$(ps -eo pid,args | awk '$2 ~ /Xorg$/ && $3 == ":98" { print $1 }')
	PP=
	while [ -e $OUT/.iter ]; do
		P=$(invpid); W=
		for w in $(pgrep -x wineserver); do inpfx $w && W=$w; done
		if [ -n "$P" ] && [ "$P" != "$PP" ] && [ "$(ps -o etimes= -p $P)" -gt 60 ]; then
			build/wine $OUT/resprobe.exe Inventor.exe 30 >>$OUT/probe-$P.txt 2>&1 </dev/null &
			PP=$P
		fi
		echo "$(date +%s),$(cat $OUT/.iter),$P,${P:+$(kb $P VmRSS)},${P:+$(kb $P VmSize)},${P:+$(kb $P Threads)},${P:+$(ls /proc/$P/fd 2>/dev/null | wc -l)},${P:+$(wc -l </proc/$P/maps 2>/dev/null)},${P:+$(st $P)},${W:+$(kb $W VmRSS)},${W:+$(st $W)},${W:+$(ls /proc/$W/fd | wc -l)},$(kb $X VmRSS),$(st $X),$(ls $T | wc -l)" >>$OUT/mon.csv
		sleep 30
	done
) &

stubs() { p=$(invpid); [ -n "$p" ] && tools/soak/stubs.sh $p | sed "s/^/$(date +%s) iter=$I pid=$p /" >>$OUT/stubs.txt; }
uilat() { python3 tools/uilat/uilat.py rubber orbit --setup --out $OUT/uilat --tag iter$I >>$OUT/uilat.log 2>&1 </dev/null || ev "uilat failed rc=$?"; }
END=$(( $(date +%s) + $(awk -v h=$H 'BEGIN { printf "%d", h * 3600 }') ))
echo iter,start,end,inv_before,inv_after,suite_s,samples_s,fails,crashlog_bytes,dumps >$OUT/iters.csv
while [ $(date +%s) -lt $END ]; do
	I=$((I + 1)); echo $I >$OUT/.iter; D=$OUT/iter$I; mkdir -p $D
	t0=$(date +%s); P0=$(invpid); ev "start ${P0:+Inventor $P0}"
	tools/invscen/run.sh all >$D/table.txt 2>$D/stderr.txt </dev/null
	cp inst/invscen/results/*.txt $D/
	t1=$(date +%s); ts=
	[ $((I % 2)) = 1 ] && stubs
	if [ $((I % 3)) = 0 ]; then
		t2=$(date +%s); tools/invscen/run.sh samples >$D/samples.txt 2>&1 </dev/null; ts=$(( $(date +%s) - t2 ))
	fi
	[ $((I % 10)) = 1 ] && uilat
	P1=$(invpid)
	[ -n "$P0" ] && [ "$P0" != "$P1" ] && ev "Inventor restarted: $P0 -> ${P1:-none}"
	grep -q "Inventor crashed" $D/stderr.txt && ev "crash (run.sh killed winedbg): $(grep -c 'Inventor crashed' $D/stderr.txt)x"
	grep -l "Licensing error" $D/*.txt 2>/dev/null | while read f; do ev "Licensing error in $f"; done
	f=$(cat $D/*.txt | grep -c '^FAIL ')
	fl=$(grep -h '^FAIL ' $D/*.txt | cut -c1-60 | sed 's/ (.*//' | sort | uniq -c | tr -s ' ' | paste -sd';')
	[ -n "$fl" ] && ev "fails: $fl"
	ab=$(awk '$2 == "ABORT"' $D/table.txt | cut -d' ' -f1 | paste -sd' '); [ -n "$ab" ] && ev "aborted: $ab"
	echo "$I,$t0,$(date +%s),$P0,$P1,$((t1 - t0)),$ts,$f,$(stat -c %s $CL 2>/dev/null),$(ls $T/*.dmp 2>/dev/null | wc -l)" >>$OUT/iters.csv
done
uilat
ev "done"
rm -f $OUT/.iter
wait
for p in $(ps -eo pid,args | awk '/resprobe\.exe/ && $2 !~ /^(awk|sh)$/ { print $1 }'); do inpfx $p && kill $p; done
