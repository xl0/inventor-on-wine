# Reparse points (junctions, NT symlinks)
Checked against wine-src integ 38e4c1c00ce.

- Storage: `server/fd.c:set_reparse_point` (FSCTL_SET_REPARSE_POINT) writes the
  raw REPARSE_DATA_BUFFER to xattr `user.WINEREPARSE` and renames the Unix file
  or dir to `name?`. So a junction `Current` is an (empty) Unix dir `Current?`
  plus the xattr. This is by design, not a broken name.
- Lookup: `ntdll/unix/file.c:lookup_unix_name` tries `name`, then `name?`, and
  `resolve_reparse_point` follows the xattr data (mount point / symlink tags).
- `name?` without the xattr is a plain dir: every path through it fails with
  STATUS_OBJECT_PATH_NOT_FOUND ("File not found" from `net start`).
  Copying a prefix without xattrs (`cp -r`, `rsync -a` without `-X`, tar
  without `--xattrs`) silently breaks every junction. Use `cp -a` / `rsync -aX`.
  Check: `python3 -c 'import os,sys; print(os.listxattr(sys.argv[1]))' 'X?'`
  (getfattr isn't installed on the server).
- Needs a filesystem with user xattrs (ext4 here); Windows' buffer bytes are
  stored verbatim (052: byte-identical to `fsutil reparsepoint query` on the VM).
