# 116 rpcrt4: listening again right after RpcMgmtStopServerListening can hang
Status: fixed on fix/116-rpc-listen-restart (4b6825cd9b6, 64579ebc35e on integ fc7ffde344a), not merged.
The tcp double-close fix (507f0b6fc13) is in integ as fc7ffde344a.
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
Review (adversarial, rev_stress.c `abba`): the first version deadlocked: RpcMgmtWaitServerListen took
listen_cs then server_cs, the stop path holds server_cs while syncing, and syncs and the server thread
now take listen_cs. Folded in the reviewer's fix (tests/r116/rev_waitlisten.diff): Wait takes server_cs
before listen_cs and waits on a duplicated server thread handle (the thread closes its own handle and a
new thread may reuse the value).
Churn with RpcMgmtWaitServerListen callers then hung at shutdown: a waiter could close
listen_done_event while another still had to wait on its handle value, and a later RpcServerListen's
event could reuse the value and be closed by the slow waiter (pre-existing). 64579ebc35e: waiters
wait on a duplicate and only close the event they saw (generation counter).
Known gaps (pre-existing, kept: a restart waits for the old server thread to exit fully, which 114's
io_status/APC handling relies on): a start issued from inside a running call (e.g. a method that
registers an AUTOLISTEN interface while a Stop is draining) waits for itself; an idle pooled client
connection (e.g. ncacn_ip_tcp kept by a binding) blocks a restart until the binding is freed.
The server thread's wait-failure exit (res == -1) still doesn't set the ready event: a sync that
signalled it just before would hang; setting it unconditionally leaves a stale signal that a new
thread's first sync can consume early (ResetEvent at creation races with a late waiter), so left.
Left: Wine's RegisterIf2 waits until the running calls end (Windows returns at once; todo_wine in
the test); Wine serves non-autolisten interfaces once any autolisten one restarted listening
(Windows doesn't); a start issued from inside a call while a Stop is draining would wait for itself.

Second bug found by the test: rpcrt4_conn_tcp_close didn't clear sock_event/cancel_event, so every
further Stop closed the tcp listener's stale handle values again (here: the np protseq's
manager event -> the new np server thread waited on its own thread handle). Fixed in 507f0b6fc13.
A stopped ncacn_ip_tcp listener (sock -1) is never reopened on restart (pre-existing, not fixed).
Also seen: a call to a stopped ncalrpc endpoint takes 5 s to fail in Wine (Windows: at once).

Stress (tests/r116/rev_stress.c): abba 300/300 and churn 90 s (WINEDEBUG=warn+heap, no heap warnings,
clean shutdown) on x64 and i386.
Test: rpcrt4:rpc test_listen_after_stop (blocked call, Stop, Listen -> 1713, autolisten register,
call). VM x64 + x86 pass (count +20). Wine: unfixed hangs the unit; fixed passes 10/10 both arches.
Regress (rpcrt4 ole32 combase oleaut32 msi rpcss services, both arches): 86/86 pass; rpcrt4:rpc and
rpcrt4:server 5/5 per arch (one i386 rpc run hit 1721 on the fixed tcp port 4114 in
test_rpc_ncacn_ip_tcp, 10/10 on rerun). VM: rpcrt4:rpc only the old rpc.c:563 failure; rpcrt4:server
x64 clean, x86 a few test_stop_wait_for_call/RegisterIf failures in server.c (unchanged test file). No protocol change.
