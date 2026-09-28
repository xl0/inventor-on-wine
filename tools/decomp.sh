#!/bin/bash
# Ghidra queries on third-party (non-Microsoft) PE binaries.
#
#   tools/decomp.sh BIN funcs [REGEX]     functions: VA RVA size name
#   tools/decomp.sh BIN decomp ADDR|NAME  pseudo-C of the function containing ADDR
#   tools/decomp.sh BIN xrefs ADDR|NAME   references to ADDR, with referencing function
#   tools/decomp.sh BIN strings [REGEX]   defined strings + referencing functions
#   tools/decomp.sh BIN imports           external symbols + referencing addrs (IAT slot, calls)
#
# ADDR is hex; below the image base it is an RVA, otherwise an absolute VA
# (winedbg backtraces: pass the address as-is only if the module loaded at its
# preferred base, else pass addr - module_base as RVA).
# First use of a binary runs full analysis (minutes) into deps/ghidra-cache/<sha256>/;
# later queries reuse it. One flock per binary serializes access.
# Clean-room: refuses Microsoft binaries (system paths, version-info
# CompanyName, Authenticode cert naming Microsoft).
set -euo pipefail
usage() { sed -n "2,16p" "$0"; exit 2; }
[ $# -ge 2 ] || usage
[[ $2 =~ ^(decomp|xrefs)$ && $# -lt 3 ]] && usage
BIN=$(realpath "$1"); shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GHIDRA=$ROOT/deps/ghidra_12.1.4_PUBLIC
CACHE=$ROOT/deps/ghidra-cache
export JAVA_HOME=$ROOT/deps/jdk-21.0.12.1+1 PATH=$ROOT/deps/jdk-21.0.12.1+1/bin:$PATH
# Keep Ghidra's settings and compiled-script cache out of ~.
export XDG_CONFIG_HOME=$CACHE/xdg/config XDG_CACHE_HOME=$CACHE/xdg/cache

shopt -s nocasematch
if [[ $BIN =~ /(windows/system32|syswow64|winsxs|microsoft\.net)/ ]]; then
	echo "decomp.sh: refusing $BIN: Microsoft system path (clean-room rule)" >&2; exit 3
fi
if m=$(python3 "$ROOT/tools/msbin.py" "$BIN"); then :; else
	echo "decomp.sh: refusing ${m/$'\t'/: } (clean-room rule)" >&2; exit 3
fi

SHA=$(sha256sum "$BIN" | cut -d' ' -f1)
PROJ=$CACHE/$SHA
NAME=$(basename "$BIN")
mkdir -p "$PROJ"
OUT=$(mktemp -u); LOG=$(mktemp)
trap 'rm -f "$OUT" "$LOG"' EXIT
exec 9>"$PROJ.lock"
flock 9
if [ ! -e "$PROJ/done" ]; then
	echo "decomp.sh: first use, analyzing $NAME (log $PROJ/analysis.log)..." >&2
	rm -rf "$PROJ"/p.*
	"$GHIDRA/support/analyzeHeadless" "$PROJ" p -import "$BIN" -max-cpu 16 \
		>"$PROJ/analysis.log" 2>&1 || { tail -20 "$PROJ/analysis.log" >&2; exit 1; }
	touch "$PROJ/done"
fi
"$GHIDRA/support/analyzeHeadless" "$PROJ" p -process "$NAME" -noanalysis -readOnly \
	-scriptPath "$ROOT/tools" -postScript Decomp.java "$OUT" "$@" >"$LOG" 2>&1 || true
if grep -q 'SCRIPT ERROR' "$LOG" || [ ! -e "$OUT" ]; then
	grep -m1 ERROR "$LOG" >&2; exit 1
fi
cat "$OUT"
