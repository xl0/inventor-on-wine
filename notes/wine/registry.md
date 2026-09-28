# Registry — checked at wine-11.18-218-g4e819f054dd + fix/010

- The server stores the registry in memory, persisted in Wine's text format
  (`server/registry.c` `load_keys`/`save_all_subkeys`). `load_registry`
  (NtLoadKey) also accepts binary regf hives (`load_hive`, read-only import:
  changes are never written back to the hive file).
- regf essentials (public format docs, e.g. libregf / Suhanov's spec): 4 KB base
  block ("regf", minor @0x18, root cell @0x24, bins size @0x28), cell offsets
  relative to 0x1000, cell = int32 size (negative = allocated) + data. nk/vk
  names are Latin-1 when flag 0x20 (nk) / 0x1 (vk) is set, else UTF-16.
  Values > 16344 bytes in 1.4+ hives go through a "db" cell (segments of 16344).
- Windows `reg save` writes 1.3 hives; RegSaveKeyEx(REG_LATEST_FORMAT) writes
  1.5. Windows silently drops a db value whose last segment cell is < 16344.
- App hives (RegLoadAppKey): `\Registry\A\<luid>`, NtLoadKeyEx(REG_APP_HIVE,
  roothandle); no SeRestorePrivilege; server key flag KEY_APP_HIVE deletes the
  tree when the root key's last handle closes (`key_close_handle`).
- Wine enumerates values sorted by name (`find_value` binary search); Windows
  enumerates in insertion/file order.
- Handy tools: `tests/regloadkey_hive.c HIVE` dumps a loaded hive (names,
  types, sizes, data checksums) for Wine/VM diffing.
- HKCR is only `HKLM\Software\Classes` in Wine (kernelbase `open_classes_root`); Windows
  merges `HKCU\Software\Classes` over it (advapi32 `test_classesroot` todo_wine). Per-user
  registrations (URL protocols, file types written by apps without admin) are invisible to
  AssocQueryString / ShellExecute. Issue 028; `tests/hkcu_proto.c`.
