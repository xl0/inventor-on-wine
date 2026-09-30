# 088 Idle Inventor keeps wineserver and WebView2 processes busy
Status: fixed (awaiting review; partial, see Left) · Owner: 088 worker · Branch: fix/088-idle-cpu (wt/088 on integ 9c1eea5beac, build wt/088-build) · Found in: 057 and 081 workers (inv3/inv4, integ 3951ce31e31..9c1eea5beac)

## Symptom
- With Inventor idle at Home (no command running), the prefix's wineserver sits at 30–50 % CPU
  (057, inv3) or ~12 % (081, inv4), and the Autodesk Assistant pane's msedgewebview2 processes
  at ~12 %.
- openbench's Test Station reference walk is bimodal on the same build: 1.7–2.3 s or 3.6–3.7 s
  (VM 1.2–1.7 s). 081 suspected the idle load; unconfirmed.
- Matters for laptops (battery, fan) and for UI latency.

## Task
Measure idle CPU per process on Wine vs the VM (the VM's Inventor needs a free licence seat; if it
is blocked, use Edge/WebView2 idle on the VM for the WebView part). Find what the wineserver is
doing (request mix: `WINEDEBUG=+server` sampling, perf on wineserver) and what the WebView2
processes spin on (timers, polling waits, message loops, GPU process fallbacks). Fix Wine-side
causes. Check whether the openbench bimodality follows the idle load.

## Findings
- VM idle (Inventor at Home, licence dialog behind an Explorer window, 60 s sample, % of one core):
  Inventor 2.8, msedgewebview2 2.7 + 0.6, AdskIdentityManager 4.0, AdskAccessCore 0.6,
  AdskAccessUIHost 0.4, AdskLicensingAgent 0.4, dwm 0.2.
- Wine inv3 (wt/088-build = integ 9c1eea5beac), Inventor at Home, Assistant pane open, % of one core:
  after an invscen run: AdskLicensingAgent 51 (one thread), wineserver 52, AdskIdentityManager 17,
  Assistant WebView2 GPU 18.5 + renderer 13.5, Inventor 2.1, winedevice 1.0.
  Agent stopped: wineserver 10, IdentityManager 11, WebView2 GPU 16.6 + renderer 11.7.
- Tool: per-process/thread CPU from /proc: `tools/idlecpu.sh PREFIX_DIR [SECS] [threads]`;
  request mix per client: strace the wineserver's reads, map pipe inodes to client fds.

### 1. AdskLicensingAgent spin = harness artifact (webview/webview init pump)
- Thread loops PeekMessage -> WM_APP (0x8000) posted to its own `webview_message` window -> repost,
  ~6800/s (send_message + get_message), i.e. ~50 % agent + ~40 % wineserver.
- browser_native.dll (Autodesk, webview/webview C++ lib): engine ctor -> add_user_script():
  AddScriptToExecuteOnDocumentCreated, then `do { dispatch(set flag); pump until flag } while (!done)`.
  The completion handler never fires because the popup's WebView2 is gone (no browser process left).
- Cause: Harness.cs Welcome() WM_CLOSEs the trial popup as soon as it is visible, i.e. often during
  WebView2 init (white phase, 085). Launch without the harness: agent idle (0.7 %); WM_CLOSE after
  the page has loaded: still idle. Harness fresh starts: 1 of 4 spun.
- Harness fix tried and reverted: closing only after 15 s visible (page loaded) avoids the spin but
  reproduces 048 (first part view hangs, main thread in a futex, winedbg can't attach): base build
  1 of 10, fix build 3 of 11 such starts. Harness left as is (early WM_CLOSE); the spin, when it
  happens, adds ~50 % agent + ~40 % wineserver to that session. Coordinator's call.
- Not verified on Windows (VM Inventor not usable; would need a WebView2 test closing the controller
  while AddScriptToExecuteOnDocumentCreated is pending).

### 2. openbench bimodality = host C-state wake latency, not idle load
- Mode is per client process (each run.sh), constant within it; flips between runs of one Inventor session.
- Does not follow the agent spin (a2: spin + fast 1.7 s; a3/a4: no spin + slow 3.7 s).
- Host: Xeon w9-3595X, powersave, C6 exit latency 290 us. Pin wineserver + Inventor + client
  to CPUs 40-43: always slow (3.7-4.0 s x3); same + nice-19 spinners on the siblings 100-103
  (cores stay out of C6): always fast (2.1-2.3 s x3). Unpinned: 1.8-2.0 or 3.4-3.6.
- So per-call cost = handoffs x wake latency; fewer sleep/wake handoffs per COM call (081) is the
  Wine-side lever. Benchmarks on this host need pinning + sibling spinners (or equal conditions).

### 3. Timer granularity (AdskIdentityManager polling)
- 5 "Request: HeartBeat/IsLoggedIn" threads: non-blocking recv, then Boost.Thread interruptible_wait
  (IdIPCServer.dll: CreateWaitableTimer + SetWaitableTimerEx(1 ms) + wait + close), ~950 loops/s each.
- tests/wait_granularity.c on Win11: default process 1 ms waits (Sleep, WaitForSingleObject,
  MsgWait, SleepConditionVariableSRW, waitable timer) take 15.5-15.7 ms even though
  NtQueryTimerResolution reports cur 1 ms (another process raised it); HIGH_RESOLUTION timer 1.6 ms;
  after timeBeginPeriod(1) all ~1.5 ms. Wine: all ~1.05 ms regardless.
- Wine: MsgWaitForMultipleObjects returns ~1.3 ms early (1 ms -> 0.02 ms, 20 -> 18.7 ms; Windows
  1 -> 15.9, 20 -> 31): win32u wait_message builds an absolute deadline from NtQuerySystemTime
  (CLOCK_REALTIME_COARSE) while the server compares with gettimeofday. Fixed (below).
- Emulating Windows' 15.6 ms default granularity would cut IdentityManager ~15x; big behaviour
  change, draft 089.

### 4. dcomp recomposed and presented every target every 16 ms
- Any WebView2/Edge GPU process, even for a static page, presented ~48 frames/s via Wine's dcomp
  composition thread (wined3d_cs: get_window_parents/rectangles/offset + get_visible_region per
  present; Xlib re-reads ~/.Xdefaults on each new DC, see 091).
- Edge, static page, idle: Wine GPU 4.7 % + browser 1.2 %; VM 0.00 % everywhere.
- Fix: compose only after Commit() (global serial, shared visuals cross devices) or when a content
  swapchain's GetLastPresentCount() changed; idle with MsgWaitForMultipleObjects instead of Sleep so
  the GDI-path composition gets flushed (window surfaces flush when a thread idles in a message
  wait; the old constant redraw hid that: dcomp tests 816/853/869/874/898 failed without it).
- After: Edge static page GPU ~0.2-1.3 %, browser 0.2-0.6 %; updates (2 Hz counter) shown;
  covering/uncovering keeps the content (061 offscreen re-present). Inventor, same state, 60 s:
  the two non-animating WebView2 GPU processes 4.3 + 4.7 % -> 1.2 + 1.1 %.

### 5. Assistant pane: page animates, Wine pays more per frame (draft 091)
- Assistant URL in its WebView2 History: ase-cdn.autodesk.com/adp/ad-csi-panel-web/r1/index.html;
  outer page has a MUI indeterminate spinner beside/under the content iframe that keeps running.
  Same URL in Edge (spinner visible): Blink work equal on Wine and VM (CDP Performance: 49 vs 64
  style recalcs/s, main thread 5.7 vs 4.6 %), but Wine renderer ~17 % + GPU ~21 % vs VM ~2-4 % +
  0.2-1.3 % (Windows CPU accounting undercounts short bursts: VM numbers are lower bounds).
- DevTools can't attach to Inventor's WebView2: WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS and the
  HKCU ...\Edge\WebView2\AdditionalBrowserArguments policy are both ignored by Inventor's loader.
  Edge (tools/edge.sh install) + `--remote-debugging-port` + node 22 WebSocket works.

## Fix (fix/088-idle-cpu)
- dcomp: Compose only after a commit or a new swapchain frame.
- win32u: Don't let MsgWaitForMultipleObjects() time out early. (test in user32:msg)
No protocol change. Tests: user32:msg new check passes on Wine (fails on unfixed win32u: 10 x 1 ms
waits in 997 us) and VM (VM's 5 other msg failures pre-existing, unrelated lines); dcomp 753/0 both
arches. regress dcomp user32 win32u d3d11 dxgi vs build/ (same integ): 0 worse of 58.

## Before / after (inv3, Inventor launched directly, popup and Assistant open, 60 s, % of one core)
| process | integ | fix/088 | VM |
|---|---|---|---|
| wineserver | 17.4 | 16.0 | - |
| AdskIdentityManager | 21.2 | 20.6 | 4.0 |
| Assistant WebView2 GPU + renderer | 24.9 + 19.4 | 27.2 + 21.2 | 2.7 + 0.6 (both wv2) |
| other WebView2 GPU (agent popup, Inventor web) | 4.3 + 4.7 | 1.2 + 1.1 | - |
| Inventor.exe | 2.1 | 2.3 | 2.8 |
| harness-induced agent spin (when it happens) | 51 + ~40 wineserver | same | - |

## Left
- 089 timer granularity (IdentityManager), 091 Chromium per-frame cost (Assistant spinner),
  090 Edge browser crash, harness popup timing vs 048.
- dcomp still polls GetLastPresentCount every refresh period (one wakeup/16 ms per device).
