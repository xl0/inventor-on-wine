# 108 wined3d's command stream thread spins after every queue drain (~15 % of an idle WebView2 GPU process)
Status: open (draft) · Owner: - · Branch: - · Found in: 091 (inv3, integ bd4259df723)

## Symptom
WebView2 GPU process animating a spinner (48 frames/s, Vulkan renderer): ~30 % of its perf samples are in the
wined3d_cs thread, half of them in `YieldProcessor()` of `wined3d_cs_run()` (cs.c, WINED3D_CS_SPIN_COUNT
2000). Counted per 5 s: ~2500 spins cut short by a new command, ~700 full spins ending in a wait, i.e. ~3
full + ~10 partial spins per frame. 2000 `pause` = 28 us on this Xeon (Sapphire Rapids class; ~140 cycles
each), so ~0.2-0.3 ms of spinning per frame, 1-2.5 % of a core per WebView.

## Notes
- Upstream tuning (f032ccc2715 "Reduce CS spin count to 2000", 2018): a game benefits, an idle animation
  pays it per frame. Pause latency varies a lot per CPU (Skylake+ ~140 cycles, Zen ~65), so a fixed count is
  a time budget that differs 2x between machines.
- Options: spin by time (a few us) instead of count; no spin after a present op; measure with a game-like
  load (Inventor viewport uilat orbit/pan, issue 060) and the 091 Edge/Assistant setup.
