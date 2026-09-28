# 048 Cold-start hang: first document never opens while trial dialog shows "We're having trouble"
Status: open (draft, seen once) · Owner: - · Branch: - · Found in: invscen suite, integ 228616fa47c (prefixes/inv, :98)

## Symptom
Cold `tools/invscen/run.sh all`: hello `connect` PASS (19 s), `first part view`
(Documents.Add of a visible part) TIMEOUT 120 s; the next scenario (tlb) hung at connect.
Screen: Inventor Home with the AdskLicensingAgent trial popup (WebView2) showing
"We're having trouble. Please try again later." instead of "Welcome to your trial".
Inventor.exe at 0 % CPU. Clicking the popup's X turned the main window black
(no repaint), popup stayed drawn. No crash dump, no winedbg. After
`wineserver -k` the next cold start passed; the "trouble" popup appeared again
mid-suite without blocking anything, so the popup alone is not the cause.

## Next
If it recurs: winedbg backtrace of all Inventor threads (main thread wait target),
`xwininfo -root -children` for the popup's owner. Probably a race between the first
document's view creation and the licensing popup (029/037 area).

## Coordinator note
Possibly the shared licensing service effect (one AdskLicensingService on
127.0.0.1:39683 serves all prefixes; see CODE.md): if another prefix's services
restarted at that moment, the trial popup could fail like this. Unverified.

## Harness note (2026-09-28)
Closing the trial welcome via its X button made the first part's ActiveView
null on 2/2 cold runs; WM_CLOSE doesn't (harness now uses WM_CLOSE). Possibly
related to this hang. Also: after hello closes its document, the Home page
stays blank (WebView2 redraw?) — unfiled.
