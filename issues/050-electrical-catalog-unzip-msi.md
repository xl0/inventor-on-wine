# 050 Electrical Catalog Browser installation fails in AceUnzipZipFiles
Status: workaround via inbox-equivalent tar.exe (`tools/tar.sh`); fix = prefix prerequisite, not a Wine bug · Owner: worker-050 · Branch: - · Found in: local Inventor 2027.1 installation

## Symptom
On Ubuntu 24.04, new-WoW64 `integ 47e296ffde4d`
(`wine-11.18-360-g47e296ffde`), Autodesk's web installer reports Electrical
Catalog Browser failed. Its custom-action package `AceInvAddIn-ca.msi` returns
1603 during `AceUnzipZipFiles`, and ODIS rolls back the add-on.

Inventor core, the 2027.1 update, DWG TrueView and Content Libraries are
reported INSTALLED. This is a separate electrical-catalog add-on, not a failure
of the main Inventor installation.

Prefix: `prefixes/inv`, Windows 11, native .NET 4.8 (32/64-bit runtimes verified),
Gecko 2.47.4, Edge/WebView2 154.0.4258.37. User-profile folders are real
prefix-local directories, not symlinks into the host home.

## Evidence (2026-09-28, 17:19:50 local time)
`inst/local/installer.log`:

```text
err:msi:custom_get_thread_return invalid Return Code 1359
err:msi:execute_script Execution of script 0 halted; action L"[...]AceUnzipZ"... returned 1603
err:msi:ITERATE_Actions Execution halted, action L"InstallFinalize" returned 1603
```

The action data names `AceInvLib.zip` and destination
`C:\users\Public\Documents\Autodesk\Inventor Electrical Library 2027\`.
The CA MSI log reaches `AceUnzipZipFiles` during `InstallFinalize`, then
records rollback. ODIS confirms the MSI returned 1603; its later rollback
uninstall returns 1605 (not installed), which ODIS tolerates.

Local evidence, relative to `prefixes/inv/drive_c/users/xl0/AppData/Local/`:

- `Autodesk/ODIS/Install.log` and `Summary.log`.
- `Temp/Autodesk_Inventor_Electrical_Catalog_Browser_CA_Core_2027_install.log`.
- Cached payload directory:
  `Temp/{447AF3F0-5A11-3D54-B8C1-9D1C88C26148}/x64/AceAddIn-ca/`.
  Contains `AceInvAddIn-ca.msi` and `Documents/ADSK/Content/AceInvLib.zip`.

## Prior observation / Windows ground truth
[013](013-cms-attrcert-msg-store.md) also noted `AceInvAddIn-ca.msi` returning
1603, but did not investigate it. That issue fixed a separate .NET installer
signature-verification bug; the catalog failure still occurs with that fix.
The same root cause across the two runs is not established.

No targeted Windows reference result for this action has been recorded.
The local workstation has no VM. Do not conflate this with Genuine Service's
`killBeacon` failure ([020](020-genuine-service-msi-1603.md)).

## Root cause (2026-09-28, server, build/ integ 228616fa47c)
Missing Windows component, not a Wine API bug: the CA shells out to the
inbox `C:\Windows\System32\tar.exe` (bsdtar/libarchive, in Windows since 10 1803;
ODIS requires 10.0.17763). Wine has no tar.exe, so the CA returns 1359.

- `AceUnzipZipFiles` = type 3073 (deferred, no-impersonate) DLL CA in Binary
  `AeCustom` (Autodesk, MFC static, x64), CustomActionData
  `ProcID|<SourceDir>Documents\ADSK\Content\AceInvLib.zip*[ACELIBLOCATION]`.
- Unzip helper (AeCustom RVA 0x1c260): temp dir `C:\ACExxxx` (GetTempFileName
  on `C:`), copies the zip to `ZIPxxxx\`, then `CreateProcessAsUserW(own token,
  "tar -xf \"ZIP\" -C \"OUT\" --keep-newer-files")`, waits, copies OUT into the
  destination (std::filesystem). Every failure path returns 0x54f = 1359;
  here CreateProcessAsUserW fails (`find_exe_file` finds no `tar`).
- Windows ground truth (VM): `C:\WINDOWS\system32\tar.exe` 10.0.26100.9278 =
  `bsdtar 3.8.8 - libarchive 3.8.8`; the exact command on AceInvLib.zip exits 0,
  20 files / 4588032 bytes (matches `unzip -l`).
- Verified on Wine: with a scratch-only `tar.exe` shim in system32 (forwarded
  `-xf/-C` to host unzip), the CA MSI installs (msiexec 0, 20 files in
  `C:\users\Public\Documents\Autodesk\Inventor Electrical Library 2027\`,
  temp dir cleaned). Rest of the CA path (CreateProcessAsUserW, filesystem copy)
  works on Wine.
- Repro: scratch prefix (copy of inv-net48), payload copied to `C:\t\ace`,
  `msiexec /i C:\t\ace\AceInvAddIn-ca.msi /qn /l*vx LOG MSIFASTINSTALL=7
  ARPSYSTEMCOMPONENT=1 REBOOT=ReallySuppress ADSK_ODIS_SETUP=1`
  (ODIS params; its INSTALLDIR is unused by this MSI's CA path).
- Side finding: the rolled-back install deleted a pre-existing empty
  `C:\users\Public\Documents` → [051](051-msi-removes-preexisting-empty-dirs.md).
  In prefixes/inv that folder had other content, so no harm there.

## Options (coordinator/user decision)
1. Prefix fix, most faithful: install libarchive's own Windows bsdtar build
   (same code Microsoft ships as tar.exe, BSD license) as `system32\tar.exe`
   (+ syswow64), pinned in deps/ like Edge/WebView2, via a small tools/ script.
   Needs approval for the download.
2. Wine fix: new `programs/tar` (bsdtar-compatible subset: `-x -f -C`, zip
   stored/deflate via bundled zlib, ignore `--keep-newer-files`; tar/gz later).
   Upstreamable in principle but a new program (~few hundred lines) for one CA.
3. Accept: optional add-on, ODIS rolls it back and continues. Manual
   workaround: extract AceInvLib.zip into the destination above.

## Resolution (2026-09-28)
Option 1 approved. `tools/tar.sh` builds libarchive 3.8.8's bsdtar (static MinGW, zlib)
and installs it as system32/syswow64 `tar.exe` (`bsdtar 3.8.8 - libarchive 3.8.8`).
Scratch copy of inv-net48: the exact CA command extracts 20 files / 4588032 bytes; the
repro msiexec returns 0 with the library installed, temp dir cleaned. Installed in
inv-net48, inv, inv2, inv3 (files only); transplant.sh runs it. Re-running the Electrical
Catalog Browser install in prefixes/inv is left to the coordinator.
