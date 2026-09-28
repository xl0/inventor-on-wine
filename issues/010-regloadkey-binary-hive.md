# 010 RegLoadKey can't load binary (regf) hives → Inventor Core install "Error 4000"
Status: fixed · Owner: worker · Branch: fix/010-regf-hive (wt/010, from master; wt/010-integ = integ d4d52733ba7 + fix) · Found in: Inventor web installer, prefix `inv`, integ + fix/008 (wt/008-integ-build)

## Observed
- With 008 fixed, the .adix signature checks pass (LP and Anark packages no
  longer fail), then "Inventor Core 2027" fails: UI "Install error ... Error 4000"
  (dialog left open, not clicked).
  ![Error 4000](attachments/010-error-4000.png)
- Install.log (22:35:19): `MsixCoreLib::WriteAdIXRegistry::WriteRegistries ...
  hrOpenVirtualKey error code: -2147024890` (0x80070006 E_HANDLE), then
  `HandleInstallFailure ... Inventor Core 2027 Error Code: 4000`; rollback noise
  (`CreateStreamOnFileUTF16 ... 0x8BAD0001`, `CreateProcess failed`) follows.
- Only InvCore.adix contains a `Registry.dat` (40 KB, binary `regf` hive);
  InvAnark/InvCoreLP have none and installed fine.
- The upstream MsixCore equivalent (msix-packaging
  `MsixCore/msixmgr/RegistryDevirtualizer.cpp`): enables SeRestore/SeBackup,
  `RegLoadKey(HKEY_USERS, "{guid}", Registry.dat)`, opens `{guid}\Registry`
  (errors other than FILE_NOT_FOUND ignored), then `OpenSubKey` per mapping →
  a NULL root key gives E_HANDLE. Matches the log.

## Windows ground truth / repro
`tests/regloadkey_hive.c` (mingw) with the InvCore Registry.dat
(`unzip InvCore.adix Registry.dat`, from `%TEMP%\{447AF3F0-...}\x64\InvCore\`):
- VM: RegLoadKey 0, `wine008hive\Registry` opens, subkey `MACHINE`.
- Wine: RegLoadKey 1017 (ERROR_BADDB), open → 2.
Wine's server `load_registry` only reads Wine's text registry format.

## Task
Support loading binary regf hives in RegLoadKey (server side, read-only is
probably enough here: MsixCore copies the tree into the real registry, then
RegUnLoadKey). Conformance test with a tiny regf hive.

## Design (worker)
- No regf parsing anywhere in Wine (server, regedit, reg.exe: text/.reg only);
  nothing upstream found. NtLoadKey already hands the server a file handle
  (`load_registry` request), so the server is the natural place.
- server/registry.c `load_registry`: if the file starts with "regf", read it
  into memory and walk it from the root nk cell: root values -> the loaded key,
  subkeys (li/lf/lh/ri lists) -> `create_key_recursive`, values (vk, inline
  data <= 4 bytes, db big-data segments) inserted like the text loader does.
  nk timestamps -> key modif. Every cell offset/size is bounds-checked; visited
  nk cells are marked so cycles/shared cells can't loop; depth capped at 512.
  Bad input -> STATUS_REGISTRY_CORRUPT. Text format still accepted.
- Not done: key classes, security (sk) cells, transaction logs (.LOG1/2),
  writing back (RegLoadKey keys stay in memory as before), RegSaveKey in regf.
- RegLoadAppKey (below): hive under \Registry\A\<LUID> via
  NtLoadKeyEx(REG_APP_HIVE, roothandle); server skips the SeRestore check for
  REG_APP_HIVE and deletes the key when the root key's last handle closes
  (Windows keeps it until all handles in the hive are closed; not modelled).
  Writes are not persisted to the file (as with RegLoadKey in Wine).

## Findings (worker)
- The failing call is actually RegLoadAppKeyW, not RegLoadKey:
  `MsixCoreLib::WriteAdIXRegistry::ExecuteForAddRequest` (adixhandler
  FUN_1808931c0) does `RegLoadAppKeyW(Registry.dat, &hkey, KEY_READ, 0, 0)`
  and walks hkey; Wine's stub returned hkey 0xdeadbeef -> E_HANDLE.
  RegLoadKeyW is imported too (other path). Both needed the regf reader.
- Windows ground truth (VM): InvCore Registry.dat is a 1.5 hive; loaded tree
  dumped with `tests/regloadkey_hive.c` matches Wine key-for-key, value data
  identical (Wine enumerates values sorted, Windows in file order: pre-existing
  Wine difference). `reg save` writes 1.3 hives (big values in one cell),
  RegSaveKeyEx(REG_LATEST_FORMAT) writes 1.5 with db big-data cells whose
  segments are all full 16344-byte cells (Windows silently drops a big value
  whose last segment cell is smaller). Corrupt root cell -> ERROR_BADDB and no
  key left behind. NtLoadKeyEx with roothandle but without REG_APP_HIVE ->
  STATUS_INVALID_PARAMETER_7 (existing ntdll test).

## Outcome
Commits on fix/010-regf-hive:
- server: Support loading binary (regf) hive files.
- kernelbase: Implement RegLoadAppKey. (+ server REG_APP_HIVE flag in
  load_registry, ntdll NtLoadKeyEx roothandle, wow64 handle thunk fix)
Tests: advapi32 registry `test_reg_load_key_hive` (synthetic 1.5 hive: compressed
and UTF-16 names, inline/cell/db big data, root value, corrupt root), existing
RegLoadAppKey/NtLoadKeyEx todo_wines removed. VM x86_64/i386: registry
173589/178033, ntdll reg 28112, 0 failures. Wine x86_64/i386: registry
7223/7621, ntdll reg 3826/5518, 0 failures.
Real flow (prefix inv on wt/010-integ-build): Inventor Core 2027 installs
(Error 4000 gone); bundle ends "Installation incomplete" because the .NET
Desktop Runtime signature check fails -> issue 013 (regression from the integ
RFC 3161 time-stamp verification).
