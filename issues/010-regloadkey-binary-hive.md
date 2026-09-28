# 010 RegLoadKey can't load binary (regf) hives → Inventor Core install "Error 4000"
Status: open (draft) · Owner: - · Branch: - · Found in: Inventor web installer, prefix `inv`, integ + fix/008 (wt/008-integ-build)

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
