# Debugging running apps — checked at wine-11.18-218-g4e819f054dd

- ptrace is blocked (yama scope 1), so no strace/gdb attach. `winedbg` works:
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
- Perf on PE code: `perf report --sort dso,sym` shows PE addresses as file offsets; for Wine's PE
  dlls offset = RVA (.text at 0x1000 both), so `addr2line -f -i -e build/dlls/X/x86_64-windows/X.dll
  $((ImageBase + off))`. Autodesk CER leaves `Temp\Inventor<ts>.dmp` (+ `.tmp` XML with the last
  commands, `Inventor<ts>.txt` with the CLR stack) and keeps `cer_dialog.exe` running (074).
