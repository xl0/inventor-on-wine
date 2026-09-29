# 074 Inventor crash after 4 h: unhandled CLR exception during "place 200 occurrences"
Status: open (draft; not seen in soak #2) · Owner: - · Branch: - · Found in: soak test (inst/soak/2026-09-28, integ c036c687c47)

## Symptom
Only crash of a 4.2 h single-session soak (21 suite runs + 6 samples runs; none in the first 20
iterations). In the 21st suite run, asmbig "place 200 occurrences" failed after 41 s with
0x800706BE (RPC call failed): Inventor died. Autodesk CER caught it (dump + dialog; no winedbg,
`C:\winedbg-crash.log` unchanged); run.sh started a new Inventor for the next scenario.
State at the last sample (3 min before): RSS 14.9 GB, VSZ 106 GB, 24k mappings (peaks 35k during
samples; max_map_count 65530), 12k kernel handles, 842 threads.

## Evidence (inst/soak/2026-09-28/matched/, not in git)
- `Inventor260929021237.dmp` (CER minidump): exception e0434352 (CLR) on thread 23c0, params
  [0x80131604 = COR_E_TARGETINVOCATION, ...], raised from coreclr; stack coreclr / clrgc /
  FwUI.dll+fa13 / ASMUFLD232.dll+1d4352 / wpfgfx_cor3.
- `Inventor20260929021241.txt`: `SVxApp::CareForAllExceptions(): SEH exception caught while executing
  request!` in AMxPlaceCompRequest; CLR stack `Autodesk.Inventor.IPC.CommTermExMethodDelegator.Execute`
  ← `InputMessageChannel.SendMessage` ← `WHxMessageChannelNative::SendMessage`.
- The inner exception of the TargetInvocationException is not in the dump strings.

## Assessment
Unclear whether this is a Wine bug, a consequence of the session's growth (072/073: memory,
stubs, fragmentation), or an Inventor fault that also happens on Windows after hours.
The dump has no inner-exception text; reproducing needs another multi-hour session.

## Next steps
- Rerun the soak with `DOTNET_DbgEnableMiniDump`-style full dump or `WINEDEBUG=+seh` limited to the
  last hour (or attach winedbg to catch the first-chance CLR exception) to get the inner exception.
- Check the VMA count at the time of death (sample every 5 s in the last hour); mmap failures at
  max_map_count would surface as OutOfMemory inside the CLR.

## Soak #2 (integ bbc7f82accb, inst/soak/2026-09-29)
Did not recur: 33 suites (incl. asmbig place 200 every time) in one session over ~7 h, RSS up to 16 GB, 24.6k
mappings, 12.6k handles. Suggests the crash tied to the slowed/bloated state of soak #1 (072/073) or is rare.
