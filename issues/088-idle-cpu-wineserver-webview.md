# 088 Idle Inventor keeps wineserver and WebView2 processes busy
Status: open (draft) · Owner: - · Branch: - · Found in: 057 and 081 workers (inv3/inv4, integ 3951ce31e31..9c1eea5beac)

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
