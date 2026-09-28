# 004 CreateProcess succeeds when an exe manifest dependency is missing
Status: fixed · Owner: 004 worker · Branch: fix/004-createprocess-sxs-error · Found in: tests/sxs_probe (by 002 worker)

## Symptom
Exe with an embedded manifest that depends on an assembly that can't be found:
Windows fails CreateProcess with ERROR_SXS_CANT_GEN_ACTCTX (14001); Wine starts
the process with an empty default activation context.
Ground truth: tests/sxs_probe/windows.txt cases none, sub_nocfg, wrongname,
nons_cfg, dotnet_cfg (launch.c prints the CreateProcess error).

## Task
Low priority (only matters for apps that probe for this). Confirm where Windows
fails (parent CreateProcess vs child init), match it, kernel32 test.

## Findings (004 worker)
Windows (tests/sxs_probe/cantgen.c, cantgen_windows.txt): missing dependency or
malformed exe manifest -> CreateProcess 14001, also with CREATE_SUSPENDED. The
process object exists briefly (job gets NEW_PROCESS + EXIT_PROCESS), but no
debug events arrive with DEBUG_PROCESS: the context is generated parent side
after the process is created, then the process is killed before it runs.
Same from 32-bit parents. Common-Controls 5.82 resolves on Windows (in-box);
Microsoft.VC90.OpenMP doesn't (not in-box).
CreateActCtx rejects a wProcessorArchitecture other than the caller's (87), so
there is no public way to resolve for another arch (archflag.c).

## Fix
- kernelbase CreateProcessInternalW: after NtCreateUserProcess (thread still
  suspended), RtlCreateActivationContext on the image (resource 1, appdir = exe
  dir); on STATUS_SXS_CANT_GEN_ACTCTX terminate + fail. Only when the child
  machine == RtlWow64GetCurrentMachine() ("*" deps resolve to the current
  arch); cross-arch launches keep the old lenient behaviour.
  Not covered: the first process of a session (`wine app.exe`, no parent).
- comctl32: WinSxS manifest for Common-Controls 5.82 (v5 dll), else apps
  depending on it (work on Windows and on Wine today) would fail to start.
- Remaining in-box Windows public assemblies Wine lacks (would now fail too):
  Microsoft.Windows.IsolationAutomation(.ProxyStub), Microsoft.Windows.SystemCompatible.
- Test: kernel32 actctx test_CreateProcess_missing_dependency (uses a copy of
  the exe: a manifest next to the shared test exe broke concurrently running
  kernel32:debugger in regress.sh).

## Coordinator decision (parked, not merged)
Fix works (branch fix/004-createprocess-sxs-error), but it turns "Wine lacks a
built-in Windows assembly" into "process refuses to start", where today such
exes run. Known missing: Microsoft.Windows.IsolationAutomation,
Microsoft.Windows.SystemCompatible. Before merging: audit the assemblies a
stock Windows 11 resolves without redists (VM: C:\Windows\WinSxS manifests /
probes) against Wine's builtin winsxs set, add the missing ones Wine can
serve, and only then merge. Low priority — few apps depend on the failure.
