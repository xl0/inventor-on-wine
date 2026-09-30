# 092 GetProcAddress, GetModuleHandle, RtlPcToFileHeader wait for another thread's DllMain
Status: fixed (fix branch) · Owner: 092 worker · Branch: fix/092-loader-lookup-lock (wt/092 on integ f720de9f520, build wt/092-build) · Found in: 048 (inv3, integ 481a8f5f6ab)

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

## Windows ground truth, extended probe (092 worker, Win11 VM, x86_64 and WoW64 identical)
`loader_dllmain.exe all` (tests/loader_dllmain, DLL build lines in dm.c). Thread A sits in dm_block's
DllMain; `_running` = target is dm_block itself, `_pending` = target is dm_top.dll (imports dm_block:
mapped + snapped, its DllMain not called yet).
| B does | Win11 |
|---|---|
| LoadLibrary new DLL, CreateThread, GetProcAddress forwarder to an unloaded DLL | blocks |
| LoadLibrary loaded DLL (name / full path), GetModuleHandle (name / path) | returns |
| GetModuleHandleEx PIN / addref / FROM_ADDRESS, LdrAddRefDll | returns |
| FreeLibrary with count 2 -> 1 | returns |
| GetProcAddress name / ordinal / forwarder to ntdll (HeapAlloc) / forwarder to a loaded DLL that is not a dependency | returns |
| GetModuleFileNameW, LoadLibraryEx AS_DATAFILE (unloaded -> datafile, loaded -> the module) | returns |
| RtlPcToFileHeader (exe, running, pending module) | returns |
| GetModuleHandle of the running / pending module | returns (handle) |
| GetProcAddress on the running / pending module | blocks until A finishes |
| LoadLibrary of the running / pending module | blocks |
Wine (integ f720de9f520): every row blocks, including GetModuleHandle of the running/pending module.

## Design (fix/092-loader-lookup-lock)
- `ldr_data_lock` (SRW) protects the load/memory order lists, hash table, base address tree and
  DDAG dependency lists. Writers hold the loader lock *and* the SRW exclusively (alloc_module,
  build_module failure, free_modref, add/remove_module_dependency); readers hold either lock
  (shared SRW is enough). Order: loader lock -> SRW; never the loader lock under the SRW, no
  exceptions/DllMain under it. LDR_PROCESS_ATTACHED is set under the exclusive SRW (publishes the
  initialized module). cached_modref is written by concurrent readers: read it once per lookup
  (reading it twice returned a different module -> lost refcounts; found by the stress test).
- Load counts: `update_load_count()` CAS on the SHORT LoadCount everywhere. Lookups without the
  loader lock never revive a count-0 module or drop a count to 0 (min argument); pin = delta 0.
- "Ready" = LDR_PROCESS_ATTACHED and count != 0. Lock-free paths:
  RtlPcToFileHeader / LdrFindEntryForAddress / LdrGetDllFullName (any module);
  LdrGetDllHandleEx (find_dll_file under the shared SRW; addref/pin only of ready modules, else
  redo under the loader lock); LdrLoadDll of a ready module (find_dll_file with NULL mapping =
  lookup only, no file I/O; otherwise the old path); LdrAddRefDll of ready modules; LdrUnloadDll
  when the count stays > 0; LdrGetProcedureAddress on ready modules, forwarders resolved only if the
  target is ready and ntdll/kernel32 or already a dependency (else retry under the loader lock).
  The loader-lock owner keeps the old path in LdrGetProcedureAddress (find_forwarded_export
  switches mode on RtlIsCriticalSectionLockedByThread).
- Remaining divergence: GetProcAddress through a forwarder to a loaded module that isn't a
  dependency yet (first time only) waits; GetModuleHandleEx addref/pin of a not yet initialized
  module waits (Windows unmeasured). LdrEnumerateLoadedModules/LdrQueryProcessModuleInformation
  still take the loader lock (unmeasured).
- Stress: tests/loader_dllmain/stress.c (threads load/free/lookup/call; refcount bugs = crashes or
  modules left loaded).

## Commits (wt/092, fix/092-loader-lookup-lock on integ f720de9f520; after review)
73d9c7b63fb ntdll: Update module load counts atomically.
83d0ff015c2 kernel32/tests: Test which loader calls wait for another thread's DllMain.
0297f605ef0 ntdll: Protect the module lists with an SRW lock.
6af1656cc0f ntdll: Look up and reference initialized modules without the loader lock.
9027d7d86ce ntdll: Look up exports of initialized modules without the loader lock. (held: 048)
Review fixes folded in: ldr_data_lock goes through lock_ldr_data()/unlock_ldr_data(), no-ops once
process_detaching is set (RtlExitUserProcess kills the other threads, which may die holding the
SRW or the futex bucket spinlock of its wakeups: ExitProcess with detach handlers doing lookups/loads
hung 30/30; child-process test "lookups_exit" in the loader test, 2/2 hang before, 0 after);
LdrGetDllFullName copies to the caller's buffer after releasing the lock; lookups return the local
instead of re-reading cached_modref. Not done: LdrGetDllHandleEx name-only fast path with file I/O
under the loader lock: a full-path LoadLibraryEx(AS_DATAFILE) of an unloaded DLL then waits for
DllMain (test row fails; Windows returns), and hits already skip I/O (open_dll_file checks the full
name first), so only misses do I/O under the shared lock, as before.
Reviewer repro (scratchpad ex/): 0/15 hangs each (8 threads, detach doing a new load / lookups),
0/10 with 1 thread; same at 6af1656cc0f.
Test `test_lookups_during_dllmain` (kernel32/tests/loader.c): 18 rows, DLLs generated with an
entry thunk into the test; todo_wine rows flip in the commit that fixes them.

## Results
- kernel32:loader on Win11 VM x86_64 + i386: 0 failures (a separate run had 28/15 failures in
  test_section_access, CreateFile sharing violation, gone on rerun: VM flake, base exe too).
- Wine tip: kernel32:loader 0 failures both arches; tests/loader_dllmain probe matches Windows
  on every row except fwd_loaded (see divergences).
- regress.sh (ntdll kernel32 kernelbase msvcrt ucrtbase msvcp* vcruntime140 combase ole32
  oleaut32 rpcrt4 shell32 advapi32 psapi dbghelp user32 shlwapi setupapi imagehlp version
  wintrust crypt32; 462 units) vs deps/regress/f720de9f...: 1 worse, FLAKY (x86_64 user32:input).
- stress.c (x86_64 + i686, 8-16 threads, 6 x 30 s + shorter): 0 failures after the cached_modref
  fix; before it GetProcAddress on a held module failed (126) a few times per run. Base build
  also 0. Throughput in this churn-only stress is ~15% lower than base (writers wait for
  shared holders; lookups that fall back redo the search); single-threaded equal.
- Inventor inv3 (hello, 4 fresh starts each, measure patch wt/092-measure.patch = LLW/SRW wait
  logging in sync.c): main thread loader-lock waits 538-556 per start, 248-368 ms -> 4-9 waits,
  14-62 ms. Inventor: 0 contended ldr_data_lock waits. Connect 20.6-21.0 s both (dominated by
  the harness waiting out the trial popup and 2 s polling, so no measurable startup change).
  Other threads' loader-lock waits: fewer (~460 vs ~550) but longer (thread starts queue behind
  the main thread's back-to-back loads; sum over parallel waiters, not wall time).

## 048 race with the harness wait disabled (hello, fresh inv3 starts, private no-wait scen-hello.exe)
| build | fresh starts | 048 hangs |
|---|---|---|
| build/ = integ f720de9f520 (A/B interleaved, this session) | 8 | 0 |
| fix/092 tip | 8 (A/B) + 10 | 6 + 4 |
| fix/092 without the GetProcAddress commit (94cc2078a1d) | 8 | 0 |
Captured hang (wt/092-runs/hang-124136): the same 048 cycle (main thread: Fc.dll delay load ->
LoadLibraryExA(Uc.dll) -> DllMain -> managed -> JIT -> coreclr lock; TP worker: System.Net.Http ->
coreclr assembly load -> LoadLibraryExW (new DLL) -> loader lock); no ldr_data_lock involvement.
So lock-free GetProcAddress (Windows behaviour) lets the TP worker reach the coreclr lock during
Uc.dll's DllMain instead of stalling in GetProcAddress first: Inventor's own lock-order bug
(also possible on Windows, which doesn't block GetProcAddress) gets ~10x likelier under Wine's
slow Uc.dll load. The committed harness waits out the popup and avoids it. Commits up to
94cc2078a1d don't change the rate; merging the last commit is a trade-off for the coordinator.
Connect times (2 s polling) unchanged in all variants.
