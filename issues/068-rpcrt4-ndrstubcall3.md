# 068 Stress Analysis mesh kills Inventor: rpcrt4 NdrStubCall3 unimplemented
Status: open (draft) · Owner: - · Branch: - · Found in: 063 verification (inv3/:100, integ 7f6770b075c)

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
