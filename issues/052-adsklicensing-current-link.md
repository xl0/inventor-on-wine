# 052 AdskLicensing "Current" link created as a plain dir named "Current?"
Status: wontfix (not a Wine bug: xattrs lost when copying inv → inv2/inv3) · Owner: - · Branch: - · Found in: prefixes/inv2, inv3 (copies of inv)

## Symptom
The Autodesk licensing installer (AdskLicensing 16.6.0.16341) registers
AdskLicensingService with ImagePath
`C:\Program Files (x86)\Common Files\Autodesk Shared\AdskLicensing\Current\AdskLicensingService\AdskLicensingService.exe`.
Under Wine `AdskLicensing\` holds `16.6.0.16341\` and an empty Unix dir
`Current?`; in inv2 `net start AdskLicensingService` failed with "File not found".

## Windows ground truth (VM)
`Current` is a junction: `dir /al` shows `<JUNCTION> Current [...\16.6.0.16341]`,
tag 0xA0000003, substitute name `\??\C:\Program Files (x86)\Common Files\Autodesk Shared\AdskLicensing\16.6.0.16341`,
print name the same without `\??\`, both NUL-terminated in the buffer (length 0x14c).

## How the installer makes it
InstallBuilder installer (AdskLicensing-install.log in inv's Temp) runs
`16.6.0.16341\helper\AdskLicensingInstHelper.exe makelink --path_name <16.6.0.16341> --target_path <...\Current>`
(Go binary; paths with mixed `\` and `/`). `makelink.CreateJunction`:
os.Mkdir(link) → CreateFileW(link, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
FILE_FLAG_BACKUP_SEMANTICS) → DeviceIoControl(FSCTL_SET_REPARSE_POINT,
go-winio EncodeReparsePoint mount-point buffer).

## Cause
Wine stores a reparse point as Unix name `name?` + xattr `user.WINEREPARSE`
(server/fd.c:set_reparse_point; see notes/wine/reparse.md). In prefixes/inv
all three Autodesk junctions are intact, and the xattr is byte-identical to
the VM's buffer:
`AdskLicensing\Current?`, `Program Files\Autodesk\AdskIdentityManager\Current?`,
`Program Files\Common Files\Autodesk\AdpDesktopSDK\bin?`.
In inv2 and inv3 the same three dirs have no xattr: the prefix copies dropped
xattrs, which leaves plain dirs. Wine then can't resolve any path through them.

Repro (`tests/junction_mklink.c`, does the helper's call sequence and then uses
the link): VM and Wine (integ 38e4c1c00ce) print identical results (attrs 0x410,
file attrs through link 0x20, CreateProcess through link ok). A service whose
binPath goes through the junction starts (1053 timeout for the non-service
probe = exe found). After `removexattr` it gives "File not found", the inv2 symptom.
Restoring the xattr fixes it.

## Repair (inv2, inv3)
Copy the xattr from inv (target paths are the same in every prefix). Do it
with the prefix's wineserver running or not (it's a plain file attribute):

    cd /home/xl0/projects/wine/prefixes && python3 - <<'EOF'
    import os
    rel = ['Program Files (x86)/Common Files/Autodesk Shared/AdskLicensing/Current?',
           'Program Files/Autodesk/AdskIdentityManager/Current?',
           'Program Files/Common Files/Autodesk/AdpDesktopSDK/bin?']
    for p in ('inv2', 'inv3'):
        for r in rel:
            v = os.getxattr(f'inv/drive_c/{r}', 'user.WINEREPARSE')
            os.setxattr(f'{p}/drive_c/{r}', 'user.WINEREPARSE', v)
    EOF

Afterwards inv2's ImagePath workaround can go back to the `Current\` path.
Future prefix copies: `cp -a` or `rsync -aX` (not `rsync -a`).
