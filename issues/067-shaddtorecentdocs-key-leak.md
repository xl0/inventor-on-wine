# 067 SHAddToRecentDocs leaks an HKCU\...\CurrentVersion\Explorer key handle per unsupported call
Status: open (draft) · Owner: - · Branch: - · Found in: soak test (inst/soak/2026-09-28, integ c036c687c47)

## Symptom
In a 4 h soak of one Inventor session (tools/soak/soak.sh), Inventor's kernel handle count grows
steadily. A handle dump (`tools/soak/resprobe.c` `dump` mode) after 7 suite iterations (~1.2 h):
447 key handles all named `\REGISTRY\User\S-1-5-21-...\Software\Microsoft\Windows\CurrentVersion\Explorer`
(`inst/soak/2026-09-28/handles-iter7.txt`, not in git). Key handles at start: ~97 total;
+25 per suite run, 1232 after 4 h (`matched/late-pre.txt`). Harmless in size, but unbounded.
The Windows VM's Inventor (running all day, same scenarios) holds none of these
(`handles-vm.txt`: 288 keys, no ...\Explorer).

## Cause (Wine source)
`dlls/shell32/shellord.c:SHAddToRecentDocs` opens `HCUbasekey` (RegCreateKeyExA ...\Explorer)
before looking at `uFlags`, then returns without RegCloseKey on
- `default:` (`FIXME("Unsupported flags: %u\n")`) — SHARD_APPIDINFO (4), SHARD_SHELLITEM (8),
  SHARD_APPIDINFOIDLIST, SHARD_LINK, SHARD_APPIDINFOLINK (Win7+ flags, what modern apps use),
- `SHARD_PIDL` when SHGetPathFromIDListA fails.

## Repro / Windows ground truth
`tests/recentdocs_leak.c` (100 calls per flag, own handle count via SystemExtendedHandleInformation;
Wine stubs GetProcessHandleCount):

| | SHARD_SHELLITEM | SHARD_APPIDINFO | SHARD_PATHW |
|---|---|---|---|
| Windows 11 VM | +2 | +0 | +0 |
| Wine c036c687c47 | +100 | +100 | +2 |

Not verified which flag Inventor passes (count tracks document opens/saves; `WINEDEBUG=fixme+shell`
on a fresh session would show "Unsupported flags").

## Task
Close the key on every path (smallest fix: move the open after the flag switch, or close before the
early returns). Implementing SHARD_SHELLITEM / SHARD_APPIDINFO* (resolve the item to a path/pidl and
add it like SHARD_PIDL) is optional follow-up. A handle-count test in shell32/tests/shellole.c or
shelllink.c is reasonable (the leak is 1 handle per call).
