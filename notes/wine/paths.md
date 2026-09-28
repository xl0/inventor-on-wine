# DOS paths (ntdll path.c)
Checked against wine-src 4e819f054dd + fix/035.

- `ntdll/path.c:RtlIsDosDeviceName_U` is the single decision point for DOS
  device names: RtlGetFullPathName_UEx and RtlDosPathNameToNtPathName_U
  call it, kernelbase CreateFileW/FindFirstFileExW/GetFileAttributesW/
  QueryDosDeviceW too.
- Windows 11 rules (ground truth `tests/dosdev_name.c`, issue 035):
  - CON, AUX, PRN, COM1-9, LPT1-9, CONIN$, CONOUT$ are devices only as the
    whole relative path ("con", "COM1:"); "c:con", ".\con", "C:\dir\con" are
    files. NUL is a device as the last element of any path except UNC,
    `\\.\` and `\\?\` ("C:\dir\nul" → `\\.\nul`, fails if dir is missing).
  - No extension: "con.iam", "nul.txt" are files (Win10 and older: devices).
  - Allowed tail: one ':', then dots/spaces, then one ':' and spaces.
  - COM/LPT take superscript ¹²³ as digits; 0 is not a device.
  - `\\.\con` → RtlIsDosDeviceName_U 0 on Win8+ (Wine still 8,6).
- Wine has no NTFS stream support: "file:" / "file:stream" become literal
  file names (Windows: ERROR_INVALID_NAME / a stream) — issue 038.
