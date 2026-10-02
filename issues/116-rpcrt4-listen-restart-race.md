# 116 rpcrt4: listening again right after RpcMgmtStopServerListening can hang
Status: fixed on fix/116-rpc-listen-restart (507f0b6fc13, e87b250ffba on integ 7e8ed9554cb), not merged.
Found in: 114 review

Probe: `tests/r116/listen_restart.c` (`[auto|slow|autoslow]`; each step in a thread, 5 s timeout).

Windows (Win11 VM, x64):
- RpcServerListen right after Stop (no RpcMgmtWaitServerListen) -> RPC_S_ALREADY_LISTENING (1713),
  same as Wine (listen_done_event still set). So the manual path doesn't reach the hang.
- RpcServerRegisterIf2(RPC_IF_AUTOLISTEN) right after Stop, even with a call still running: returns at
  once; calls to the autolisten interface work at once, calls to the non-autolisten interface fail
  1723 and RpcMgmtIsServerListening says 1715 (not listening).

Cause (Wine): after Stop the server thread closes its listeners, sets server_ready_event and then waits
for its connections (= running calls) to finish. A start in that window (autolisten registration, or
RpcServerListen when only autolisten interfaces had been listening) saw `server_thread` still set,
synced with it and waited forever on server_ready_event, holding server_cs, which the finishing call
needs in RPCRT4_release_server_interface -> permanent deadlock of all of rpcrt4's server side.
The thread also skipped its SetEvent when std_listen flipped back to TRUE after it decided to exit
(could hang the Stop's own sync).

Fix: `RpcServerProtseq.server_exiting` (under listen_cs), set by the server thread when it decides
to exit. Syncs skip an exiting thread; RPCRT4_start_listen_protseq waits for an exiting thread
(duplicated handle, no locks) and starts a new one; RPCRT4_start_listen no longer holds server_cs
around start/sync (protseqs are never removed). The exiting thread always sets the ready event
for the sync that woke it.
Left: Wine's RegisterIf2 waits until the running calls end (Windows returns at once; todo_wine in
the test); Wine serves non-autolisten interfaces once any autolisten one restarted listening
(Windows doesn't); a start issued from inside a call while a Stop is draining would wait for itself.

Second bug found by the test: rpcrt4_conn_tcp_close didn't clear sock_event/cancel_event, so every
further Stop closed the tcp listener's stale handle values again (here: the np protseq's
manager event -> the new np server thread waited on its own thread handle). Fixed in 507f0b6fc13.
A stopped ncacn_ip_tcp listener (sock -1) is never reopened on restart (pre-existing, not fixed).
Also seen: a call to a stopped ncalrpc endpoint takes 5 s to fail in Wine (Windows: at once).

Test: rpcrt4:rpc test_listen_after_stop (blocked call, Stop, Listen -> 1713, autolisten register,
call). VM x64 + x86 pass (count +20). Wine: unfixed hangs the unit; fixed passes 10/10 both arches.
Regress (rpcrt4 ole32 combase oleaut32 msi, both arches): 86/86 pass. No protocol change.
