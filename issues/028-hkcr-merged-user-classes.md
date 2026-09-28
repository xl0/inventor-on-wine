# 028 HKCR lacks HKCU\Software\Classes: per-user URL protocols never launch
Status: fixed · Owner: worker 028 · Branch: fix/028-hkcr-merged-view (wt/028) · Found in: prefixes/inv, Autodesk sign-in (Edge → AdskIdentityManager)

## Symptom
Autodesk sign-in in Edge ends on signin.autodesk.com/idmgr/callback ("You're signed in",
button "Open Product"). The button does `window.location.assign("adsk.idmgr:/login?code=..&state=..")`.
In Wine, Edge shows nothing: no "This site is trying to open Autodesk Identity Manager"
prompt, no handler process. AdskIdentityManager (IDM) keeps logging "Failed to refresh token.
Not logged in." and its dialog stays on "Retry browser sign-in".

## Cause
IDM registers its schemes per user only (IdServices.log "custom uri ... to registry"):
`HKCU\Software\Classes\{adsk.idmgr,adskidmgr}` ("URL Protocol", shell\open\command =
`"...\AdskIdentityManager\1.19.2.0\AdskIdentityManager.exe" "%1"`). Wine's HKEY_CLASSES_ROOT
is only `HKLM\Software\Classes` (kernelbase registry.c `open_classes_root`). There is no merged
view, so `reg query HKCR\adsk.idmgr` fails. AssocQueryStringW(ASSOCF_IS_PROTOCOL, any ASSOCSTR,
"adsk.idmgr") returns 0x80070483 (ERROR_NO_ASSOCIATION). Chromium resolves the program name
from the scheme's association before it prompts. Its source says: when there is no program
name, ShellExecute won't do anything, so it returns without prompting and the navigation is
dropped silently.

## Windows ground truth
- `tests/hkcu_proto.c` (creates HKCU\Software\Classes\wineassoctest as a URL protocol, then
  queries it) on the Win11 VM: RegOpenKeyEx(HKCR, "wineassoctest") = 0; AssocQueryString COMMAND =
  S_OK (FRIENDLYAPPNAME = 0x80070490 for notepad.exe there). Wine: 2 / 0x80070483 / 0x80070483.
- Wine's own advapi32 `test_classesroot` / `test_classesroot_enum` already describe the merged
  view (HKCU wins, writes through HKCR go to the existing key); the opening checks are todo_wine.

## Workaround (applied to prefixes/inv, 2026-09-28)
Copied both scheme keys to `HKLM\Software\Classes` (same values). Edge then showed the
prompt. With "Always allow" ticked and Open clicked, the handler IDM process signalled
AdOAuth2Code-<pid> to the running IDM → "Completed login successfully" (session valid 30 days).
inv-vm needs the same if its sign-in goes through the real IDM registration.

## Task
Implement the HKCR merged view: open/create/enum/query through HKCR sees HKCU\Software\Classes
first, then HKLM\Software\Classes. This is a big, long-standing Wine gap. Check upstream
bugzilla / Wine-Staging for prior work first. A narrower shell32 fix (consulting HKCU\Software\
Classes in assoc/ShellExecute) would cover this app but not Chromium's registry lookups; decide
after reading the tests.

## Prior work (researched 2026-09-28)
- Bug 14771 (2008, NEW; dup 17019): Firefox default-browser check. No patch attached.
- André Hentschel 2010 / George Stephanos 2013 (GSoC) upstreamed only the tests
  (`test_classesroot`, `_enum`, `_mask`: HKCR handles tagged `(h & 3) == 2`); the implementation
  never landed. Nothing in Wine-Staging (current tree or history) or Proton.
- ReactOS `advapi32/reg/hkcr.c` (Jérôme Gardou): tagged handles, user-then-machine fallback for
  open/create/query/set/delete, merged enum. Reference for the design only.
- GitLab !11386 (2026-07, open, bug 57229): shell32-only fallback in `execute_from_key`
  plus winemenubuilder export of per-user protocols. Covers ShellExecute only, not
  AssocQueryString / Chromium registry reads.
- Related merged: !2483/!966 (shared Software\Classes Wow6432Node), !10580 (wine.inf creates
  HKCU\Software\Classes).

## Windows ground truth (tests/hkcr_merge.c, Win11 VM, elevated)
- HKCR handles carry the tag bit and point at ONE real key: HKCU side (`\REGISTRY\USER\<sid>_Classes\..`)
  when it exists, else HKLM side. Absolute and relative opens resolve the same way at every level.
- Values: per value, user side wins, else machine side (partial HKCU overlays keep HKLM values).
  A machine-side handle sees a user key created later (query + set go to the user key).
- Enumeration (keys and values) is the sorted, deduplicated union; RegQueryInfoKey counts the union.
- RegCreateKeyEx through HKCR: an existing key (either side, user first) is opened; a new key is
  always created on the HKLM side, even below a user-only parent (parents created in HKLM too).

## Fix (fix/028-hkcr-merged-view, on master)
`a9fd231f808 kernelbase: Merge HKCU\Software\Classes into HKEY_CLASSES_ROOT.` +
`965df4bb935 shlwapi/tests: Test AssocQueryString with a per-user URL protocol.`
Full merged view in kernelbase registry.c (~450 lines, one commit: enumeration can't be split
off without regressing overlay keys or leaving test_classesroot_enum half-todo):
- HKCR-derived handles are tagged (bit 1; server and ntdll ignore the low 2 bits). The other
  side of a tagged key is found by name (NtQueryKey path, swap `\Registry\Machine\Software\Classes`
  <-> `\Registry\User\<sid>\Software\Classes`, open with KEY_WOW64_64KEY since the name is already
  redirected). HKCR root uses the cached special root + a cached HKCU\Software\Classes handle.
- Open / delete-key: user side first. Create: existing key opened, else created on the HKLM side
  (parents too). Query value: user, then machine. Set value: user side if it exists.
- RegEnumKeyEx/RegEnumValue/RegQueryInfoKey: sorted dedup merge of both server lists (sorted
  case-insensitively like RtlCompareUnicodeString). A small cursor cache (8 entries, cleared by
  RegCloseKey) keeps sequential enumeration linear.
- RegOverridePredefKey(HKCR) disables merging for the root (override used as before).
- Not merged: RegDeleteValue, RegNotifyChangeKeyValue, security (act on the handle's own key);
  combase/ole32 keep their private HKLM-only classes root (per-user COM registrations still
  invisible to COM activation).
- Cost: HKCR open+query ~2x, enumeration ~3x server calls (NtQueryKey + counterpart open per op).

Results: advapi32 registry: all HKCR todo_wine flipped (4), 0 failures x86_64/i386 on Wine and
the Win11 VM; shlwapi assoc new test passes on VM and Wine (fails on master). tests/hkcr_merge.c
output on Wine matches the VM except "set via a HKLM-side handle" (now matches too) and
FRIENDLYAPPNAME (pre-existing, unrelated). regress.sh (advapi32 kernelbase shlwapi shell32 ole32
combase msi urlmon ieframe, both arches) vs master 4e819f0 and integ 6c63dc3: 0 worse of 170.
Success check (scratch prefix): scheme only in HKCU\Software\Classes -> AssocQueryString S_OK,
ShellExecuteEx launches the handler with the URL (master: 0x80070483 / ERROR_FILE_NOT_FOUND).
Once merged, the HKLM copies of the IDM schemes in prefixes/inv are no longer needed.

## Review (2026-09-28, wt/028r)
Fixups on the branch: `fixup! kernelbase: Merge ...` (RegQueryInfoKey counted from a stale enum
cache: 4 subkeys after deleting one of 4, VM says 3; now counts HKCU-side duplicates, no cache) and
`server: Share HKCU\Software\Classes\Wow6432Node like the HKLM one.` (+ wine.inf HKCU Wow6432Node
CLSID/Interface/DirectShow/Media Type/MediaFoundation; better ordered before the kernelbase commit).
VM ground truth (scratch wow.c): 32-bit view of HKCR\CLSID = `<sid>_Classes\WOW6432Node\CLSID`; a
64-bit-only per-user CLSID is invisible there. Branch before the fix: 32-bit/KEY_WOW64_32KEY got the
64-bit user key and, through a user-side HKCR\CLSID handle, the 64-bit HKLM registrations.
Open (confirmed): writes through HKCR go to an existing HKCU key (Windows does the same), but
combase/ole32 read HKLM only. `regsvr32 quartz` with a stale HKCU\...\CLSID\{FilterGraph} key
wrote the registration to HKCU -> CoCreateInstance 0x80040154 (master: HKLM, works). prefixes/inv has
4 CLSIDs on both sides and 122 CLSIDs + 349 Interfaces per-user only. Needs combase/ole32 to use the
merged view (they keep a private root so RegOverridePredefKey(HKCR) registration capture doesn't
hide real classes, so not a plain switch to Reg*).
Minor (confirmed vs VM): merged RegEnumKeyEx ignores a missing KEY_ENUMERATE_SUB_KEYS (VM: 5).
Theoretical: enum cache keyed by handle value (stale after CloseHandle/NtClose + handle reuse);
two threads enumerating the same HKCR handle restart the walk each call (O(n^2)).
Resolved by 030 (combase/ole32 use the merged view via RegOpenUserClassesRoot, HKLM only when
elevated, like Windows). Final upstream-ordered series incl. 028: fix/028-030-series.
