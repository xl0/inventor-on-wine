# 168 A fault in a Vulkan Unix call exits the process from wined3d's command stream thread: DLL detach code runs there
Status: draft · Owner: - · Branch: - · Found in: 165 (upstream code, integ e00a74f6590 and master 4e819f054dd)

## Symptom
When a Vulkan call faults on its Unix side (driver bug, or wined3d handing over a dead handle as in 150/165),
winevulkan's PE thunk prints `err:vulkan:vkXxx Exception 0xc0000005 in Unix call.` and calls `ExitProcess(3)`
(`UNIX_CALL_CHECKED`, dlls/winevulkan/vulkan_loader.h; upstream de8a4357d3d "winevulkan: Do not show debug assertion
dialog on Unix side exceptions", 2026-06-16; before it was `assert(!status)`).
With wined3d's Vulkan renderer the calling thread is the command stream thread. ExitProcess() kills every other
thread and then runs DLL_PROCESS_DETACH for all DLLs on the command stream thread. Application DLLs that release
Direct3D objects there (Inventor: OGSDeviceDx11.dll via OGSDevices.dll's static destructors) call wined3d's
client-side functions on the command stream thread: `wined3d_not_from_cs()` asserts in `device_resource_remove()`
and the modal box "Assertion failed ... cs->thread_id != GetCurrentThreadId()" stays on screen (165), i.e. exactly
the dialog that commit wanted to avoid. The process is left half dead: one thread in the box, the main thread gone
(Linux thread group leader is a zombie, `/proc/PID/environ` unreadable), its ROT entry still registered.

## Repro
165's cause is fixed on fix/165, so use the unfixed build: `tests/r165/deadwin.exe 2` (with `detach.dll` next to it)
under `WINE_D3D_CONFIG=renderer=vulkan` on integ e00a74f6590 or master: exit code 3, stderr
`process detach on thread NNNN, not the main thread` + `Assertion failed: cs->thread_id != GetCurrentThreadId()`.
Any other fault inside a Vulkan Unix call on the command stream thread gives the same.

## Options (not done; an upstream design question)
- winevulkan: `TerminateProcess(GetCurrentProcess(), 3)` instead of `ExitProcess(3)`: no detach code after a fault
  in the driver, no dialog, exit code kept. One line; it is what the unhandled-exception path does anyway.
- or wined3d: no assertion when the process is shutting down (`RtlDllShutdownInProgress()`); the detach code would
  then run Direct3D calls inline on a thread that just faulted in the driver: worse.
