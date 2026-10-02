# 110 CoDisconnectObject can drop the apartment reference twice → stub manager freed under a running call
Status: fixed · Owner: worker 110 · Branch: fix/110 (wt/110, on integ ada44a971eb: 0f77d6e0a18) ·
Found in: review of 109 (74c47dc99ea), also in upstream/base

## Symptom
A running or queued call holds a stub-manager reference, so after CoDisconnectObject the manager
stays findable. A second CoDisconnectObject (e.g. a "Close" method that disconnects itself, then
a teardown path disconnecting again), or a client's last release arriving during that call, takes
the call's reference: the manager is freed and dispatch_rpc's final release (rpc.c ~2165) hits
freed memory (RtlEnterCriticalSection). Where: CoDisconnectObject (two stub_manager_int_release,
no guard) and stub_manager_ext_release → stub_manager_int_release when external refs reach 0.
A third variant: an object marshaled again during the call reused the dying stub manager, so
the new objref was dead once the call returned.

## Windows (VM, tests/r109/disc_probe.c, x64)
Object A (STA, other process) calls CoDisconnectObject(A) in its first call and pumps.
- newcall: a second client call on A during that call is dispatched, S_OK; a call after it
  returns: RPC_E_DISCONNECTED. Object refs return to 1 (no leak, no over-release).
- double (two CoDisconnectObject): same as newcall.
- rel (client Releases the in-call proxy): Release returns 1 (the call holds a proxy ref;
  Wine returns 0, see side observations), server fine.
- relmd (client CoReleaseMarshalData of A's objref from another process): S_OK. Wine:
  CO_E_OBJNOTCONNECTED (cross-process CoReleaseMarshalData not implemented; not this issue).
- remarshal: A marshals itself again after the disconnect; after the call that objref
  unmarshals and works (S_OK) while the old proxy gets RPC_E_DISCONNECTED.

## Fix (0f77d6e0a18 combase: Drop the apartment reference of a stub manager only once.)
`stub_manager_disconnect`: drops the apartment ref once (`disconnected` flag under apt->cs) and
removes the manager from the apartment's object tree (a new marshal gets a new stub manager).
The ifstubs stay in the IPID tree until the last internal ref, so calls in progress and new
calls by IPID still dispatch; after that RPC_E_DISCONNECTED (dispatch_rpc, 109).
Used by CoDisconnectObject and by stub_manager_ext_release at 0 external refs (RemRelease,
CoReleaseMarshalData, CoLockObjectExternal unlock). RemAddRef/RemRelease on a disconnected
manager only change extrefs. Apartment teardown unchanged (managers left then are connected).
099: client-side orphaned calls don't hold stub managers; dispatch_rpc's ref keeps one alive
until the STA finished the call (also when the client cancelled).

## Tests
- ole32:marshal test_disconnected_call (once/double/release/remarshal; replaces 109's
  test_release_in_disconnected_call, whose release of another object's last stub stays in every
  mode): new call during the call S_OK with 2 server calls, first call S_OK, later call
  RPC_E_DISCONNECTED, remarshaled objref works. "release" = the object CoReleaseMarshalData's its
  client's objref in-process during the call. Unfixed Wine: 7 failures (second call
  RPC_E_DISCONNECTED, first call 0x800703e6 = server crashed). VM x64 + i386: 0 failures;
  Wine x64 + i386: 0 failures; `regress.sh unit ole32:marshal -n 20`: 20/20 per arch.
- Regress ole32|combase|rpcrt4|oleaut32|rpcss vs wt/109-regress (same tree as integ ada44a971eb):
  64/64 pass, 0 worse.
- Stress (`stress.exe run SECS`, R109_SAFE=0, WINEDEBUG=warn+heap): unfixed x64 30 s runs: AVs in
  2 of 4 runs, one left the server hung (exit 0x103). Fixed: x64 3×60 s and i686 3×60 s:
  0 AV, 0 heap messages, 0 over-releases, server exits 0.
  stress.c fix: ILockBytes::Flush / IStream::Revert / IStorage::Revert take no arguments; the
  i686 build called/implemented them with one, corrupting the stdcall stack (client crashed
  in its own code).

## Side observations (not fixed)
- Wine proxies don't hold a reference during a call: releasing the last ref of a proxy another
  thread is calling through returns 0 (Windows 1), so the proxy may be torn down under the call (not verified). App
  bug by COM rules, but Windows tolerates it.
- stress.c "live objects at exit" is thousands on Wine (also unfixed, also R109_SAFE=3 = no
  disconnects), 0 on Windows: server objects leak; not disconnect-related, not investigated.
- The final release of a disconnected stub manager can happen on an RPC worker thread
  (dispatch_rpc), so an STA object's last Release runs off its apartment thread. Pre-existing.
- A marshal racing the last external release can still put a new ifstub on a manager that is
  being disconnected (objref dead after the marshal returns). Pre-existing, rare.
