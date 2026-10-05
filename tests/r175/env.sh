# source: environment for the 175 probes. D (display, default :100), B (build, default wt/175-build)
W=/home/xl0/projects/wine
eval "$($W/tools/sysroot.sh env)"
export DISPLAY=${D:-:100} WINEPREFIX=$W/wt/175-prefix WINEDEBUG=${WINEDEBUG:--all}
[ "$DISPLAY" = :100 ] && export DRI_PRIME=pci-0000_16_00_0
B=$W/${B:-wt/175-build}; S=$W/inst/175; T=$W/tests/r175
probe() { # probe SECS: start owned.exe in $S (its r175.cmd lives there), log $S/owned.log
  (cd $S && setsid nohup $B/wine $T/owned.exe ${1:-120} > $S/owned.log 2> $S/owned.err < /dev/null &); }
cmd() { printf '%s\n' "$@" > $S/r175.cmd; sleep 1.2; }
