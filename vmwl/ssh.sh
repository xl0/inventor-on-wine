#!/usr/bin/env bash
# ssh into the Wayland VM as xl0. Usage: vmwl/ssh.sh [ssh args / command...]
exec ssh -i "$(dirname "$(realpath "$0")")/id_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none -o LogLevel=ERROR -p 2223 xl0@127.0.0.1 "$@"
