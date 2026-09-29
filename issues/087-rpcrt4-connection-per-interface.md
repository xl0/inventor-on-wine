# 087 rpcrt4: one client connection per interface (no alter_context); dead servers keep ~20 pipes each
Status: open (draft, low) · Owner: - · Branch: - · Found in: 086

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
