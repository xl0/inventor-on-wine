# 026 Edge: sandboxed renderer processes are never launched (blank page)
Status: open (draft) · Owner: - · Branch: - · Found in: Edge 154 in inv-vm (wt/017-build)

## Symptom
Edge (msedge.exe, not WebView2) shows its toolbar, but every page stays blank white.
No --type=renderer process is ever created: +process shows no CreateProcess attempt
for it in 60+ s. Network, storage, GPU and utility processes do start.
With --no-sandbox, renderers start and pages render, including the Autodesk sign-in.
--disable-features=NetworkServiceSandbox doesn't help. The WebView2 runtime of the
same version starts its (sandboxed) renderers fine.

## Notes
- The Edge log says "Edge is running elevated: 3": Wine processes run with an
  elevated admin token.
- No failing sandbox API found in a relay trace (CreateRestrictedToken,
  DuplicateTokenEx, job objects and CreateProcessAsUserW all succeed for utilities).
  The browser just never requests a renderer. Chromium logging (--v=3) shows nothing.
- Workaround in inv-vm (tools/transplant.sh): the HKCR http/https/MSEdgeHTM open
  commands add --no-sandbox.
- Repro: `msedge.exe --user-data-dir=C:\users\xl0\edgetest https://example.com` in inv-vm.
