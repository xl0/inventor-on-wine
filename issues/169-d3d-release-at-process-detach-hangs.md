# 169 Direct3D objects released at process detach wait forever for wined3d's (killed) command stream thread
Status: draft · Owner: - · Branch: - · Found in: 165 (probe; upstream wined3d, not seen in Inventor)

## Symptom
A DLL that still holds a D3D11 object and releases it in DllMain(DLL_PROCESS_DETACH) at a normal process exit
hangs the exit on Wine: ExitProcess() has already killed wined3d's command stream thread, and the release on the
exiting thread queues commands for it and spins in `wined3d_cs_mt_finish()` / `wined3d_resource_wait_idle()`
(one core busy, the process never ends). GL and Vulkan renderer alike.

## Repro
`tests/r165/deadwin.exe 0 detach` (detach.dll next to it; any renderer): prints
`done, the buffer is released at process detach` and never exits (`timeout` → 124). Without `detach` the probe
releases the buffer itself and exits 0.
Windows: not run (the VM was down during 165); expected to exit at once, releasing COM objects from static
destructors is common.

## Notes
Inventor does not hit this on a normal exit (it tears its device down before exiting). wined3d has no
`RtlDllShutdownInProgress()` handling anywhere; the command stream functions would have to run inline (or drop
the work) once the thread is gone.
