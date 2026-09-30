# 090 Edge browser process dies a few minutes after start (delay-load procedure not found)
Status: fixed on branch (awaiting merge) · Owner: worker 090 · Branch: fix/090-edge-delayload (wt/090, from integ 1d006ebdafb) · Found in: 088 (inv3, integ 9c1eea5beac + fix/088)

## Symptom
`msedge.exe --user-data-dir=C:\t\edge088 --no-first-run file:///C:/t/static.html` on inv3
(:100): the browser disappears after ~3-5 min, idle; next start shows "Restore pages".
Crashpad dumps in C:\t\edge088\Crashpad\reports (ptype browser), 2 of 3 not caused by kills:
exception 0xc06d007f (delay-load ERROR_PROC_NOT_FOUND) raised in kernelbase+0x4f72e
(RaiseException), i.e. Edge's delay-load helper found a missing export. The dump doesn't
contain the DelayLoadInfo memory, so the DLL/function is unknown.


## Findings (worker 090)
- The DelayLoadInfo is in the dump after all: ExceptionInformation[0] points into the faulting
  thread's stack (captured). Parsed with a small minidump reader; szDll/szProcName point into
  msedge.exe's .rdata (read at the RVA from the file on disk): all 4 inv3 dumps are
  **USERENV.dll!DeriveAppContainerSidFromAppContainerName**, delay-imported by msedge.exe itself
  (the exe carries Chromium's sandbox broker), hmodCur = Wine's userenv (loaded).
  Faulting thread name (ThreadNamesStream): `ThreadPoolSingleThreadForegroundBlocking`.
- Wine's userenv had no DeriveAppContainerSidFromAppContainerName (only a CreateAppContainerProfile stub).
- Windows ground truth (tests/appcontainer_sid.c on the VM): SID = S-1-15-2 + first 7 LE dwords of
  SHA-256(lowercased UTF-16LE name) (matches for every name incl. `a\b`, `a b`, non-ASCII);
  length 1..64 else E_INVALIDARG (NULL, "" too), *sid untouched on failure; SID freeable with FreeSid.
- Edge context: Chromium sandbox `AppContainerBase::CreateProfile` (sandbox/win/src/app_container_base.cc,
  open source) on the browser's process-launcher thread, for the on-device-model service
  (moniker `cr.sb.odm<hash>`, display name "Chrome Sandbox"), ~190 s after start. Flow: Derive SID (userenv,
  delay-import) -> named mutex -> kernelbase AppContainerRegisterSid (ERROR_ALREADY_EXISTS = reuse) ->
  create %LOCALAPPDATA%\Packages\<name>\AC with low label + ACE for the SID -> on failure UnregisterSid.
  Its BindFunc does GetProcAddress(kernelbase) + CHECK for AppContainerRegisterSid/UnregisterSid/
  LookupMoniker/FreeMemory: with only userenv fixed, Edge died at the same time with an int3
  (STATUS_PROCEDURE_NOT_FOUND in rcx) after `LdrGetProcedureAddress "AppContainerRegisterSid" not found`.
- kernelbase ground truth (tests/appcontainer_register.c, Win11 = Wine now): Register writes
  HKCU\Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppContainer\Mappings\<SID>
  {Moniker, DisplayName, subkey Children}; again -> 0x800700b7 but DisplayName is overwritten, Moniker kept;
  SID need not match the moniker, any SID accepted; NULL sid/moniker/display or "" moniker -> E_INVALIDARG.
  Lookup: moniker on the process heap (FreeMemory), missing -> 0x80070002, non-S-1-15-2 SID -> ERROR_NOT_APPCONTAINER.
  Unregister deletes the tree; missing -> 0x80070002. Not exported from kernel32 on Win11.

## Fix (fix/090-edge-delayload)
- e8b3a0017cf userenv: Implement DeriveAppContainerSidFromAppContainerName (bcrypt SHA-256) + test.
- 42b8bed5622 kernelbase: Implement AppContainer SID registration functions + test.
Tests: userenv 211/0 failures and kernelbase:security 91/0 on Win11 VM and Wine, both arches.
regress userenv|advapi32|kernelbase: 38/38 pass.

## Verification (inv4, :101)
- Before (build/ = integ): browser gone after 191 s, crashpad 0xc06d007f (same DelayLoadInfo).
- userenv only: gone after 188 s, int3 (AppContainerRegisterSid).
- Both fixes: alive after 1000 s idle, no crashpad reports. Then example.com and an httpbin 302 redirect
  to `/get?code=abc&state=xyz` handed to the running browser via a second msedge.exe (process singleton)
  render fine.
- Remaining: the profile dir step fails (`Failed to create the AppContainer profile directory`) ->
  Edge unregisters and the on-device-model service doesn't start; harmless for sign-in. Cause:
  SetNamedSecurityInfo(LABEL_SECURITY_INFORMATION) access denied, draft 094.
