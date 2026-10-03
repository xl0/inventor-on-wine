#!/usr/bin/env bash
# Screenshot the VM display to a PNG. Usage: vm/shot.sh [out.png]
set -euo pipefail
out=$(realpath -m "${1:-/tmp/vm.png}")
cd "${VM_DIR:-$(dirname "$0")}"  # VM_DIR: another VM's dir (vmwl/)
python3 - "$out" <<'EOF'
import socket, sys, time, os
from PIL import Image
ppm = sys.argv[1] + '.ppm'
s = socket.socket(socket.AF_UNIX); s.connect('mon.sock'); s.recv(4096)
s.send(f'screendump {ppm}\n'.encode())
while not os.path.exists(ppm): time.sleep(0.1)
time.sleep(0.3)
Image.open(ppm).save(sys.argv[1]); os.remove(ppm)
EOF
echo "$out"
