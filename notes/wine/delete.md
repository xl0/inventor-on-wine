# Delete semantics (kernelbase DeleteFileW/RemoveDirectoryW, server/fd.c)
Checked against wine-src master 4e819f054dd + fix/054. Ground truth: `tests/delete_posix.c`.

- Windows 10+/11 DeleteFileW and RemoveDirectoryW behave like
  FileDispositionInformationEx DELETE|POSIX_SEMANTICS (no IGNORE_READONLY): the name goes
  away when the deleting handle closes, even if other (FILE_SHARE_DELETE) handles stay open.
  Plain FileDispositionInformation / Ex DELETE / FILE_FLAG_DELETE_ON_CLOSE keep the old
  "unlink on last close" semantics. Wine matches this since fix/054.
- Server: `set_fd_disposition` stores flags in `fd->closed->disp_flags`;
  `inode_add_closed_fd` unlinks POSIX deletes at once (also with byte-range locks held,
  keeping the fd open so the Unix locks survive), others at the last close of the inode.
- Mappings: every section blocks non-POSIX deletes (STATUS_CANNOT_DELETE); POSIX deletes are
  blocked only by image sections, or by any section with FORCE_IMAGE_SECTION_CHECK.
- Known gaps (Wine): FileStandardInformation.DeletePending is always FALSE; a pending
  (non-POSIX) delete doesn't make opens by name fail with STATUS_DELETE_PENDING; undelete on a
  handle of a POSIX-deleted file succeeds (Windows STATUS_FILE_DELETED); a change notification
  on a removed directory isn't signalled; read-only directories can be removed (Windows:
  ERROR_ACCESS_DENIED).
- Risk: the immediate unlink ignores errors. On Unix filesystems that refuse to unlink open
  files, the name stays for good (old code retried at last close). NFS silly-renames instead.
