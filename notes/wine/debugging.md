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
- Display :98 screenshots: only trust `x/shot.sh` (root GetImage on the
  headless NVIDIA server is packed 24 bpp despite a 32 bpp header).
  Cross-check coords with Win32 `GetWindowRect` vs `xwininfo -root -tree`.
- Map a winedbg address in third-party code to pseudo-C: `tools/decomp.sh
  BIN decomp ADDR` (VA if the module sits at its preferred base, else
  ADDR - module base as RVA; `info share` in winedbg lists bases).
- Installers that run throwaway `%TEMP%\*.bat` helpers (InstallBuilder: its
  log `%TEMP%\installbuilder_installer_<pid>.log` only shows exit codes):
  capture them with a host loop `cp $TEMP/*.bat out/; sleep 0.02`.
