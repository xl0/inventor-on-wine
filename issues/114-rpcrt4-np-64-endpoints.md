# 114 The 65th STA that marshals an object in a process hangs (rpcrt4 named-pipe wait limit)
Status: open (draft) · Owner: - · Branch: - · Found in: 112 worker (tests/r112/uninit_race.c)

## Symptom
A process that creates STA threads one after another, each marshaling an object (CoInitialize,
CoMarshalInterface, CoUninitialize), hangs in round 65: the new STA's CoMarshalInterface →
rpc_start_remoting → RpcServerUseProtseqEpW → RPCRT4_sync_with_server_thread waits forever.
Integ (1b02d8257a0) and fix/112 alike. Windows: 100 rounds fine.

## Repro
`RACE_NOCALL=1 RACE_VERBOSE=1 wine uninit_race.exe 100` (tests/r112/uninit_race.c; mingw
`-lole32 -luuid`): prints "round 64" and stops.

## Suspicion (not verified)
Each apartment registers its own ncalrpc endpoint (\pipe\OLE_<oxid>) and nothing unregisters it
when the apartment goes away; the named-pipe protseq server thread waits on one listen event per
endpoint plus the manager event with WaitForMultipleObjectsEx (rpc_transport.c,
rpcrt4_protseq_np_wait_for_new_connection), which fails beyond MAXIMUM_WAIT_OBJECTS (64), so the
server thread no longer answers RPCRT4_sync_with_server_thread. Apps with many short-lived STAs
(thread pools of STAs, per-document threads) would hang after 64 of them.
