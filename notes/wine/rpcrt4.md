# rpcrt4 (checked against integ 9c1eea5beac + fix/087)

- Client connections live in per-endpoint associations (`rpc_assoc.c`, `client_assoc_list`,
  keyed by protseq/addr/endpoint/options, shared by all binding handles of the process).
  A call takes an idle connection out of `free_connection_pool` exclusively and puts it back
  after the reply; concurrent calls to one server open more connections. Pooled connections
  stay until the association's last binding handle is freed (COM proxies to a dead server
  keep them forever).
- ncalrpc and ncacn_np are both named pipes in Wine: one pipe handle + one cached overlapped
  event per client connection (`RpcConnection_np.event_cache`). The server runs one io thread
  per connection (`RPCRT4_io_thread`), requests go to thread-pool workers.
- Presentation contexts (087): `RpcConnection.contexts` holds the interfaces bound on a
  connection. The client binds the first interface with bind, adds others to an idle
  connection with alter_context (context id = index), and sends `ActiveContextId` in each
  request. Authenticated connections are not reused for other interfaces (no auth trailer on
  alter_context; the server rejects authenticated alter_context with a fault).
  Server: the context table is only touched on the io thread; a request's interface is
  resolved there before it is queued (`RpcPacket.if_id`).
- Windows (Win11, rpcrt4:server test): a second interface via the same or another binding
  handle to the same endpoint adds no handles, for ncacn_np, ncalrpc and ncacn_ip_tcp.
- `RpcMgmtIsServerListening` over ncacn_ip_tcp returns ERROR_ACCESS_DENIED on Windows and Wine.
- Handle counts in tests: NtQuerySystemInformation(SystemExtendedHandleInformation) works on
  both; GetProcessHandleCount returns 0 on Wine (072).
