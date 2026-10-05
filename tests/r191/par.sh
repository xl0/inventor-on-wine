#!/bin/bash
# par.sh TAG RUNS TIMEOUT "DISPLAYS" EXE ARGS...: the probe alternately on base and fix (BUILDS overrides), RUNS times
# per build on each display of DISPLAYS in parallel; hang dumps classified at the end
cd /home/xl0/projects/wine
tag=$1; runs=$2; to=$3; disps=$4; shift 4
for d in $disps; do
  ( for i in $(seq 1 $runs); do for b in ${BUILDS:-base fix}; do
      taskset -c 20-59,80-119 inst/191/run.sh $b $d par-$tag-$b-$d-r$i $to "$@" 2>&1 | grep -v "Killed\|debuginfod"
    done; done ) > inst/191/out/par-$tag-$d.log 2>&1 &
done
wait
for b in ${BUILDS:-base fix}; do
  n=$(cat $(for d in $disps; do echo inst/191/out/par-$tag-$d.log; done) | grep -c "^par-$tag-$b-")
  h=$(cat $(for d in $disps; do echo inst/191/out/par-$tag-$d.log; done) | grep "^par-$tag-$b-" | grep -c HANG)
  o=$(cat $(for d in $disps; do echo inst/191/out/par-$tag-$d.log; done) | grep "^par-$tag-$b-" | grep -vc "HANG\|DONE")
  f=$(cat inst/191/out/par-$tag-$b-*.out 2>/dev/null | grep -ac "handle_syscall_fault code")
  echo "RESULT $tag $b: $n runs: hang $h, other $o; swallowed faults $f; $(python3 inst/182/classify.py inst/191/out/par-$tag-$b-*-hang.txt 2>/dev/null | tail -1)"
done
