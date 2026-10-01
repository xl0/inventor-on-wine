# USER call cost (server round trips) — checked at wine integ ed241c72d09 + fix/081

- A wineserver round trip costs ~20 us on this host (120 threads, client and
  server on different cores); Windows answers the same USER queries from the
  desktop heap in ~10 ns (`tests/getwindow_perf.c`, issue 057). Anything an app
  calls per window per message becomes visible.
- Client-side (no server call): GetWindowLong/GetParent/IsWindowVisible of own
  windows (`WND` struct, ~100 ns), anything in the shared objects
  (`server/protocol.def`: desktop/queue/input/class/window `*_shm_t`, read with
  `get_shared_window` + seqlock loop in win32u). With 057 also
  GetWindow(GW_HWNDNEXT/GW_HWNDPREV/GW_CHILD): `window_shm_t` next/prev/
  first_child/last_child, kept by `server/window.c:update_shared_children`
  (called on every Z-order list change: link_window, set_parent_window,
  update_window_zorder).
- 081 caches with a serial in shared memory (bumped by the server on every change,
  read with the seqlock loop, snapshot keyed by it):
  hook chains (`queue_shm_t.hooks_serial` + `get_hook_chain` snapshot per thread/id;
  LL/winevent hooks still use start/get_hook_info/finish), GetProp
  (`window_shm_t.props_serial`, per-thread cache; results by unknown names aren't cached).
- Still server calls: GetWindow(GW_HWNDFIRST/LAST/OWNER of other processes),
  PeekMessage/GetMessage (every call that returns a message, and when the queue
  bits say there may be one), GetProp by a name that isn't an atom.
- MFC apps (Inventor) run CWinApp::OnIdle after every message: the idle-time
  command UI update walks the whole window tree (GetWindow + SendMessage per
  window). Each incoming COM call is a message, so every cross-process call paid
  one tree walk (~25 ms for Inventor's thousands of windows before 057).
- Profiling recipe: `perf record --call-graph dwarf -t <unix tid>` stops at
  `__wine_syscall_dispatcher` (PE frames unknown) but names the Nt*/NtUser* entry
  and the unix-side helper; winedbg `attach`/`bt 0xTID`/`detach` loops give PE
  stacks (slow, ~1/s, disturbs timing). Server request histogram: `strace -p TID
  -e write,writev -xx -s 8`, the first int of each request is its number in
  `enum request` (include/wine/server_protocol.h); count only pipe fds (X11 writes
  go to a socket). openbench `INVSCEN_SYNC` brackets one walk for a tracer (081).
- Benchmarks on this host (088): per-call timings of cross-process calls are bimodal (e.g. openbench
  walk 1.8 vs 3.6 s) by CPU placement: C6 exit latency is 290 us (powersave, menu governor). Pin the
  processes (taskset) and keep the siblings busy (nice-19 spinners) for stable numbers.
- Windows 11 wait granularity (tests/wait_granularity.c): a process without timeBeginPeriod gets
  ~15.6 ms for 1 ms Sleep/waits/waitable timers (HIGH_RESOLUTION timers 1.6 ms), even if
  NtQueryTimerResolution says 1 ms. Wine with 089: same (ntdll round_timeout, server timer rounding;
  WINE_TIMER_RESOLUTION=0 turns it off; Wine timing threads exempt via ThreadWineHighResolutionTimers).
- Absolute timeouts (NT time > 0) go to the server unchanged and expire on its gettimeofday.
  NtQuerySystemTime must read the same precise clock (093: CLOCK_REALTIME_COARSE lagged ~1 ms,
  so deadlines built from it expired early by the caller's clock). Windows: an absolute wait/timer
  never returns before NtQuerySystemTime() >= due; due already passed = immediate, no tick wait;
  absolute deadlines follow wall-clock steps (tests/abs_timeout.c).
- Idle CPU per process of a prefix: tools/idlecpu.sh; server request mix per client: strace the
  wineserver's reads and map pipe inodes (/proc/PID/fd) to clients.
- Document creation in Inventor (098): server request stats per client thread with the debug patch
  `tools/wineserver-reqstats.patch` (SIGHUP to wineserver dumps counts + handler time to
  /tmp/wineserver-reqstats.txt; also per-process view counts), PE backtraces of the callers of an Nt*
  call with `tools/gdb/bpbt.py` (`bpbt NtFoo N SKIP` in gdb: unix-side breakpoint + .pdata unwind from the
  syscall frame; PE-side stub breakpoints see a stale frame). Off-CPU of one thread:
  `perf record -e context-switches -c 1 --call-graph dwarf -t TID` (works with paranoid 1).
- Costs found there: WIC CreateComponentEnumerator re-reading HKCR per call (2.3 ms vs 3 us native;
  cached since 098), server view list scans (tree since 098), ADP analytics mutexes (~2k
  WaitForSingleObject/ReleaseMutex per document; in-kernel only with ntsync, host kernel 6.8 has none),
  CreateFileMapping/MapViewOfFile/Unmap cycles: 8 server calls, ~250 us vs 49 us on Win11
  (tests/mapcycle_perf.c), case-insensitive misses scanning whole directories (find_file_in_dir).
