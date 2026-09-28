# 051 msi: RemoveFiles deletes pre-existing empty directories of the whole Directory tree
Status: fixed · Owner: worker-051 · Branch: fix/051-msi-remove-created-folders (5cf7b917647) · Found in: 050 repro (AceInvAddIn-ca.msi rollback), integ 228616fa47c

## Symptom
A failed (rolled-back) install of `AceInvAddIn-ca.msi` in a fresh prefix deleted
`C:\users\Public\Documents` (existed before, empty). Next run the
`COMMONDOCSFOLDER` AppSearch (HKLM Shell Folders "Common Documents", directory
locator) failed and the package installed into `C:\Documents\`.

## Evidence
`+msi,+file`: during rollback RemoveFiles, after the component loop, RemoveDirectoryW
on `...\Inventor Electrical Library 2027\`, `...\Inventor 2027\` (x3),
`C:\users\Public\Documents\Autodesk\`, `C:\users\Public\Documents\`.
The package has component `UPI2_COMP` with Directory_ = TARGETDIR (attrs 388) and
no CreateFolder rows. `ACTION_RemoveFiles` (dlls/msi/files.c) calls
`remove_folder(comp->Directory)`, which recurses into all child folders of the
Directory table and removes every empty, non-persistent one — for TARGETDIR that
is the whole tree, including pre-existing folders like COMMONDOCSFOLDER.

## Task
Windows ground truth first (small test MSI: component in TARGETDIR, a pre-existing
empty dir listed in the Directory table below it; install+uninstall, and a
failing-CA rollback). Windows is expected to remove only folders it created / the
component's own folder. Fix in dlls/msi if confirmed.

## Windows ground truth (Win11 VM, msi tests action.c `test_remove_existing_folders`)
Probe package: components in `msitest\new` (file), `msitest\existingcomp` (file),
`msitest` (no keypath, CreateFolder on `msitest\existing`); failing type-19 CA
after InstallExecute for rollback.
- Windows records every folder it creates in
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Installer\Folders`
  (value name = path with trailing `\`, REG_SZ; data "" for InstallFiles-created,
  "1" for CreateFolders-created, not modelled) and deletes the value on removal.
- Uninstall and rollback remove only those folders: a pre-existing (empty)
  component dir, a pre-existing CreateFolder dir, a pre-existing parent, and an
  empty dir created by someone else after install all stay. Installer-created
  ones (incl. parents) are removed when empty.

## Fix
`create_directory()` (files.c) registers each folder it creates in the Folders
key; `remove_folder()` / `remove_persistent_folder()` go through
`msi_remove_created_folder()`, which only removes registered folders and drops
the value. Products installed by older Wine have no entries, so their created
folders stay behind empty on uninstall (no data loss).
Not fixed (pre-existing, unrelated todo): Wine doesn't walk up to remove an
installer-created parent that isn't a component dir (`test_create_remove_folder`).
Tests: action unit passes on VM (x64/x86) and Wine (x64/i386), new test fails 5x
on unpatched master; regress `^(msi|setupapi)$` 40/40 pass, 0 worse vs master and
integ baselines. End-to-end run of AceInvAddIn-ca.msi in a scratch prefix stops
at LaunchConditions (needs the ODIS/Inventor context), so not re-verified there.
