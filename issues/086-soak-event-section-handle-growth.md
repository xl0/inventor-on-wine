# 086 Long sessions: unnamed Event and Section handles grow ~200 per suite
Status: fixed (Wine part; rest app or 087) · Owner: worker 086 · Branch: fix/086 (153a675bdb2, 2735345e750 on integ ed241c72d09) · Found in: soak #2 (inst/soak/2026-09-29, integ bbc7f82accb)

## Symptom
One Inventor session running `tools/invscen/run.sh all` repeatedly: kernel handles
2.5k → 12.6k over 4 h (+2.25k/h); at the end Event 7886, Section 2202 (Key 391,
Mutant 155 — those were fixed by 067/072). ~190-240 unnamed events per suite,
same rate as soak #1. RSS grows +2.8 GB/h (mappings +3k/h), not explained by COM
stubs (flat since 072). Data: inst/soak/2026-09-29/{summary,trends}.txt,
handles-iter*.txt, stubs.txt (not in git); tools/soak/.

## Task
Find who creates and never closes the events/sections (Wine component or the
application), decide whether Windows leaks the same (VM: same scenario loop,
handle counts via Task Manager-equivalent / GetProcessHandleCount — note 075),
fix Wine-side leaks, and see how much of the RSS/mapping growth they explain.

## Method (worker 086)
`tools/soak/handle-trace.patch` (debug-only, not on the branch): wineserver logs every handle to an
unnamed event/section (`LK A pid handle type obj request creator-pid`, `LK C` on close; server started
by hand with WINE_LEAK086=1, stderr to a file); kernelbase CreateEvent*/CreateFileMapping* in
Inventor.exe log a backtrace (`WINEDEBUG=trace+leak`); combase proxy-manager AddRef/Release history.
`tools/soak/handle-attr.py` replays the server log to the live set and groups by stack. gdb scripts
(inst/086/{pmlist,proxies2,assoc}.py, not in git) list combase proxy managers and rpcrt4 associations.
Suite = `run.sh all` on inv3, ~2.3 min. Backtraces in all processes hung Inventor's start; limit
them to Inventor.exe (notes/wine/debugging.md).

## Findings
Per suite (13 scenarios, no samples), handle deltas:

| | Event | File | Section |
|---|---|---|---|
| Wine integ ed241c72d09 | +196..221 | +38 | +5..6 |
| Wine fix/086 | +117..125 | +39 | +5..6 |
| Windows VM (soak #1 vm/handles-r*.txt, 3 rounds) | +97 (ALPC +2..3) | 0 | +5 |

- AdpIPC.dll (Autodesk analytics, app bug, Windows too): `SenderWin::Exec` does CreateEventW +
  overlapped WriteFile per message and never closes the event. ~72/suite (all in `export`),
  ~21..77 per samples run. Most of Windows' +97.
- combase (Wine, fixed): drawing2's DWG and DXF exports each start an out-of-process translator
  server; DwgTrans.dll keeps ~40 proxies ({8660D691-62BD-11D2-89FB-080009DB78B6}) per server after
  it exits (refcount history: the unmarshal ref is never released - app-held; Windows keeps an ALPC
  connection per dead server). Each proxy's channel cached its own call event: +80 events/suite.
  Now cached per thread (ole32 marshal test: 10 proxies, one call each: Windows +0, Wine +9 → +0).
- win32u (Wine, fixed): NtUserWaitForInputIdle never closed the idle-event handle the server
  returns per call (~4/suite, Inventor waits on every helper it starts). No test: needs a GUI child.
- rpcrt4 (Wine, not fixed): per dead server ~20 pooled client connections, one per interface
  (no alter_context), each a pipe + cached event: +39 events and +39 Files/suite. Draft 087.
- Minor: crypt32 regstore change-notification events from RealDWG's CertCreateCertificateChainEngine
  (~8/suite, not analysed: app not freeing chains/engine or a Wine store ref).
- Sections come from `samples`, not the suite: DlSrv → dvtk `saveToMappedFile` (ViewImpl::computeHL)
  creates a pagefile section per view-compute job for InventorViewCompute.exe (`/meta:HANDLE`;
  the worker DuplicateHandle's it without CLOSE_SOURCE). ~75% are never closed in Inventor
  (816 created, ~620 live after two .idw runs: +479 then +146). Owner is Inventor's job code;
  whether Windows leaks them too is unverified (VM Inventor blocked by "Device limit reached").
  Views of sections: ~760 tmpmap mappings, 82 MB - not the memory growth.
- RSS/mappings: soak #2 per iteration RSS +1.5..1.9 GB and maps +1000..1500 only in samples
  iterations, ~0 in plain suites (flat after samples started failing at iter 21). The handle leaks
  explain none of it; the growth is anonymous memory (3.6 of 4.5 GB RSS after two .idw runs).
  Next: heap vs app caches during samples, Windows samples run once the VM seat is free.

Tests: ole32:marshal (new test_proxy_call_handles) passes on Win11 VM x64 + i386 and Wine x64 +
i386; user32:msg ok. regress combase|ole32|oleaut32|rpcrt4|user32|win32u|shell32|msi|urlmon vs
integ ed241c72d09: 0 REAL, 1 FLAKY (user32:input).

## Soak #3 (2026-10-01, integ 492d5679270, with 086/087 merged)
Per hour, one session 4.1 h (soak #2 in brackets): Event +747 [+1655], File +42 [+300], Key +87 [+70], Section +963 [+540]
(11 samples runs against 6; ~320 per samples run [~366]: the Inventor-side saveToMappedFile leak is unchanged).
End dump: Event 3496, Section 3535, File 330, Key 427, Mutant/Semaphore flat (soak #2 end: 7886 / 2202 / 1492 / 391).
Remaining Event growth ~750/h is not attributed (handle-attr.py not rerun); Section stays the largest and is app-side.
Data: inst/soak/2026-10-01/summary.txt, part3/handles-{start,end}.txt.

## Windows ground truth: samples twice on one session (VM, 2026-10-02)
Same Inventor session (started ~2 h earlier, only light use), `run.sh --vm samples` twice back to back
(9m19s each), Inventor.exe sampled with tools/soak/resprobe.exe (NtQuerySystemInformation handle table,
`dump` for per-type counts; workspace inst/ref086/ not in git):

| | WS | Private (commit) | Handles | Event | Section | File | Thread | IoCompletion |
|---|---|---|---|---|---|---|---|---|
| before | 360 MB | 207 MB | 1539 | 583 | 57 | 175 | 59 | 20 |
| after run 1 | 2761 MB | 3863 MB | 4651 | 2307 | 951 | 254 | 209 | 105 |
| after run 2 | 2583 MB | 5508 MB | 6180 | 3572 | 1209 | 252 | 213 | 112 |

- Windows grows too, and by more than Wine: private bytes +3.65 GB (run 1) and +1.65 GB (run 2) = +5.3 GB
  for two runs (Wine: ~1.8 GB RSS per run). WS stays ~2.6-2.8 GB because Windows trims/pages out; commit
  is the comparable number. So the multi-GB growth per samples run is Inventor's own (caches/leaks), not Wine.
- Handles +3112 (run 1), +1529 (run 2); Event +1724/+1265, Section +894/+258 (soak #3 Wine: ~320 Sections
  per run), Thread +150 then +4 (view-compute worker threads, then flat), IoCompletion +85/+7,
  Key +52/+6, Mutant +62/-1. Run 1 includes first-use costs; run 2 is closer to steady state:
  Event +1265 and Section +258 per samples run on Windows, i.e. the Event/Section leak (saveToMappedFile
  sections, AdpIPC events) is app-side and at least as large on Windows as on Wine.
- Both runs: RESULT FAIL only on Speedometer.ipt "rebuild"/"reopen" volume check (vol 7.0235 vs 7.0199 cm3);
  everything else PASS.
