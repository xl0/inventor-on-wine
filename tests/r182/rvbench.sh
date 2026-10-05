#!/bin/bash
# rvbench.sh DISPLAYNUM REPS: the reviewer's bystander-latency bench (inst/182-review/rv182.exe bench 6 1 rop: one thread
# of ROP blits on the screen DC + a 1 ms rubber band thread), base (build-next) and fix alternating on one display
cd /home/xl0/projects/wine
d=$1
for ((r = 1; r <= $2; r++)); do
  if ((r % 2)); then o="base fix"; else o="fix base"; fi
  for b in $o; do PIN=20-39 inst/182/run.sh $b $d rv-$b-rop1-$r 90 inst/182-review/rv182.exe bench 6 1 rop; done
done
