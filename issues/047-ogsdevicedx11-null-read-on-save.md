# 047 Inventor crash: NULL read in ogsdevicedx11 on a TBB worker (asm save)
Status: fixed · Owner: worker-047 · Branch: fix/047-d3d11-deferred-context-lock · Found in: invscen suite, integ 228616fa47c (prefixes/inv, :98)

## Symptom
`tools/invscen/run.sh all` (cold start): asmbig `save` (SaveAs of the 200-occurrence
big.iam) failed after 12.5 s with RPC_E 0x800706BE: Inventor died, CER wrote a dump,
the next scenario restarted it. Warm rerun of the suite: save PASS (1.1 s).
Same signature in two older dumps from the campaign runs (integ 91495f487ad,
10:46 and 11:16), so not new in this build; 3 of the 12 dumps in the prefix.

## Evidence
Dumps (prefixes/inv/drive_c/users/xl0/AppData/Local/Temp/):
Inventor260928143009.dmp (this run), Inventor260928104637.dmp, Inventor260928111601.dmp.
`winedbg 'C:\users\xl0\AppData\Local\Temp\<dmp>'` in the prefix, faulting thread:

    page fault on read access to 0x0000000000000000
    0 ogsdevicedx11+0x32ebb
    1 ogsgraphics+0x30bda9
    2 ogsgraphics+0xecb50
    3 ogsgraphics+0xeef33
    4-8 tbb12 (worker thread) <- ucrtbase _beginthreadex_trampoline

Also prefixes/inv3 (samples campaign, 15:35): SaveAs copy of the 2022 sample
scissors.iam (3 parts) crashed with the same frames 0-8; dump
inv3 ...\Temp\Inventor260928153545.dmp. Not size-related: small asm, warm session.

Graphics device (D3D11, wined3d-vk) work on a TBB thread; probably the save-time
thumbnail/preview render (cf. 037, SaveAsBitmap unshaded). Intermittent.

## Next
Disassemble ogsdevicedx11+0x32ebb (Autodesk code, allowed) to see which D3D11 call's
result is NULL (failed Map / CreateX / GetBuffer?); rerun asmbig save in a loop with
WINEDEBUG=+d3d11 warn to catch the failing call.

## Root cause (worker-047)
`ID3D11Device::CreateDeferredContext` returned E_FAIL on a TBB worker; OGS only handles
E_OUTOFMEMORY / DXGI_ERROR_INVALID_CALL / DEVICE_REMOVED, leaves its per-thread context NULL
and later calls `ctx->OMSetBlendState` through it.
Why E_FAIL: `wined3d_deferred_context_create()` takes the feature level from
`device->cs->c.state->feature_level` (the immediate context's state) without any lock.
`ClearState()` and `ExecuteCommandList(restore_state = FALSE)` on the immediate context run
`wined3d_state_reset()` = memset + state_init under the wined3d mutex, so a concurrent
creation can read feature level 0 -> `wined3d_select_feature_level()` fails -> E_FAIL.
Inventor's render thread calls ExecuteCommandList(list, FALSE) ~1500 times per session
while TBB workers (up to ~120 on this host) lazily create their deferred contexts, so the
first render after a pool grows (save thumbnail, GoHome) can hit it.
Windows (VM, WARP + default adapter, `tests/d3d11_deferred_race.c`): 0/20000 failures with a
thread looping ClearState; Wine: ~14% of CreateDeferredContext calls fail (GL, Xvfb).

Fix: d3d11 takes the wined3d mutex around `wined3d_deferred_context_create()` (as the other
d3d11 create paths do). Test `test_deferred_context_multithread` in d3d11 tests (fails 13/~N
unfixed on Wine, passes on the VM x86_64 + i386 and fixed Wine).

## How it was caught
Crash is intermittent and CER minidumps have no heap. `winedbg --file` script attached to the
running Inventor (`attach 0xPID`, `break *<ogsdevicedx11 base>+0x32d24` = OGS's "Device
Context now is NULL!" guard, `break *base+0x1da27` = return of its CreateDeferredContext call,
then `p $eax` / `x /64x $rdi+0x1160` / `bt` / `cont` blocks) caught it in ~1 of 4
save-heavy suite runs; OGS errors also show up as OutputDebugString ("OGS ERROR: ...").

## Findings (worker-047)
Crash site (Ghidra/objdump of OGSDeviceDX11.dll): `FUN_180032e00` = DeviceContextDx11 vtable
slot 0x148, sets blend state: `this->ctx(+0x11a8)->OMSetBlendState(...)` (ID3D11DeviceContext
slot 35) with `ctx == NULL`. `this` is a per-thread OGS::Devices::DeviceContextDx11 (0x1260 bytes,
parent VirtualDeviceDx11 at +0x11b0/+0x1180).
- Caller: OGSGraphics `DrawContextPool::Acquire` -> OGSDevices `DeviceContextCache::Acquire`
  (tbb enumerable_thread_specific keyed by GetCurrentThreadId; creates the object on first use per
  TID via VirtualDeviceDx11 vtbl 0x480 = FUN_180079f50), then calls slots 0xc0, 0xd8 (guarded:
  logs "Device Context now is NULL!" and returns), 0x148 (unguarded -> crash).
- +0x11a8 is written only by: ctor (`FUN_18001d7f0` -> `FUN_18001d9a0`: `ID3D11Device::
  CreateDeferredContext(0, &ctx)` (or GetImmediateContext); on failure only an OGS warning,
  ctx stays NULL), the device-removed path (sets +0xfa8, which makes Acquire skip the calls),
  and the release path `FUN_1800336d0` (ClearState + Release + NULL; from the dtor
  `FUN_18001dff0` / VirtualDeviceDx11 dtor).
- Wine's CreateDeferredContext cannot realistically fail (calloc + wined3d_state_create).
  +d3d11 traces of 4 full suite runs: 64-70 deferred contexts created (one per TBB thread),
  all S_OK, none ever released or ClearState'd. So the NULL comes from a context that was
  released (object destroyed / cache cleared while a TBB traversal still uses it) or from a
  failure that only happens in the crashing runs (it was the latter: E_FAIL, see above).
- CER rawdata: both older crashes with a command journal were in a save ("Save Doc
  (DLxDrawingDoc)" at 51 s uptime, "Save Doc (AMxAssmDoc)" at 2m21), i.e. the save-time
  thumbnail render early in a session.
