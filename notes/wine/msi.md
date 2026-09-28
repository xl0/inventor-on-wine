# msi (checked against master 4e819f054dd + fix/051)

- Script execution: `InstallFiles` etc. are scheduled into `SCRIPT_INSTALL` and
  run at `InstallExecute`/`InstallFinalize`. On failure `MSI_InstallPackage`
  sets `need_rollback`, which makes `msi_get_component_action` return
  `comp->Installed` (absent), and runs `SCRIPT_ROLLBACK` — e.g. `RemoveFiles`
  (rollback action of `InstallFiles`) then removes the components' files/folders.
- Folder removal (`files.c:remove_folder`, `action.c:remove_persistent_folder`)
  recurses into all Directory-table children of a component's directory.
  Since 051 only folders registered in HKLM `...\Installer\Folders` (written by
  `files.c:create_directory`, as on Windows) are removed.
- Tests: `dlls/msi/tests/action.c` / `install.c` build packages from idt strings
  (`create_database`); a failing rollback is easiest with `InstallExecute` then a
  type-19 CA (`"fail\tFAIL\t4600"`). Must run elevated (winrun is).
