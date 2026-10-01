# 100 Triage: Wine conformance units that regress.sh classes FLAKY or TIMEOUT
Status: triage done (no fixes) · Owner: - · Branch: - · Found in: tools/regress.sh compare runs

Measured on build/ = integ 2cfee5f132e (wine-11.18-444) via 20-ish isolated runs per unit
(scratch prefixes in /tmp, own Xvfb 1024x768x24 per stream, no WM, regress.sh env: lavapipe,
WINEDEBUG=-all, `module=b`), plus the history of the 30 `deps/regress/*/results.txt` (30 builds, same
harness; "hist" below). Master (wt/regress-master-build, 4e819f054dd, wine-11.18-218) rerun for the
units marked "master: yes". The coordinator rebuilt build/ during the triage; nothing here was
re-measured on the new tip. Network from the host works (test.winehq.org answers 200), so no unit
here is blocked by network. `FLAKY` in compare = unit got worse in one run vs baseline, not worse in re-runs.

## Findings that cover several units
1. **Fixed TCP port 7533 collides between parallel shards.** webservices:proxy, webservices:channel and
   winhttp:notification all bind 127.0.0.1:7533 (`info.port = 7533`, bind failure => `return 1` => test
   says "failed to start test server 0"). regress.sh runs 64 arch-units at once in one network
   namespace. Repro: 10 concurrent proxy runs on 10 prefixes: 9/10 fail. Not Wine, not the test:
   the harness. (httpapi uses ports 50000-52000 with a search loop, so it only flips on rare overlap.)
2. **Native SEGV inside lavapipe (libvulkan_lvp.so) in d3d12 tests, then Wine dies.** mfplat (D3D12 buffer
   tests at mfplat.c ~12000) and dxgi (d3d12 swapchain/fullscreen) end with rc 139 "dumped core",
   no Wine backtrace. gdb: SEGV #1 in libvulkan_lvp.so `mov 0x40(%rsi),%eax` with rsi=0 on a Windows
   thread; SEGV #2 in ntdll.so `server_call_unlocked` (unix/server.c:291 `get_thread_data()` with
   NULL TEB, i.e. the Wine SEGV handler made a server call on a thread with no TEB) => kernel kills the
   process. Likely a foreign/native thread (lavapipe/vkd3d) hitting something. Before the crash the D3D12
   tests read uninitialised texture memory ("Unexpected texture color 0x...", random each run).
   Wine should not die silently on this (the second SEGV is a Wine robustness bug), and the garbage
   texture contents are vkd3d/lvp. Same on master.
3. **No Gecko in the template prefix** => every mshtml test, ieframe:ie/webbrowser, urlmon:url bind to
   `REGDB_E_CLASSNOTREG` (url.c:1834 "binding failed: 80040154") and then wait out their 120 s. Wire
   trace shows the HTTP transfer to test.winehq.org itself works. wine-gecko 2.47.4 (x86/x86_64 msi)
   is not on this machine; installing it needs a download (user's rule: confirm first).
4. **Xvfb mode set**: dxgi (62 deterministic failures: expects 800x600 modes), user32:monitor,
   sysparams, shell32:* etc. are consistent failures from the fixed 1024x768 Xvfb screen/no RandR
   modes. Baseline noise, not flaky. 173 of 1757 units are bad in all 30 runs (list: `awk` over results.txt).

## Harness fixes (done in tools/regress.sh)
Finding 1: units with fixed localhost ports serialize on /tmp/regress-ports.lock (also winhttp:winhttp 7532,
wininet:http 7531, httpapi). Finding 3: Gecko 2.47.4 + `regsvr32 mshtml.dll` in the template (wineboot runs
with mshtml disabled, so without regsvr32 the text/html MIME handler is missing and urlmon/ieframe still
time out); ieframe:ie, urlmon:url and ~25 mshtml/itss/urlmon units pass. Finding 4: Xvfb 1920x1200 set to
1024x768 plus RandR modes (tools/xvfb-modes.c): user32:monitor, dxgi (62 -> 2), ddraw pass.
Always-bad units 174 -> 118, timeouts 5 -> 0. Mode-switch tests (d3d9, ddraw) are now occasionally flaky
under load (missing WM_DISPLAYCHANGE / mode not applied), they failed always before.
`tools/regress.sh unit DLL:TEST -n N` runs a single unit repeatedly.

## Table (sorted by cost x fixability; cost = rate x run time + effect on compare)

"hist" = bad runs/30 in deps/regress (counted by status!=pass; for crash/timeout units bad = not the
usual status). Time = avg unit time in a full run. Hypothesis: a env/our setup, b test race/timing,
c Wine bug, d needs internet.

| # | unit | arch | rate hist / isolated | failing lines / messages | env dependence | master too | hyp | evidence | next step |
|---|------|------|----------------------|--------------------------|----------------|-----------|-----|----------|-----------|
| 1 | webservices:proxy | both | 6/30 x64, 5/30 i386 (+crash 1) / 1 of 20 when another proxy ran concurrently, 0 otherwise | proxy.c:1033 "failed to start test server 0" (once proxy.c:327/350/351/614: got 0x8007274c/0x803d0001/-1 = socket errors after another instance owned the port) | parallel units; none otherwise | yes (4e819 x64 fail 1) | a | proxy.c:1027 `info.port = 7533`, bind failure `return 1` (proxy.c:950-957); same in channel.c:1584, notification.c:2105; 10 concurrent runs: 9 fail | harness: wrap these 3 units (x64+i386) in `flock /tmp/regress-port7533` in run_one (or unshare a net namespace, needs lo up; plain `unshare -rn` breaks wine's prefix ownership check) |
| 2 | webservices:channel | i386 | 3/30 | channel.c:880 (64x) / 857 / 897-920 "got 0x803d0003", "wait failed 258", 0x8007274d | parallel units | n/a (same code) | a | port 7533 (channel.c:1584) | same flock |
| 3 | winhttp:notification, httpapi:httpapi | i386 | 1/30 each | notification.c:2115 "failed to start winhttp test server 0"; httpapi.c:811 "Got error 38" | parallel units | n/a | a | port 7533 (notification.c:2105); httpapi scans ports 50000+ and can overlap | same flock; httpapi: low priority |
| 4 | ieframe:ie, urlmon:url | both | 30/30 TIMEOUT 120 s (4 x 120 s shard time per run; the i386 urlmon:url/ieframe:ie start near the end of the list so they likely set the 250 s wall) | ie.c:199 "unexpected call Invoke_NAVIGATEERROR"; url.c:1834 "binding failed: 80040154" then 493-525 "expected OnObjectAvailable / wait timed out" | no Gecko; network is fine | yes (same 120 s) | a | WINEDEBUG=+wininet: HTTP 200 + chunked body received, test then needs mshtml docobject (CLASSNOTREG) | cheapest: regress.sh skips/short-timeouts them (-t per unit, or a known-timeout list) to cut the tail; real fix: install wine-gecko into the template (ask user for the download); also fixes ~25 mshtml/urlmon/itss units |
| 5 | user32:input | i386 | 10/30 bad (7 crash, 3 fail) / 2 of 20 (both while 100 CPU spinners ran), 0 of 18 on an idle box | crash: `X Error of failed request: BadWindow (X_UnmapWindow) resource 0x400001` after input.c:5121 (all 7 crash logs identical); fail: input.c:1833/1872 mouse-move-point positions, 1879/1885/1895/1901 "cursor didn't change position after mouse_event()", input.c:4004 "SendInput returned 0" (x2), 6407-6432 "got pos (49,51)" | load (CPU contention) triggers the fail form; crash form in full runs only; **sharing one Xvfb between concurrent input tests makes it fail every time** (16/16) | yes (base x64: 1 fail in 7 isolated, 0 else) | c (crash), b/c (fail) | crash: XUnmapWindow with a stale window id; the only XUnmapWindow calls on a non-own window are the shared clip window (winex11.drv/mouse.c:479, :529, `init_clip_window`) - id 0x400001 is the first window of another X client, so it is a window created by a process that exited; fail: send_hardware_message returns STATUS_ACCESS_DENIED when injected input's desktop != thread's input desktop (server/queue.c, send_hardware_message) - not checked which | crash: WINEDEBUG=+x11drv,+cursor on a ClipCursor loop with a process that exits; confirm id with `xwininfo -id`. fail: log the SendInput last error under load |
| 6 | user32:input | x86_64 | 6/30 bad (3 crash, 3 fail) / 1 of 20 | same as above (1833 positions, 1872 "got 61", 4004, X_UnmapWindow crash 3x) | same | yes | c / b | same | same |
| 7 | mfplat:mfplat | both | x64 24 crash + 6 fail /30; i386 21 crash + 9 fail /30; isolated 18/20 and 15/20 crash | lines 1645/1665/1695 "Unexpected hr 0xc00d36bb" (always, MF_E_... missing decoder), 11962-12072 "Unexpected texture color" / "Unexpected dword 0x5555..." (d3d12 uninitialised memory), then rc 139 | lavapipe; no display dependence seen | yes (master x64 9/10 rc 139) | c (lvp/vkd3d + Wine handler) | see Finding 2; failures count varies 3/7/14 so compare calls it worse every time | worker: capture the crashing Vulkan call (VK_LOADER_DEBUG, VKD3D_DEBUG=trace, lvp symbols or `LVP_` env), fix Wine's handler dying in `server_call_unlocked` on a TEB-less thread; ask whether the d3d12 sub-tests can be skipped in the harness |
| 8 | dxgi:dxgi | both | crash 2/30 each, else 62 deterministic failures; 24 s | dxgi.c:3723 window/monitor/client rect (0,0)-(1024,768) vs 800x600 (deterministic); crash: log ends after dxgi.c:5613 "dumped core" | Xvfb has no 800x600 mode | yes | a (fails), c (crash) | same trace type as Finding 2 | the crash: as #7; the 62: add 800x600 to the Xvfb (`xrandr`/`-screen` modes) or accept |
| 9 | kernel32:debugger | x86_64 | 5/30 FAIL (25 pass) / **fails 100 %** in isolation (about 25 runs, also under 100 spinners, taskset-pinned contention on 2 CPUs, /dev/shm prefix; the one pass was the first run right after starting a persistent `wineserver -p`, then 3 fails) and in a kernel32-only regress run | debugger.c:1188 "loaddll on ntdll should appear before exception" + :1257 "Expecting two ntdll instances" (test_debug_loop_wow64, 64-bit wine debugging wow64 msinfo32) | something in a full run makes it pass 5/6 of the time; not reproduced | yes (master fails identically in isolation, 4/4) | b/c | LOAD_DLL events for the 32-bit ntdll come after the first breakpoint when the box is quiet; passing needs the other order | worker: trace the order (WINEDEBUG=+server on the debugger) and decide whether the Wine-side order is wrong (Windows: both ntdlls first) |
| 10 | user32:win | i386 | 3/30 pass, fails 27 with 4 or 2 failures; x64 always 2-4 | win.c:10808/10809 (also 10716/10717, 10689/10690 depending on iteration) "transparent window didn't get WM_NCHITTEST message", "button under static window didn't get WM_LBUTTONUP"; sometimes 4115 "SetForegroundWindow(desktop) error: 1400" | passes ~13 % isolated (2/15), same on master (0/2 pass) | yes | c/b | the test injects a click via SendInput from a helper (win.c ~10780); same injection family as #5 | find why the click is not delivered (X pointer position/XTest without WM?); flaky count 4 vs 2 makes compare flag it |
| 11 | user32:msg | both | 30/30 fail with 1 failure; 1 of 6 isolated had +8 (focus/active lines msg.c:20370-20396 "expected focus ... got 0") | msg.c:5744 "Test succeeded inside todo block: ShowWindow(SW_SHOWMAXIMIZED):overlapped: 35" (deterministic, the 1 failure) | 80 s unit; the +8 appeared while 100 spinners ran | yes | a (todo succeeds without a WM) / b (+8) | log lines | the deterministic 1 is a baseline constant; ignore. The +8 under load: not investigated further |
| 12 | mf:mf | both | fail 30/30 (2-4 failures; sizes 2,3,4) | mf.c:7676 "Unexpected time 1999886" (always, clock halts at 1999886 vs 2000000), mf.c:6313 skip-ish "MP4 media source is not supported" (always), mf.c:1943 "Unexpected hr 0x80070057" and mf.c:7100 "Release returned 1" (intermittent, 2 of 10 isolated) | no | yes | b | 1943: `test_media_stream_send_delayed_samples` removes from a shared collection (E_INVALIDARG, race); 7100 refcount race at shutdown | upstream test race; low value (42 s unit though); fix 7676 first (deterministic) |
| 13 | mfmediaengine:mfmediaengine | x86_64 | 1/30 timeout 120 s / 0 of 25 | log ends in "GStreamer-CRITICAL gst_query_set_uri: assertion gst_uri_is_valid" | no | not seen on master (0 of 5+) | c | hang with no further output | gdb -p when it hangs; low priority |
| 14 | d3d10core:d3d10core | x86_64 | 1/30 timeout | test printed "27385 tests executed ... 0 failures" then hangs at exit | lavapipe | n/a | c | exit hang after the test finished (same lavapipe/vkd3d teardown family as #7/#8) | with #7 |
| 15 | ntdll:exception | x86_64 | 2/30 / 0 of 40 | exception.c:4811 "cs32: ecx fdc042c4 / 00006FFFFDC042C4", :4895 "cs64: wrong fs 0063 / 0000" | no | not seen (0/20 master) | b/c | test_wow64_context resumes a suspended thread, `Sleep(1)`, suspends and compares contexts at random places; ctx taken while the thread is mid mode switch | loop 500 runs under load to get a repeatable failure; 2 s unit, low cost |
| 16 | ntdll:threadpool, ntdll:time | both | fail 24/30 (threadpool x2 arch), pass 6/30 | timing asserts (1 failure) | **passes when the box is quiet** (both pass in a 32-shard run of ntdll/kernel32/user32 only); fail in full runs | n/a | b | host load | ignore or lower load; deterministic count of 1 so compare does not flag |
| 17 | ntoskrnl.exe:ntoskrnl | x86_64 | 9/30 | driver.c:1025 "got 0x102" (wait timeout), 4 failures | load | n/a | b | | low |
| 18 | quartz:filtergraph | both | 30/30 fail with exactly 5 failures, 0 flips | filtergraph.c:4424 "Got hr 0x80040218", 4426, 4436 "Got 0x102", 4440/4441 "Expected MPEG sequence header" | missing gstreamer MPEG decoder | yes | a | identical in all 30 runs and in 10 isolated; never flaky in the history. It appears in compare lists only when a re-run catches a crash/other count | drop from the list |
| 19 | shell32:shelldispatch | i386 | 0/30; 0/20 isolated | - | - | - | - | never failed in the 30 results or 20 isolated runs | not reproducible; a compare-only fluke (re-runs under 32-shard load); drop |
| 20 | misc one-offs | | 1/30 each | d3dx9_35/36/42/43:core (i386+x64 same run, one build), dcomp:dcomp (8 failures, first run only), ntdll:om (om.c:2416 access mask), ole32:clipboard, shell32:autocomplete, winhttp:notification, httpapi | single events | - | b | no repro | ignore |

## Cost summary (full run, 250 s wall, 32 shards)
- urlmon:url + ieframe:ie x2 arches: 480 shard-seconds and a likely 100 s of tail. Always TIMEOUT so never
  flagged FLAKY, but costs wall time. (#4)
- user32:input / user32:win / mfplat / dxgi: flagged in most compare runs, each re-run costs 4 x unit time
  (input 64 s, win 200 s, dxgi 100 s, mfplat 20 s). (#5-#10)
- webservices and winhttp:notification: cheap units but ~1 in 5 runs flagged; trivial fix. (#1-#3)

## Suggested fix workers (order)
1. Harness: flock around the port-7533 units; per-unit timeout or known-timeout skip for ieframe:ie and
   urlmon:url (or gecko in the template after the user agrees to the download).
2. user32:input: X_UnmapWindow BadWindow crash (clip window, winex11) - a Wine bug (hypothesis, unverified) - and the SendInput access-denied under load.
3. d3d12 on lavapipe: SEGV in lvp + Wine handler's second SEGV in server_call_unlocked (mfplat, dxgi,
   d3d10core exit hang).
4. kernel32:debugger wow64 LOAD_DLL ordering (real, deterministic when quiet).
5. user32:win click injection, mf:mf 7676.

## Method notes
- Harness for the isolated runs: /tmp/tri/run.sh (scratch, not committed). Each stream needs its own
  display for user32 input/win/msg: streams sharing one Xvfb made input fail 10/10, which looked like a
  flaky unit until the displays were split.
- A pipe `wine ... | grep` hangs after the debugger test (leaked msinfo32/winedbg holds the pipe); redirect
  to a file like regress.sh does.
- regress.sh has no single-unit mode; `-m '^kernel32$' -a x86_64 -j 8` is the smallest (68 s).
