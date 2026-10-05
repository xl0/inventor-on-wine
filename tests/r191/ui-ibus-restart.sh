#!/bin/bash
# ui-ibus-restart.sh WINE DISPLAYNUM PREFIX OUT: xicrace ui with XMODIFIERS=@im=ibus while the XIM server is stopped
# and started again (winex11's xim_destroy / xic_destroy / xim_open callbacks), and a recreation by another thread after it
cd /home/xl0/projects/wine; eval "$(tools/sysroot.sh env)"
W=$1; d=$2; export WINEPREFIX=$3; out=$4
unset WAYLAND_DISPLAY; export DISPLAY=:$d XMODIFIERS=@im=ibus
cmds=$out.cmds; : > $cmds; : > $out.log
setxkbmap -layout us
WINEDEBUG=${WINEDEBUG:-err+all,+xim} setsid nohup $W tests/r191/xicrace.exe ui "$(realpath $cmds)" > $out.log 2>&1 </dev/null &
for i in $(seq 600); do grep -aq "^ready" $out.log && break; sleep 0.2; done; sleep 1
lines() { grep -ac "^ok\|^text" $out.log; }
cmd() { local n=$(lines); echo "$*" >> $cmds; for i in $(seq 100); do [ "$(lines)" -gt "$n" ] && break; sleep 0.1; done; }
xfocus() { cmd focus $1; sleep 0.4; local w=$(xdotool search --onlyvisible --name "^xicrace $1\$" | tail -n 1); [ -n "$w" ] && xdotool windowactivate --sync $w 2>/dev/null; sleep 0.3; cmd focus $1; sleep 0.3; }
step() { cmd text; echo "$1: $(grep -a '^text' $out.log | tail -n 1)"; }
keys() { xdotool key --delay 40 "$@"; sleep 0.3; }
xfocus A; keys a 1;                                step "with the server"
inst/191/ibus.sh stop $d; sleep 1
keys a 2;                                          step "server stopped"
xfocus B; keys b 2; xfocus A;                      step "focus changes without a server"
inst/191/ibus.sh start $d; sleep 2
xfocus A; keys a 3;                                step "server started again"
cmd other A; sleep 0.6; xfocus A; keys a 4;        step "recreation by another thread after it"
xfocus C; keys c 4;                                step "C"
cmd quit; sleep 1
echo "xim_destroy $(grep -ac 'xim_destroy' $out.log) xic_destroy $(grep -ac 'xic_destroy' $out.log) xim_open $(grep -ac 'xim_open' $out.log) created XIC $(grep -ac 'created XIC' $out.log) created NULL $(grep -ac 'created XIC (nil)' $out.log)"
grep -a "err:\|X Error\|handle_syscall_fault" $out.log | grep -v "create_logical_proc_info\|err:ole" | head -5
