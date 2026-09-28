# 058 C++ throw inside a catch block escapes the outer SEH handler (Inventor crash)
Status: open · Owner: - · Branch: - · Found in: samples campaign, after 056 (integ c09f08e4924, inv3)

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
