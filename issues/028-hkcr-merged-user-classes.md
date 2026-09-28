# 028 HKCR lacks HKCU\Software\Classes: per-user URL protocols never launch
Status: open (draft) · Owner: – · Branch: – · Found in: prefixes/inv, Autodesk sign-in (Edge → AdskIdentityManager)

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
