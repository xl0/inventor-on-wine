# 108 wined3d's command stream thread spins after every queue drain (~15 % of an idle WebView2 GPU process)
Status: fixed (awaiting review) · Owner: issue-108 worker · Branch: fix/108 f92b2850e73 (wt/108 on integ b6dab895f3e, build wt/108-build) · Found in: 091 (inv3, integ bd4259df723)

## Symptom
WebView2 GPU process animating a spinner (48 frames/s, Vulkan renderer): ~30 % of its perf samples are in the
wined3d_cs thread, half of them in `YieldProcessor()` of `wined3d_cs_run()` (cs.c, WINED3D_CS_SPIN_COUNT
2000). Counted per 5 s: ~2500 spins cut short by a new command, ~700 full spins ending in a wait, i.e. ~3
full + ~10 partial spins per frame. 2000 `pause` = 28 us on this Xeon (Sapphire Rapids class; ~140 cycles
each), so ~0.2-0.3 ms of spinning per frame, 1-2.5 % of a core per WebView.

## Upstream
- Spin count unchanged since f032ccc2715 ("Reduce CS spin count to 2000", 2018). 2023 changes (MRs 3212/4625,
  "Reduce command stream CPU usage"; c73359902c7 thread-ID alerts instead of events; ea48ac41e61 client-side
  wined3d_pause) left the CS spin alone. Nothing newer on origin/master (2026-10-01), no MR, no tunable
  (registry / WINE_D3D_CONFIG). Alert = futex in-process (NtAlertThreadByThreadId), no server call.
- DXVK's CS thread doesn't spin at all (condvar), but it hands over chunks of commands, wined3d single packets.

## Measurements (inv3/:100, RTX 6000 Ada, wined3d-vk; debug counters: inst/108/debug.patch)
- Wake latency after an alert (pinned): 4-16 us; unpinned ~5-10 % of wakes take 130-500 us (C6 exit).
- Edge spin.html (Chromium, 48 fps): per 5 s ~2000-2700 drains, ~700 full spins (misses), hits spread
  log-uniformly over 0.25-30 us (2^9..2^16 TSC cycles at 2 GHz). Misses come after PRESENT, DRAW and
  SET_RASTERIZER_STATE (~1 each per frame); the previous outcome barely predicts the next (P(miss|miss) 43 %,
  P(miss|hit) 32 %). No queries pending.
- tests/cs_spin_bench.c (D3D11 loop: FPS, draws/frame, optional sync readback, GAP_US busy work before each
  draw): at 48 fps one drain/frame, always a miss; the spin is negligible next to the frame's work (~0.1 %).
  Throughput sensitivity, unthrottled 1000 draws/frame, fps by spin count (pinned):

  | gap before each draw | spin 2000 | 300 | 0 |
  |---|---|---|---|
  | 8 us | 107 | 109 | 96 |
  | 15 us | 61.0 | 57.6 | 57.2 |
  | 25 us | 37.9 | 36.4 | 36.4 |

  i.e. a blanket shorter spin costs 4-11 % when the application thread submits with 5-30 us gaps (each
  wait then costs it an alert, ~1 us, plus the CS wake). That rules out just lowering the count.
- Inventor orbit (debug counters on the fix): phases with mostly long-spin hits (score stays long) and
  phases with ~8000 waits/s, half of them woken within 25 us; no queries pending either.

## Fix (fix/108): `wined3d: Spin for the full count in the CS thread only while that avoids waits.`
Adaptive spin limit in wined3d_cs_run(): full WINED3D_CS_SPIN_COUNT (2000) while it pays, else
WINED3D_CS_SHORT_SPIN_COUNT (200). Score in [-8, 8], starts at 0 (= long, upstream behaviour):
+1 for a hit after more than the short count; -4 for a long spin that ends in a wait; in short mode
each wait is timed (QueryPerformanceCounter around the wait, only on the sleep path): woken within
WINED3D_CS_SPIN_TIME (25 us, roughly a full spin here; shorter CPUs bias towards long = conservative)
counts +1, else -4. Weights ~ cost ratio: a wasted full spin (~25 us) vs an avoided wait (~5 us CPU +
the submitter's alert).

## Results (pinned A/B, interleaved, base = same build without the patch, dll swap by rename)
- Edge spin.html, GPU process wined3d_cs thread (perf, 3 pairs): CPU 2.55/2.31/2.55 -> 1.75/1.71/1.87 %
  of a core (-30 %), share of its samples in the spin loop 32 -> 7 %, context switches 709 -> 721-772/s.
- Inventor + Assistant idle (3 pairs, inst/108/invruns.txt): wined3d_cs threads of the prefix median
  3.8 -> 3.3 %, all WebView2 22.0 -> 21.7 %, prefix total 36.5 -> 36.6 % (noise ~ +-1.5).
- cs_spin_bench gap 8/15/25 us (throughput): base 106.9/61.0/37.9 fps, fix 108.0/61.8/37.6.
  Sync readback latency (1x1 copy + Map) p50 ~0.9 ms both; unthrottled 200 draws same or better.
- uilat (inst/108/uilat, 3 restarts per variant with rubber/orbit/pan + 4 with orbit/pan only, interleaved):
  rubber step p50 4.7 vs 4.9 ms, drag 100 fps / lag p50 2.1 vs 2.2 ms; pan 59.6 fps both, step p50 10.4 vs
  10.2 ms; orbit median 46.6 (17 runs) vs 48.3 (17 runs), step p50 ~17 ms both. Degraded orbit runs on both
  (base 34.6, 40.0; fix 15.6, 5.7), see Open.
- Conformance (d3d11 d3d10core d3d10 d3d10_1 d3d9 d3d8 ddraw dxgi wined3d, both arches, same build with
  base vs fix dll): identical results, 45 units, pass=36 fail=8 crash=1 in both.
- Regression-relevant: every wined3d user with the CS thread (CSMT on by default): games and d3d apps whose
  application thread submits with gaps of a few to tens of us between commands; those keep the full spin as
  long as most full spins catch a command. Startup behaviour is upstream's (score 0 = long).

## Open
- Orbit outliers: fix-o1's 2nd orbit ran at steady ~190 ms/frame, not reproduced in 14 later fix orbits
  (also at load 80); base-r3/fix-r3 had irregular 100-400 ms stalls at the same time (host load). Debug
  counters show no pending queries in Inventor, so the short mode can't add 100 us query-poll timeouts;
  attributed to the environment, but unexplained.
- Side observation: cs_spin_bench "sync" copying from the swapchain back buffer right after Present takes
  ~14 ms per Map (wined3d-vk); a plain texture ~0.9 ms. Not investigated.
