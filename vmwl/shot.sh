#!/usr/bin/env bash
# Screenshot the Wayland VM display. Usage: vmwl/shot.sh [out.png]
VM_DIR=$(dirname "$(realpath "$0")") exec "$(dirname "$0")/../vm/shot.sh" "$@"
