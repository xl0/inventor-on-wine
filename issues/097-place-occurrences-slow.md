# 097 Placing 200 assembly occurrences is ~4x slower than Windows
Status: open (draft, perf) · Owner: - · Branch: - · Found in: invscen asmbig (inv, integ 2cfee5f132e)

## Symptom
asmbig "place 200 occurrences" (COM API, one part placed 200 times into a new assembly):
Wine 24-40 s (noisy with host load), VM 7.3 s. Soak #1's 074 crash also happened in this step.
081 cut per-COM-call overhead (~103 wineserver requests per call now); this step is still 4x.

## Task
Profile the step on Wine (perf on Inventor + wineserver; WINEDEBUG=+server request mix; per-call
cost), find where Wine time goes (server requests, heap, GDI/USER per occurrence, graphics
updates, file/registry), fix Wine-side causes. Prove gains with interleaved A/B on one prefix.
