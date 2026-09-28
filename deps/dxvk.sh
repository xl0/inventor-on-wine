#!/usr/bin/env bash
# Fetch (pinned, sha256-verified) DXVK + vkd3d-proton and install into a prefix
# as native DLL overrides. Usage: deps/dxvk.sh PREFIX
set -euo pipefail
prefix=$(realpath "${1:?usage: $0 PREFIX}")
export WINEPREFIX=$prefix
cd "$(dirname "$0")"
root=$(realpath ..)

fetch() { # url sha256 file
  [ -f "$3" ] || curl -fL -o "$3" "$1"
  echo "$2  $3" | sha256sum -c --quiet
}
fetch https://github.com/doitsujin/dxvk/releases/download/v3.1.1/dxvk-3.1.1.tar.gz \
  40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608 dxvk-3.1.1.tar.gz
fetch https://github.com/HansKristian-Work/vkd3d-proton/releases/download/v3.0.1/vkd3d-proton-3.0.1.tar.zst \
  3cf2315522af5e43605ef6d3c41dad91387040bf97199934f3f7ab76caaa2f0c vkd3d-proton-3.0.1.tar.zst
[ -d dxvk-3.1.1 ] || tar xzf dxvk-3.1.1.tar.gz
[ -d vkd3d-proton-3.0.1 ] || tar --zstd -xf vkd3d-proton-3.0.1.tar.zst

sys=$prefix/drive_c/windows
for d in dxvk-3.1.1 vkd3d-proton-3.0.1; do
  cp $d/x64/*.dll "$sys/system32/"
  cp $d/x86/*.dll "$sys/syswow64/" 2>/dev/null || cp $d/x32/*.dll "$sys/syswow64/"
done

for dll in $(ls dxvk-3.1.1/x64 vkd3d-proton-3.0.1/x64 | grep '\.dll$' | sed 's/\.dll$//'); do
  "$root/build/wine" reg add 'HKCU\Software\Wine\DllOverrides' \
    /v "$dll" /d native /f >/dev/null
done
"$root/build/server/wineserver" -w
echo "installed into $prefix"
