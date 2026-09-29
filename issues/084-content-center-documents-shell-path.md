# 084 Content Center Files path retains the My Documents shell identifier
Status: open (draft; Wine cause unconfirmed) · Owner: - · Branch: - · Found in: local Inventor 2027.1 file open

## Symptom
Inventor warns when opening a document:

> The following location(s) in your project are currently unavailable.
> If you continue opening the file, you may experience significantly slower
> performance than normal.

The unavailable **Content Center Files** path is:

```text
::{450d8fba-ad25-11d0-98a8-0800361b1103}\Inventor\Content Center Files\R2027
```

![Content Center Files warning](attachments/084-content-center-shell-path.png)

Observed on the local workstation with the native Inventor 2027.1 install,
`prefixes/inv`, Wine `integ 3951ce31e3`
(`wine-11.18-397-g3951ce31e3`), Windows 11 mode. The warning also occurred
on an earlier local build. The latest screenshot was taken while opening an
Autodesk 2022 sample assembly.

The dialog offers OK to continue or Cancel to abort. The advertised slowdown
has not been measured; do not conflate it with the separate desktop/Xorg lag.
This is also separate from the Electrical Catalog Browser installer failure
(050).

## Evidence and uncertainty
- The GUID is `CLSID_MyDocuments` (`wine-src/include/shlguid.h`), not a
  filesystem drive or directory.
- The prefix's `Explorer\Shell Folders` `Personal` value is
  `C:\users\xl0\Documents`; `Explorer\User Shell Folders` `Personal` is
  `%USERPROFILE%\Documents`. The registry itself does not contain the GUID
  in those values.
- At filing, the corresponding filesystem folder
  `C:\users\xl0\Documents\Inventor\Content Center Files\R2027` is absent.
  Missing-directory creation must be investigated alongside path conversion.
- The installed `Default.ipj` has `FolderOptions/ContentCenterFolder` with
  `ContentCenterConfig`, but no explicit path child. This is not proof that
  it is the user's active project; the effective setting's origin is unknown.
- A shell parsing name can legitimately contain this GUID. Its appearance
  alone does **not** prove a Wine API violation or literal filesystem use.
  No trace yet identifies the API/flags Inventor uses to obtain the setting.

## Windows ground truth
Not yet collected; the local workstation has no reference VM.
On the server's Windows reference, compare the same Inventor version with
an unmodified/default project and the equivalent Documents location.
Check both an absent and an existing `R2027` directory.

## Proposed workaround (not yet verified)
Create the real directory and set **Tools → Application Options → File →
Default Content Center Files** explicitly to:

```text
C:\users\xl0\Documents\Inventor\Content Center Files\R2027
```

If the active project overrides it, use **Projects → Folder Options →
Content Center Files** instead. This is a configuration workaround, not a
Wine fix or a way to restore missing component files in an existing assembly.
The coordinator has not changed this setting or created the directory.

## Task
Trace how Inventor obtains the effective Content Center Files path and where
it checks/creates the directory. Separate a persisted bad setting from an
API result produced on every launch.

Once the actual API and flags are known, build a small Windows/Wine probe.
Candidate boundaries include known-folder paths, PIDL display names and
`IShellItem::GetDisplayName`; distinguish filesystem names from shell parsing
names. Existing shell-dialog probes/issues 041 and 043 may help, but this
symptom is not established as either regression.

Match Windows behavior at the faulty boundary if a Wine discrepancy is found;
do not globally rewrite GUID-containing shell names or hard-code Inventor's
path. Preserve the user's active project and open documents during testing.
If this is only an absent prerequisite directory or application configuration,
document that finding rather than proposing a speculative Wine patch.
