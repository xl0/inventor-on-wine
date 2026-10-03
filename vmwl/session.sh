#!/usr/bin/env bash
# Start a Wayland compositor on the VM's display as user xl0. Usage: vmwl/session.sh gnome|kde|sway|stop
# tty1 autologins xl0 and execs the compositor named in ~/.wl-session (see provision2.sh); the guest log is ~/.wl-session.log.
# Client env (vmwl/wl.sh sets it): XDG_RUNTIME_DIR=/run/user/1000, WAYLAND_DISPLAY = the wayland-N socket there.
set -euo pipefail
cd "$(dirname "$0")"
case "${1:-}" in
  gnome|kde|sway) ./ssh.sh "echo $1 > ~/.wl-session; sudo systemctl stop getty@tty1; rm -f /run/user/1000/wayland-*; sudo systemctl start getty@tty1" ;;
  stop) ./ssh.sh "rm -f ~/.wl-session; sudo systemctl stop getty@tty1; rm -f /run/user/1000/wayland-*; sudo systemctl start getty@tty1" ;;
  *) sed -n 2p "$0"; exit 1 ;;
esac
if [ "$1" != stop ]; then
  for _ in $(seq 60); do ./ssh.sh 'ls /run/user/1000/wayland-? 2>/dev/null' >/dev/null && break; sleep 1; done
  ./ssh.sh 'ls /run/user/1000/wayland-? 2>/dev/null' >/dev/null || { echo "session.sh: no wayland socket after 60 s; see ~/.wl-session.log in the guest" >&2; exit 1; }
  [ "$1" = gnome ] && { sleep 8; ./input.py key esc; }  # the shell starts in the overview
fi
exit 0
