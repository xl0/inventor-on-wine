# 133 winewayland: WaitForInputIdle returns immediately (clipboard thread message loop sets the process idle event); Inventor DWG/DXF export fails
Status: draft · Found in: wayland test pass (notes/wine/wayland.md)

## Symptom
invscen on Wayland: `drawing2` "export DWG" fails with REGDB_E_CLASSNOTREG (0x80040154) and `sheetmetal` "export flat
pattern DXF" with E_FAIL; both pass on X with the same build/prefix (drawing2 14/14, sheetmetal 9/9). 5/5 runs fail on
Wayland, 3/3 pass on X. (invscen `export` fails its IGES step on X as well today, not Wayland related.)
Cause: DwgTrans.dll (imports WaitForInputIdle) starts `DBXBridge.exe /ParentProcessIdentity=...`, waits for it to be
input-idle, then does `CoGetClassObject({b472e45c-...}, CLSCTX_LOCAL_SERVER)`; the bridge has not registered its class yet
(it needs ~240 ms), the call fails and Inventor `NtTerminateProcess`es the bridge.

## Repro
Minimal: `tests/wl_idle.c` (`x86_64-w64-mingw32-gcc -mwindows -o tests/wl_idle.exe tests/wl_idle.c -luser32`). The child
creates a window, sleeps 1.5 s without pumping messages; the parent calls `WaitForInputIdle(child, 10000)`.
Result is written to C:\wl_idle.txt of the prefix.
- X (build/, DISPLAY=:101): `WaitForInputIdle = 0 after 4611 ms`
- Wayland (wt/wayland-build): `WaitForInputIdle = 0 after 61 ms`
- `wl_idle.exe thread` (child additionally starts a helper thread running GetMessage): X also returns after 91 ms.
Inventor: `INV_PREFIX=... tools/invscen/run.sh drawing2` on Wayland (see notes/wine/wayland.md for the env).

## Evidence
`WINEDEBUG=err,+relay,+process,+timestamp` with `RelayInclude=ntdll.NtTerminateProcess;...` (inst/wayland/dbxbridge-timeline-*.txt):
- Wayland: bridge created t0=92997.506, winewayland.drv attach +122 ms, utx.dll +207 ms, Inventor's CoGetClassObject fails
  and `Call ntdll.NtTerminateProcess` at +210 ms (bridge never got to DWGSERVICE.dll/dbxbridgePS.dll).
- X: bridge created 92550.564, utx.dll +200 ms, DWGSERVICE.dll +237 ms, Inventor unmarshals the bridge interface (loads
  dbxbridgePS.dll) at +253 ms; no terminate.
`WINEDEBUG=+waylanddrv`: `clipboard_thread created per-process clipboard window` in every process.

## Open
Component (guess): the Wayland driver exposes a generic win32u/server behaviour. dlls/winewayland.drv/dllmain.c starts a
thread running `GetMessageW` at process init whenever the compositor lacks zwlr_data_control (mutter/GNOME: always);
win32u/message.c sets the one per-process idle event (`thread_info->idle_event`, server process->idle_event) when ANY
thread of the process waits for messages, so WaitForInputIdle returns before the main thread has initialised (also
reproduced on X with a plain helper thread, so the core behaviour is wrong independent of the driver). Windows
semantics (initial thread only?) not verified on the VM (not touched in this task). Fix would be in win32u/server
(idle per thread / initial thread) or move the clipboard thread off the message queue path.
