#!/bin/bash
# xicrace-ui.sh WINE DISPLAYNUM PREFIX OUT: keyboard input around X window recreation (191), driven with xdotool.
# Runs "xicrace.exe ui" (windows A, B: own edit; C: edit of another thread) and types into the edits before and after
# the X window was recreated by another thread / by the owner, with dead keys and compose sequences (us intl layout,
# compose key = Menu) and a Cyrillic layout, and across focus changes. Prints one "text ..." line per step; compare
# the output of two builds with diff. XMODIFIERS is passed through (@im=none: Xlib's local input method).
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
W=$1; d=$2; export WINEPREFIX=$3; out=$4
unset WAYLAND_DISPLAY; export DISPLAY=:$d
cmds=$out.cmds; : > $cmds; : > $out.log
setxkbmap -layout us -variant intl -option compose:menu
WINEDEBUG=${WINEDEBUG:-err+all} setsid nohup $W tests/r191/xicrace.exe ui "$(realpath $cmds)" > $out.log 2>&1 </dev/null &
for i in $(seq 600); do grep -aq "^ready" $out.log && break; sleep 0.2; done; sleep 1
lines() { grep -ac "^ok\|^text" $out.log; }
cmd() {   # send a command to the probe, wait for its answer
  local n=$(lines); echo "$*" >> $cmds
  for i in $(seq 100); do [ "$(lines)" -gt "$n" ] && break; sleep 0.1; done
}
xfocus() {   # give the X focus to window A / B / C (its X window may be new) and let the probe focus its edit
  cmd focus $1; sleep 0.4
  local w=$(xdotool search --onlyvisible --name "^xicrace $1\$" | tail -n 1)
  [ -n "$w" ] && xdotool windowactivate --sync $w 2>/dev/null; sleep 0.3
  cmd focus $1; sleep 0.3
}
step() { cmd text; echo "$1: $(grep -a '^text' $out.log | tail -n 1)"; }
keys() { xdotool key --delay 40 "$@"; sleep 0.3; }

xfocus A; xdotool type --delay 40 "abc"; sleep 0.3;            step "A plain"
keys dead_acute e Multi_key o c;                               step "A dead_acute e, compose o c"
cmd other A; sleep 0.6; xfocus A
xdotool type --delay 40 "def"; sleep 0.3;                      step "A after recreation by another thread"
keys dead_acute a Multi_key o c dead_grave e;                            step "A dead keys + compose after it"
cmd other A; sleep 0.6; xfocus A
keys x dead_acute i;                                           step "A after a second one"
cmd own A; sleep 0.6; xfocus A
keys y dead_diaeresis u;                                             step "A after recreation by the owner"
cmd clear
xfocus B; keys b 1 dead_acute o;                               step "B focused"
xfocus A; keys a 1;                                            step "A focused again"
xfocus B; cmd other A; sleep 0.6
keys b 2;                                                      step "B typed while A is recreated"
xfocus A; keys a 2 dead_acute u;                               step "A focused after its recreation in the background"
cmd clear
xfocus C; keys c 1 dead_acute e Multi_key o c;                      step "C (edit of another thread)"
cmd other C; sleep 0.6; xfocus C
keys c 2 dead_acute a;                                         step "C after recreation by another thread"
setxkbmap -layout ru; sleep 0.3
keys Cyrillic_ef Cyrillic_yeru;                                step "C Cyrillic layout"
xfocus A; keys Cyrillic_ve Cyrillic_a;                         step "A Cyrillic layout"
cmd other A; sleep 0.6; xfocus A
keys Cyrillic_be;                                              step "A Cyrillic after recreation"
setxkbmap -layout us
cmd quit; sleep 1
grep -a "err:\|fixme:.*xim\|X Error\|GUARDMALLOC\|X191" $out.log | grep -v create_logical_proc_info | head -5
