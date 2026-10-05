#!/bin/bash
# ut.sh: the brief's conformance units on wt/177-build, both arches (tools/regress.sh unit); results in out/ut.txt, logs in out/ut-*/
cd /home/xl0/projects/wine
: > inst/177/out/ut.txt
for u in gdi32:dc gdi32:bitmap gdi32:clipping user32:win user32:msg user32:input user32:cursoricon user32:dce user32:clipboard opengl32:opengl; do
  d=inst/177/out/ut-${u/:/-}
  tools/regress.sh unit $u -b wt/177-build -o $d > $d.log 2>&1
  grep -h " $u " $d/results.txt >> inst/177/out/ut.txt
done
echo ALLDONE >> inst/177/out/ut.txt
