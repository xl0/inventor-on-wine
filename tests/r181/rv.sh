#!/bin/bash
# rv.sh TAG vulkan|gl ARGS..: the reviewer's probe inst/181-review/rv181.exe (source next to it) under build TAG (181)
T=$1 R=$2; shift 2
. "$(dirname "$0")/env.sh" $T ${DISP:-98} || exit 2
xdotool mousemove 1900 1060
WINE_D3D_CONFIG=renderer=$R timeout ${TIMEOUT:-120} "$B/wine" "$W/inst/181-review/rv181.exe" "$@" 2>>$W/inst/181/r/stderr-$T.log </dev/null
