# 130 Inventor rubber-band drag gets slower with session age (Xorg-bound)
Status: draft · Found in: soak #4 (inst/soak/2026-10-02/uilat.log, uilat-fresh/)

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

## State at pause (2026-10-02 ~18:20, before reboot)
Root cause found, fix written (WIP commit on fix/130 in wt/130), A/B with the fast repro done; real-session aging
on the fix build and tests still to do.

**Mechanism.** 077 (c0ba235c6c0, winex11 WM size-move detection) runs `wm_size_move_begin()` on every ConfigureNotify
that window_update_client_config() sees as a WM change. With openbox (reparenting WM) that is every move+resize of a
managed window: the real ConfigureNotify comes first with the old frame position ("mismatch config"), then the
synthetic one (trace: inst/130/trace-rubber.txt). Inventor's Line-tool dynamic-input popup is moved+resized on every
rubber step, so each frame did XISelectEvents(root, +RawButtonRelease), XQueryPointer, XISelectEvents(root, -...).
XISelectEvents on the root makes Xorg recompute the deliverable XI event masks of the whole window tree
(RecalculateDeviceDeliverableEvents -> xi2mask_merge; the top Xorg symbol in perf, ~30 %): cost is linear in the
number of windows with XI2 selections on the display (every Wine whole window selects touch events).
Measured with inst/130/xisel.py (us per root XISelectEvents+XSync): empty Xvfb 25; +100/300/1000 stand-in windows
670/1950/6400 (inst/130/xiwins.py); :100 with a fresh Inventor 470-530, after one suite + samples 1540.
**What accumulates:** hidden top-level "Static" windows (1678/1919/1920/2304 x 884, no owner, no children) of
Inventor's WebBrowser WebView2 GPU process: ~3 per document, 320 after 1 suite + samples + ~15 uilat setups
(inst/130/topcount.c lists top-levels per process/class/size; X side: inst/130/xres.py, inst/130/xcount.sh).
Their whole windows (unmapped, root children, with client window children) carry XI2 masks, so the root select grows
more expensive with session age. Whether Windows keeps them too is unchecked (Chromium-side; not the regression).

**Fast repro:** fresh Inventor + inst/130/xiwins.py :100 N hold (N stand-in XI2 windows), then inst/130/m.sh TAG
(uilat rubber on inv3/:100, summary line in inst/130/log.txt). Assistant panel must be closed (it repaints at 47 Hz and
spoils settle/fps; closed in inv3 now, persists).
| build | stand-ins | rubber fps | Xorg % |
|---|---|---|---|
| build/ 04293594c50, aged (1 suite + samples) | 1000 | 36.1 | 36 |
| build/ fresh | 1000 | 54.6, 52.2 | 33-34 |
| build/ fresh | 0 | 115.5, 119.2 | 13-14 |
| fix fresh | 0 | 119.2, 119.5 | 8-9 |
| fix fresh | 0/250/500/1000/2000/3000 | 119.5/119.5/119.5/119.5/115.5/119.2 | 9..18 |
Real aging on build/: per-scenario rubber after each suite scenario 100-119 fps (noisy), after samples 109 / 104
(Xorg 23-26 %); the soak (32 suites) reached 40-50.

**Fix (WIP):** wm_size_move_begin() queries the grab state (wm_grab_active: keyboard grab or XQueryPointer buttons)
first and selects the raw button release on the root only when a grab is active, then re-checks (no lost release).
Common case: one XQueryPointer, no root selects (record: XInputExtension:46 gone from Inventor's top requests).
Other candidates (120 fake moves, 085b, X resource leaks of Inventor's connection) not needed to explain it:
Inventor's X resources are flat (GC 125->146, WINDOW 68->72 over a suite).

**Next steps:** (1) inv3 is on wt/130-build now (.update-timestamp updated by wineboot); real aging on the fix build:
`WINE_BUILD=wt/130-build inst/130/age.sh f1 hello tlb ... export` + `age.sh f2 samples`, twice, rubber must stay ~119;
optionally base curve `inst/130/curve.sh basecurve 0 250 500 1000 2000` on build/ for the plot. (2) Turn the WIP into a
proper commit (subject e.g. "winex11: Only select raw button releases on the root during a WM grab."); check 077's
tests/sizemove_scen.sh (Mod4/Alt drag, keyboard move) still gets ENTER/EXITSIZEMOVE. (3) Regress winex11
(user32 win/msg, winex11 is not unit-tested directly). (4) Note in notes/wine/window-surfaces.md or user-perf.md:
XISelectEvents on the root costs O(windows with XI2 masks) in Xorg; never do it per event.
