# 052 AdskLicensing "Current" link created as a plain dir named "Current?"
Status: open · Owner: - · Branch: - · Found in: prefixes/inv (real install), also inv2/inv3 (copies)

## Symptom
The Autodesk licensing installer (AdskLicensing 16.6.0.16341) registers
AdskLicensingService with ImagePath
`C:\Program Files (x86)\Common Files\Autodesk Shared\AdskLicensing\Current\AdskLicensingService\AdskLicensingService.exe`.
On Windows `Current` is (presumably) a link/junction to `16.6.0.16341\`. Under
Wine the directory holds `16.6.0.16341\`, and an EMPTY plain directory whose Unix
name is literally `Current?` (bytes `Current` + 0x3F) — so `net start
AdskLicensingService` fails with "File not found" and the service never runs
from its registered path. (Licensing still worked because something — probably
AdskLicensingAgent — started the versioned exe directly; unverified.)

## Workaround (prefixes/inv2 only, 2026-09-28)
ImagePath repointed to `...\AdskLicensing\16.6.0.16341\AdskLicensingService\AdskLicensingService.exe`;
inv2 is now the licensing host for all prefixes (see CODE.md).

## Task
Find how the installer creates `Current` (VM: `fsutil reparsepoint query`,
`dir /al`; which API — CreateSymbolicLinkW, DeviceIoControl
FSCTL_SET_REPARSE_POINT mount point with "\??\" target, mklink /J via cmd) and
why Wine produces "Current?" (NT→Unix name conversion of a char Wine can't map?
a stray "\??\" or ':' ?). Fix + test (kernel32/ntdll reparse point tests exist).
Installer logs: prefixes/inv ODIS logs, AdskLicensing install log in Temp.
