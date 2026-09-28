#!/bin/sh
# Usage: tools/invscen/run.sh [--vm] SCENARIO
# Builds tools/invscen/{Harness,SCENARIO}.cs with the prefix's .NET 4.8 csc
# (Inventor interop types embedded, /link) and runs it against the Inventor
# running in prefixes/inv on :98 (starts it if needed). Artifacts go to
# inst/invscen/SCENARIO/. Env: WINEDEBUG, INVSCEN_TIMEOUT (whole run, s).
# --vm: run the same exe on the Windows VM instead (vm/winrun.sh, attaches to
# the Inventor already running there; never starts it), artifacts in
# C:\t\scen\SCENARIO, copied back to inst/invscen/vm/SCENARIO/.
set -e
cd "$(dirname "$0")/../.."
VM=; [ "$1" = --vm ] && VM=1 && shift
S=${1:?usage: $0 [--vm] SCENARIO}
export WINEPREFIX=$PWD/prefixes/inv DISPLAY=${DISPLAY:-:98} \
	DRI_PRIME=pci-0000_ca_00_0 WINE_D3D_CONFIG=renderer=vulkan WINEDEBUG=${WINEDEBUG:--all}
[ "$DISPLAY" = :98 ] || echo "warning: DISPLAY=$DISPLAY" >&2
B=inst/invscen/bin O=inst/invscen/$S
mkdir -p $B $O
if [ ! -e $B/$S.exe ] || [ tools/invscen/$S.cs -nt $B/$S.exe ] || [ tools/invscen/Harness.cs -nt $B/$S.exe ]; then
	build/wine 'C:\windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /debug- \
		/out:"inst\\invscen\\bin\\$S.exe" /link:'C:\Program Files\Autodesk\Inventor 2027\Bin\Public Assemblies\Autodesk.Inventor.Interop.dll' \
		"tools\\invscen\\Harness.cs" "tools\\invscen\\$S.cs"
fi
if [ -n "$VM" ]; then
	d='C:\t\scen\'$S
	rc=0; WINRUN_ID=scen WINRUN_TIMEOUT=${INVSCEN_TIMEOUT:-1800} vm/winrun.sh $B/$S.exe "$d" || rc=$?
	mkdir -p inst/invscen/vm && rm -rf inst/invscen/vm/$S
	scp -rq -i vm/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes \
		-o LogLevel=ERROR -P 2222 "dev@127.0.0.1:C:/t/scen/$S" inst/invscen/vm/ || true
	exit $rc
fi
# Not prefix-specific: assumes no other prefix runs Inventor.
if ! ps -eo args | grep -q '^C:.*\\Inventor\.exe'; then
	echo "starting Inventor" >&2
	setsid nohup build/wine 'C:\Program Files\Autodesk\Inventor 2027\Bin\Inventor.exe' >>inst/invscen/inventor.log 2>&1 &
fi
exec timeout ${INVSCEN_TIMEOUT:-1800} build/wine $B/$S.exe "$(build/wine winepath -w $O)"
