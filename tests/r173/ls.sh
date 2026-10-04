#!/bin/bash
# ls.sh BUILD DISPLAYNUM TAG SEEDS...: lockstress 15000 SEED nogl per seed under the watchdog (400 s), one summary line each
cd /home/xl0/projects/wine
b=$1; d=$2; tag=$3; shift 3
for s in "$@"; do
  r=$(inst/173/run.sh $b $d $tag-$b-$d-s$s 400 tests/r157/lockstress.exe 15000 $s nogl 2>&1 | grep -v Killed)
  op=$(grep -a 'Major opcode' inst/173/out/$tag-$b-$d-s$s.out | head -1 | sed 's/.*: *//')
  echo "$r${op:+ [$op]}"
done
