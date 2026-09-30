# 048 Cold-start hang: first document never opens while trial dialog shows "We're having trouble"
Status: fixed in harness (no Wine bug; app race, see Cause) · Owner: 048 worker · Branch: fix/048 (wt/048 on integ 481a8f5f6ab, build wt/048-build) · Found in: invscen suite, integ 228616fa47c (prefixes/inv, :98)

## Symptom
Cold `tools/invscen/run.sh all`: hello `connect` PASS (19 s), `first part view`
(Documents.Add of a visible part) TIMEOUT 120 s; the next scenario (tlb) hung at connect.
Screen: Inventor Home with the AdskLicensingAgent trial popup (WebView2) showing
"We're having trouble. Please try again later." instead of "Welcome to your trial".
Inventor.exe at 0 % CPU. Clicking the popup's X turned the main window black
(no repaint), popup stayed drawn. No crash dump, no winedbg. After
`wineserver -k` the next cold start passed; the "trouble" popup appeared again
mid-suite without blocking anything, so the popup alone is not the cause.

## Next
If it recurs: winedbg backtrace of all Inventor threads (main thread wait target),
`xwininfo -root -children` for the popup's owner. Probably a race between the first
document's view creation and the licensing popup (029/037 area).

## Coordinator note
Possibly the shared licensing service effect (one AdskLicensingService on
127.0.0.1:39683 serves all prefixes; see CODE.md): if another prefix's services
restarted at that moment, the trial popup could fail like this. Unverified.

## Harness note (2026-09-28)
Closing the trial welcome via its X button made the first part's ActiveView
null on 2/2 cold runs; WM_CLOSE doesn't (harness now uses WM_CLOSE). Possibly
related to this hang. Also: after hello closes its document, the Home page
stays blank (WebView2 redraw?) — unfiled.

## Repro (088, 2026-09-29)
Closing the trial popup with WM_CLOSE only after it was visible 15 s (page loaded), during hello's
connect: `first part view` TIMEOUT in 1 of 10 cold starts on integ 9c1eea5beac (build/), 3 of 11 on
fix/088 (the early close, current harness: no hang in this session's 5 cold starts, 1 with a part view). Hung state: main window black
where the popup was, Home not repainted; Inventor main thread in futex wait, all threads sleeping,
0 % CPU; `winedbg attach` never completes (loader lock held?). Harness change reverted.

## Deadlock (048 worker, 2026-09-30, build/ = integ 481a8f5f6ab)
Repro: local Harness.cs change (close the popup only after it was visible 15 s), loop
`wt/048-loop.sh N [BUILD]` (fresh inv3 each run, stops at the first hang). Hang 1 of 12.
Capture: `wt/048-capture.sh` (gdb helpers now in tools/gdb/: winesyms.py = ELF + Wine PE symbols
despite the preloader; sehbt.py: `sehbt` = PE backtraces from each syscall frame via the modules'
.pdata (walks native Autodesk/coreclr code), `stackscan LO HI`, `sehfrom SLOT`).
Rate on build/: 4 hangs in 63 fresh starts (1/12, 3/50 + 1 traced pass); all 4 the same cycle.
- loader_section owner = main thread (RecursionCount 1), 10 waiters.
- Main thread: harness Documents.Add, dispatched re-entrantly (apartment_wndproc) from a managed
  message pump inside WM_CREATE of the Home MDI child's FwWebBrowser child (WebView2 creation),
  itself inside FwUI's "hidden modal dlg" (DialogBoxIndirectParam) -> RxFileRef -> ... -> FcSrv ->
  Fc.dll delay-load helper -> LoadLibraryExA(Uc.dll) -> Uc.dll DllMain (static init:
  IOPxXtraTreeList::Initialize -> CUIxMessageTerm -> FwUICommon -> rm.dll
  RMxMessageSender::Send -> RMxMessageDispatcher::_FindMessageEntry -> virtual call into managed
  (C++/CLI) code -> JIT -> coreclr EnterCriticalSection(L) (owner = thread 0xefc).
- Thread 0xefc: System.Net.Http managed code -> coreclr assembly load (holds L; both threads share
  the same coreclr call chain, i.e. both are binding) -> LoadLibraryExW("...\10.0.9\
  System.Net.NetworkInformation.dll", LOAD_WITH_ALTERED_SEARCH_PATH) -> LdrLoadDll -> loader lock.
- Others waiting on the loader lock: thread exits (LdrShutdownThread), new threads (loader_init),
  coreclr GetProcAddress (LdrGetProcedureAddress).

## Windows ground truth: tests/loader_dllmain/ (thread B's call while thread A sits in a DllMain)
| B does | Win11 | Wine |
|---|---|---|
| LoadLibrary of a new native DLL | blocks | blocks |
| LoadLibraryEx of a new IL-only (R2R) DLL | blocks | blocks |
| LoadLibrary of an already loaded DLL | returns | blocks |
| GetModuleHandle | returns | blocks |
| GetProcAddress | returns | blocks |
| LoadLibraryEx AS_DATAFILE | returns | blocks |
| CreateThread (thread runs) | blocks | blocks |

## Cause: application lock-order race, not a Wine loader divergence
- Cycle: main thread holds the loader lock (first document -> delay-load of Uc.dll -> its DllMain
  static init calls into C++/CLI managed code, e.g. via TreeControlDotNET.dll) and needs coreclr's
  binder lock; a .NET TP worker (the Home page's first HttpClient, System.Net.Http ->
  System.Net.NetworkInformation) holds the binder lock across LoadLibraryExW of a new R2R assembly
  and waits for the loader lock. Same coreclr offsets in all 4 hangs.
- Windows blocks that LoadLibraryExW too (table above: new native and new IL-only loads wait for
  another thread's DllMain), so the same interleaving deadlocks on Windows. Running managed code
  from DllMain is the app's bug (classic mixed-mode loader-lock problem).
- Why the harness hits it: with the late popup close, the harness's first Documents.Add is
  dispatched re-entrantly while Inventor, still in the popup's hidden modal loop, builds the Home
  MDI child (WebView2 creation pumps messages) and Home's .NET code starts its first HTTP request.
  Trace of a passing run: main's loader-lock span for the Uc.dll graph 52.06-52.32 s, TP worker's
  NetworkInformation load at 52.33 s (it had been waiting). Wine's slower DLL loading widens the
  window (~250 ms for the Uc.dll graph), but that is a probability factor, not a semantic bug.
- Wine divergences seen as bystanders (not in the cycle): GetProcAddress, GetModuleHandle,
  LoadLibrary of a loaded DLL, AS_DATAFILE loads and RtlPcToFileHeader (C++ throw) wait for another
  thread's DllMain on Wine, not on Windows -> draft 092.
- Harness: see "Harness policy" below.

## Harness policy (tools/invscen/Harness.cs)
Close the popup once visible 15 s (avoids 088's agent spin) and let connect wait until it is gone,
so the first Documents.Add can't land in Inventor's startup (Home + first HttpClient). The watcher
ignores FwUI's untitled 2x2 "hidden modal dlg" (visible while the popup is up; dismissing it ends
Inventor's modal state and failed connect in 17 of 29 runs of the first attempt).
| harness | build/ | fresh starts | 048 hangs |
|---|---|---|---|
| late close (15 s), no wait | integ 481a8f5f6ab | 63 | 4 |
| late close (15 s), no wait | integ f720de9f520 | 40 | 2 |
| late close + connect waits (committed) | integ f720de9f520 | 40 | 0 |
One run of the first wait attempt (hidden dialog not exempt yet) had ActiveView null (037-like), 1/29.
