# 099 A COM call cancelled by the message filter keeps writing into freed call state
Status: fixed · Owner: worker 099 · Branch: fix/099 (wt/099, on integ 492d5679270: cf739bb2a07 cdab4188fa8 5ce88cc17d3) · Found in: review of fix/097 (combase/rpc.c, integ 2cfee5f132e)

## Symptom (code reading, not seen in an app)
The brief assumed CoWaitForMultipleHandles returns RPC_E_CALL_CANCELED while the call is in flight
and the late completion then writes into the freed `message_state` / the caller's RPCOLEMESSAGE.
Verified: on integ that never happened, because CoWaitForMultipleHandles kept looping after
PENDINGMSG_CANCELCALL (`hr` set, `continue`) until the call's event was signalled, so a "cancelled"
call returned RPC_E_CALL_CANCELED only after the server had finished (617 ms for a 600 ms method).
Not Windows behaviour, but no UAF; 097's "don't reuse the event" was not needed then.
Making the cancel return right away (as Windows does) exposes the UAF: with only that change the
probe hangs (x64) or WINEDEBUG=warn+heap reports `invalid block type` (double free of the request
buffer by FreeBuffer and I_RpcSendReceive), the server reads a garbage ORPCTHIS, wrong replies.

## Windows ground truth (Win11 VM, x64 + i386, `tests/com_cancel.c`)
Client STA with a message filter; slow (600 ms) IPersist::GetClassID on a server in an in-process
STA thread, the MTA, another process's STA, another process's MTA; the server posts a message to
the client thread when the call starts; MessagePending returns PENDINGMSG_CANCELCALL.
- The call returns RPC_E_CALL_CANCELED at once (0-16 ms), [out] CLSID zeroed by the proxy.
  MessagePending is called once; the posted message that triggered it stays queued (not dispatched).
- The server still runs the cancelled call to completion; its reply is dropped.
- The server sees the cancel: CoTestCancel in the method returns RPC_S_CALLPENDING (0x80010115)
  before and RPC_E_CALL_CANCELED after the client cancelled (within one tick, cross-process too).
  Wine: CoTestCancel / CoCancelCall are `@ stub` (calling them crashes); not done here.
- The next call on the same thread gets its own reply: immediately for an MTA server (the late
  reply arrives later), after the cancelled call finishes for an STA server (serialized).

## Fix (fix/099)
- cf739bb2a07 combase: Keep the state of a client call until both the caller and the call are done.
  `message_state` (client) gets `refs` (caller until FreeBuffer + in-flight call), `orphaned` and
  `msg`, a private copy of the caller's RPCOLEMESSAGE that the call (rpc_sendreceive_thread /
  rpc_execute_call via DM_EXECUTERPC or the MTA pool) works on; copied back to the caller after a
  successful wait. On a failed wait the caller marks it orphaned and returns; FreeBuffer then only
  drops the caller's ref; the last release frees the call's message (reply or request buffer,
  RpcInterfaceInformation), closes the event (no leak, replaces 097's NULLing), releases actctx and
  stub/chan. `client_call_done` = SetEvent + release (worker side); rpc_execute_call tells client
  calls from server dispatch (dispatch_rpc) by `bypass_rpcrt`. The binding stays alive for an
  orphaned rpcrt4 call via the connection's binding ref (I_RpcGetBuffer..I_RpcFreeBuffer).
  The per-thread cached event (086) is only returned to the cache by a caller whose call completed.
- cdab4188fa8 combase: Return from a call right away when the message filter cancels it.
  CoWaitForMultipleHandles breaks out on CANCELCALL without dispatching the pending messages, and
  only consults the filter while an outgoing call is pending (Windows: not in plain STA waits;
  compobj's todo_wine on MessagePending dropped). Otherwise direct CoWaitForMultipleHandles callers,
  ISynchronize::Wait and rpc_get_local_class_object would return early / spin on a cancel.
  Test ole32:marshal test_cancel_call (sta, mta, process sta, process mta): cancel returns at
  once, server still in call 1, message still queued; next call gets CLSID {2}; after the late
  completion the third gets {3}. The filter cancels only on the server's posted message.
- 5ce88cc17d3 combase: Free the call state when a client channel fails to get a buffer.
  Pre-existing (also upstream): NdrProxyGetBuffer raises without FreeBuffer when GetBuffer fails,
  so every call to a dead server leaked the call event + message_state + cif (reviewer's deadsrv.c:
  handles 47 -> 547 over 500 calls; now 44 -> 44). Test test_dead_server_call_handles (20 calls to
  an exited server process: Wine without the fix +20 handles).
- Reviewer's stress (cstress.c: 16-32 STA threads, random cancels, 4 server types, warn+heap, 37k
  calls): 0 heap warnings, 0 crossed replies.

## Follow-up (done in 112, 2ee2ac231ec; staleak.c: refs back to 1 like Windows)
An orphaned call to an in-process STA that CoUninitializes without pumping never runs, so the call
state and the server object it references leak (reviewer's staleak.c). Option: complete pending
DM_EXECUTERPC messages with RPC_E_DISCONNECTED before the apartment window is destroyed.

## Tests
- ole32:marshal + compobj (final, 5ce88cc17d3): VM x64 + i386 0 failures; Wine x64 + i386 0 failures
  (marshal also under warn+heap, clean).
  integ combase: 4 failures (call finished before returning); early return without the
  ownership change: heap errors + wrong replies.
- regress ole32|combase|rpcrt4|oleaut32|rpcss vs integ 492d5679270: 0 worse of 64 units;
  `regress.sh unit ole32:marshal -n 20`: 20/20 pass per arch.
