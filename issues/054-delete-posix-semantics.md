# 054 DeleteFile/RemoveDirectory: name stays while other handles are open (no POSIX delete semantics)
Status: fixed · Owner: worker 054 · Branch: fix/054-posix-delete · Found in: samples campaign, integ 38e4c1c00ce (prefixes/inv3)

## Symptom
The samples scenario restores its work tree with .NET `Directory.Delete(dir, true)`.
On Wine this failed with IOException 0x80070091 "Directory is not empty".
- Everything below was deleted except an empty `2022\Models`.
- Inventor had a directory handle open on `Models` (the active project's workspace).
  inv3's wineserver held an fd on `.../samples/2022/Models`.
- The same restore passes on the VM while Inventor has that project open.

## Windows ground truth (`tests/delete_open_dir.c`, Win11 VM vs Wine)
The probe builds base\sub\f.txt, opens one handle (all share modes incl. FILE_SHARE_DELETE),
then calls DeleteFile(f.txt), RemoveDirectory(sub), RemoveDirectory(base).

| handle held during the deletes | Windows 11 | Wine |
|---|---|---|
| none | all succeed | same |
| directory handle on sub (FILE_LIST_DIRECTORY) | all succeed, sub gone at once | sub still exists; base fails 145 (ERROR_DIR_NOT_EMPTY) |
| FindFirstChangeNotification on sub | all succeed, sub gone at once | same as above |
| open file sub\f.txt | all succeed, f.txt gone at once | f.txt still exists; sub and base fail 145 |

Windows 10 1709+ deletes with POSIX semantics: the name goes away when the
deleting handle closes, not when the last handle closes. Wine keeps the name
until the last handle closes.

## Cause (suspected)
kernelbase `DeleteFileW` / `RemoveDirectoryW` use plain FileDispositionInformation.
Windows uses FileDispositionInformationEx with FILE_DISPOSITION_POSIX_SEMANTICS
(falling back when the FS refuses). server/fd.c already unlinks at once for
POSIX-semantics dispositions (`inode_add_closed_fd`).

## Task
Confirm with ntdll tests which disposition flags Windows' DeleteFileW/RemoveDirectoryW
effectively apply (black-box: NtQueryInformationFile on another handle, rename/
create-same-name after delete). Then switch kernelbase to the Ex path, and check the
server side for directories and for handles opened without FILE_SHARE_DELETE.
Impact: apps that delete trees while something watches them (Inventor projects,
.NET Directory.Delete) fail with ERROR_DIR_NOT_EMPTY. The harness works around it
(samples.cs mirrors in place).

## Findings (worker 054)
Windows 11 ground truth: `tests/delete_posix.c` (VM, NTFS %TEMP%).
- DeleteFileW ≡ FileDispositionInformationEx DELETE|POSIX_SEMANTICS (no IGNORE_READONLY,
  no FORCE_IMAGE_SECTION_CHECK); RemoveDirectoryW likewise. Evidence: data-mapped file
  (view + mapping + file handle open) is deleted by DeleteFile and by Ex DELETE|POSIX, but
  refused (STATUS_CANNOT_DELETE) by Disposition TRUE, Ex DELETE and Ex DELETE|POSIX|FORCE_IMAGE;
  read-only file → ERROR_ACCESS_DENIED; image sections always refuse.
- After a POSIX delete the name is gone at once: GetFileAttributes ERROR_FILE_NOT_FOUND, not
  listed, CREATE_NEW / CreateDirectory of the same name succeed. Other handles keep working
  (read/write ok); they see FileStandardInformation DeletePending=1, NumberOfLinks 0
  (hard link: the other link survives, handle sees links 1, pending 1).
  Undelete on such a handle → STATUS_FILE_DELETED; creating a file relative to a deleted
  dir handle → STATUS_DELETE_PENDING.
- Non-POSIX pending delete (Disposition TRUE, Ex DELETE, FILE_FLAG_DELETE_ON_CLOSE): name
  stays listed until the last handle closes; GetFileAttributes → ERROR_ACCESS_DENIED.
- Handles without FILE_SHARE_DELETE still block: ERROR_SHARING_VIOLATION.
- Byte-range locks held by another handle don't block (name gone at once).
- Change notifications on the dir and on the parent fire; they don't block removal.
- Read-only directory: RemoveDirectory → ERROR_ACCESS_DENIED (Wine: succeeds; not touched).

Wine before: server already implements POSIX semantics for the Ex call (1ccd037e00c,
Joel Holdsworth 2023, for msys2/cygwin unlink), but kernelbase never used it, the server
deferred the unlink when the inode had locks, and refused data mappings for POSIX deletes.
No upstream MR for DeleteFile/RemoveDirectory POSIX semantics found.

## Fix (fix/054-posix-delete, on master 4e819f054dd)
1. server: Fail disposition of special files with STATUS_INVALID_PARAMETER
   (keeps DeleteFile("nul") at ERROR_INVALID_PARAMETER as on Win11; pipes/mailslots are
   STATUS_INVALID_PARAMETER on Windows too, nul is STATUS_INVALID_DEVICE_REQUEST at NT level).
2. server: Allow POSIX deletion of files mapped as data (image sections still refuse).
3. server: Unlink POSIX-deleted files at once even with locks held.
4. kernelbase: DeleteFileW/RemoveDirectoryW use Ex DELETE|POSIX_SEMANTICS (no fallback: in
   Wine both classes take the same server path). Removes 9 todo_wine in ntdll file tests that
   already expected this behaviour.
Tests: ntdll file (data mapping, FORCE_IMAGE, lock), kernel32 file (recreate name after
DeleteFile with a handle open), kernel32 directory (RemoveDirectory with a change
notification). All pass on Win11 VM (pre-existing unrelated failures ntdll file.c:6903,
kernel32 file.c:439/6926 also fail with master's tests) and on Wine x86_64/i386.
`tests/delete_open_dir.exe` now matches Windows. regress (ntdll kernel32 kernelbase msvcrt
ucrtbase shell32 msi setupapi + advapi32 ole32 scrrun shlwapi cmd xcopy msvcp140 msvcr100
msvcr90 cabinet mscms wininet, 354 units) vs master baseline: 0 worse.
Remaining gaps and risks: notes/wine/delete.md.

## Verified in Inventor (build c09f08e4924, inv3)
.NET Directory.Delete(C:\t\samples\2022, true) with that samples.ipj the active project
(watched by Inventor) now succeeds; tests/delete_open_dir.exe matches Win11 in all 4 cases.
