# 098 First part view and new documents are 5-15x slower than Windows
Status: open (draft, perf) · Owner: - · Branch: - · Found in: invscen suite (inv, integ 2cfee5f132e)

## Symptom (Wine vs VM, invscen step times)
- hello "first part view" (first Documents.Add of a visible part after start): 13.5 s vs 0.9 s
- asm "new assembly": 2.8 s vs 0.4 s; drawing "new drawing": 3.4 s vs 0.5 s; drawing close 1.0 vs 0.2
- part "reopen" 1.7 vs 0.5; export "STEP export" 1.4 vs 0.2; script "iLogic add rule" 1.8 vs 0.6

## Task
Profile document creation (first and subsequent): DLL loading (count, time, loader lock after
092), graphics device/swapchain/shader setup (vkd3d-shader compile of OGS effects? caching),
window creation, registry/file access, .NET JIT. Separate first-view one-time costs (shader
compile, DLL loads — can they be cached like on Windows?) from per-document costs. Fix Wine-side
causes; prove with interleaved A/B on one prefix.
