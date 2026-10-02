# 116 rpcrt4: RpcServerListen right after RpcMgmtStopServerListening can hang
Status: open (draft, plausible from reading) · Owner: - · Branch: - · Found in: 114 review

If RpcServerListen is called after RpcMgmtStopServerListening but before the old server thread
finishes (no RpcMgmtWaitServerListen in between), RPCRT4_start_listen_protseq sees server_thread
still set; the sync waits on server_ready_event, which the dying thread no longer sets (std_listen
TRUE again) → hang. Pre-existing. Probe: tests/r114/rawrpc.c (restart mode) as a base.
