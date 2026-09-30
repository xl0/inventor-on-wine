# 092 GetProcAddress, GetModuleHandle, RtlPcToFileHeader wait for another thread's DllMain
Status: open (draft) · Owner: - · Branch: - · Found in: 048 (inv3, integ 481a8f5f6ab)

## Symptom
Wine's loader takes `loader_section` for lookups of already loaded modules, so while any
thread runs a DllMain (or a whole LoadLibrary), other threads stall in GetProcAddress,
GetModuleHandle, LoadLibrary of a loaded DLL, LoadLibraryEx(AS_DATAFILE) and RtlPcToFileHeader
(every C++ `throw`: _CxxThrowException). Windows doesn't block these.
Seen in Inventor's 048 hangs as bystanders: coreclr P/Invoke resolution (GetProcAddress) on
3 threads, IdSDKPlugin C++ throws (RtlPcToFileHeader) on 2 threads, all parked behind the
main thread's DllMain. Not the cause of 048 (that cycle goes through a new-DLL load, which
blocks on Windows too), but any app lock held across one of these calls deadlocks on Wine only,
and every first-use DLL load stalls unrelated threads.

## Windows ground truth
`tests/loader_dllmain/` (build lines in the files; `loader_dllmain.exe MODE [IL_DLL]`, dm_block.dll +
dm_plain.dll next to it): thread B's call while thread A sits in dm_block.dll's DllMain.
| B does | Win11 | Wine |
|---|---|---|
| LoadLibrary of a new native DLL / new IL-only DLL | blocks | blocks |
| CreateThread (thread start) | blocks | blocks |
| LoadLibrary of an already loaded DLL | returns | blocks |
| GetModuleHandle, GetProcAddress | returns | blocks |
| LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE) | returns | blocks |
| RtlPcToFileHeader | returns | blocks |

## Task
Let lookups of fully initialized modules (module list/tree, export lookup without forwarders that
need a load, refcount bump of a loaded module, PC -> module) run without waiting for the
loader lock, as Windows does (it uses a separate module-list lock). Forwarded exports that
need a load and first loads keep the loader lock. Conformance test in ntdll/kernel32 loader tests.
