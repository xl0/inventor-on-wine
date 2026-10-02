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
