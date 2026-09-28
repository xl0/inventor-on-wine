#!/bin/sh
# Usage: tools/invscen/run.sh [--vm] SCENARIO | all
#   WINE_BUILD=dir picks the Wine build (default build/).
#   INV_PREFIX, DISPLAY, DRI_PRIME pick another Inventor setup (default
#   prefixes/inv, :98, pci-0000_ca_00_0); artifacts of another prefix P go to
#   inst/invscen/P/SCENARIO/.
# Builds tools/invscen/{Harness,SCENARIO}.cs with the prefix's .NET 4.8 csc
# (Inventor interop types embedded, /link) and runs it against the Inventor
# running in prefixes/inv on :98 (starts it if needed). Artifacts go to
# inst/invscen/SCENARIO/. Env: WINEDEBUG, INVSCEN_TIMEOUT (whole run, s).
# --vm: run the same exe on the Windows VM instead (vm/winrun.sh, attaches to
# the Inventor already running there; never starts it), artifacts in
# C:\t\scen\SCENARIO, copied back to inst/invscen/ref/SCENARIO/ (reference).
# all: run $SUITE in order, logs in inst/invscen/results/S.txt (--vm:
# inst/invscen/ref/S.txt), then print a PASS/FAIL table and steps > 3x slower
# than the VM reference log (only steps >= 1 s on Wine; connect includes the
# Inventor start).
set -e
W=${WINE_BUILD:-build}  # Wine build dir to run under
cd "$(dirname "$0")/../.."
VM=; [ "$1" = --vm ] && VM=1 && shift
S=${1:?usage: $0 [--vm] SCENARIO|all}
# export last: its STEP import makes Wine mshtml ask to install Gecko (prefix has none),
# a modal prompt that blocks Inventor until dismissed.
SUITE=${SUITE:-"hello tlb part asm drawing feat params sheetmetal asmcon asmbig drawing2 script export"}
if [ "$S" = all ]; then
	L=inst/invscen/results; [ -n "$VM" ] && L=inst/invscen/ref
	mkdir -p $L
	WP=$(realpath ${INV_PREFIX:-prefixes/inv})
	for s in $SUITE; do
		echo "== $s" >&2
		# A crashed Inventor (issue 034) lingers in winedbg --auto + CER dialog and
		# blocks connect: kill them so the scenario starts a fresh Inventor. Never
		# wineserver -k: this prefix's AdskLicensingService may serve the other
		# prefixes' Inventors too (one service on 127.0.0.1:39683 for all).
		for p in $(ps -eo pid,args | awk '$2 ~ /winedbg/ && $3 == "--auto" { print $1 }'); do
			if [ -z "$VM" ] && tr '\0' '\n' </proc/$p/environ | grep -qx "WINEPREFIX=$WP"; then
				echo "Inventor crashed: killing $WP's Inventor, winedbg, CER dialog" >&2
				for q in $(ps -eo pid,args | awk '$2 !~ /^(awk|sh|bash|\/bin\/sh)$/ && /Inventor\.exe|winedbg|senddmp\.exe|Autodesk CER.dialog/ { print $1 }'); do
					if tr '\0' '\n' </proc/$q/environ 2>/dev/null | grep -qx "WINEPREFIX=$WP"; then kill -9 $q 2>/dev/null || true; fi
				done
				sleep 5; break
			fi
		done
		"$0" ${VM:+--vm} $s >$L/$s.txt 2>&1 </dev/null || true
	done
	# table: scenario, result, pass/fail/skip, total step time, first failure
	printf '%-11s %-6s %5s %5s %5s %8s  %s\n' scenario result pass fail skip time first-failure
	for s in $SUITE; do
		awk -v s=$s '
			/^PASS / { p++ } /^FAIL / { f++; if (!ff) { ff = $0; sub(/^FAIL /, "", ff); sub(/: .*/, "", ff) } }
			/^SKIP / { k++ } /^RESULT / { r = $2 }
			/^(PASS|FAIL) .* \([0-9.]+s\)/ { match($0, /\(([0-9.]+)s\)/); t += substr($0, RSTART + 1, RLENGTH - 3) }
			END { printf "%-11s %-6s %5d %5d %5d %7.1fs  %s\n", s, r ? r : "ABORT", p, f, k, t, ff }' $L/$s.txt
	done
	[ -n "$VM" ] && exit 0
	echo; echo "steps >3x slower than the VM reference (Wine >= 1 s):"
	for s in $SUITE; do
		[ -f inst/invscen/ref/$s.txt ] || continue
		awk -v s=$s '
			function key(l) { sub(/^(PASS|FAIL) /, "", l); sub(/ \([0-9.]+s\).*/, "", l); return l }
			function sec(l) { match(l, /\(([0-9.]+)s\)/); return substr(l, RSTART + 1, RLENGTH - 3) }
			!/^(PASS|FAIL) .* \([0-9.]+s\)/ { next }
			FNR == NR { ref[key($0)] = sec($0); next }
			{ k = key($0); w = sec($0) + 0; v = ref[k] + 0
			  if (k != "connect" && k in ref && w >= 1 && w > 3 * (v < 0.1 ? 0.1 : v)) printf "  %-11s %-34s wine %6.1fs  vm %5.1fs\n", s, k, w, v }' \
			inst/invscen/ref/$s.txt inst/invscen/results/$s.txt
	done
	exit 0
fi
P=${INV_PREFIX:-prefixes/inv}
export WINEPREFIX=$(realpath $P) DISPLAY=${DISPLAY:-:98} \
	DRI_PRIME=${DRI_PRIME:-pci-0000_ca_00_0} WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-renderer=vulkan} WINEDEBUG=${WINEDEBUG:--all}
B=inst/invscen/bin O=inst/invscen/$S L=inst/invscen/inventor.log
[ "$WINEPREFIX" = $PWD/prefixes/inv ] || O=inst/invscen/${P##*/}/$S L=inst/invscen/${P##*/}/inventor.log
mkdir -p $B $O
if [ ! -e $B/$S.exe ] || [ tools/invscen/$S.cs -nt $B/$S.exe ] || [ tools/invscen/Harness.cs -nt $B/$S.exe ]; then
	$W/wine 'C:\windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /debug- \
		/out:"inst\\invscen\\bin\\$S.exe" /link:'C:\Program Files\Autodesk\Inventor 2027\Bin\Public Assemblies\Autodesk.Inventor.Interop.dll' \
		"tools\\invscen\\Harness.cs" "tools\\invscen\\$S.cs" </dev/null
fi
if [ -n "$VM" ]; then
	d='C:\t\scen\'$S
	rc=0; WINRUN_ID=scen WINRUN_TIMEOUT=${INVSCEN_TIMEOUT:-1800} vm/winrun.sh $B/$S.exe "$d" || rc=$?
	mkdir -p inst/invscen/ref && rm -rf inst/invscen/ref/$S
	scp -rq -i vm/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes \
		-o LogLevel=ERROR -P 2222 "dev@127.0.0.1:C:/t/scen/$S" inst/invscen/ref/ || true
	exit $rc
fi
if ! (for p in $(pgrep -x Inventor.exe); do
	tr '\0' '\n' </proc/$p/environ | grep -qx "WINEPREFIX=$WINEPREFIX" && exit 0; done; exit 1); then
	echo "starting Inventor" >&2
	setsid nohup $W/wine 'C:\Program Files\Autodesk\Inventor 2027\Bin\Inventor.exe' >>$L 2>&1 </dev/null &
fi
exec timeout ${INVSCEN_TIMEOUT:-1800} $W/wine $B/$S.exe "$($W/wine winepath -w $O)"
