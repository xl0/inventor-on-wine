# 035 DOS device names inside full paths ("C:\dir\con.iam") are devices on Wine, files on Windows 11
Status: fixed · Owner: worker 035 · Branch: fix/035-dos-device-names · Found in: test campaign, invscen asmcon

## Symptom
Inventor `AssemblyDocument.SaveAs("...\\asmcon\\con.iam")` fails with E_FAIL
on Wine; the same scenario saves `con.iam` (178 KB) on the VM. Any other
file name saves fine on Wine.

## Windows ground truth (Win11 VM, `tests/dosdev_name.c`)
In a temp dir, `CreateFileW(dir\NAME, CREATE_ALWAYS)` + `GetFullPathNameW` +
`RtlIsDosDeviceName_U` for NAME in con, con.iam, CON.txt, nul.txt, aux.c, prn.x,
com1.txt, lpt1.txt, "con .txt", con:, conin$, foo.con:

- Windows 11: every one except `con:` (ERROR_INVALID_NAME) creates a regular
  disk file (type 1), `GetFullPathNameW` returns `dir\NAME`, and
  `RtlIsDosDeviceName_U(full path)` = 0. For the bare name,
  `RtlIsDosDeviceName_U` is still nonzero only for `con` (6), `con:` (6) and
  `conin$` (0xc); `con.iam`, `CON.txt`, `nul.txt`, `com1.txt`, "con .txt"
  → 0.
- Wine: all of them except foo.con map to `\\.\NAME` (RtlIsDosDeviceName_U of
  the full path = 0x00580006 etc.); CreateFile opens the console / NUL
  device or fails (err 6 / 2).

So Windows 11 dropped the legacy rule that device names are recognised in
any path component and with any extension (older Windows did that; Wine
implements the old rule in ntdll `RtlIsDosDeviceName_U` / path conversion).

## Task
Match Windows 11: check where the device-name recognition is applied (bare
name vs. last component of a full path, extension stripping) with a
conformance test in `dlls/ntdll/tests/path.c` (existing RtlIsDosDeviceName_U
tests encode the old behaviour — check what Windows 10 vs 11 do and mark
accordingly; the VM only gives Win11 ground truth).

## Outcome
Fix: `ntdll: Only recognize NUL as a DOS device name inside a path.`
(113899c66d3 on fix/035-dos-device-names). All DOS→NT conversion
(RtlGetFullPathName_U, RtlDosPathNameToNtPathName_U, kernelbase CreateFile /
FindFirstFile / GetFileAttributes / QueryDosDevice) goes through
`RtlIsDosDeviceName_U`, so the fix is only there. ntdll/tests/path.c: Win11
values expected, Win10 ones via broken() (`legacy` flag); new cases incl.
`c:\dir\con.iam`, `c:con`, `.\con`, NT-name conversion of `c:\windows\con.iam`.

Win11 ground truth (full probe: `tests/dosdev_name.c`; the rules, see
notes/wine/paths.md): CON/AUX/PRN/COMn/LPTn/CONIN$/CONOUT$ only as the whole
relative path (`c:con`, `.\con`, `\??\CONIN$` → 0); NUL as last element of
any non-UNC, non-`\\.\`/`\\?\` path. No extension any more; trailing part
may be: one `:`, then dots/spaces, then one `:` and spaces (`nul::`, `nul :.`
yes; `nul:::`, `nul .::`, `nul:x` no). COM¹²³/LPT¹²³ accepted (bare).
`\\.\con` → 0 on Win11 (Wine still returns 8,6: left alone, only
RtlIsDosDeviceName_U's return differs, conversion results match).
NtCreateFile on `\??\...\con.iam` is a plain file on both.

Probe on Wine after the fix matches Win11 for RtlIsDosDeviceName_U (150
strings), GetFullPathNameW and RtlDosPathNameToNtPathName_U. Remaining
CreateFile diffs are unrelated: bare `con`/`conin$` under winrun have a
console on Windows, none under headless Wine; `dir\con:` is
ERROR_INVALID_NAME on Windows but a file named `con:` on Wine (stream-name
syntax, issue 038).

Tests: ntdll:path 0 failures on Win11 VM and Wine (both arches);
regress (ntdll kernel32 kernelbase msvcr*/msvcp*/ucrtbase shell32 shlwapi
cmd xcopy setupapi advpack msi scrrun shcore, 326 units) vs master 4e819f054dd
and integ 91495f487ad: only FLAKY. Inventor asmcon scenario not re-run.
