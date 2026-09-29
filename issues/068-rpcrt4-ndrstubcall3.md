# 068 Stress Analysis mesh kills Inventor: rpcrt4 NdrStubCall3 unimplemented
Status: fixed · Owner: worker 068 · Branch: fix/068-ndrstubcall3 · Found in: 063 verification (inv3/:100, integ 7f6770b075c)

## Symptom
Part `beam` (tools/invscen/beam.cs) → Environments → Stress Analysis → Create Study (OK) → Fixed +
Force → Mesh View (or Simulate): Inventor dies, CER "Autodesk Inventor Professional has stopped" dialog.
Inventor's stdout (inst/invscen/inv3/inventor.log):
```
wine: Call from 00006FFFE0D33008 to unimplemented function RPCRT4.dll.NdrStubCall3, aborting
```
Same steps on the Win11 VM mesh and solve fine (063: 1.522 mm / 119.8 MPa).

## Evidence
The FEA COM proxy/stub DLLs are MIDL x64 output with NDR64 stubs: FEAComputeServerPS.dll,
FEACommunicator.dll, FEAManager.dll, ServiceModule.dll (Inventor Bin) import `NdrStubCall3`
(+ NdrStubForwardingFunction, NdrCStdStubBuffer2_Release). Wine's rpcrt4.spec has only
`NdrClientCall3`; there is no `NdrStubCall3`. The stub side runs in Inventor (callbacks from
FEAComputeServer.exe), so the first incoming call aborts Inventor.

## Task
Implement `NdrStubCall3` (x64 stubless server entry for /protocol all or ndr64 stubs). MIDL
`/protocol all` keeps the DCE format strings, so dispatching to the NdrStubCall2 path when the
server info carries a DCE transfer syntax (MIDL_SERVER_INFO pSyntaxInfo) may be enough; check
what NdrClientCall3 does in Wine and match it. Test with a MIDL-built /protocol all (and ndr64)
interface if feasible; retest Stress Analysis mesh + solve (063 has the numbers).

## Findings (worker 068)
- FEAComputeServerPS.dll (parsed by hand: ProxyFileInfo → stub/proxy vtbls) is MIDL `/protocol all`.
  The MIDL_SERVER_INFO and MIDL_STUBLESS_PROXY_INFO keep the NDR (DCE) ProcString and FmtStringOffset
  at the top level. pSyntaxInfo holds 2 entries: [0] NDR 2.0 (the same strings), [1] NDR64.
  Stub dispatch tables: NdrStubForwardingFunction for the IDispatch slots, NdrStubCall3 for the rest.
- Windows (probe with a factory-created CStdStubBuffer, IRpcStubBuffer::Invoke):
  NdrStubCall3 uses the top-level NDR strings when the message's TransferSyntax is NULL or NDR.
  This holds whatever the pSyntaxInfo order, and even with nCount=0 and no syntax info. With
  TransferSyntax = NDR64 it takes the NDR64 path. NdrStubCall3 and NdrServerCallAll are exported
  only on x64 (32-bit rpcrt4 lacks both).
- Wine never negotiates NDR64: ndr64_client_call picks the NDR syntax, and the channel doesn't set
  TransferSyntax. So NdrStubCall3 = NdrStubCall2.

## Fix
`rpcrt4: Implement NdrStubCall3().` (win64 export, forwards to NdrStubCall2; prototype in rpcndr.h).
Test cstub.c test_NdrStubCall3 (x64): /protocol all-shaped server info, NDR syntax info without
strings, invoked through a factory-created stub. Passes on Win11 VM and Wine.
The two Windows failures in cstub (lines 549/560, Releasevtbl) are pre-existing on master.
regress rpcrt4/ole32/combase/oleaut32 vs master 4e819f054dd: 64 units, 0 worse.

## Retest in Inventor (integ 7f6770b075c + fix, inv3/:100)
The NdrStubCall3 abort is gone: relay shows `NdrStubCall3` called on an RPC thread (a callback from
FEAComputeServer.exe), returning 0. Mesh View still kills Inventor, now in the Autodesk method behind
that call: a CoCreateInstance of a manifest-only CLSID fails (no activation context), which leads to a
NULL IProgress and an AV. Filed [070](070-com-incoming-call-actctx.md). No solve numbers yet.
