#!/bin/bash
# run.sh TAG RENDERER MODE [ARGS..]: one gpuchild.exe run under build TAG (fix|base|master, see env.sh)
# with wined3d renderer vulkan|gl on display ${DISP:-98}; the pointer is parked in the bottom right corner (181).
T=$1 R=$2; shift 2
. "$(dirname "$0")/env.sh" $T ${DISP:-98} || exit 2
xdotool mousemove 1900 1060
WINE_D3D_CONFIG=renderer=$R timeout ${TIMEOUT:-120} "$B/wine" "$W/tests/r181/gpuchild.exe" "$@" 2>>$W/inst/181/r/stderr-$T.log </dev/null
