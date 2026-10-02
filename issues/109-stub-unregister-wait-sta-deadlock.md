# 109 combase: STA server deadlocks in RpcServerUnregisterIf while a call to the same interface waits for the STA
Status: open (draft) · Owner: - · Branch: - · Found in: soak #3 (inst/soak/2026-10-01, integ 492d5679270)

## Symptom
Inventor's DWG export (drawing2 `export DWG`, `Document.SaveAs` .dwg) hangs for good: harness TIMEOUT 120 s.
Afterwards Inventor's main thread is stuck in the export ("Executing Data Export" in the status bar) and
every new-document API call fails with E_FAIL (Documents.Add, sketch, SaveAs...), so the soak session is dead.
Hit twice in 28 suites (iter 4 under host load ~150, iter 27 at load ~8); never in soak #2 (32 suites).

## Evidence (gdb sehbt of the live processes, inst/soak/2026-10-01/wedge2/)
- Inventor: main thread + 6 other threads in `DwgTrans.dll -> ClientIdentity_Release -> ifproxy_destroy ->
  ifproxy_release_public_refs -> RemRelease -> ClientRpcChannelBuffer_SendReceive -> CoWaitForMultipleHandles`;
  the call's threadpool worker sits in `np_read` waiting for the reply (sehbt.txt).
- The translator server (DBXBridge.exe, a "ParentProcessIdentity=..." process started per export; the one of the
  first hang had even survived `kill-inventor` for 3 h) is deadlocked on itself (sehbt-server-*.txt):
  - STA main thread: `RPCRT4 call_server_func -> Rundown_RemRelease -> stub_manager_int_release ->
    stub_manager_delete -> stub_manager_delete_ifstub -> rpc_unregister_interface(iid, TRUE) ->
    RpcServerUnregisterIf` waiting for the in-flight calls of that interface to finish;
  - RPC worker thread: `RPCRT4_worker_thread -> process_request_packet -> dispatch_rpc` waiting for the same STA
    thread to run its call.
  Both servers (iter 4 and iter 27) show exactly this; a healthy later server was idle.

## Cause
`rpc_unregister_interface(riid, TRUE)` (combase/rpc.c, called from stubmanager.c `stub_manager_delete_ifstub`)
drops the registration of an IID when the last stub using it goes away and waits for outstanding calls on that
IID, from the apartment thread that those calls (any object with this IID, not just the dying stub) must be
dispatched to. Old code (moved from ole32), not part of the recent combase/rpcrt4 changes; exposed by Inventor
releasing several proxies of the translator from different threads while another thread calls into it.

## Ideas
- Don't wait when running on the thread of an STA that may have calls queued (wait=FALSE, as the non-ifstub path
  at stubmanager.c:273 already does), or keep the interface registered (never unregister until apartment
  teardown), or unregister from a non-apartment thread.
- Repro: out-of-process STA server with two objects of one IID, client calls a method on A while releasing B's
  last ref (RemRelease dispatched to the STA first). Windows ground truth not checked.
