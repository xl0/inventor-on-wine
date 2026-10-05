#!/bin/bash
# p173.sh BUILD DISPLAYNUM: the 173 probes on one display
cd /home/xl0/projects/wine
b=$1; d=$2
inst/177/batch.sh $b $d iconlock 5 40 tests/r173/iconlock.exe 8 icon
inst/177/batch.sh $b $d vrace 5 50 tests/r173/visual_race.exe 10
inst/177/batch.sh $b $d vracetext 5 50 tests/r173/visual_race.exe 10 text
inst/177/batch.sh $b $d flushpost 2 60 tests/r173/flushpost.exe 15
inst/177/batch.sh $b $d uistress 3 90 inst/173-review/uistress.exe 30
inst/177/batch.sh $b $d iconrace-draw 2 60 inst/173-review/iconrace.exe draw 6 4
inst/177/batch.sh $b $d iconrace-wdraw 2 60 inst/173-review/iconrace.exe wdraw 6 4
inst/177/batch.sh $b $d iconrace-cursor 2 60 inst/173-review/iconrace.exe cursor 5
for s in ${SEEDS:-1 2 3}; do inst/177/batch.sh $b $d lockstress-s$s 1 400 inst/173/lockstress.exe 15000 $s nogl; done
