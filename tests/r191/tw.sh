#!/bin/bash
# tw.sh DISPLAYNUM TAG RUNS [THREADS]: thrwin.exe (190) alternately on base and fix, one process per run; summary per build.
# Pass XCOMPOSEFILE=/dev/null to make XOpenIM short (overlap likely), XMODIFIERS=@im=... for another input method.
cd /home/xl0/projects/wine
d=$1; tag=$2; runs=$3; n=${4:-16}
for i in $(seq 1 $runs); do for b in ${BUILDS:-base fix}; do
  inst/191/run.sh $b $d tw-$tag-$b-$d-r$i 20 tests/r191/thrwin.exe $n 2>&1 | grep -v "Killed\|debuginfod"
done; done > inst/191/out/tw-$tag-$d.log 2>&1
for b in ${BUILDS:-base fix}; do
  l=$(grep "^tw-$tag-$b-" inst/191/out/tw-$tag-$d.log)
  echo "RESULT thrwin $tag $b :$d threads $n: $(echo "$l" | wc -l) runs: done $(echo "$l" | grep -c ": DONE"), hang $(echo "$l" | grep -c HANG), died $(echo "$l" | grep -c "without DONE"); glibc aborts $(cat inst/191/out/tw-$tag-$b-$d-r*.out | grep -ac 'double free\|realloc()\|malloc()\|free()\|corrupted'), XIM open failures $(cat inst/191/out/tw-$tag-$b-$d-r*.out | grep -ac 'Could not open input method')"
done
