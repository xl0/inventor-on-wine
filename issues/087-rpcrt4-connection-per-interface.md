# 087 rpcrt4: one client connection per interface (no alter_context); dead servers keep ~20 pipes each
Status: fixed · Owner: worker 087 · Branch: fix/087 (c85ded37cae, 54bcf300f36, 3480777eed2 on integ 9c1eea5beac) · Found in: 086

## Symptom
Inventor's DWG and DXF exports each start an out-of-process translator server; Inventor keeps
~40 proxies per server after it exits (app behaviour, 086). The rpcrt4 association to each dead
server keeps ~20 pooled client connections, one per interface ever called (IRemUnknown,
IUnknown, ~17 translator interfaces), each a named-pipe handle + a cached overlapped event:
+38 events and +38 File handles per suite (`run.sh all`), unbounded in a long session.
Windows keeps one ALPC connection per server (VM: ALPC ports +2..3 per suite).

## Cause (Wine source)
`RpcAssoc_GetIdleConnection` (dlls/rpcrt4/rpc_assoc.c) only reuses a pooled connection whose
`ActiveInterface` matches; a new interface opens a new connection and binds it. DCE RPC allows
several presentation contexts per connection (alter_context, p_cont_id per request); Wine's
client never sends alter_context and the server ignores PKT_ALTER_CONTEXT (rpc_message.c only
knows the header sizes). Each server-side connection also gets its own thread.

## Fix direction
Client: reuse any idle connection of the association and add the interface with alter_context
(track contexts per connection, send the context id in requests); server: accept alter_context,
keep a context-id → interface table per connection. Medium size, touches client and server.
Evidence: gdb dump of `client_assoc_list` in Inventor (inst/086/assoc.py): assocs to 8 dead
`\pipe\OLE_<oxid>` endpoints, refs 40, 19-20 pooled connections each.

## Windows ground truth (worker 087)
rpcrt4:server test (`test_second_interface`): client calls IMixedServer, then IInterpServer on
another binding handle from the same string binding. Win11 VM x64 + i386: handle count unchanged
for ncacn_np, ncalrpc and ncacn_ip_tcp (also with the same binding handle, and after an
RpcMgmtIsServerListening). Unfixed Wine: +2 (pipe + event) for np/lrpc, +3 for tcp.

## Fix (fix/087)
- `rpcrt4: Count a new client connection in its association before connecting it.` Pre-existing:
  failed connects/binds were uncounted on release, so connection_cnt dropped to 0 with live
  connections (group id reset) or underflowed (group id never reset: stale gid → 1727 forever).
- `rpcrt4: Support alter_context requests in the server.` Per-connection table of presentation
  contexts (id → interface); bind and alter_context add to it; requests dispatch by context id,
  resolved on the io thread before queueing (the table is only touched there). Alter_context
  answers alter_context_resp (empty sec_addr), errors a fault. A failed bind leaves the connection
  unbound (binding + contexts dropped; was a NULL deref on a later alter_context). Unknown context
  id → fault nca_s_unk_if. At most RPC_MAX_CONTEXTS (256) contexts per connection.
- `rpcrt4: Bind other interfaces on idle client connections with alter_context.` The pool prefers
  an idle connection with the interface bound, else takes any compatible unauthenticated one and
  sends alter_context (context id = index); requests carry the connection's active context id.
  A rejected alter_context returns the connection to the pool and the error to the caller; a
  cancel is returned; other failures drop the connection and bind a new one.
- Left out: alter_context on authenticated connections (client doesn't reuse them for another
  interface; server faults an alter_context with an auth trailer). Wine's COM doesn't authenticate.

## Review round (coordinator's adversarial review)
Fixed: rejection dropped the connection (+ connection_cnt underflow → association broken, 1727);
nacked bind left server_binding with NULL Assoc (alter_context crash); unknown context fault
status 0; uncapped context table; cancel swallowed by the reconnect. Windows (raw PDUs,
scratchpad rawvm.c): alter_context_resp sec_addr length 0; unknown context id → fault 0x1c010003;
after a bind_nack the server closes the pipe (Wine keeps it and faults an alter_context).
New test test_unknown_interface (np, lrpc): unregistered interface twice between calls to two
others; failed with 1727 on the previous fix. Reviewer repros on the new build: `st dead` on
ncalrpc/np: 1717 only for the unknown interface; tcp call after a server restart 1726 (same as
base: stale pooled socket); `st client` 8 threads: 0 errors on all three.

## Results
- Wine tests: rpcrt4:server passes x64 + i386 (new test fails on unfixed Wine). VM: x64 clean,
  i386 has 4 pre-existing server-side failures (same with build/'s test exe).
- regress (after review, same 190 units): 0 worse.
- regress rpcrt4 ole32 combase oleaut32 rpcss shell32 msi urlmon actxprxy taskschd wbemprox netapi32
  dcomp mmdevapi sechost advapi32 vs integ 9c1eea5beac: 190 units, 0 worse.
- Inventor inv4, hello then drawing2 x4 (inst/087/{base,fix}.log, resprobe after each):
  per drawing2 base Event +37..38, File +38..39; fix Event +2..6, File +4..5.
  `run.sh all` on fix: 13/13 PASS.

Soak #3 (2026-10-01, integ 492d5679270): File handles +42/h over a 4.1 h session (soak #2 +300/h): confirmed.
