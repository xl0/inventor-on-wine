# env.sh: source it. Sets B (build dir) / WINEPREFIX for a build tag (fix | base | master) on a display (181).
# Usage: . tests/r181/env.sh TAG [DISPLAYNUM=98] ; then `w PROG ARGS` runs it under that build.
W=/home/xl0/projects/wine
eval "$($W/tools/sysroot.sh env)"
case $1 in
fix) B=$W/wt/181-build;; base) B=$W/build;; master) B=$W/wt/regress-master-build-h26;;
*) echo "env.sh: tag fix|base|master" >&2; return 1;;
esac
TAG=$1
export WINEPREFIX=$W/inst/181/pfx-$1 DISPLAY=:${2:-98} WINEDEBUG=${WINEDEBUG:--all} WINEBUILD=$B
if [ "$DISPLAY" = :98 ]; then export DRI_PRIME=pci-0000_ca_00_0; fi
w() { "$B/wine" "$@"; }
mkpfx() { [ -e "$WINEPREFIX/system.reg" ] || WINEDLLOVERRIDES="mscoree,mshtml=" "$B/wine" wineboot -u >$W/inst/181/wineboot-$TAG.log 2>&1; "$B/server/wineserver" -w; }
