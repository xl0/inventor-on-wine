# Debugging running apps — checked at wine-11.18-218-g4e819f054dd

- Hung process where winedbg can't attach (it needs the loader lock for its break-in thread):
  `gdb -p PID -ex 'source tools/gdb/winesyms.py' -ex 'source tools/gdb/sehbt.py' -ex sehbt`
  (ELF + Wine PE symbols despite the preloader; .pdata unwinder for native PE code from each
  syscall frame; `p loader_section` = OwningThread (Wine tid = TEB+0x48 per thread); in gdb use
  `unsigned long long`, PE DWARF makes `long` 32-bit). Crit owners: `*(crit+0x10)`. 048.
- (Old host, yama scope 1: no strace/gdb attach; now ptrace_scope=0.) `winedbg` works:
  `wine winedbg --command "info process"`, then a `--file` script with
  `attach 0xPID` / `bt all` / `bt 0xTID` / `detach`. `bt all` walks every
  process in the prefix — filter by "in process 0PID".
- Linux tid → Wine tid: list `/proc/PID/task` sorted numerically; order matches
  winedbg's thread list order for the process (both are creation order).
- Hot-loop args without relay: `break *ADDR`, `cont`, `info all-reg`
  (shows xmm as doubles), `delete 1`, `detach`.
- Narrow relay: set `HKCU\Software\Wine\Debug\RelayInclude` (e.g.
  `kernel32.Sleep;kernelbase.Sleep`) then run with `WINEDEBUG=+relay`.
  Kill fast — a spinning caller writes ~100 MB/s.
- Display :98 screenshots: only trust `x/shot.sh` (plain root XGetImage). `xwd -root`
  switches to its multi-colormap path whenever a Wine window is mapped (winex11 gives
  each process its own colormap; openbox installs the focused one): 24 bpp packed
  output, and some Wine windows (dialogs over Inventor) come out black (036).
  Cross-check coords with Win32 `GetWindowRect` vs `xwininfo -root -tree`.
- Map a winedbg address in third-party code to pseudo-C: `tools/decomp.sh
  BIN decomp ADDR` (VA if the module sits at its preferred base, else
  ADDR - module base as RVA; `info share` in winedbg lists bases).
- Installers that run throwaway `%TEMP%\*.bat` helpers (InstallBuilder: its
  log `%TEMP%\installbuilder_installer_<pid>.log` only shows exit codes):
  capture them with a host loop `cp $TEMP/*.bat out/; sleep 0.02`.
- Crash backtraces from apps whose stdout is lost (e.g. Inventor): AeDebug
  `Debugger` runs `swprintf(value, pid, event)`, so a cmd redirect works:
  `wine reg add 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug' /v Debugger
  /d 'cmd /c winedbg --auto %ld %ld >>C:\winedbg-crash.log 2>&1' /f` (default value:
  `winedbg --auto %ld %ld`; 32-bit apps read the Wow6432Node copy). Also set
  `HKCU\Software\Wine\WineDbg` `ShowCrashDialog`=dword:0. With the dialog on, the
  `--auto` winedbg waits in the dialog before printing anything, so with no one to click
  it you get no log and the crashed process never exits.
  Log: registers, backtrace of the faulting thread, modules, threads. The process
  then exits.
- Intermittent crash in third-party code whose app-side crash handler (Autodesk CER) writes
  minidumps without heap: attach `winedbg --file script.txt` to the running app (script:
  `attach 0xPID`, `break *ADDR` on a rarely-hit precursor such as the app's own NULL guard,
  then a few thousand `p $eax` / `x /16x $rdi` / `bt` / `cont` blocks, `detach`). Module
  base from `/proc/<unix pid>/maps`. Examine syntax: `x /Nx EXPR` with `$reg` (32-bit words).
  The attached app runs ~2-3x slower (debug events); killing winedbg kills the debuggee.
  Autodesk OGS prints its errors via OutputDebugString ("OGS ERROR: ..."), visible there
  or with `+debugstr` (047).
- Managed exceptions in CoreCLR-hosted code (Inventor's .NET 10 add-ins, no debugger): start the
  app with `DOTNET_EnableEventPipe=1 DOTNET_EventPipeOutputPath='C:\t\x.nettrace'
  DOTNET_EventPipeConfig=Microsoft-Windows-DotNETRuntime:0x8000:4` (exception keyword), then
  `strings -el -n 6 x.nettrace | sort | uniq -c`: exception types, messages and the throwing
  methods' names (064: "Invalid SID." in Connectivity IOUtil::HasWritePermission). .NET Framework
  4.x ignores these variables.
- File opens (NtCreateFile) happen in wineserver, not the app: strace the prefix's wineserver
  for `openat`; strace on the app only shows path lookups (stat) and directory reads.
- Handle leaks: `tools/soak/resprobe.c PID dump` lists every handle (value, type, name) of a
  process on Wine and on the VM; diff dumps around a workload. Unnamed objects: relay their
  creator (`RelayInclude` = e.g. `kernelbase.CreateMutexW;...`, uppercase `KERNEL32.X` lines carry
  the real `ret=`), keep the creation `retval` per handle value, match against the live handles of
  a later dump; module bases from `resprobe mods` (/proc maps misses most PE images). 072.
  GetProcessHandleCount (ProcessHandleCount) is a stub returning 0 in Wine: conformance tests
  count own handles via NtQuerySystemInformation(SystemExtendedHandleInformation) (067).
- Creation sites of leaked unnamed events/sections without relay (086): `tools/soak/handle-trace.patch`
  (debug only, don't commit): wineserver logs `LK A/C` per handle of an unnamed event/section
  (start the server by hand with `WINE_LEAK086=1`, stderr to a file), kernelbase logs a backtrace per
  CreateEvent/CreateFileMapping in Inventor.exe under `WINEDEBUG=trace+leak`;
  `tools/soak/handle-attr.py` groups the live ones by stack. Unwinding in every process that
  inherits WINEDEBUG (licensing agent, WebView2) left Inventor on its splash (both with
  RtlCaptureStackBackTrace and a static-table unwinder); limited to Inventor.exe it works. The patch
  unwinds with RtlLookupFunctionTable + RtlVirtualUnwind (static images only; RtlLookupFunctionEntry
  may call CoreCLR's function-table callback, which takes CLR locks). A raw stack scan is safe but
  full of stale frames.
- Perf on PE code: `perf report --sort dso,sym` shows PE addresses as file offsets; for Wine's PE
  dlls offset = RVA (.text at 0x1000 both), so `addr2line -f -i -e build/dlls/X/x86_64-windows/X.dll
  $((ImageBase + off))`. Autodesk CER leaves `Temp\Inventor<ts>.dmp` (+ `.tmp` XML with the last
  commands, `Inventor<ts>.txt` with the CLR stack) and keeps `cer_dialog.exe` running (074).
- Native crash in a host-library thread (lavapipe/llvmpipe/GLib workers have no TEB): with 102's
  ntdll fix Wine prints `err:seh:handle_host_thread_signal signal N at PC (addr A) in a non-Wine
  thread` and the default action kills the process (before: a second SEGV in the handler, rc 139,
  no trace). ptrace (gdb attach) changed timing enough to hide 102's race; instead a temporary
  `backtrace()`/`backtrace_symbols_fd(.., 2)` in that function prints `lib.so(+off)` frames
  through the signal frame. Mesa symbols: Ubuntu's `*-dbgsym_<ver>_amd64.ddeb` from
  ddebs.ubuntu.com (debuginfod.ubuntu.com lacked them), `dpkg-deb -x` to scratch (no install),
  `addr2line -f -i -e .../.build-id/xx/yyyy.debug OFF`. Mesa source: tag `mesa-<ver>`.
- CoreCLR frames (`coreclr.dll+RVA`) without disassembly: Microsoft publishes the PDB. RSDS GUID+age from
  the PE debug directory → `https://msdl.microsoft.com/download/symbols/coreclr.pdb/<GUID><age>/coreclr.pdb`,
  `llvm-pdbutil-14 dump --publics --section-headers`, nearest public symbol per RVA; then read the function in
  dotnet/runtime at the matching tag (124). No llvm-pdbutil on the 26.04 host: `tools/pdbpub.py FILE.pdb RVA...` does
  the lookup (works for any MS module with a published PDB, e.g. mfc140u.amd64.pdb; MFC's source ships with Visual Studio).
- C++ exception already caught, box on screen (MFC `ReportError`): the catch block runs on top of the thrower's frames,
  which sehbt skips. `stackscan RSP RSP+0x5000` on that thread, find `_CxxThrowException`'s return slot above
  `call_consolidate_callback`, then `sehfrom SLOT` unwinds the throw chain (164: COleDocument::OnIdle's ENSURE).
- Standalone .NET 10 tests without an SDK: compile C# on the VM with the .NET 4.8 csc
  (`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /platform:x64`), put a
  `X.runtimeconfig.json` (framework Microsoft.NETCore.App 10.0.0, rollForward LatestMinor) next to it and run
  `dotnet.exe X.exe` (copy `Program Files/dotnet/{dotnet.exe,host,shared/Microsoft.NETCore.App}` from an inv
  prefix into a scratch prefix, 77 MB). Don't run it with `WINEDLLOVERRIDES=mscoree=`: IL-only
  assemblies (mscorlib.dll facade) then fail to load with "Module not found" (124).
- Inventor crash at startup with an access violation reading 0 at `CommonUI.dll+0x60b90` (CUIxMessageTerm constructor,
  NULL channel builder; stack FwWebBrowser.dll+0x330ee <- FwUI.dll+0x45865a), log full of `err:module:fixup_imports_ilonly
  mscoree.dll not found, IL-only binary L"System.Runtime.dll"` and one `err:seh:user_callback_handler ignoring exception
  e0434352`: Inventor was started with `WINEDLLOVERRIDES=mscoree=` in its environment (meant for wineboot only). The CLR
  exception is thrown in the main frame's WM_CREATE and swallowed by Wine's callback handler; the crash comes later (152).
- Minidumps without a debugger: `tools/mdmp.py DUMP [-m] [-n FRAMES]` prints the exception (code, address, registers) and
  scans the faulting thread's stack for return addresses as module+RVA; `tools/decomp.sh BIN decomp RVA` names the function.
- Process that exited "from the wrong thread" (165): `err:vulkan:vkXxx Exception 0x... in Unix call.` means winevulkan
  called `ExitProcess(3)` on the thread that made the Vulkan call, for wined3d the command stream thread; DLL detach code
  then runs there (wined3d's `cs->thread_id != GetCurrentThreadId()` assertion box when an app DLL releases D3D objects).
  Signs without a log: `/proc/PID/environ` of the process gives "Permission denied" (thread group leader is a zombie:
  the main thread was killed, another thread lives on; `pgrep` still lists it, `prefix.sh kill-inventor` and run.sh's
  "is Inventor running" check don't see it) and a 0-byte `Temp\Inventor<ts>.dmp`. A harness `dialog-N.png` in the
  *previous* scenario's folder dates the box.
- Forcing a "present vs. window destroyed" race: make the command stream thread lag (a Sleep before each present,
  inst/165/debug-165.patch `WINE_165_PRESENT_DELAY=ms`); the same patch prints who calls RtlExitUserProcess and the
  stack of a failing `wined3d_not_from_cs()`.
