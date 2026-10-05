#!/bin/bash
# dbg.sh DISPLAYNUM: probes on the debug build (lock order pairs, X errors with backtraces, XID allocation audit)
cd /home/xl0/projects/wine
d=$1
inst/182/batch.sh dbg $d d-dcchurn 1 60 inst/173-review/iconrace.exe dcchurn 8 4
inst/182/batch.sh dbg $d d-gs 2 90 tests/r182/gdistress.exe 20 6
inst/182/batch.sh dbg $d d-gsx 1 90 tests/r182/gdistress.exe 15 6 3 x
inst/182/batch.sh dbg $d d-iconlock 1 40 tests/r173/iconlock.exe 8 icon
inst/182/batch.sh dbg $d d-vrace 1 50 tests/r173/visual_race.exe 10
inst/182/batch.sh dbg $d d-vracetext 1 50 tests/r173/visual_race.exe 10 text
inst/182/batch.sh dbg $d d-flushpost 1 60 tests/r173/flushpost.exe 15
inst/182/batch.sh dbg $d d-uistress 1 90 inst/173-review/uistress.exe 30
inst/182/batch.sh dbg $d d-wdraw 1 60 inst/173-review/iconrace.exe wdraw 6 4
inst/182/batch.sh dbg $d d-cursor 1 60 inst/173-review/iconrace.exe cursor 5
inst/182/batch.sh dbg $d d-vstate 1 200 tests/r184/vstate.exe loop 30
inst/182/batch.sh dbg $d d-ls1 1 400 inst/173/lockstress.exe 3000 1 nogl
inst/182/batch.sh dbg $d d-ls2 1 400 inst/173/lockstress.exe 3000 2 nogl
inst/182/batch.sh dbg $d d-lsgl 1 400 inst/173/lockstress.exe 600 3
