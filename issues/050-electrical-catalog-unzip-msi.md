# 050 Electrical Catalog Browser installation fails in AceUnzipZipFiles
Status: open (draft, low) · Owner: - · Branch: - · Found in: local Inventor 2027.1 installation

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

## Investigation
Root cause unknown; 1603 alone does not establish a Wine bug.

- Reproduce the CA MSI failure in a disposable prefix, preserving its payload
  layout and ODIS-supplied properties; collect verbose MSI logging.
- Inspect the MSI CustomAction table and identify the API failure behind the
  unzip action and return code 1359. Check source/destination access and archive
  integrity before attributing it to Wine.
- Reduce to a standalone repro if possible; obtain Windows ground truth if
  required. Do not rerun MSI repair/uninstall against the user's live prefix.

Workaround: omit Electrical Catalog Browser unless its functionality is needed.
