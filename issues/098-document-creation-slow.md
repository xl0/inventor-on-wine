# 098 First part view and new documents are 5-15x slower than Windows
Status: partly fixed (fix branch) · Owner: worker-098 · Branch: fix/098 (wt/098 on integ 2cfee5f132e) · Found in: invscen suite (inv, integ 2cfee5f132e)

## Symptom (Wine vs VM, invscen step times)
- hello "first part view" (first Documents.Add of a visible part after start): 13.5 s vs 0.9 s
- asm "new assembly": 2.8 s vs 0.4 s; drawing "new drawing": 3.4 s vs 0.5 s; drawing close 1.0 vs 0.2
- part "reopen" 1.7 vs 0.5; export "STEP export" 1.4 vs 0.2; script "iLogic add rule" 1.8 vs 0.6

## Task
Profile document creation (first and subsequent): DLL loading (count, time, loader lock after
092), graphics device/swapchain/shader setup (vkd3d-shader compile of OGS effects? caching),
window creation, registry/file access, .NET JIT. Separate first-view one-time costs (shader
compile, DLL loads — can they be cached like on Windows?) from per-document costs. Fix Wine-side
causes; prove with interleaved A/B on one prefix.

## Status
Three fixes on fix/098 (build wt/098-build, prefix inv4/:101): docbench first part view
14.6 -> 12.8 s (suite hello 14.4 -> 11.2), first asm 3.2 -> 2.5, first drawing 2.9 -> 1.7,
warm documents ~1.2 -> ~0.95 s, STEP export 1.4 -> 0.4, part reopen 1.7 -> 1.2 (A/B below).
Remaining first-view cost is mostly per-server-call latency (asset loop, registry, mutexes),
see "Remaining".

## Method
- `tools/invscen/docbench.cs`: INVSCEN_N rounds of Documents.Add(visible)+Close per type (part asm drw);
  round 1 after a fresh Inventor start = one-time costs, later rounds = per-document.
  INVSCEN_SYNC=C:\t\sync098 + `wt/098-sync.sh OUT N TRACER` attaches a tracer around each add
  (tracers: wt/098-perf.sh, wt/098-req.sh = server request stats via the debug patch
  tools/wineserver-reqstats.patch (SIGHUP dumps per-thread request counts/time), wt/098-gdb.sh,
  wt/098-tools/bp.sh = gdb + tools/gdb/bpbt.py: PE backtraces at a unix-side Nt* function).
- `wt/098-switch.sh BUILD` restarts inv4 on a build.

## Findings (integ 2cfee5f132e, inv4)
- The VM reference is not a cold start: the VM Inventor session is long-running, so its
  "first part view 0.9 s" is a warm document. Wine warm (round 2): part/asm/drw 1.2 s each.
  One VM docbench right after a fresh VM Inventor start (097 had just started it, possibly
  busy): part 4.0, asm 2.8, drw 2.5 s. Wine cold: part ~14.5, asm ~3-4.5, drw ~3-4.5 s.
- First part view (~14.5 s) on Wine: main thread ~12 s CPU, ~180k server round trips (~20 us
  each here) of which:
  - ~54k from ntdll alloc_tls_slot: every DLL with a TLS directory walked all ~500 threads
    with NtGetNextThread + NtQueryInformationThread + NtClose (219 DLLs, 316 MB load in this step);
  - ~45k registry (enum_key/open_key/get_key_value): windowscodecs CreateComponentEnumerator
    re-enumerating HKCR\CLSID\{CATID_WICMetadataReader}\Instance for every PNG metadata block
    (gdiplus PNG decode of UI images), ~800 enumerations; ~330 per later document;
  - ~21k mutex select/release (no ntsync on this 6.8 kernel: in-kernel with ntsync).
- 3.2 s of the first part view the main thread joins an adskassetapi worker (asset library):
  xirang maps one file 15218 times (CreateFileMapping, MapViewOfFileEx, CloseHandle,
  DuplicateHandle, UnmapViewOfFile, CloseHandle = 8 server calls). tests/mapcycle_perf.c: 250 us
  per cycle on Wine vs 49 us on Win11; server map_view/unmap_view scanned the ~1550-view list.
- Shader compilation is not significant: OGS keeps compiled effects on disk
  (%TEMP%\ogs\<id>\Shader\...\*.fxo), so D3DCompile/vkd3d-shader HLSL compiles happen only on a
  cold cache; device + swapchain + first shaders take <1 s (Vulkan instance/device/swapchain
  creation ~0.35 s in the NVIDIA driver); no vkd3d-shader or driver pipeline compile shows in perf. .NET JIT runs on a background thread (~8 s CPU) and is
  not on the main thread's critical path (DOTNET_ReadyToRun=0 / EnableWriteXorExecute=0 change nothing).

## Fixes (fix/098)
- 0ed662ef06c ntdll: Keep a list of threads with TLS data instead of enumerating threads.
  (+ kernel32 loader test: a thread that existed before the load sees the DLL's TLS data;
  Win11 x86_64/i386 0 failures; fails on Wine if other threads are skipped.)
  Review fold-in: the TEB's TLS array pointer is read once and replaced by compare-and-swap: a
  self-terminated thread keeps its entry and a new thread may reuse and clear its TEB meanwhile
  (fault on NULL + index, also on the base build). Review repros (scratchpad tls/stress3.c,
  large-TLS DLL + self-terminating threads): base faults, fix 3/3 clean on x86_64 and i386;
  stress4.c (TLS data check from short/long-lived threads during loads) 0 bad on both arches.
  Threads killed by another thread keep their entry until the TEB is reused; later TLS loads
  allocate (leaked) data for them.
- 095c04829f8 server: Look up memory views in a tree. (list kept for debug-event load order)
- 7856919f576 windowscodecs: Cache the component lists of CreateComponentEnumerator.
  Re-read on WICComponentEnumerateRefresh only. tests/wic_enum.c: 3 us per metadata-reader
  enumeration on Win11 (45 readers) vs 2330 us on Wine before, 3.8 us after. Reviewer's Win11
  probe (decoders/format converters, wicreg.c): a component registered while the process runs
  doesn't appear in it until WICComponentEnumerateRefresh (a new process sees it); one removed
  from the registry keeps appearing until a refresh. Wine with the commit matches for all four
  categories. (My own metadata-reader probe registration was never accepted by Win11.)
No protocol change.

## A/B (inv4, interleaved, 3 runs each, fresh Inventor per run, docbench INVSCEN_N=3; medians, s)
| step | base 2cfee5f132e | fix/098 |
|---|---|---|
| add part 1 (first view) | 14.6 | 12.8 |
| add asm 1 | 3.2 | 2.5 |
| add drw 1 | 2.9 | 1.7 |
| add part 2/3 | 1.2 | 0.95 |
| add asm 2/3 | 1.25 | 1.05 |
| add drw 2/3 | 1.05 | 0.8 |
Raw: wt/098-runs/ab1/.

## Remaining (first part view on fix/098, ~12.8 s)
- ~2.8 s: main thread joins the adskassetapi worker: 15218 x (CreateFileMapping, MapViewOfFileEx,
  CloseHandle, DuplicateHandle, UnmapViewOfFile, CloseHandle) = 8 server calls each (~184 us per
  cycle, ~33 us on Win11). xirang's stream read() maps the requested range per call (app design).
  Only fewer/faster server calls help: e.g. get_mapping_info returning the section fd (saves
  get_handle_fd, 1 of 8; protocol change, not done).
- ~100k other main-thread server calls: app registry reads (NtQueryValueKey/NtOpenKeyEx ~10k),
  ADP analytics mutexes (~19k select/release_mutex; ntsync would make them in-kernel),
  icon churn (CreateIconIndirect/DestroyIcon: alloc/free_user_handle + GetCursor ~8k), window
  management. Wineserver is also ~50% busy with msedgewebview2 clients during the step (088).
- CPU: case-insensitive misses scan whole directories (find_file_in_dir, ~0.4 s: DLL search
  path probes in Inventor\Bin), DLL image reads (219 DLLs, 316 MB, ~0.4 s), Vulkan
  instance/device/swapchain creation (~0.35 s, NVIDIA driver), .NET JIT (background thread).

## Suite A/B (inv4, interleaved, 3 runs each, fresh Inventor; "hello part", 60 s idle, then
"drawing script export" in the same Inventor; medians, s; raw wt/098-runs/suite2/)
| step | base | fix/098 | VM ref (warm) |
|---|---|---|---|
| hello first part view | 14.4 | 11.2 | 0.9 |
| part new part | 1.3 | 0.9 | 0.5 |
| part reopen | 1.7 | 1.2 | 0.5 |
| drawing new drawing (first drawing) | 3.8 | 2.2 | 0.5 |
| drawing reopen | 1.2 | 0.7 | 0.6 |
| drawing close | 0.3 | 1.1 | 0.2 |
| export STEP export | 1.4 | 0.4 | 0.2 |
| script iLogic add rule | 1.8 | 1.8 | 0.6 |
Drawing close is bimodal in both builds (0.2-0.3 or 1.1-1.2 s per run: A 0.2/0.3/1.2,
B 1.1/1.1/0.2), not a regression signal. Without the idle gap (suite1, back-to-back
scenarios) some later steps (drawing views, imports) were 0.2-0.3 s slower on fix/098: the
shorter first view leaves Inventor's startup background work running into them.
iLogic add rule is unchanged (not analysed).

## Tests
- kernel32:loader Win11 x86_64 17775 / i386 17672 tests, 0 failures; Wine both arches 0 failures.
- windowscodecs (all units) and gdiplus on Wine: 0 failures.
- tools/regress.sh (ntdll kernel32 kernelbase windowscodecs gdiplus psapi dbghelp user32 ole32
  oleaut32 msvcrt ucrtbase; 332 units) vs deps/regress/2cfee5f132e: 1 worse, FLAKY (rerun after the fold-in: same)
  (x86_64 kernel32:debugger, base re-runs fail the same way).

## Bisect of the "first part slower on integ" A/B (inv, 2026-10-01)
Coordinator's A/B (docbench first part 9.8 -> 11.3 s on 4f92c92ace1 vs 2cfee5f132e) does not reproduce.
Variants: A = 2cfee5f132e, M = e92bc658688 (097 only), B = build/ 4f92c92ace1. Interleaved A,M,B x4, fresh
Inventor per run, docbench INVSCEN_N=3, host load 5-27 (medians, s; raw wt/bis-runs/r2/, script wt/bis-run.sh):
| step | A | M | B |
|---|---|---|---|
| first part | 9.8 | 9.3 | 7.95 |
| first asm | 2.4 | 2.2 | 1.95 |
| first drawing | 2.45 | 2.4 | 1.95 |
Spread within a variant <= 0.7 s; B fastest in 4/4 rounds. 097 gains ~0.5 s, 098 another ~1.3 s: no culprit.
An earlier 3-round attempt (wt/bis-runs/r1) ran while another agent's regress held the host at load 60-120:
first part 8-18 s, 5 of 9 runs hit the "no ActiveView" flake (037); discarded. The earlier 9.8 vs 11.3 was
most likely host load. Lesson: check /proc/loadavg (< ~25) before an A/B.
