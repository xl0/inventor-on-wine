# 094 SetNamedSecurityInfo(LABEL_SECURITY_INFORMATION) fails with ERROR_ACCESS_DENIED
Status: fixed · Owner: worker-094 · Branch: fix/094-setnamedsecurityinfo-label (wt/094, on integ 201dbe77eca) · Found in: 090

## Symptom
Edge 154 (msedge.exe, Chromium sandbox `app_container_base.cc` CreateAppContainerDirectory) can't create
the AppContainer profile dir for its on-device-model service (`cr.sb.odm<hash>`, ~3 min after start):
chrome_debug.log `Failed to create the AppContainer profile directory for cr.sb.odm...`, then it unregisters
the SID and the service launch fails (`Unexpected on_device_model service disconnect`). No crash (since 090).

## Windows ground truth (Win11 VM, `tests/setsecinfo_access.c`)
Same for files, directories, registry keys (and events for the NtSetSecurityObject part):

| info flag | NtSetSecurityObject needs | SetNamedSecurityInfoW opens with |
|---|---|---|
| OWNER, GROUP | WRITE_OWNER | WRITE_OWNER |
| DACL | WRITE_DAC | READ_CONTROL + WRITE_DAC |
| SACL | ACCESS_SYSTEM_SECURITY | READ_CONTROL + ACCESS_SYSTEM_SECURITY |
| LABEL | WRITE_OWNER | READ_CONTROL + WRITE_OWNER |
| ATTRIBUTE | WRITE_DAC | READ_CONTROL + WRITE_DAC |
| SCOPE | ACCESS_SYSTEM_SECURITY | READ_CONTROL + ACCESS_SYSTEM_SECURITY |
| PROCESS_TRUST_LABEL, BACKUP | denied with any single right | ERROR_ACCESS_DENIED |
| (UN)PROTECTED_DACL/SACL alone | nothing (success) | ERROR_ACCESS_DENIED |

SetSecurityInfo(LABEL) with a label SACL sets the label (read back: 1 mandatory ACE; OI|CI kept on dirs,
keys, events, stripped on files).

## Cause and fix
advapi32 SetNamedSecurityInfoW didn't map LABEL_SECURITY_INFORMATION to WRITE_OWNER (wineserver already
requires it), and SetSecurityInfo only put pSacl into the SD for SACL_SECURITY_INFORMATION, so
LABEL-only calls cleared the label (NtSetSecurityObject forces SE_SACL_PRESENT, NULL SACL).
Commit de1a77c5b5d fixes both; test `test_SetNamedSecurityInfo_label` in advapi32/tests/security.c
(dir LABEL, DACL|LABEL, file LABEL, key LABEL + readback).

Left alone (niche, no app needs them): Wine doesn't request READ_CONTROL for SACL/LABEL or for
keys; wineserver needs no right for ATTRIBUTE/SCOPE/TRUST/BACKUP; Wine doesn't store labels on
files (file SD comes from the unix mode); the OWNER RIGHTS SID isn't honoured on files.

## Tests
- advapi32:security on VM x86_64 + i386: 3749 tests, 8 failures, all pre-existing (lines 5859-5893,
  same 8 with the unpatched test exe); new checks pass. Wine x86_64 + i386: 0 failures (5 without the fix).
- `tests/ac_profile_dir.exe` on Wine: all steps 0 (was 5 for DACL|LABEL and LABEL).
- regress subset advapi32|kernelbase|userenv|dcomp: 40 units, all pass.

## Edge (inv4, wt/094-build, `--user-data-dir=C:\t\edge094 --enable-logging --v=1`)
No "Failed to create the AppContainer profile directory"; `%LOCALAPPDATA%\Packages\cr.sb.odm...\AC\Temp`
exists. The on-device-model launch still fails (`Unexpected on_device_model service disconnect`, crashpad
DumpWithoutCrashing 0x0517a7ed on the launcher thread) at the next step:
UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES) -> draft 095.

## Also on this branch
40ba90b8354 dcomp/tests: `PARENTSRC = ../` made make_makefiles look up `dlls/dcomp//Makefile.in`, miss the
shared dcomp_private_iface.idl and drop it from SOURCES; now `../../dcomp`. make_makefiles is a no-op.
