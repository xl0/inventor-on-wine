# 111 Last Release of a proxy during an in-flight call returns 0 (Windows 1)
Status: fixed · Owner: worker 112 · Branch: fix/112 (wt/112: ee90af12ce9) ·
Found in: 110 worker (tests/r109/disc_probe.c `rel` mode)

Releasing a proxy's last reference while another thread is calling through it returned 0 on
Wine; Windows returns 1 (the call keeps the proxy referenced).

## Cause
Nothing in combase referenced the proxy manager during a call. The release destroyed the proxy
manager under the call: ifproxies destroyed (StdProxy freed while NdrClientCall still runs with it
as `This`; the channel survived only through NDR's own channel ref) and RemRelease sent before the
call was answered. No crash seen in practice (NDR only traces `This` after GetBuffer), but the
server lost its references during the call.

## Windows (VM)
disc_probe `rel`: Release returns 1. ole32:marshal test_release_proxy_in_call (in-process STA
host): Release from another MTA thread during the call returns 1; the call returns S_OK; when it
has returned the object's stub is released (object refs back to 1). x64 + i386.

## Fix (ee90af12ce9 combase: Keep a proxy referenced while a call through it is in progress.)
The client channel keeps a weak pointer to its proxy manager (identity); GetBuffer (on success)
AddRefs it, FreeBuffer releases it last. So the last release during a call is deferred to the
call's FreeBuffer (also for calls orphaned by a message-filter cancel, 099: FreeBuffer runs when
the caller returns; the orphaned call holds only its message state). The channel can't outlive
the manager except through a stale proxy pointer (app bug, was a UAF before too).
A Release racing a call that hasn't reached GetBuffer yet still destroys the proxy (app bug).

## Tests
- test_release_proxy_in_call: VM x64 + i386 pass; unfixed Wine "got 0"; fixed x64 + i386 pass.
- disc_probe all modes, warn+heap, x64 + i686: rel → 0x1, no heap warnings; other modes unchanged.
