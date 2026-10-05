# 194 winex11: window state leftovers found in the review of 174 (awesome restore never un-maximizes, stale _NET_WM_STATE request, 1 px creep, unflushed requests)
Status: draft, not worked on · found by the reviewer of fix/174 (2026-10-05, probes in inst/174-review/) · all are the
same on build-next and with the 174 fix, upstream code · related: 174, 178–180 (minimize / restore oddities), 077

1. **Application-side restore under awesome 4.3 never un-maximizes the X window** (`t6.sh`). Wine adds the two
   maximize atoms in one _NET_WM_STATE message, which sets awesome's `c.maximized`; the remove doesn't clear it, and
   `net_wm_state_serial` waits forever. This is Inventor's own restore button on the user's WM. Reproduced.
2. **Stale _NET_WM_STATE request.** `update_net_wm_states()` runs before 174's early return in
   `sync_window_position()`, so the 174 defect also exists for state: WM maximize plus a no-move SetWindowPos makes
   Wine ask to un-maximize; awesome ignores it and X stays 1598x974 against Win32 500x350 for good (`t5.sh`).
   Reproduced.
3. **1 px creep, cause.** `host_window_map_point()` ignores the frame's border width (awesome: 1), so every real
   ConfigureNotify maps 1 px up-left until the synthetic one corrects it. With a stale request in between the window
   moves (the trigger 174 removed). A second creep, on both builds: hide/show under awesome moves the window 1 px per
   cycle (200 → 199 → 198).
4. **Requests made at the state unlock are not flushed.** A refused WM resize stays on screen until the next X event
   on that thread.
5. **Fullscreen one-off** (seen once on build-next, not on two fix starts, not investigated): Mod4+m on the freshly
   started, application-maximized Inventor ended with _NET_WM_STATE_FULLSCREEN at 1920x1080.
6. **`tests/r174/wstate.exe` at 144 DPI** printed `win 29826157,29826157-…` for a window at about -5,-5 raw: either
   the tool or Wine's raw-to-virtual mapping of negative coordinates for DPI-unaware callers. Unchecked.

Harness notes from the same review: `inst/130/sm.sh` exports WINEDLLOVERRIDES (use `inst/173/sm.sh`);
`tests/r175/wm.sh` leaves awesome's default wallpaper on screen; never run one scratch prefix on two displays at once.
