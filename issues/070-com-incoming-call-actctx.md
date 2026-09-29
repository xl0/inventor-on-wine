# 070 Stress Analysis mesh: manifest CLSID not found inside an incoming COM call (no activation context)
Status: open (draft) · Owner: - · Branch: - · Found in: 068 verification (inv3/:100, integ 7f6770b075c + 068 fix)

## Symptom
`beam` → Stress Analysis → Create Study → Fixed + 100 N edge force → Mesh View: Inventor dies
(CER dialog). With 068 fixed the NdrStubCall3 abort is gone; the crash is now an access violation in
`FEA_Addin_Utils!ProgressIndicator::GetProgressIndicator` (+0x74b30): it AddRefs a NULL IProgress.
Same steps on the Win11 VM mesh and solve fine (063: 1.522 mm / 119.8 MPa).

## Evidence
Relay trace (`WINEDEBUG=+relay,warn+ole` with RelayInclude on NdrStubCall3, CoCreateInstance,
Rtl(De)ActivateActivationContext): FEAComputeServer.exe calls back into Inventor; on the Inventor
RPC thread:
```
Call rpcrt4.NdrStubCall3(...)
  Call combase.CoCreateInstance({...}, CLSCTX_INPROC_SERVER)          -> S_OK
  Call combase.CoCreateInstance({A9485C80-1DCC-4F77-9F26-E2569F4C03F1}, 0x17) ret=FEA_Application_Common+0x330c
    err:ole:com_get_class_object class {a9485c80-...} not registered   -> 0x80040154
Ret  rpcrt4.NdrStubCall3() retval=0
```
No Rtl*ActivationContext call on that thread during the call. The caller (FEA_Application_Common
FUN_180003264, a Communicator "create progress" method, Ghidra) wraps the call in MFC's
AFX_MAINTAIN_STATE2 and does `CoCreateInstance(A9485C80, 0x17, ...)`. A9485C80 is registered only in
FEA_Application_Common.dll's embedded manifest (`<comClass ... threadingModel="free">`, next to
E4E2FE54 from 063); it isn't in the registry. Without an active activation context, the lookup
fails, the progress bar is NULL, and Inventor later AVs on it.

## Open question (needs Windows ground truth)
How does the class resolve on Windows inside that call? Candidates:
1. Windows activates, for an incoming cross-apartment/cross-process call, the activation context that was
   active when the object was created/marshaled (the callee is an Inventor-side object of an
   FEAComputeServerPS.dll interface).
2. MFC's AFX_MAINTAIN_STATE2 activates the DLL module's context on Windows but not on Wine (e.g.
   module state m_hActCtx not created, or its creation fails on Wine).
Probe idea: a server object created on thread A under an actctx (a manifest with a comClass),
marshaled to a second process. Inside a method called from there: GetCurrentActCtx, plus
CoCreateInstance of a class that only the manifest declares. Also test the in-process cross-apartment case.

## Task
Find which of the above Windows does and match it. Retest Mesh View + Simulate on `beam` (063 numbers).
