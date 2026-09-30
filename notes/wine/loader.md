# ntdll loader locking — checked at integ f720de9f520 (+ fix/092)

- `loader_section` (PEB->LoaderLock) serializes loads, DllMain calls (process/thread
  attach/detach), unloads. Windows blocks the same set: a new DLL load, CreateThread (thread
  start runs THREAD_ATTACH), LoadLibrary / GetProcAddress of a module whose DllMain hasn't
  finished. Ground truth: `tests/loader_dllmain/` and issue 092.
- fix/092: `ldr_data_lock` (SRW) guards the load/memory order lists, hash table, base
  address tree and DDAG dependency lists; writers hold loader lock + SRW exclusive, readers
  either lock. Lock order loader_section -> ldr_data_lock, never the reverse.
  Lookups without the loader lock: RtlPcToFileHeader, LdrFindEntryForAddress,
  LdrGetDllFullName, LdrGetDllHandleEx, LdrLoadDll/LdrAddRefDll/LdrUnloadDll/
  LdrGetProcedureAddress on "ready" modules (LDR_PROCESS_ATTACHED set, LoadCount != 0).
  Pre-092 Wine took the loader lock for all of these (GetProcAddress, GetModuleHandle,
  every C++ throw via RtlPcToFileHeader waited behind any DllMain).
- LoadCount is a SHORT changed from both sides: `update_load_count()` (CAS). Lock-free callers
  never revive a count-0 module nor drop the last reference. `build_module` resets LoadCount
  to 1 after snapping imports (drops cyclic-import refs), so lock-free addrefs are only done on
  attached modules.
- `cached_modref` is written by concurrent lookups: read it once per lookup (a second read
  returned another module and lost refcounts; `tests/loader_dllmain/stress.c` found it).
- `find_forwarded_export` resolves forwards without the loader lock only to ready modules that
  are ntdll/kernel32 or already a dependency of the exporter; otherwise the caller retries under
  the loader lock (it tells the modes apart with RtlIsCriticalSectionLockedByThread).
- `open_dll_file` with a NULL mapping pointer = lookup by full name only (no file I/O);
  LdrLoadDll's lock-free attempt uses it so new loads by path don't open the file twice.
- Test DLLs whose DllMain runs test code: kernel32/tests/loader.c `create_entry_point_dll`
  (entry point = jump thunk into the test exe).
- After rebuilding ntdll, kernel32:loader's `test_dll_file("ntdll.dll")` compares the loaded
  ntdll with the prefix's system32 copy: `echo 0 > $WINEPREFIX/.update-timestamp; wine wineboot -u`.
