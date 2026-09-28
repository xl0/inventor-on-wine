#!/usr/bin/env bash
# Run a Windows exe in the VM's interactive desktop session (autologon user dev),
# print its stdout/stderr, exit with its exit code. Usage: vm/winrun.sh X.exe [args...]
# SSH sessions live in session 0 (no desktop), so we go through a scheduled task.
#   WINRUN_ID      caller namespace (default main): task winrun-ID, dir C:\t\ID.
#                  Different IDs run concurrently; same ID is serialized.
#   WINRUN_TIMEOUT seconds (default 600); on timeout the run is killed, exit 124.
set -euo pipefail
exe=$(realpath "$1"); shift
cd "$(dirname "$0")"
id=${WINRUN_ID:-main}; tmo=${WINRUN_TIMEOUT:-600}
exec 9>"winrun-$id.lock"; flock 9
o=(-i id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
   -o BatchMode=yes -o LogLevel=ERROR)
name=$(basename "$exe")
d="C:\\t\\$id"

ssh "${o[@]}" -p 2222 dev@127.0.0.1 "New-Item -Force -ItemType Directory $d | Out-Null"
scp -q "${o[@]}" -P 2222 "$exe" "dev@127.0.0.1:C:/t/$id/$name"
# $* is passed to cmd verbatim; keep args simple (no quotes).
# The deadline lives on the Windows side, so a killed host ssh can't leave it polling.
ssh "${o[@]}" -p 2222 dev@127.0.0.1 "
\$ErrorActionPreference = 'Stop'
function Kill-Run {  # the task and anything still running from our dir
  Stop-ScheduledTask winrun-$id -ErrorAction SilentlyContinue
  Get-Process | ? { \$_.Path -like '$d\\*' } | Stop-Process -Force -ErrorAction SilentlyContinue
}
Kill-Run
Remove-Item -Force -ErrorAction SilentlyContinue $d\out.txt, $d\rc.txt
\$a = New-ScheduledTaskAction -Execute cmd.exe -WorkingDirectory $d -Argument '/v:on /c $d\\$name $* > $d\out.txt 2>&1 & echo !errorlevel! > $d\rc.txt'
\$p = New-ScheduledTaskPrincipal -UserId dev -LogonType Interactive -RunLevel Highest
Register-ScheduledTask -Force winrun-$id -Action \$a -Principal \$p | Out-Null
Start-ScheduledTask winrun-$id
\$end = (Get-Date).AddSeconds($tmo)
while (-not (Test-Path $d\rc.txt)) {
  if ((Get-Date) -gt \$end) { Kill-Run; Get-Content -ErrorAction SilentlyContinue $d\out.txt; 'winrun: timeout'; exit 124 }
  Start-Sleep -Milliseconds 200
}
Start-Sleep -Milliseconds 200
Get-Content $d\out.txt
exit [int](Get-Content $d\rc.txt).Trim()
"
