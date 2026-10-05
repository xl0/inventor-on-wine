#!/bin/bash
# minloop.sh N [xproc]: owned.exe: N x (min, restore) and N x (sysmin, sysrestore); after each restore the owner must
# not be iconic and every owned window visible again. Prints the failures per kind (175: A/B of driver builds).
. "$(dirname "$0")/env.sh"
N=${1:-5}
probe 600; sleep 7
[ "$2" = xproc ] && { cmd late xproc; sleep 2; }
chk() { cmd report; awk '/^report cmd/ {s = ""} /^  [a-z]+ +[0-9A-F]+ visible/ && $1 != "noact" && $1 != "plain" && ($4 != 1 || $6 != 0) {s = s sprintf("%s(vis %s iconic %s) ", $1, $4, $6)} END {printf "%s", s}' $S/owned.log; }
for m in min:restore sysmin:sysrestore; do
  fails=0
  for i in $(seq $N); do
    cmd ${m%:*}; sleep 1.5; cmd ${m#*:}; sleep 1.5; r=$(chk)
    [ "$r" ] && { fails=$((fails + 1)); echo "  ${m#*:} $i: $r"; cmd restore; sleep 1.5; r=$(chk); [ "$r" ] && { echo "  not recovered: $r"; break; }; }
  done
  echo "${m}: $fails of $N restores incomplete"
done
cmd quit; sleep 1
