# rpcrt4 (checked against integ 9c1eea5beac + fix/087)

- Client connections live in per-endpoint associations (`rpc_assoc.c`, `client_assoc_list`,
  keyed by protseq/addr/endpoint/options, shared by all binding handles of the process).
  A call takes an idle connection out of `free_connection_pool` exclusively and puts it back
  after the reply; concurrent calls to one server open more connections. Pooled connections
  stay until the association's last binding handle is freed (COM proxies to a dead server
  keep them forever).
- ncalrpc and ncacn_np are both named pipes in Wine: one pipe handle + two cached overlapped
  events per connection (`RpcConnection_np.event_cache[2]`: the server's io thread reads the next
  request while a worker writes the reply). The server runs one io thread per connection
  (`RPCRT4_io_thread`), requests go to thread-pool workers. Every pipe read/write is a wineserver
  request; reads go through `read_buf` (whole messages, 097) instead of header/rest/body reads.
- Cost of a cross-process COM call (097, `tests/qi_remote.c`, remote QI loop): Wine integ ~580 us,
  with 097 ~320 us, Windows ~160 us. Path: client thread -> pipe -> server io thread -> worker ->
  posted DM_EXECUTERPC to the STA (combase dispatch_rpc) -> reply back the same way.
- Presentation contexts (087): `RpcConnection.contexts` holds the interfaces bound on a
  connection. The client binds the first interface with bind, adds others to an idle
  connection with alter_context (context id = index), and sends `ActiveContextId` in each
  request. Authenticated connections are not reused for other interfaces (no auth trailer on
  alter_context; the server rejects authenticated alter_context with a fault). A rejected
  alter_context keeps the connection pooled (dropping the last one ends the server's association
  group and runs down client context handles). Max RPC_MAX_CONTEXTS per connection.
  Server: the context table is only touched on the io thread; a request's interface is
  resolved there before it is queued (`RpcPacket.if_id`).
- `RpcAssoc.connection_cnt` reaching 0 resets the association group id; every connection with
  `->assoc` set is uncounted in RPCRT4_ReleaseConnection, so it's counted before connecting.
- Windows (Win11, rpcrt4:server test): a second interface via the same or another binding
  handle to the same endpoint adds no handles, for ncacn_np, ncalrpc and ncacn_ip_tcp.
- Windows wire details: alter_context_resp has an empty sec_addr; a request with an unknown
  context id gets fault nca_s_unk_if; after a bind_nack the server closes the connection.
- `RpcMgmtIsServerListening` over ncacn_ip_tcp returns ERROR_ACCESS_DENIED on Windows and Wine.
- Handle counts in tests: NtQuerySystemInformation(SystemExtendedHandleInformation) works on
  both; GetProcessHandleCount returns 0 on Wine (072).
- combase proxies (Windows ground truth, 097): QI for IUnknown, IMarshal, IMultiQI, IClientSecurity,
  IRpcOptions, ICallFactory succeed and IAgileObject, INoMarshal fail locally; any other IID goes
  to the server every time (no negative cache). .NET RCW creation QIs IManagedObject,
  IProvideClassInfo, IInspectable, INoMarshal, IAgileObject, IRpcOptions per new object, so each
  remote one is a call into the server's STA (and, for MFC servers, a full idle cycle there).
  Wine lacks ICallFactory (no async calls).
