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
