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
