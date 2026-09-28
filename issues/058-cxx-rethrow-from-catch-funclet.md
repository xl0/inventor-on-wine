# 058 C++ throw inside a catch block escapes the outer SEH handler (Inventor crash)
Status: wontfix (not a Wine EH bug; Autodesk API filter by design) · Owner: worker 058 · Branch: - · Found in: samples campaign, after 056 (integ c09f08e4924, inv3)

## Symptom
Fan Cover Mold.iam Rebuild2 (2022 samples, in the full-run sequence right after
a failed Buffer Prep Skid save-as — see 059) kills Inventor. Stack: the throw
comes from vcruntime140_1 `call_catch_block4` (a throw inside a catch block in
Autodesk's fwsrv) under IDispatch_Invoke_Stub; combase's new catch-all handler
from 056 never sees it. Dumps: prefixes/inv3/.../Temp/Inventor260928175655.dmp
(and the earlier 17:09:54 one). See issues/056-*.md notes.

## Suspected (unverified)
Wine's x64 exception dispatch doesn't propagate an exception thrown from a C++
catch funclet (__CxxFrameHandler4 / nested exception, "collided unwind") to an
outer __try/__except frame further up (combase's), or Wine's vcruntime140_1
builtin mishandles it. Or Autodesk's CER catches it first (unhandled filter).

## Task
Which vcruntime140_1 is loaded (Wine builtin vs Autodesk's app-local Microsoft
copy — clean-room: never disassemble the MS one)? Reproduce synthetically: a
COM server method (MSVC-compiled if possible — mingw uses a different EH model;
clang-cl/MSVC-style EH via `x86_64-w64-mingw32-clang` or a prebuilt test from
the VM) that throws inside a catch block, called cross-apartment; Windows
ground truth on the VM; then fix ntdll/msvcrt EH so the outer handler runs.

## Findings (worker 058)
Not a Wine exception-dispatch bug. Wine's EH delivered the exception correctly. The first
handler above the throw belongs to Autodesk, and that handler reports to CER.
- All CRT DLLs are Wine builtins: system32 vcruntime140_1/msvcp140/ucrtbase carry the Wine
  builtin marker, and both dumps list PE-Wine modules only (vcruntime140.dll in system32 is MS
  native but the builtin loads; no DllOverrides). Inventor/Bin ships no app-local CRT.
- Both dumps (17:09:54, 17:56:55), raw parse of the faulting thread's stack below the exception
  context: ntdll dispatch_exception -> call_seh_handlers -> call_seh_handler -> RxUtil's
  __C_specific_handler -> filter RxUtil::UnhandledExceptionHandler(EXCEPTION_POINTERS*, ITypeInfo*,
  DISPID) -> ("API method X caught an unhandled exception. Memory may have been left in an
  inconsistent state.") UTx::BugAlertBox + UTxUnhandledException::UnhandledExceptionHandler -> cer.dll.
  The __except sits in RxDispatch::Invoke (RxUtil+0x2b66c), Inventor's IDispatch::Invoke wrapper
  around ITypeInfo::Invoke. That's below the rpcrt4/combase frames, so COM's handler is never
  consulted. So the dispatcher did walk from the rethrow in call_catch_block4, through the
  consolidate frame, the FH4 fwsrv frames/funclets and oleaut32 up to RxUtil. The TEB chain in
  the dump is intact (call_catch_block4 finally -> rpcrt4 stub frame -> combase __EXCEPT_ALL).
- Windows would do the same if the exception occurred there: any exception escaping an API
  method is reported by Autodesk and kills Inventor. The exception is a rethrown MFC
  `CException*` (throw info mfc140u+0x3a9e48). It's thrown under a catch funclet in
  FWSrv+0x10e560, which calls `FWxChangeRequest::Execute`. The real bug is whatever makes that
  operation fail on Wine after the failed Buffer Prep Skid save (059).
- Synthetic probe `tests/cxx_catch_throw.c` + `cxx_catch_throw_eh.cpp` (clang 14 MSVC-target
  C++ EH, FH3; clang can't emit FH4). It throws from a catch block: rethrow, a new throw, a throw
  from a callee, nested catch+rethrow. Callers: a C++ catch, a native __except,
  cross-apartment IDispatch (plain and CreateStdDispatch/DispCallFunc), IPersist. Win11 VM and
  Wine integ 061fa687382 give identical results: always caught, RPC_E_SERVERFAULT over COM,
  and the second call works.
- Live retry: full `samples` run on prefixes/inv with +seh. The Buffer Prep Skid save passed
  this time, so Fan Cover Rebuild2 passed and there was no crash (the crash needs 059's
  failure first).
Next: pursue 059. When it reproduces, find what FWxChangeRequest::Execute throws: an MFC
CException from an operation that fails on Wine.
