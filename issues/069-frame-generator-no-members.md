# 069 Frame Generator: Insert Frame OK creates no members (unconfirmed vs Windows)
Status: open (draft, needs a cause / Windows reference) · Owner: - · Branch: - · Found in: 064 verification (inv3/:100, integ 7f6770b075c)

## Symptom
frame.iam from tools/invscen/frame.cs (skeleton rectangle 100 x 50 cm). Design → Insert Frame:
panel shows Standard ANSI / Family ANSI AISC (Rectangular tube) / Size 2x1x1/8, pick the 4
skeleton lines → green preview of 4 members, OK → "Create New Frame" dialog (OK) → "Frame
Member Naming" (4 rows, AISC 2x1x1/8 - 19.685 / 39.37) OK → the panel resets, nothing is created:
no Frame*.iam / members in the assembly (TransactionManager: no new transaction; tools/invscen/tx.cs),
`inst/invscen/inv3/frame/frame/Frame/` stays empty, no CC member part generated in Content Center
Files, no error or dialog. 3 of 3 tries, also after an Inventor restart.
Place from CC and bolted connection (same CC libraries) generate their parts fine.

## Evidence so far
- .NET exception trace (EventPipe, notes/wine/debugging.md) of the whole flow: nothing new between
  Insert Frame OK and the naming dialog's OK. When the panel opens: 4x
  `Connectivity.Content.Exceptions.IndexingEngineException InvalidQuery_UnknownCategoryParameter [2078]`
  from ContentService::Search (LegacyPipeline SearchUtility::Search) — may be normal.
- wineserver strace during the naming OK: Frame Generator reads Standard.iam / Standard.ipt
  templates and lists the target dir, then no file create.
- The VM has no Content Center libraries, so no Windows reference yet.

## Next
Windows reference (VM with desktop CC libraries, or a known-good Frame Generator run) before
digging; then trace the Frame Generator add-in (native: +seh/relay around the naming OK).
