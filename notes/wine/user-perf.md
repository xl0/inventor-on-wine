# USER call cost (server round trips) — checked at wine integ bbc7f82accb + fix/057

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
- Still server calls: GetWindow(GW_HWNDFIRST/LAST/OWNER of other processes),
  GetProp (~8-20 us vs 0.65 us), PeekMessage/GetMessage when the queue is empty,
  hook chains (get_hook_info/start_hook_chain per hooked message).
- MFC apps (Inventor) run CWinApp::OnIdle after every message: the idle-time
  command UI update walks the whole window tree (GetWindow + SendMessage per
  window). Each incoming COM call is a message, so every cross-process call paid
  one tree walk (~25 ms for Inventor's thousands of windows before 057).
- Profiling recipe: `perf record --call-graph dwarf -t <unix tid>` stops at
  `__wine_syscall_dispatcher` (PE frames unknown) but names the Nt*/NtUser* entry
  and the unix-side helper; winedbg `attach`/`bt 0xTID`/`detach` loops give PE
  stacks (slow, ~1/s, disturbs timing). Server request histogram: `strace -p TID
  -e write,writev -xx -s 8`, the first int of each request is its number in
  `enum request` (include/wine/server_protocol.h).
