#!/bin/bash
# Build prefixes/inv-vm: a fresh copy of prefixes/inv-net48 (pristine, never
# modified) + the Inventor 2027.1 install transplanted from the Windows VM.
# Stopgap for testing Inventor while the Wine install path is fixed; bugs
# seen only here may be transplant artifacts.
#
#   tools/transplant.sh footprint  boot a copy of the VM's `base` snapshot (pre-Inventor)
#                                  beside the running VM (ssh 2223, vnc :7), dump file
#                                  lists + registry on both, delete the copy (~30 min)
#   tools/transplant.sh fetch      copy new/changed Autodesk/third-party files and the
#                                  MS redists Autodesk shipped from the running VM
#   tools/transplant.sh build      recreate prefixes/inv-vm (default: all three steps)
#
# Rules: everything under Autodesk/FlexNet dirs is copied as shipped, including
# app-local Microsoft DLLs. System-wide Microsoft pieces (VC++ 14.50, .NET 10
# desktop/aspnetcore) are installed by running the same hash-pinned redist
# installers Autodesk ran; nothing is copied from C:\Windows. WebView2 (part of
# Windows 11, needed by AdskLicensingAgent's sign-in UI) comes from Microsoft's
# pinned standalone installer in deps/ (tools/edge.sh), same version as the VM; so does Edge (the default
# browser, where the Autodesk sign-in form opens). HKLM/HKCU Run
# entries are not imported (Autodesk Access would autostart on every boot).
# Data: inst/transplant/ (gitignored): base/ cur/ dumps, files/ (C:\ mirror),
# redist/, delta.reg, manifest.txt.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/inst/transplant
PFX=$ROOT/prefixes/inv-vm
SSH=(ssh -i "$ROOT/vm/id_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
     -o BatchMode=yes -o LogLevel=ERROR -o ConnectTimeout=10)
vm() { "${SSH[@]}" -p "$1" dev@127.0.0.1 "${@:2}"; }
mkdir -p "$T"

footprint() {
	cat >"$T/footprint.ps1" <<'EOF'
# File list (path, size, mtime, sha256) and registry exports -> C:\t\fp.
$o = 'C:\t\fp'; New-Item -Force -ItemType Directory $o | Out-Null
$sha = [System.Security.Cryptography.SHA256]::Create()
$w = New-Object System.IO.StreamWriter("$o\files.tsv", $false, (New-Object System.Text.UTF8Encoding $false))
$err = New-Object System.IO.StreamWriter("$o\errors.txt")
function Walk($d) {
  try { $fs = (New-Object System.IO.DirectoryInfo $d).GetFileSystemInfos() } catch { $err.WriteLine("$d`t$_"); return }
  foreach ($f in $fs) {
    if ($f.Attributes -band [IO.FileAttributes]::ReparsePoint) { $err.WriteLine("$($f.FullName)`treparse"); continue }
    if ($f -is [IO.DirectoryInfo]) { Walk $f.FullName; continue }
    $h = '?'
    try { $s = [IO.File]::Open($f.FullName, 'Open', 'Read', 'ReadWrite,Delete'); try { $h = [BitConverter]::ToString($sha.ComputeHash($s)).Replace('-', '') } finally { $s.Close() } } catch {}
    $w.WriteLine((($f.FullName, $f.Length, $f.LastWriteTimeUtc.ToString('s'), $h) -join "`t"))
  }
}
foreach ($r in 'C:\Program Files', 'C:\Program Files (x86)', 'C:\ProgramData', 'C:\Windows', 'C:\Users') { Walk $r }
$w.Close(); $err.Close()
reg export HKLM\SOFTWARE "$o\hklm_software.reg" /y | Out-Null
reg export HKLM\SYSTEM\CurrentControlSet\Services "$o\services.reg" /y | Out-Null
reg export HKCU\Software "$o\hkcu_software.reg" /y | Out-Null
'done'
EOF
	# Second VM from the base snapshot: internal snapshot clusters are immutable,
	# so a -U read of the live qcow2 is consistent. Own vars.fd/tpm copies.
	local B=$T/basevm
	rm -rf "$B"; mkdir -p "$B"
	qemu-img convert -U -l snapshot.name=base -O qcow2 "$ROOT/vm/win.qcow2" "$B/base.qcow2"
	cp "$ROOT/vm/snap/base/vars.fd" "$B/"; cp -r "$ROOT/vm/snap/base/tpm" "$B/"
	swtpm socket --tpm2 --tpmstate dir="$B/tpm" --ctrl type=unixio,path="$B/tpm/sock" --daemon --terminate
	qemu-system-x86_64 -name winbase -machine q35,accel=kvm,smm=on \
		-global driver=cfi.pflash01,property=secure,value=on \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.ms.fd \
		-drive if=pflash,format=raw,file="$B/vars.fd" \
		-cpu host,hv_relaxed,hv_vapic,hv_spinlocks=0x1fff,hv_time,hv_vpindex,hv_synic,hv_stimer \
		-smp 8 -m 16G \
		-chardev socket,id=tpm,path="$B/tpm/sock" -tpmdev emulator,id=tpm0,chardev=tpm -device tpm-crb,tpmdev=tpm0 \
		-drive file="$B/base.qcow2",if=none,id=disk0,cache=unsafe -device nvme,drive=disk0,serial=winref0,bootindex=1 \
		-netdev user,id=net0,hostfwd=tcp:127.0.0.1:2223-:22 -device e1000e,netdev=net0 \
		-device qemu-xhci -device usb-tablet -vga std -vnc 127.0.0.1:7 \
		-daemonize -pidfile "$B/qemu.pid"
	# The base VM may install pending Windows updates and reboot mid-run: retry.
	local p
	for p in 2222 2223; do (
		until vm $p 'New-Item -Force -ItemType Directory C:\t | Out-Null' 2>/dev/null; do sleep 15; done
		scp -q "${SSH[@]:1}" -P $p "$T/footprint.ps1" dev@127.0.0.1:C:/t/footprint.ps1
		until vm $p 'powershell -ExecutionPolicy Bypass -File C:\t\footprint.ps1' 2>&1 | grep -q '^done'; do
			sleep 60; until vm $p 'hostname' >/dev/null 2>&1; do sleep 15; done
		done
		local d=cur; [ $p = 2223 ] && d=base
		mkdir -p "$T/$d"; scp -q "${SSH[@]:1}" -P $p 'dev@127.0.0.1:C:/t/fp/*' "$T/$d/"
	) & done
	wait
	kill "$(cat "$B/qemu.pid")"; sleep 5; rm -rf "$B"
}

# MS redistributables Autodesk ran (from its 3rdParty payload); sha256 pinned.
# The .NET ones match Microsoft's releases.json sha512 for 10.0.9.
REDIST=(
	"x86/VCRedist/2022/VC_redist.x86.exe e7267c1bdf9237c0b4a28cf027c382b97aa909934f84f1c92d3fb9f04173b33e /install /quiet /norestart"
	"x64/VCRedist/2022/VC_redist.x64.exe 8995548dfffcde7c49987029c764355612ba6850ee09a7b6f0fddc85bdc5c280 /install /quiet /norestart"
	"x64/dotNet/100/windowsdesktop-runtime-10.0.9-win-x64.exe d4eb932ad12ec61cb5293348021dbaa43fa373bc19ed1876e9dbf8cb38633ad3 /install /quiet /norestart"
	"x64/aspNetCore/100/aspnetcore-runtime-10.0.9-win-x64.exe e81a9577f1839f83bda527882277f228e865968264ddc0fe4fea51ff1c0d7fa6 /install /quiet /norestart"
)

fetch() {
	# Transplant set: new or changed (sha256) files under Autodesk/FlexNet roots.
	python3 - "$T" <<'EOF'
import re, sys
t = sys.argv[1]
inc = re.compile(r'^C:\\(Program Files\\Autodesk\\|Program Files\\Common Files\\(Autodesk|Macrovision)|'
                 r'Program Files \(x86\)\\Common Files\\Autodesk|ProgramData\\Autodesk\\|ProgramData\\FLEXnet|'
                 r'Users\\Public\\Documents\\Autodesk\\|Users\\dev\\AppData\\(Roaming|Local|LocalLow)\\Autodesk\\)', re.I)
# Transient per-machine state: sign-in tokens, crash reports, pending telemetry.
exc = re.compile(r'\\Identity Services\\|\\AppData\\Local\\Autodesk\\CER\\|\\ADPSDK\\JSON\\Upload\\', re.I)
def load(p):
    return {l.split('\t')[0].lower(): l for l in open(p, encoding='utf-8-sig')}
b, c = load(f'{t}/base/files.tsv'), load(f'{t}/cur/files.tsv')
sel = [l for k, l in c.items() if inc.match(k) and not exc.search(k)
       and (k not in b or b[k].split('\t')[3] != l.split('\t')[3])]
open(f'{t}/files.tsv', 'w').writelines(sel)
open(f'{t}/files.lst', 'w').writelines(l.split('\t')[0][3:].replace('\\', '/') + '\n' for l in sel)
EOF
	scp -q "${SSH[@]:1}" -P 2222 "$T/files.lst" dev@127.0.0.1:C:/t/transplant.lst
	rm -rf "$T/files"; mkdir -p "$T/files" "$T/redist"
	vm 2222 'tar.exe -cf - -C C:/ -T C:/t/transplant.lst' | tar -C "$T/files" -xf - 2>&1 | grep -v SCHILY || true
	[ "$(find "$T/files" -type f | wc -l)" = "$(wc -l <"$T/files.lst")" ] || { echo "fetch: file count mismatch" >&2; exit 1; }
	# Junctions (the footprint skips reparse points): "link<TAB>target" as C:\ paths.
	local ps
	ps=$(grep -iP '^C:\\(Program Files[^\\]*|ProgramData|Users)\\.*(Autodesk|Macrovision|FLEXnet).*\treparse' "$T/cur/errors.txt" |
		cut -f1 | sed "s/.*/'&'/" | paste -sd,)
	vm 2222 "foreach (\$p in $ps) { \"\$p\`t\$((Get-Item \$p).Target)\" }" | tr -d '\r' >"$T/links.tsv"
	# Autodesk's bundle dir, named after the Inventor 2027 bundle product code.
	vm 2222 'tar.exe -cf - -C "C:/Users/dev/AppData/Local/Temp/{447AF3F0-5A11-3D54-B8C1-9D1C88C26148}" 3rdParty' |
		tar -C "$T/redist" -xf -
}

build() {
	local r f sum args
	for r in "${REDIST[@]}"; do
		read -r f sum args <<<"$r"
		echo "$sum  $T/redist/3rdParty/$f" | sha256sum -c --quiet
	done
	local wineserver=$ROOT/build/server/wineserver
	export WINEPREFIX=$PFX WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
	[ -d "$PFX" ] && { WINEPREFIX=$PFX $wineserver -k 2>/dev/null || true; rm -rf "$PFX"; }
	cp -a "$ROOT/prefixes/inv-net48" "$PFX"
	"$ROOT/build/wine" wineboot -u
	$wineserver -w
	unset WINEDLLOVERRIDES
	for r in "${REDIST[@]}"; do
		read -r f sum args <<<"$r"
		echo "transplant: $f $args"
		# shellcheck disable=SC2086
		"$ROOT/build/wine" "$T/redist/3rdParty/$f" $args || echo "transplant: $f exited $?"
		$wineserver -w
	done
	# Files: C:\Users\dev -> the prefix user's profile.
	rsync -a --exclude '/Users/' "$T/files/" "$PFX/drive_c/"
	rsync -a "$T/files/Users/Public/" "$PFX/drive_c/users/Public/"
	rsync -a "$T/files/Users/dev/" "$PFX/drive_c/users/$USER/"
	local link target
	while IFS=$'\t' read -r link target; do
		link=$(cpath "$link"); target=$(cpath "$target")
		ln -sfnr "$PFX/drive_c/$target" "$PFX/drive_c/$link"
	done <"$T/links.tsv"
	# Registry delta (cur minus base), filtered to Autodesk/third-party, paths rewritten.
	python3 - "$T" "$USER" <<'EOF'
import re, sys
t, user = sys.argv[1:]
AD = re.compile(r'autodesk|inventor|adsk|flexnet|macrovision', re.I)
def parse(p):  # reg export -> {key: {name: raw value text}}
    keys = {}; cur = None; buf = ''
    for line in open(p, encoding='utf-16').read().split('\n'):
        line = line.rstrip('\r')
        if buf: line = buf + line.lstrip(); buf = ''
        if cur is not None and not line.startswith('[') and line.endswith('\\'):
            buf = line[:-1]; continue
        if line.startswith('[') and line.endswith(']'):
            cur = keys.setdefault(line[1:-1], {})
        elif cur is not None and line:
            m = re.match(r'^(@|"(?:[^"\\]|\\.)*")=(.*)$', line)
            if m: cur[m.group(1)] = m.group(2)
    return keys
def text(v):  # decode hex(2)/hex(7) (utf-16) for matching
    m = re.match(r'hex\((2|7)\):(.*)', v)
    return bytes(int(x, 16) for x in m.group(2).split(',')).decode('utf-16le', 'replace') if m and m.group(2) else v
def packed(g):  # MSI packed GUID
    g = g.strip('{}').replace('-', ''); r = lambda s: s[::-1]
    return (r(g[0:8]) + r(g[8:12]) + r(g[12:16]) + ''.join(r(g[i:i + 2]) for i in range(16, 32, 2))).upper()
def rewrite(v):  # C:\Users\dev -> C:\users\<user>
    m = re.match(r'hex\((2|7)\):(.*)', v)
    if m and m.group(2):
        s = bytes(int(x, 16) for x in m.group(2).split(',')).decode('utf-16le')
        s2 = re.sub(r'C:\\Users\\dev(?=\\|$|\0)', lambda _: 'C:\\users\\' + user, s, flags=re.I)
        return v if s2 == s else f'hex({m.group(1)}):' + ','.join('%02x' % c for c in s2.encode('utf-16le'))
    return re.sub(r'C:\\\\Users\\\\dev(?=\\\\|")', lambda _: 'C:\\\\users\\\\' + user, v, flags=re.I)

base, cur = {}, {}
for r in ('hklm_software', 'hkcu_software', 'services'):
    base.update(parse(f'{t}/base/{r}.reg')); cur.update(parse(f'{t}/cur/{r}.reg'))
delta = {}
for k, vals in cur.items():
    old = base.get(k)
    d = {n: v for n, v in vals.items() if old is None or old.get(n) != v}
    if old is None or d: delta[k] = d
prods = {packed(m.group(1)) for k in delta
         for m in [re.search(r'\\Uninstall\\(\{[0-9A-F-]{36}\})$', k, re.I)]
         if m and AD.search(cur[k].get('"Publisher"', ''))}
PK = re.compile('|'.join(prods))
NOISE = re.compile(r'\\Classes\\(Local Settings|PackagedCom|ActivatableClasses|Extensions|AppX[^\\]*)(\\|$)|'
    r'^HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Classes(\\|$)|\\Services\\[^\\]*\\Security$|'
    r'\\CurrentVersion\\Run$|^HKEY_[A-Z_]*\\SOFTWARE\\(WOW6432Node\\)?(Wine|WOW6432Node|dotnet)(\\|$)|'
    r'\\NGenService\\', re.I)  # Wine/WOW6432Node\test1: other workers' tests on the VM
MSRT = re.compile(r'\\dotnet\\|\\Microsoft\\Edge|EdgeUpdate|WebView2Runtime|VC_Redist|Visual C\+\+', re.I)
MSNOISE = re.compile(r'\\(AppModel|TaskCache|Windows Search|Windows Defender|Appx|bam|UserAssist|FeatureUsage|MuiCache|'
    r'Compatibility Assistant|Notifications|PushNotifications|TileProperties|UFH|RADAR|Diagnostics|Windows Error Reporting|Wosc)\\', re.I)
keep, skip = {}, []
for k, d in delta.items():
    if NOISE.search(k): skip.append(('noise', k)); continue
    if MSRT.search(k) or any(MSRT.search(text(v)) for v in d.values()): skip.append(('ms-runtime', k)); continue
    if re.search(r'\\Classes\\Installer\\|\\CurrentVersion\\Installer\\', k, re.I):  # MSI database: Autodesk products only
        dd = d if PK.search(k) else {n: v for n, v in d.items() if PK.search(n) or PK.search(v)}
        if dd: keep[k] = dd
        else: skip.append(('msi-other', k))
    elif re.match(r'HKEY_[A-Z_]*\\SOFTWARE\\(WOW6432Node\\)?Microsoft\\|HKEY_LOCAL_MACHINE\\SYSTEM\\', k, re.I):
        if (AD.search(k) or any(AD.search(text(v)) for v in cur[k].values())) and not MSNOISE.search(k): keep[k] = d
        else: skip.append(('windows', k))
    else: keep[k] = d  # Autodesk, Classes (COM), other vendors
out = ['Windows Registry Editor Version 5.00', '']
for k, d in keep.items():
    out += ['[%s]' % k] + ['%s=%s' % (n, rewrite(v)) for n, v in d.items()] + ['']
open(f'{t}/delta.reg', 'w', encoding='utf-16').write('\r\n'.join(out) + '\r\n')
open(f'{t}/delta-skipped.txt', 'w').writelines(f'{w}\t{k}\n' for w, k in skip)
print(f'registry: {len(keep)} keys imported, {len(skip)} skipped, {len(prods)} Autodesk MSI products', file=sys.stderr)
EOF
	"$ROOT/build/wine" regedit /S "$(winepath_w "$T/delta.reg")"
	$wineserver -w
	"$ROOT/tools/edge.sh"  # last: leaves MicrosoftEdgeUpdate running
	manifest
}

# C:\X\Y -> X/Y relative to drive_c, Users\dev -> the prefix user.
cpath() { sed -E "s/^C:\\\\//; s/\\\\/\\//g; s|^Users/dev/|users/$USER/|; s|^Users/Public/|users/Public/|" <<<"$1"; }
winepath_w() { echo "Z:${1//\//\\}"; }

manifest() {
	python3 - "$T" "$ROOT/tools/msbin.py" <<'EOF' >"$T/manifest.txt"
import collections, subprocess, sys
t, msbin = sys.argv[1:]
rows = [l.rstrip('\n').split('\t') for l in open(f'{t}/files.tsv')]
paths = [f'{t}/files/' + r[0][3:].replace('\\', '/') for r in rows]
ms = {}
for i in range(0, len(paths), 500):
    out = subprocess.run(['python3', msbin] + paths[i:i + 500], capture_output=True, text=True).stdout
    ms.update(l.split('\t', 1) for l in out.splitlines())
print('# inv-vm transplant manifest (tools/transplant.sh)\n')
print('## Redist-installed (Microsoft installers Autodesk ran; run under Wine)')
print('VC++ 2022 14.50.35719 x86+x64, .NET Windows Desktop Runtime 10.0.9 x64, ASP.NET Core Runtime 10.0.9 x64,')
print('WebView2 Evergreen Runtime 154.0.4258.37 x64 and Microsoft Edge 154.0.4258.37 x64 (deps/, not Autodesk')
print('redists; in-box on Windows 11)')
print('Not reproduced: files Autodesk/redists put in C:\\Windows (Installer cache, NGen images).\n')
s = collections.Counter(); n = collections.Counter()
for r in rows:
    k = '\\'.join(r[0].split('\\')[:4]); s[k] += int(r[1]); n[k] += 1
print(f'## Transplanted files: {len(rows)}, {sum(s.values())/1e9:.2f} GB')
for k, v in s.most_common(): print(f'{v/1e6:9.1f} MB {n[k]:6d}  {k}')
print(f'\n## App-local Microsoft files (kept as shipped): {len(ms)}')
for p in sorted(ms): print(p[len(t) + 7:], '|', ms[p])
print('\n## Registry: see delta.reg (import), delta-skipped.txt (not imported)')
EOF
}

case ${1:-all} in
	footprint) footprint ;; fetch) fetch ;; build) build ;;
	all) footprint; fetch; build ;;
	*) sed -n 2,22p "$0"; exit 2 ;;
esac
