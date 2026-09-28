# 051 msi: RemoveFiles deletes pre-existing empty directories of the whole Directory tree
Status: open (draft, low) · Owner: - · Branch: - · Found in: 050 repro (AceInvAddIn-ca.msi rollback), integ 228616fa47c

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
