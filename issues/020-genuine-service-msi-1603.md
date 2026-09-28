# 020 Autodesk Genuine Service MSI returns 1603 (killBeacon step)
Status: open (draft, low) · Owner: - · Branch: - · Found in: Inventor install on integ 77645e2b221 (prefix inv)

## Observed
During the base product install the Autodesk Genuine Service MSI returned 1603,
failing in its `killBeacon` custom action. ODIS treated it as success and lists
the package INSTALLED; on the VM it installed cleanly. Logs: ODIS Install.log /
MSI log in prefixes/inv (see the install run of 2026-09-28 01:2x–01:32).

## Suspected (unverified)
killBeacon likely terminates/queries a process (taskkill/tasklist/WMI?) and
gets a result Wine formats or reports differently (cf. 007 tasklist).

## Task
Low priority (non-blocking). Identify the custom action and the failing call,
compare with the VM, fix if it's a Wine bug.

## Findings (installer driver, rerun on integ ec5464293b0 — recurred)
- Wine log: `err:msi:ITERATE_Actions Execution halted, action L"killBeacon" returned 1603`
  (02:01:40; ODIS again "Error-code found in success codes list", package INSTALLED).
- AGS.msi tables (`tests/msiq.c`, Wine msi.dll): `killBeacon` = type 1 (DLL from
  Binary `_C6F2E30BA588722456690C1C107219A4`, entry `m4`), sequence 493,
  condition `NOT Installed OR IS_MINOR_UPGRADE OR REMOVE~="ALL"`, no
  continue-on-error flag. Neighbours stopBeacon/stopNC are InstallShield
  `SetAllUsers.dll!KillProcess` (they pass).
- The Binary is InstallShield 23 `CLRWrap.dll` (Flexera) whose embedded
  `<EntryPoint Assembly="ClrPsHelper" ... Method="RunScript" Script="killBeacon">`
  runs a **PowerShell script** in-process via `System.Management.Automation`
  (script checks uninstall conditions, runs `GenuineService.exe uninstall_forbidden`).
  CLR starts (amsi/wldp fixmes from the .NET 4.8 runtime right before the failure).
- `System.Management.Automation` is not in the prefix's GAC_MSIL: it ships with
  Windows PowerShell 5.1 (part of Windows, not the .NET 4.8 redist). So the CA most
  likely fails on the missing assembly — a missing Windows component rather than a
  Wine API bug (unverified: no CLR fusion log taken). Options for the coordinator:
  accept (ODIS tolerates it), or check with a fusion log / `+loaddll,+mscoree` run
  of the msi (`msiexec /i ags.msi /l*v`) which assembly load fails.
