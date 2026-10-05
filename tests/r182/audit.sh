#!/bin/bash
# audit.sh DISPLAYNUM DLL:TEST...: run conformance units on the 182 debug build (lock order + XID audit), logs in out/audit-DLL-TEST.out
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
d=$1; shift
for u in "$@"; do
  dll=${u%%:*}; t=${u##*:}
  exe=$PWD/wt/182-dbg-build/dlls/$dll/tests/x86_64-windows/${dll}_test.exe
  r=$(WINEDEBUG=err+all WINETEST_INTERACTIVE=0 inst/182/run.sh dbg $d audit-$dll-$t 600 $exe $t 2>&1 | grep -v Killed)
  f=inst/182/out/audit-$dll-$t.out
  echo "$u: $(grep -a -o "[0-9]* tests executed.*" $f | tail -1 | tr -d '\r') | unlocked gdi/foreign allocs: $(grep -a -c 'XIDALLOC pid' $f) | X errors: $(grep -a -c 'XERROR pid' $f) | $(echo $r | grep -o 'HANG.*')"
done
