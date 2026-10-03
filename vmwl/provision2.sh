#!/bin/bash
# Guest-side session plumbing, run as root after provision.sh. tty1 autologs in xl0; its login shell execs the compositor
# named in ~xl0/.wl-session (gnome|kde|sway). vmwl/session.sh writes that file and restarts getty@tty1.
set -euxo pipefail
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/override.conf <<'EOT'
[Service]
Environment=XDG_SESSION_TYPE=wayland  # mutter needs a wayland-typed logind session
ExecStart=
ExecStart=-/sbin/agetty --autologin xl0 --noclear %I $TERM
EOT
systemctl set-default multi-user.target
cat > /home/xl0/.bash_profile <<'EOT'
[ -f ~/.bashrc ] && . ~/.bashrc
if [ "$(tty)" = /dev/tty1 ] && [ -f ~/.wl-session ] && [ -z "$WL_STARTED" ]; then
  export WL_STARTED=1  # gnome-session re-execs a login shell
  export WLR_RENDERER_ALLOW_SOFTWARE=1
  exec >~/.wl-session.log 2>&1
  case $(cat ~/.wl-session) in
    gnome) systemctl --user import-environment XDG_SESSION_ID XDG_SEAT XDG_VTNR; export XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_DESKTOP=gnome; exec gnome-session --session=gnome ;;
    kde)   export XDG_CURRENT_DESKTOP=KDE XDG_SESSION_DESKTOP=KDE; exec startplasma-wayland ;;
    sway)  exec sway ;;
  esac
fi
EOT
chown xl0:xl0 /home/xl0/.bash_profile
