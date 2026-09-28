# 056 COM stub doesn't catch noncontinuable (C++) exceptions from server methods: server crashes instead of RPC_E_SERVERFAULT
Status: fixed · Owner: worker 056 · Branch: fix/056-stub-exceptions · Found in: samples campaign, integ 38e4c1c00ce (prefixes/inv3, samples2016 run)

## Symptom
The samples2016 scenario calls `Rebuild2` on Fan Cover Mold.iam over COM, from another
process. The VM returns PASS. On Wine, Inventor crashed (CER dump
inv3 `...\Temp\Inventor260928170954.dmp`). The client got RPC_E 0x800706BE, then
0x800706BA for the rest of the run. Faulting thread (winedbg on the dump):

    RaiseException (code e06d7363 = MSVC C++ exception)
    vcruntime140_1 call_catch_block4 (a throw from inside a catch block)
    fwsrv+0x10e57e ... fwsrv+0xc3767, rxassembly+0xd894 ... (Inventor)
    oleaut32 DispCallFunc / ITypeInfo_Invoke, rxutil, rxassembly
    IDispatch_Invoke_Stub, NdrStubCall2, CStdStubBuffer_Invoke
    combase rpc_execute_call, apartment_wndproc, ... Inventor main loop

So an Inventor C++ exception escaped the COM method. It went unhandled, because
`CStdStubBuffer_Invoke`'s `stub_filter` (dlls/rpcrt4/cstub.c) returns
EXCEPTION_CONTINUE_SEARCH for EXCEPTION_NONCONTINUABLE exceptions. All MSVC
C++ throws are noncontinuable. Why Inventor threw at all on Wine (not on the VM)
is a separate question. The run had earlier failed to save Buffer Prep Skid.iam
(E_FAIL after 89 s; the same save passes in isolation).

## Windows ground truth (`tests/stub_exception.c`, Win11 VM vs Wine)
The probe is a cross-apartment IDispatch (STA server thread, MTA client, oleaut32
proxy/stub). The server's Invoke raises an exception. Each case runs in its own child
process.

| exception raised in the server | Windows: caller gets | Wine |
|---|---|---|
| 0xE06D7363, EXCEPTION_NONCONTINUABLE (C++ throw) | 0x80010105 RPC_E_SERVERFAULT | unhandled, server thread crashes |
| 0xE06D7363, continuable | 0x80010105 | returns 0xE06D7363 |
| 0xC0000005 access violation | 0x80010105 | returns 0xC0000005 |
| 0x80001234, noncontinuable | 0x80010105 | unhandled, crash |

## Task
Make the stub/channel catch these like Windows and return RPC_E_SERVERFAULT, whatever
the exception code or flags. Windows honours IGlobalOptions COMGLB_EXCEPTION_HANDLING
(DONOT_HANDLE*) to let them through; check whether Inventor sets it (probe
CoCreateInstance(CLSID_GlobalOptions) in-process is not possible from outside; test
the default only). Find where Windows catches it (the stub, or the channel
around it). Add tests to dlls/ole32/tests/marshal.c or rpcrt4/tests.
High impact: any exception inside an Inventor API call kills Inventor on Wine instead
of failing that one call.

## Findings (worker 056)
Probe `tests/stub_exception.c` (modes disp/sta/mta/direct/wrap, 5 exception cases) on Win11:
- All cross-apartment calls (IDispatch via PSDispatch, IPersist via the combase PS, STA and MTA
  servers, and a custom IRpcStubBuffer whose Invoke itself raises) return RPC_E_SERVERFAULT
  for every code/flag, incl. continuable RPC_X_BAD_STUB_DATA. The server thread survives and
  the next call succeeds. No unhandled-exception filter runs on the server.
- The rpcrt4 stub itself (IRpcStubBuffer::Invoke called directly with a fake channel) does
  NOT catch server-method exceptions of any kind; it does catch unmarshal errors
  (missing in-arg: returns 0x800706f7). So the catch is in the channel, the stub is phase-based.
- IDispatch::Invoke: EXCEPINFO, *puArgErr and the result VARIANT come back zeroed (the
  proxy clears them); nothing server-specific is filled in. Wine already matches.
- COMGLB_EXCEPTION_HANDLING not tested (default only).

## Fix
- combase rpc_execute_call: __EXCEPT_ALL around IRpcStubBuffer_Invoke → RPC_E_SERVERFAULT.
- rpcrt4: stub_filter declines exceptions while *pdwStubPhase == STUB_CALL_SERVER (instead of
  declining noncontinuable ones); NdrStubCall2 now sets the phase like widl stubs do.
  oleaut32 typelib-marshaled and PSDispatch stubs go through the same CStdStubBuffer path.
- Test: ole32 marshal.c test_server_exception (C++ noncontinuable, AV, RPC_X_BAD_STUB_DATA,
  then a normal call). Passes on the VM (x86_64, i386) and on Wine; unfixed Wine dies.
Probe on patched Wine matches Windows in every mode.

## Retest in Inventor (build c09f08e4924, inv3): NOT fixed for this case
Full `samples` (2022 set) run: Inventor still died in Rebuild2 of Fan Cover Mold.iam, with the same
stack (dump inv3 Temp\Inventor260928175655.dmp). Frame 0: RaiseException from vcruntime140_1
call_catch_block4, i.e. a C++ throw inside a catch block in fwsrv, below IDispatch_Invoke_Stub.
The new combase handler didn't get it, and CER wrote a dump. Either Wine's unwinder can't dispatch a
throw from a catch funclet to outer SEH frames, or Inventor calls its crash reporter itself.
It's reproducible in the sequence (it happened in 2 of 3 full runs), never in isolation: the step before
is always "save as Buffer Prep Skid.iam" failing with E_FAIL after ~55-90 s (it passes alone, and on the VM).
Next: probe a throw-from-catch (MSVC EH) in a cross-apartment call; look at what that save fails on.
