#!/bin/bash
# rv3.sh DISPLAYNUM REPS TAG: rv182 bench 6 1 rop on base, fix (pre-sync) and fix with X182_NOPRESYNC=1 (experiment build), rotating order
cd /home/xl0/projects/wine
d=$1; tag=$3
for ((r = 1; r <= $2; r++)); do
  case $((r % 3)) in 0) o="base pre nopre";; 1) o="pre nopre base";; 2) o="nopre base pre";; esac
  for v in $o; do
    case $v in base) b=base; e=;; pre) b=fix; e=;; nopre) b=fix; e=1;; esac
    res=$(X182_NOPRESYNC=$e PIN=20-39 inst/182/run.sh $b $d rv3-$tag-$v-$r 90 inst/182-review/rv182.exe bench 6 1 rop)
    echo "$v ${res#*: }"
  done
done
