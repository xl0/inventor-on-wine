#!/bin/sh
# stubs.sh PID: stub managers / proxies per combase apartment of a Wine process
P=$1; D=/home/xl0/projects/wine/build/dlls/combase/x86_64-windows/combase.dll
B=$(awk -v d=$D '$6 == d { split($1, r, "-"); print r[1]; exit }' /proc/$P/maps)
gdb -q -batch -p $P -ex "add-symbol-file $D -o $(printf '0x%x' $((0x$B - 0x180000000)))" -ex "source $(dirname $0)/stubs.py" 2>&1 | grep '^apt'
