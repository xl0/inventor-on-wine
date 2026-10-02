# 114 The 65th STA that marshals an object in a process hangs (rpcrt4 named-pipe wait limit)
Status: fixed (fix/114: 19075a1d4ee, 897cce56c6b on integ 76edc4302b7) · Branch: fix/114 · Found in: 112 worker

## Symptom
A process that creates STA threads one after another, each marshaling an object (CoInitialize,
CoMarshalInterface, CoUninitialize), hangs in round 65: the new STA's CoMarshalInterface →
rpc_start_remoting → RpcServerUseProtseqEpW → RPCRT4_sync_with_server_thread waits forever.
Windows: 200 rounds fine.

## Repro
`RACE_NOCALL=1 RACE_VERBOSE=1 wine uninit_race.exe 100` (tests/r112/uninit_race.c): stops at "round 64".
`tests/r114/sta_endpoints.c [N]`: STA marshals, main MTA calls, STA exits; prints handles by type.

## Cause (confirmed)
- combase registered one ncalrpc endpoint per apartment (`\pipe\lrpc\` + `\pipe\OLE_<oxid>`), never
  removed (rpcrt4 has no API to drop an endpoint).
- rpcrt4's np protseq server thread waited on manager event + one listen event per endpoint with
  WaitForMultipleObjectsEx. 64 endpoints = 65 handles → ERROR_INVALID_PARAMETER ("wait failed with
  error 87"), the server thread exits, the next RpcServerUseProtseqEp waits for it forever.
  Any process with >63 ncalrpc/ncacn_np endpoints hits it (ncacn_ip_tcp has the same array design,
  not fixed: COM doesn't use it).

## Windows ground truth (sta_endpoints.exe 200, Win11 VM)
Process handles: 95 before COM, 128 after the first STA (ALPC Port 1 → 6), then flat through round 199
(no per-apartment endpoint). 200 rounds in 422 ms.

## Fix
1. rpcrt4: FSCTL_PIPE_LISTEN completes with an APC to the server thread (which already waits
   alertably) instead of an event; the wait array is just the manager event. The APC spawns the
   connection; a synchronous STATUS_PIPE_CONNECTED is handled inline. `RpcConnection_np.listening`
   replaces `listen_event`. No endpoint limit, one event handle less per endpoint.
2. combase: one endpoint per process, `\pipe\OLE_<pid>` (pid = oxid >> 32), registered once
   (InitOnce); calls are dispatched by IPID anyway. Handles now flat like Windows.

## Tests
- ole32:marshal test_many_apartments (100 STAs: marshal, cross-apartment CreateInstance call, end):
  VM x64 + i386 pass; Wine x64 + i386 pass (marshal unit 0.8 → 1.5 s). Unfixed: times out at the
  apartment that hits the limit, later tests hang.
- rpcrt4:server test_many_endpoints (100 ncalrpc endpoints, call over the last): VM x64 pass, i386
  only the 8 pre-existing failures (lines 2459-2504, same on the integ exe); Wine both pass.
  Unfixed: hangs.
- sta_endpoints 200 rounds: Wine x64 843 ms, i386 763 ms, handles flat. uninit_race RACE_NOCALL 300: ok
  (with calls it still hangs in round 0 on integ: that's 112, not in integ).
- Regress rpcrt4|ole32|combase|rpcss|services|oleaut32|actxprxy|ole2: 64/64 pass, 0 worse vs integ.
  `unit ole32:marshal -n 5` 5/5 per arch; `unit rpcrt4:server -n 5 -j 1` 5/5 per arch.
- r109 stress.c run 30 3 (warn+heap) x64 + i686: no AV, no heap errors; same result mix as integ
  (live objects at exit there = 112).

## Notes
- `regress.sh unit rpcrt4:server -n 5` without `-j 1` fails: parallel copies bind the same TCP port
  (RPC_S_DUPLICATE_ENDPOINT 1740) and cascade.
