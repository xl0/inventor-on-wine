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
- HKCR merged view (fix/028, kernelbase): HKCU\Software\Classes wins over HKLM per key and per
  value; enumeration is the sorted union; new keys go to HKLM; handles are tagged `(h & 3) == 2`.
  Windows' user side is `\REGISTRY\USER\<sid>_Classes` (HKCU\Software\Classes links to it); Wine's
  is `\Registry\User\<sid>\Software\Classes`. Before 028 Wine had no merge at all.
  Ground truth: `tests/hkcr_merge.c`, `tests/hkcu_proto.c`.
- COM (combase/ole32, fix/030) keeps a private classes root that ignores RegOverridePredefKey:
  RegOpenUserClassesRoot (tagged, merged) for non-elevated processes, HKLM only when elevated
  (Windows ignores all per-user COM registrations in elevated processes). Wine processes are
  limited unless the manifest asks for admin. Ground truth: `tests/com_peruser.c`.
- The HKCU `Software\Classes\Wow6432Node\{CLSID,Interface,...}` redirection markers are empty
  keys: deleting an "empty" HKCR key can remove them (advapi32 test_redirection did).
- 32-bit view of HKCR: Windows redirects the user side too (`<sid>_Classes\WOW6432Node\CLSID`).
  Wine marks both HKLM and (since the 028 review) HKCU `Software\Classes\Wow6432Node` KEY_WOWSHARE:
  subkeys present there (wine.inf: CLSID, Interface, ...) are redirected, others shared. A
  Wow6432Node without the flag redirects on path traversal only, which splits 32-bit writes/reads.
- wineserver applies Wow64 redirection itself for Wow64 callers without KEY_WOW64_64KEY
  (`Software` -> `Software\Wow6432Node`): reopening an already-resolved key name by absolute path
  from a 32-bit process needs KEY_WOW64_64KEY.
