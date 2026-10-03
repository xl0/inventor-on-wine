# 130 Inventor rubber-band drag gets slower with session age (Xorg-bound)
Status: fixed · Owner: worker-130 · Branch: fix/130 (b34210c5d78) · Found in: soak #4 (inst/soak/2026-10-02/uilat.log, uilat-fresh/)

## Symptom
tools/uilat rubber (Line tool, 480 moves at 120 Hz): fresh Inventor session 118.0 fps, step p50 5.5 ms. In the soak
session: iter 1 (3 min after start, one suite done) 107.7 fps; iter 11 (1.2 h) 42.9 fps; iter 21 (2.6 h) 40.1; iter 31
(3.9 h) 50.1; iter 32 44.5 (shown_moves 173-274 of 480; lag p50 stays 2.4-3.0 ms, i.e. frames are dropped, not delayed).
Xorg CPU during the drag rises 18 % (iter 1) -> 33 % -> 42 % (iter 21/31/32) with Inventor at 21-27 %; fresh: Xorg 17 %.
Orbit (3D, GL path) unaffected: 60 fps, step p50 11-12 ms throughout.
Soak #3 (build 492d5679270) had 119 fps in all 4 sessions/4 h with Xorg 9-15 % (aged-session iter 21/24/33 all 118.5-119.5),
so this is new since then (merged: 109 110 112/111 114 116 107 099 095 077 085b 119 120/122 101 102 104 105 106).
The aborted attempt (0.05 h) gave 106.9 fps, so the loss is already visible after one suite.

## Task
Find what makes Inventor's 2D rubber-band redraw cost Xorg more as the session ages (X request count per op with
`uilat --record` in an aged vs fresh session; candidates among the merged changes touching window/DC/region/update
paths, e.g. 120/122, 101-106). Bisect with soak-like aging: run `run.sh all` 3-5 times, then uilat rubber.

## Root cause
077 (c0ba235c6c0, winex11 WM size-move detection): `wm_size_move_begin()` ran XISelectEvents(root,
+XI_RawButtonRelease), XQueryPointer, XISelectEvents(root, -...) for every ConfigureNotify/GravityNotify that
window_update_client_config() takes for a WM change. With openbox (reparenting WM) that is every move+resize of a
managed fixed-size popup: the real ConfigureNotify comes first, mapped with the old frame position ("mismatch
config"), then the synthetic one (x11drv trace of 30 rubber steps: 30 "config changed"; `tests/r130/moveloop.exe 300
popup` under openbox: 1202 XISelectEvents + 598 XQueryPointer for 300 SetWindowPos). Inventor's Line tool moves and
resizes its dynamic-input popup on every mouse move, so each rubber frame paid 2-4 root selects.

A root XISelectEvents makes Xorg recompute the deliverable XI masks of the whole window tree
(RecalculateDeviceDeliverableEvents -> xi2mask_merge, the top Xorg symbol in perf, ~30 %): linear in the windows with
XI2 selections on the display (every Wine whole window selects touch events). `tests/r130/xisel.py` (us per root
select + XSync): empty Xvfb 25; +100/300/1000 stand-in windows (`tests/r130/xiwins.py`) 670/1950/6400; :100 with a
fresh Inventor 470-550, after one suite + samples 1540, after 12 suites 4800.

**What accumulates:** hidden top-level "Static" windows in the WebView2 GPU process, one per composition swapchain
Chromium ever created (dxgi hack never destroys its backing window; draft [131](131-composition-swapchain-window-leak.md)):
~80 per suite, ~230 per samples run, each with a winex11 whole + client X window. Inventor's own X resources are
flat (GC 125 -> 146, WINDOW 68 -> 72 over a suite). Not involved: 120's fake moves, 085b, cursors, surfaces.

## Fix
fix/130 b34210c5d78 `winex11: Only select raw button releases on the root window during a WM grab.`
wm_size_move_begin() checks the grab state first (keyboard grab of the focused process, or XQueryPointer buttons),
selects the raw release only then and re-checks after selecting, so a release between the two checks is still not
lost (it then ends up as "no size-move", as a release before the select did before). Common case: XQueryPointer
only (2 per popup move+resize under openbox, was 4 selects + 2 queries); the selects happen once per real WM drag.

## Verification
Fast repro: fresh Inventor + `tests/r130/xiwins.py :100 N hold`, then uilat rubber (inst/130/m.sh; all lines in
inst/130/log.txt). The Assistant panel must be closed (it repaints at 47 Hz; closed in inv2/inv3 now).
| build | session | stand-ins | rubber fps | Xorg % |
|---|---|---|---|---|
| build/ 04293594c50 | fresh | 0 | 115.5, 119.2 | 13-14 |
| build/ | fresh | 1000 | 54.6, 52.2 | 33-34 |
| build/ | 1 suite + samples | 1000 | 36.1 | 36 |
| fix | fresh | 0/250/500/1000/2000/3000 | 119.5/119.5/119.5/119.5/115.5/119.2 | 9-18 |

Real aging, both at once (2026-10-03; inv2 on build/ :99, suites only since inv2 has no samples; inv3 on the fix
:100, suite + samples twice); fps / root select cost in us:
| build/ after suite # | 0 | 1 | 2 | 3 | 4 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | again |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| fps | 119.5 | 119.5 | 119.2 | 107.9 | 105.0 | 88.8 | 84.9 | 78.0 | 70.0 | 80.8 | 74.5 | 47.8 | 45.8 |
| select us | 555 | 806 | 1078 | 1351 | 1660 | 2263 | 2524 | 2799 | 3096 | 3445 | 3751 | 4792 | |
| Xorg % | 15 | 17 | 21 | 24 | 24 | 25 | 25 | 26 | 27 | 27 | 27 | 27 | 30 |
(suite 5: 8.2 fps at load 25 with wineserver at 27 %, and one 45.5 after a failed samples start: outliers, the
other prefix ran samples; left out.)
| fix after | start | suite | samples | suite | samples | again | again |
|---|---|---|---|---|---|---|---|
| fps | 119.5 | 119.5 | 119.5 | 119.2 | 116.7 (load 16) | 119.2 | 119.5 |
| select us | 475 | 753 | 1562 | 1772 | 2574 | | |
| Xorg % | 10 | 10 | 10 | 6 | 10 | 10 | 11 |
`uilat --record` in the aged sessions: Inventor XISelectEvents per run 911-2021 on build/, none in the top 30 with
the fix. Standalone (Xvfb + openbox + 1000 stand-ins, moveloop.exe 300 popup; inst/130/ml.sh): build/ 1202
XISelectEvents, 38.2 ms/step; fix 6, 31.2 ms/step (= the probe's own wait).

077's cases, tests/sizemove_scen.sh on Xvfb with awesome 4.3 and openbox, each with and without picom (xrender),
inst/130/sm.sh, logs inst/130/sm/: all as in 077's table in all four setups. modmove 1/1, modresize 1/1,
caption 1/1, click 0/0, quick 5/5, modfirst 1/1 (awesome), kbmove 1/1 and kbresize 1/1 (openbox), selfmove 0/0,
dblclick 1/1 (the loop's own pair only), stale 0/0; POSCHANGED between ENTER and EXIT.
Regress user32 + win32u + winex11.drv + dinput (64 units, both arches) vs deps/regress/04293594c50: 0 worse
(same 6 failing units: user32 msg/sysparams/win; baseline also had a flaky x86_64 user32:input).

Left as is: the XQueryPointer per spurious "WM change" (one round trip, tens of us), and upstream's unconditional
root select in ungrab_clipping_window() (once per ClipCursor release, same Xorg cost).
