# 109 rpcrt4: RpcServerUnregisterIf(wait) loses the wakeup of a call ending at the same time
Status: fixed · Owner: worker 109 · Branch: fix/109-stub-unregister (wt/109, on integ b6dab895f3e:
d43e0242d65 74c47dc99ea) · Found in: soak #3 (inst/soak/2026-10-01, integ 492d5679270)

## Symptom
Inventor's DWG export (drawing2 `export DWG`) hangs for good (harness TIMEOUT 120 s); afterwards every
new-document API call fails E_FAIL. 2 of 61 suites in soak #3, never in soak #2.
The translator server DBXBridge.exe (gdb sehbt, inst/soak/2026-10-01/wedge2/sehbt-server-*.txt): STA thread in
`Rundown_RemRelease -> stub_manager_delete -> rpc_unregister_interface(iid, TRUE) -> RpcServerUnregisterIf`
waiting; the only RPC worker in `dispatch_rpc` is the worker of that same RemRelease (waiting for the STA to
finish it), not a second call. Nothing else holds a call on the interface being unregistered.

## Cause (old upstream race, rpcrt4 since 2006)
RpcServerUnregisterIf reads `cif->CurrentCalls`, then sets `Delete = TRUE`, creates the event and stores
`CallsCompletedEvent`, all under server_cs. The ending call (`RPCRT4_release_server_interface`) decrements
CurrentCalls and checks Delete/CallsCompletedEvent without the lock. If the call ends between the read and the
event store (the CreateEventW server round trip is a wide window), nobody signals the event: the unregistering
thread waits forever (and writes the event into the sif the call just freed).
combase unregisters an IID (wait = TRUE) when its last stub goes away. DBXBridge does ~5000 such
register/unregister cycles per export, so a RemRelease arriving while the reply epilogue of the previous call
on that IID still runs (the worker decrements CurrentCalls after sending the reply) hits the window now and then.
Not caused by 087/097/099: the code is unchanged; faster call paths (097 whole-message reads, 086/097 event
caching, 087 RemRelease on the already-open shared connection) plausibly make the overlap more frequent, not
proven. The brief's hypothesis (a call queued to the STA for the same IID) can't happen with consistent
accounting: every marshal registers the IID, so a live stub keeps it registered; only a disconnected object
(CoDisconnectObject drops its registrations) with a call in progress makes the STA wait for itself (below).

## Evidence
- `tests/unregif_race.c` (raw rpcrt4 server, in-process client; unregister 0-63 us after the call's server
  routine signals): unfixed Wine hangs within ~30 rounds (4/4 runs); fixed 4x3000 rounds ok; Win11 3000 ok.
- Inventor with the window widened (debug Sleep(20) between Delete and the event store, Sleep(1) before the
  call's release): unfixed: drawing2 hangs in run 1, Inventor's own STA in exactly the soak signature
  (Rundown_RemRelease -> ... -> RpcServerUnregisterIf); fixed: 15/15 drawing2 runs, 2783 waits, no hang.
- Unwidened, unfixed: 45 drawing2 runs passed (rare race), one benign "has 1 calls, wait 1" seen.

## Windows ground truth (Win11 VM)
- RpcServerUnregisterIf(WaitForCallsToComplete) during a call returns once the call ends (unregif_race 3000x).
- `tests/com_disconnect_wait.c`: STA server, object A's method calls CoDisconnectObject(A) and pumps; the client
  releases B (same IID, last stub): handled at once (0 ms), A's call returns S_OK, a second call on A fails
  RPC_E_DISCONNECTED. Wine (even with the rpcrt4 fix): the release hangs (STA waits for its own call).

## Fix (fix/109-stub-unregister)
- d43e0242d65 rpcrt4: decrement + Delete check + SetEvent under server_cs. Test rpcrt4:rpc
  test_RpcServerUnregisterIf_wait (300 rounds, unregister in a thread with 30 s timeout; unfixed Wine fails).
- 74c47dc99ea combase: interfaces stay registered once their first stub is marshaled (no
  rpc_unregister_interface, no stub_manager_disconnect / `disconnected`); calls to gone stubs fail
  RPC_E_DISCONNECTED in dispatch_rpc (as Windows). Test ole32:marshal test_release_in_disconnected_call
  (child process "-disconnect"). Also removes the old UAF where a no-wait unregister (CoDisconnectObject)
  freed the RPC_SERVER_INTERFACE that in-flight calls' cif->If pointed to.
  Ceiling: RPCRT4_find_interface scans the registered list per call; it now holds every IID ever marshaled
  (dozens-hundreds) instead of only live ones.
- Tests: VM x64+i386 marshal 0 failures, rpcrt4:rpc only the pre-existing I_RpcMapWin32Status failure; Wine
  x64+i386 ole32 marshal/compobj, rpcrt4 rpc/server, combase roapi pass; regress subset
  combase|ole32|rpcrt4|oleaut32|actxprxy 64/64 pass.
- Inventor (inv4, final build): dwgloop 25 DWG exports + 12x drawing2 (12 DWG + 12 DXF + PDF): all PASS.

## Side observations
- On the unfixed build one dwgloop run failed export DWG 20 with E_FAIL (2.8 s, no hang) and Inventor then
  failed new documents E_FAIL; not reproduced in 25 exports on the fixed build. Unknown, not investigated.
- `tools/invscen/dwgloop.cs`: drawing + INVSCEN_N (10) DWG exports.

## Soak #4 (2026-10-02, integ d7799da4d5c, with the fix)
One Inventor session, 4.0 h, 32 suites, 10 samples runs: `drawing2 export DWG` passed 32/32, no hang, no E_FAIL cascade
(soak #3: 2 of 61 hung). No crashes. Details: inst/soak/2026-10-02/summary.txt.
