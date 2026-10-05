#!/bin/bash
# mine.sh [kill]: processes whose WINEPREFIX is under inst/191 (pid, exe, prefix, command); "kill" kills them by PID
for d in /proc/[0-9]*; do
  p=${d#/proc/}
  e=$( (tr '\0' '\n' < $d/environ | grep '^WINEPREFIX=.*inst/191/') 2>/dev/null) || continue
  echo "$p $(readlink $d/exe 2>/dev/null | sed 's#.*/wine/##') ${e#WINEPREFIX=/home/xl0/projects/wine/} $(tr '\0' ' ' < $d/cmdline 2>/dev/null | cut -c1-50)"
  [ "$1" = kill ] && kill -9 $p
done
