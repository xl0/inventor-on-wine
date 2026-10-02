# 112 COM server objects stay alive after all clients released them
Status: fixed · Owner: worker 112 · Branch: fix/112 (wt/112, on integ 1b02d8257a0: e5cdd751e1c) ·
Found in: 110 worker (tests/r109/stress.c, R109_SAFE=3)

stress.c with no disconnects reported thousands of server objects alive at exit on Wine
(10838 of 14928 in 10 s, x64), 0 on Windows.

## Cause
Not a refcount leak. Clients release properly; RemRelease is neither lost nor batched
(stress `run SECS 4`: every unconsumed objref released + all proxies released → 0 live objects on
Wine and Windows, before and after the fix). The leak is STA teardown:
- A server STA thread stops pumping and calls CoUninitialize while calls from clients are already
  posted to it (DM_EXECUTERPC). Those posted messages never run. The RPC worker in dispatch_rpc
  waits for them forever and holds an apartment ref, so CoUninitialize's apartment_release doesn't
  reach 0: the apartment is never destroyed and its stub managers keep every object alive (in the
  stress test that's every object with an objref nobody unmarshaled, i.e. most of them).
- In-process calls posted that way (bypass_rpcrt) hang forever (the window is destroyed with the
  message in the queue). Same for 099's follow-up (orphaned in-process call, staleak.c): the
  call state and the object leaked; fixed too.

## Windows ground truth (Win11 VM, x64 + i386)
`tests/r112/uninit_pending.c [N [release]]`: STA server (other process) marshals an object, stops
pumping; N client threads call it; the STA calls CoUninitialize.
- The queued calls fail RPC_E_DISCONNECTED during CoUninitialize (before it returns, 0-16 ms); the
  method never runs. The object is back to its own reference right after CoUninitialize (stub
  released). A later call: 0x800706b5 RPC_S_UNKNOWN_IF (Wine: RPC_E_DISCONNECTED, interfaces stay
  registered since 109; not changed).
- Same for an in-process STA thread (ole32:marshal test, "thread").

## Fix (e5cdd751e1c combase: Fail the calls queued for an STA when its thread uninitializes it.)
- leave_apartment (thread's last CoUninitialize of an STA) calls `rpc_cancel_queued_calls`:
  sets `apt->uninitialized` under apt->cs and completes every DM_EXECUTERPC still queued on the
  apartment window with RPC_E_DISCONNECTED. Posting (`post_sta_call`, dispatch_rpc and in-process
  SendReceive) checks the flag under apt->cs, so nothing is queued after that.
- A call from another process now drops its stub/channel/stub manager/apartment refs in `call_done`
  on the thread that completes or cancels it (the STA itself), not in the worker after waking. So
  the STA's own release in CoUninitialize is the last one, the apartment is destroyed on its thread,
  and stub managers' final releases (STA objects) run on the STA thread (also fixes 110's side
  observation for calls into STAs).
- `call_done` also releases the stub buffer/channel of in-process calls right away (they held the
  object until the caller's FreeBuffer, i.e. after CoUninitialize returned).
- The drain loop re-posts a pending WM_QUIT (PeekMessage returns it whatever the filter;
  compobj's CoWaitForMultipleHandles test crashed on it).
- In-process STA calls look the target apartment up by tid at send time (was: cached hwnd) to
  check the flag; no apartment → RPC_E_DISCONNECTED.
- Remaining race (pre-existing class): a transient apartment ref taken right before the STA's
  final release (an incoming call's lookup) makes that thread run the teardown.

## Tests
- ole32:marshal test_uninit_with_queued_call (thread, process): the server waits until the call
  is queued (GetQueueStatus QS_POSTMESSAGE; Windows queues it as a posted message too), then
  CoUninitialize: call fails RPC_E_DISCONNECTED within 5 s, method not run, object refs back to 1
  when CoUninitialize returns. (The child stays alive until the client has its reply: the fault
  is sent by an RPC worker after the STA released it.) VM x64 + i386: pass.
  Unfixed Wine: thread: call hangs (+ later tests disturbed); process: 0x800706be after the server
  exits, 4 refs. Fixed Wine x64 + i386: 0 failures.
- stress.c (`run SECS SAFE`, warn+heap, 30 s): live objects at exit, before → after:
  SAFE=3 x64 10838 → 0 (Windows 0); SAFE=0/3/4 x64 + i686 after: 0 live, 0 AV, 0 heap, 0 over-release.
- `regress.sh unit ole32:marshal -n 20`: 20/20 per arch (earlier Sleep-based test version was
  flaky under load). compobj -n 10: 10/10. Regress ole32|combase|rpcrt4|oleaut32|rpcss vs
  wt/110-regress (same tree as integ): 64/64 pass, 0 worse.
- `tests/r112/uninit_race.c [N]` (STA uninitializes at a random moment while an MTA thread
  unmarshals+calls, in-process): fixed 12x60 rounds in parallel: all RPC_E_DISCONNECTED, 0 bad;
  integ hangs in round 0; Windows 100/100. More than 64 rounds hit 114 (65th STA hangs, integ too).
