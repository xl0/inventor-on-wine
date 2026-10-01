# 095 UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES) unsupported
Status: fixed on branch (awaiting merge) · Owner: worker-095 · Branch: fix/095-security-capabilities (wt/095, on integ de4940b5b86) · Found in: 094

## Symptom
Edge 154's on-device-model service (`cr.sb.odm<hash>`, AppContainer sandbox, ~3 min after start) never
starts: chrome_debug.log `Unexpected on_device_model service disconnect; reason: 0`, and crashpad writes a
DumpWithoutCrashing report (exception 0x0517a7ed, browser launcher thread). kernelbase rejected attribute 9
(SECURITY_CAPABILITIES) with ERROR_NOT_SUPPORTED, so Chromium's StartupInformationHelper gave up before
CreateProcess. (Attribute 26 = COMPONENT_FILTER, which Chromium tolerates as ERROR_NOT_SUPPORTED;
15 = ALL_APPLICATION_PACKAGES_POLICY is sent only for low-privilege app containers.)

## Windows ground truth (Win11 VM, tests/seccaps_probe.c; `run EXE` mode launches EXE in the container)
- UpdateProcThreadAttribute sizes: SECURITY_CAPABILITIES only sizeof (24/16), else ERROR_INVALID_PARAMETER;
  ALL_APPLICATION_PACKAGES_POLICY and COMPONENT_FILTER only 4, else ERROR_BAD_LENGTH; CHILD_PROCESS_POLICY
  only 4 (Wine also accepts 8); MITIGATION_POLICY 24 -> ERROR_BAD_LENGTH; with the THREAD flag -> ERROR_NOT_SUPPORTED.
- CreateProcess(+AsUser) with SECURITY_CAPABILITIES: NULL package SID -> 87; non-package SID (S-1-15-3-1,
  S-1-15-2-1) -> ERROR_NOT_APPCONTAINER; package SID not registered (AppContainer Mappings key) ->
  ERROR_FILE_NOT_FOUND; non-capability SID or package SID in the capabilities -> 87; source token already
  lowbox -> 87.
- Child token = lowbox of the given token (or the parent's): primary, TokenIsAppContainer 1, package SID,
  capabilities exactly as given (attributes kept, 0 stays 0), IL low, groups unchanged (IL group -> low),
  privileges reduced to SeChangeNotify + SeIncreaseWorkingSet (attributes kept), still "elevated".
- Child environment (only with the attribute, not with CreateProcessAsUser(lowbox token)): LOCALAPPDATA =
  <LOCALAPPDATA of the env block>\Packages\<moniker>\AC, TEMP = TMP = that + \Temp; also for an explicit
  env block. The directory is not created.
- Access in the child: user profile, dirs without an ACE for the package/ALL APPLICATION PACKAGES: denied;
  ACE for the package SID or S-1-15-2-1: allowed; the image itself needs such an ACE. Parent's unqualified
  named event: access denied (named objects live in AppContainerNamedObjects\<SID>).
  ALL_APPLICATION_PACKAGES_POLICY=1 (LPAC): S-1-15-2-1 ACEs no longer apply (HKCU/HKLM keys denied too);
  a WoW64 LPAC child of kernel32_test died with STATUS_DLL_NOT_FOUND.
- NtCreateLowBoxToken (tests/lowbox_args.c): from a lowbox token STATUS_ACCESS_DENIED; non-capability SID or
  S-1-15-3 alone: STATUS_INVALID_PARAMETER; NULL capability SID: STATUS_ACCESS_VIOLATION; count>0 with NULL
  array: STATUS_INVALID_PARAMETER_MIX; count > 4096: STATUS_INVALID_PARAMETER (x64; 4096 distinct caps ok);
  revision != 1 or > 15 sub-authorities: STATUS_INVALID_SID; access 0 = access of the source handle.
  (3+ identical capability SIDs were rejected too, 2 were not: not emulated.)
- TokenAppContainerSid: minimum size sizeof(TOKEN_APPCONTAINER_INFORMATION) (8/4), retlen = that + SID length
  (x64 lowbox: 48); non-lowbox: TokenAppContainer NULL.
- CreateProcess with the attribute: no LOCALAPPDATA in the environment -> ERROR_ENVVAR_NOT_FOUND (203);
  LOCALAPPDATA longer than MAX_PATH is rewritten normally.

## Design / fix (fix/095-security-capabilities)
- server: tokens carry the package SID + capabilities (new `create_lowbox_token` request; duplicates and
  filtered tokens keep them); lowbox tokens keep only SeChangeNotify/SeIncreaseWorkingSet.
  get_token_info reports is_appcontainer, get_token_sid(TokenAppContainerSid), get_token_groups(capabilities).
- ntdll: real NtCreateLowBoxToken (capability SID validation); TokenIsAppContainer, TokenAppContainerSid,
  TokenCapabilities (+ wow64), TokenIntegrityLevel low for lowbox tokens (others still report high).
- kernelbase: accept attributes 9, 15, 26 (sizes as above; 15/26 are ignored by CreateProcess, LPAC not
  implemented);
  CreateProcess builds the lowbox token (shared with CreateAppContainerToken, which now returns
  ERROR_NOT_APPCONTAINER), requires the registration and rewrites LOCALAPPDATA/TEMP/TMP.
- Not done (no app needs them): access checks for app containers (Wine enforces none), LPAC, named object
  isolation, TokenAppContainerNumber, lowbox-source -> 87 in CreateProcess (Wine: 5).

## Tests
Commits (wt/095, fix/095-security-capabilities on integ de4940b5b86, protocol version 969; review fixes folded in):
354990a429c ntdll: Create real lowbox tokens in NtCreateLowBoxToken().
2d401476617 kernelbase: Return ERROR_NOT_APPCONTAINER from CreateAppContainerToken() for non-package SIDs.
9bc9a04f613 kernelbase: Accept the ALL_APPLICATION_PACKAGES_POLICY and COMPONENT_FILTER attributes.
6c4ab969234 kernelbase: Support PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES.
Review fixes: TokenAppContainerSid size probing (was stuck at 20 on x64), NtCreateLowBoxToken argument checks
(+ wow64 thunk with NULL/huge arrays), access 0, missing/long LOCALAPPDATA, leftover %TEMP%\seccaps.
After the rebase: kernelbase:security 118 tests 0 failures on VM + Wine (both arches); kernel32:process VM
only the pre-existing :4638, Wine 112/176 = baseline; regress subset 0 worse of 158 units.

- kernel32:process gets test_security_capabilities_attribute (attribute sizes, unregistered -> 2,
  non-package -> 4250, NULL -> 87, bad capability -> 87; child in the container checks IsAppContainer,
  package SID, capabilities + attributes, low IL, LOCALAPPDATA/TEMP). The child image is copied into a temp
  dir with an ALL APPLICATION PACKAGES ACE. VM x86_64/i386: only the pre-existing :4638 "mismatch" failure
  (also with build/'s exe); Wine both arches: 112/176 failures = master baseline (line shift only).
  The VM child can't print (no inherited stdout); `seccaps_probe.exe run k32.exe` shows it runs 14 checks
  identically on Win11 and Wine.
- kernelbase:security: todo_wine removed (IsAppContainer, package SID, low IL, ERROR_NOT_APPCONTAINER),
  + lowbox-from-lowbox STATUS_ACCESS_DENIED and package SID as capability STATUS_INVALID_PARAMETER.
  VM 64/32: 93 tests, 0 failures; Wine 64/32: 0 failures.
- regress ntdll|kernel32|kernelbase|advapi32|userenv|wow64 (158 units) vs master 4e819f05: only
  kernel32:debugger x86_64 (known integ issue 105).

## Edge (inv4 :101, wt/095-build, `--user-data-dir=C:\t\edge095b`, file:///C:/t/static.html)
No "Unhandled attribute", no GPU/launch failures, page renders (renderers now get real lowbox tokens),
the on-device-model service starts at ~3 min (its own log lines "Edge LLM: Error getting component
directory" and a D3D12 fence failure in its performance estimation, then it exits), no "Unexpected
on_device_model service disconnect", no crashpad report in 4 min (5+ min in the earlier run edge095).
Trap hit on the way: early test runs used a build where only server/ntdll were rebuilt after the
protocol change -> stale request numbers in ntoskrnl/win32u, drivers failing, Edge browser int3/GPU
launch errors; all gone after a full `make`.
