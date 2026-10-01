# 099 A COM call cancelled by the message filter keeps writing into freed call state
Status: draft · Owner: - · Branch: - · Found in: review of fix/097 (combase/rpc.c, integ 2cfee5f132e)

## Symptom (code reading, not seen in an app)
ClientRpcChannelBuffer_SendReceive: when IMessageFilter::MessagePending returns
PENDINGMSG_CANCELCALL, CoWaitForMultipleHandles returns RPC_E_CALL_CANCELED while the call is still
in flight (rpc_sendreceive_thread on a pool thread, or DM_EXECUTERPC posted to the target STA for
in-process calls). The proxy then frees the call: ClientRpcChannelBuffer_FreeBuffer frees
`message_state` (which holds `params`, incl. the event handle) and the request buffer, and the
RPCOLEMESSAGE lives on the proxy's stack. The late completion still reads `params->handle`, writes
`params->status`/`hr` and (I_RpcSendReceive / the stub) the reply into that message.
fix/097 (d08143577f2) only stops the cancelled call's event from being reused for the next call.

## Task
Make a cancelled in-flight call own its state until it completes (e.g. refcounted message_state
plus a private copy of the request, reply discarded), as Windows presumably does ("orphan
transactions" per the PENDINGMSG_CANCELCALL docs). Repro: STA host object with a slow method,
client message filter returning PENDINGMSG_CANCELCALL + a posted message during the call, then a
second call (crashes/corrupts under the current code, so not in the conformance tests yet).
