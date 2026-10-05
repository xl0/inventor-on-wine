# 171 win32u: two threads changing the surface of one window corrupt the window surface list (NULL write in register_window_surface, lock leaked, later hang)
Status: fixed (branch, not on integ; reworked after review) · Owner: worker 171 · Branch: fix/171 (3 commits on integ b5d75449ffe, tip 545a8c7b1fb; first version = fix/171-v1, rejected) · Found in: 157 (lockstress on the host Wayland session) · win32u, driver independent · upstream bug

## Symptom
A thread hangs forever in `pthread_mutex_lock(&surfaces_lock)` (dce.c: `register_window_surface` or
`flush_window_surfaces`), the owner of the mutex is a live thread that is not inside the locked region any more; soon
every UI thread of the process waits there. Before that, one `UpdateLayeredWindow` or `SetWindowPos` returned
0xc0000005 (reads as TRUE). Other outcome of the same race: `flush_window_surfaces` locks the mutex of a freed surface
(use after free), holding `surfaces_lock`.

## Upstream or ours
Upstream. `register_window_surface` after `release_win_ptr` is unchanged since the code moved to win32u (44f667fa14c,
a8b0b07c45c), also in origin/master 455e3509b98. Reproduced on the unpatched master build (4e819f054dd,
`wt/regress-master-build-h26`): 10 of 10 runs. Our commits in window.c / dce.c don't touch the gap (the only line of ours
in the locked part of `apply_window_pos` is 027's NtUserPostMessage); the rate on `build/` is the same.

## Windows (Win11 VM, `tests/r171/ulwrace.exe basic`, output in inst/171/basic-win11.txt)
| call from a thread that doesn't own the window | owner pumping | owner not pumping |
|---|---|---|
| UpdateLayeredWindow (content, move + resize) | TRUE, 0 ms, rect and screen updated, no message to the owner | same: TRUE at once |
| UpdateLayeredWindow, psize NULL with a source DC | TRUE, nothing drawn (Wine draws: not looked at) | - |
| UpdateLayeredWindow on a hidden window | TRUE | - |
| UpdateLayeredWindow after SetLayeredWindowAttributes | FALSE, ERROR_INVALID_PARAMETER (Wine same) | - |
| SetLayeredWindowAttributes | TRUE | TRUE at once, screen updated (Wine: TRUE, the surface update is posted to the owner) |
| SetWindowPos | TRUE, WM_WINDOWPOSCHANGING/CHANGED... in the owner thread | blocks until the owner pumps (1016 ms in the probe; Wine same) |

So UpdateLayeredWindow on another thread's window is a legitimate, direct operation that must not go through the
owner's message loop. The race stress (`ulwrace race 3000 2`) runs clean on Windows: 3000 resizes, 20978 updates, 0 errors.

## Cause
`apply_window_pos` replaced `win->surface` with the window pointer held (user lock) and called
`register_window_surface( old, new )` after `release_win_ptr`. `UpdateLayeredWindow` is the one caller that runs on a
thread that doesn't own the window (SetWindowPos is sent to the owner, `update_window_state` posts), so two threads can
be in `apply_window_pos` for one window:

1. A (owner, SetWindowPos with a new size): `win->surface` S0 -> SA, releases the window, waits for dce.c's
   `surfaces_lock` (a third thread holds it: `flush_window_surfaces` keeps it over the driver flushes).
2. B (UpdateLayeredWindow): `win->surface` SA -> SB, reaches `register_window_surface( SA, SB )`.
3. The flushing thread unlocks; B gets the mutex before A: `list_remove( &SA->entry )` on an entry that is still
   {NULL, NULL} from calloc: write to NULL + 8 with `surfaces_lock` held. ntdll turns the fault into the return value
   of the syscall (172) and the mutex is never released.
4. If A wins there is no fault. With a surface that had been in the list before (`get_window_surface` reuses the
   window's surface when the size matches, so a thread can put back one that the other thread already replaced) the
   misordered `list_remove` doesn't fault: it relinks stale neighbours, and a late `list_add_tail` leaves a surface in
   the list that the window no longer references. It is freed when the caller drops its reference and
   `flush_window_surfaces` then locks a destroyed mutex (seen: `ulwrace destroy` on build/, 4 of 10 runs stuck in
   `window_surface_lock <- window_surface_flush <- flush_window_surfaces` with `surfaces_lock` held, no fault logged).

`destroy_window` and `destroy_thread_windows` had the same gap (surface taken under the lock, unregistered later).
The flushing thread matters: with two threads only (no third thread pumping) 13 runs x 3000-5000 rounds stayed clean;
with one painting + pumping thread the fault comes after 15-80 owner rounds on an idle host, 300-2600 under load.

Evidence: inst/171/seh/run-1.log (`WINEDEBUG=+seh`) + stuck-1.txt (gdb):
```
016c:trace:seh:handle_syscall_fault code=c0000005 flags=0 addr=0x758cd15f0550 ip=758cd15f0550     register_window_surface+96: mov %rax,0x8(%rcx)
016c:trace:seh:handle_syscall_fault  info[0]=0000000000000001  info[1]=0000000000000008              write to NULL + 8
016c:trace:seh:handle_syscall_fault  rax=0 rcx=0 rdx=0000758cc40a3f80 rsi=0000758cc40a5e40 rdi=<surfaces_lock>   rdx = old, rsi = new
016c:trace:seh:handle_syscall_fault returning to user mode ip=00006ffffdc27644 ret=c0000005         NtUserUpdateLayeredWindow
Thread 3: register_window_surface (old=0x758cc40a63f0, new=0x758cc40a3f80) <- apply_window_pos <- set_window_pos <- NtUserSetWindowPos   (still waiting to add ...3f80)
Thread 2: flush_window_surfaces <- NtUserPeekMessage, waiting for surfaces_lock
```
The surface the updating thread removed (rdx, ...3f80) is the one the owner thread is still waiting to add. (In the
157 evidence, `inst/157/evidence/171-fault.txt`, the registers read the same way: rdx/r13 = old, rsi/r12 = new.)

## Fix (fix/171, second version; the first one is kept as fix/171-v1 and must not be merged)
- 6939f5b5a58 `win32u: Count the registrations of a window surface.` `register_window_surface` stays where upstream
  calls it (after the window is released); `struct window_surface` gets `LONG registered` (after `ref`: fills padding
  on 64-bit, no other field moves; WINE_GDI_DRIVER_VERSION 112 as for 062's fields) and dce.c does
  `if (old && !--old->registered) list_remove(...)`, `if (new && !new->registered++) list_add_tail(...)` under
  `surfaces_lock`. No lock is nested that wasn't before.
- 9d6e6146deb `win32u: Release a window surface that was set while the window is destroyed.` `free_window_handle`
  takes `win->surface` under the lock that removes the handle and unregisters + releases it after `user_unlock`
  (UpdateLayeredWindow between `destroy_window` dropping the surface and the handle being freed installed one that
  nobody released: 81 list entries left after 1500 create/destroy rounds).
- 545a8c7b1fb `user32/tests: Test UpdateLayeredWindow on a window of another thread.` (`test_layered_window_threads`):
  update with move + resize while the owner thread doesn't process messages (deterministic; guards against forwarding
  to the owner), then up to 500 owner resizes against updates from the main thread with a third thread painting.
  All waits bounded (10 s, ok()), 30 s deadline for the update loop, helper windows WS_EX_NOACTIVATE.

Why the counter is right (derived again, not taken from the review):
- Every change of `win->surface` happens under the user lock, so per surface S the changes form one sequence, and since
  a window has one surface at a time it alternates: in, out, in, out... (S can come back: `get_window_surface` reuses the
  window's surface when the size matches, a thread can hold such a stale choice; the driver returning its previous
  surface is the same case). A change with old == new is neither (skipped in `register_window_surface`, and the
  reference taken and dropped cancel out). The dummy surface is filtered before counting.
- Each change is followed by one `register_window_surface` call from the thread that made it; the calls run under
  `surfaces_lock` in any order. `registered` = executed ins - executed outs; the list operations happen exactly at
  0 -> 1 (add, entry not linked) and 1 -> 0 (remove, entry linked), so the entry is never unlinked twice or linked
  twice, whatever the order (out before in: 0 -> -1 -> 0, never listed).
- Listed implies alive: `registered > 0` means some in has run whose matching out has not. Either that out's change
  hasn't happened (S is still `win->surface`, which holds a reference) or it has and the thread that made it still
  holds the reference it took over from the window: all four callers release it after their
  `register_window_surface` call. So a surface is never freed while listed, and every call runs on a live surface
  (the in is made by a thread that holds its own reference from `get_window_surface`).
- When all calls have run: listed exactly if S is a window's surface. In between a surface can be the window's without
  being listed yet, or listed while already replaced: both exist upstream (register after release) and only decide
  whether one flush pass includes it.
- Destroy racing an install: the install after `destroy_window` cleared the surface is an in whose out is
  `free_window_handle`'s (2nd commit); no install can follow, the handle is gone under the same lock. Thread exit:
  `destroy_thread_windows` nulls the handles and takes the surfaces in one locked pass; a pending in of another thread
  is cancelled by its out, in either order.
- Scaled wrappers (DPI): the window's surface is the wrapper, only it is registered; `scaled_surface_set_target` swaps
  the driver surface inside without changing `win->surface` (old == new). Wrapper <-> bare driver surface switches are
  ordinary out + in on two objects. Run in a 144 dpi prefix (gdb: both listed surfaces `scaled_surface_funcs`).
- A driver handing one surface to two windows (none does) would still be handled: listed while any window has it.

Rejected: forwarding UpdateLayeredWindow to the owner thread (Windows doesn't; breaks callers whose owner doesn't pump);
registering under the user lock (first version, see below).

## Review of the first version (inst/171-review/, 2026-10-04) and what was wrong
fix/171-v1 moved `register_window_surface` in front of `release_win_ptr`: user lock -> dce.c `surfaces_lock`. My argument
"nothing under `surfaces_lock` takes the user lock" only covered what the lock holder calls itself. `flush_window_surfaces`
holds the list lock while it waits for each surface mutex, and upstream has holders of a surface mutex that take the user
lock (draft 176): `NtUserUpdateLayeredWindow` (surface locked -> NtGdiAlphaBlend from a window DC with a dirty visible
region -> `update_visible_region` -> `get_win_ptr`) and `move_window_bits_surface` (-> NtGdiSetDIBitsToDeviceInternal ->
`update_dc` -> in winex11 `X11DRV_GetDC` waits for win_data, held by `X11DRV_WindowPosChanged` waiting for the user lock).
Found by the reviewer:
1. user -> list lock -> surface mutex -> user: `rv.exe wdc 12` hangs 10 of 10 on v1, 0 of 10 on integ; three threads,
   none touching another thread's window.
2. user -> list lock -> surface -> win_data -> user on winex11 (once in 92 loaded runs).
3. Every USER call in the process waits behind whole flush passes: `rv.exe lat 12 8`, GetWindowLongW on another thread's
   window max 454-1343 ms (integ 0.5-1.5 ms). My benchmark missed it: its painters call GetDC per paint and were
   throttled by the same lock.
4. winewayland and winemac take their window data in the flush callback: the same widening there (derived).
Also from the review: drafts 176 (win32u updates a window DC with a surface locked: an upstream user -> surface -> user
cycle on one window) and 177 (Xlib: `X11DRV_ThreadDetach` -> XSync -> `_XError` waits for the display lock held by a thread
in `create_shm_image` across its own XSync). Not fixed here.

## Verification (second version, wt/171-build = 545a8c7b1fb; Xvfb :1400 fix / :1401 build/, prefixes inst/171/pfx-*)
`tests/r171/run.sh BUILD TAG RUNS TIMEOUT EXE ARGS`; `rv.exe` = the reviewer's probe (inst/171-review/rv.c).
| probe | build/ (integ b5d75449ffe) | fix |
|---|---|---|
| `ulwrace race 1500 1` | 27 of 30 FAULT | 200 of 200 ok |
| `ulwrace destroy 1500 1` | 10 of 10 ok this time (before the reboot 4 of 10 stuck) | 20 of 20 ok |
| surfaces left after `destroy 1500 1` (`tests/r171/leak.sh`) | - | 0 |
| `ulwrace exit 2000 1` | (10 of 10 ok before) | 10 of 10 ok |
| 144 dpi prefix (scaled wrapper surfaces): `race 1500 1`, `destroy 1500 1`, leak | - | 30 of 30, 10 of 10, 0 left |
| `rv.exe wdc 12` (review finding 1; v1: 10 of 10 HANG) | 10 of 10 DONE | 10 of 10 DONE |
| `rv.exe lat 12 8`, GetWindowLongW on another thread's window, 3 runs (v1: max 454-1343 ms) | max 2706, 3148, 264 us; calls > 1 ms: 7, 5, 0 | max 506, 222, 562 us; calls > 1 ms: 0, 0, 0 |
| `rv.exe stress 20 rgn gl` | 6 of 6 FAULT (171) | 6 DONE, 4 HANG of 10; with stacks 5 DONE, 3 stuck of 8, all three = 177 |
| `rv.exe stress 20 wdc rgn` | 5 FAULT, 5 HANG of 10 | 10 of 10 HANG = 176 (owner in `update_surface_region` wants the surface, updater in NtGdiAlphaBlend -> `get_win_ptr` wants the user lock; inst/171/v3-stress-hold/stuck-1.txt) |
| `tests/r157/lockstress.exe 3000 1 nogl` | (8 ok, 2 BadWindow of 10 before) | 9 ok, 1 BadWindow death (173) of 10 |
The 177 hangs (stacks inst/171/v3-gl-hold/stuck-{1,3,8}.txt: thread 2 `X11DRV_ThreadDetach -> XSync -> _XError` in
pthread_cond_wait, another thread in `create_shm_image <- X11DRV_CreateWindowSurface`) are at the rate the review measured
for variants that don't serialize surface changes behind flush passes (about 30 %); v1 hid most of them (6 of 80) by
doing exactly that. integ can't be compared with this stress: it faults (171) first.

Conformance (`tools/regress.sh unit`, 2 runs per arch, vs deps/regress/b5d75449ffe...-h26, the newest): user32:win fail 4
(base 4, the same two lines), user32:msg fail 1 (1), user32:sysparams fail 6 (6), gdi32:dc fail 3 (3), user32:dce,
user32:input, win32u:win32u, d3d9:device pass. 0 worse.

Windows: see "Windows runs of the test" below. Inventor: not run in this round (licence seat elsewhere); the first
version passed the suite on inv4, the coordinator runs it for the second one in the merge round.

### Windows runs of the test (Win11 VM, 2026-10-04 evening)
The VM was unstable after the host reboot: `vm/run.sh` boots Windows to the logon, then qemu-system-x86_64 10.2.1 dies with
SIGSEGV (seen with qemu in the foreground: "Segmentation fault (core dumped)"; "previous shutdown was unexpected" in the
Windows event log for boots from 19:34 on, i.e. also for other workers' starts; lifetimes 35 s to 11 min; after two failed boots Windows sits in its recovery screen, where the VM
stays up; `qemu-img check`: 18 leaked clusters, no errors). Started from a private copy of the launcher without the
virtio-fs device (inst/171/vmrun-novfs.sh, no Z: share; winrun doesn't need it) it stayed up for the whole session
(> 50 min) and all numbers below are from that session. It is still running that way.
- New subtest alone (test build with an `r171` argument, inst/171/user32_test_r171*.exe): 5 of 5 pass on x86, 5 of 5 on
  x64, 4-6 s each. (In the crashing session before: x64 3 of 3; x86 1 pass, 1 timeout after 120 s, 1 run where the owner
  thread had no window after 10 s, then qemu segfaulted during the next run: not reproduced on the stable VM.)
- Whole user32:win, integ and fix test binaries alternating, both arches:
  | | runs | clean | test_mdi (win.c:2750, scroll info) | other |
  |---|---|---|---|---|
  | integ (build/) | 18 | 11 | 6 runs | 1 run: win.c:13655, 13657 |
  | fix | 20 | 12 | 6 runs | 1 run: test_mouse_input (11 lines); 2 runs: test_topmost (win.c:12459, 12462, once also 12733) |
  The new subtest never failed. test_mdi and test_mouse_input run before it. test_topmost runs after it: its two
  failures were consecutive runs (x64, then x86) 6-9 minutes after the VM boot and didn't come back in the 16 fix runs
  after that; integ has no test_topmost failure in 18 runs but another late one. I can't tie it to the new subtest (its
  windows are gone and never activated when it returns) and can't rule it out with these numbers.

## Not covered (existing, upstream)
- winex11 dies with BadWindow (X_GetProperty <- XGetWMHints <- handle_wm_hints_notify <- X11DRV_PropertyNotify) when
  the first UpdateLayeredWindow of a window comes from another thread while the owner handles events: the layered
  surface switches the visual, `set_window_visual` re-creates the X window, the owner's handler looked the HWND up
  before and uses the old `event->window` after waiting for the win data. Backtrace: inst/171/xerr-bt.txt; repro
  `ulwrace destroy 2000 1 - x` (8 of 10 runs on build/, same on master and with the fix). Belongs to 173 (BadWindow
  deaths); lockstress on Xvfb dies the same way (X_ChangeProperty / X_GetProperty) in 1-2 of 10 runs on both builds.
- `scaled_surface_set_target` (DPI-scaled surfaces) changes a shared surface without a lock; two threads in
  `create_window_surface` for one window can race there. Not reachable in our setups (no scaling), not touched.
- Two threads' `pWindowPosChanged` calls for one window run in any order after the window is released; the drivers
  don't keep the surface pointer (read: winex11, winewayland), so this is only about which geometry is applied last.
- `destroy_window` reads `win->current_drawable` after `release_win_ptr`.
- Wine sets last error 1400 on successful cross-thread UpdateLayeredWindow, draws for psize NULL, and delays the
  SetLayeredWindowAttributes update until the owner pumps (table above).

## Review, interrupted (2026-10-04, read-only so far; nothing run, no verdict)
Final, from the code:
- Two different `surfaces_lock`s: dce.c:53 (window-surface list; this fix's new edge) and window.c:294
  (client surfaces; the one in the 157 rule).
- Only `register_window_surface` and `flush_window_surfaces` take the dce.c lock; the flush holds it across every
  `window_surface_flush`, i.e. across the surface mutex and the driver's flush callback.
- New edge: user lock → dce.c surfaces_lock, at apply_window_pos, free_window_handle, destroy_window,
  destroy_thread_windows.
- winex11's flush (bitblt.c ~1852) only trylocks win_data (`try_set_window_hidden`), so no blocking edge there.
Leads to finish first:
1. When that trylock fails, bitblt.c:1857 calls `NtUserPostMessage(hwnd, WM_X11DRV_SET_HIDDEN)` with the list lock
   and the surface mutex held (our 062 code). If NtUserPostMessage can take the user lock (path: get_window_thread →
   get_user_object_thread → get_user_entry, then put_message_in_queue: not read to the end), a flusher (list lock,
   wants user lock) deadlocks against apply_window_pos (user lock, wants list lock). Read `get_user_entry`; if it
   locks, reproduce with a faint-alpha layered window + resize + a thread holding win_data.
2. `scaled_surface_flush` under the list lock (NtGdi* calls, nested flush of the target): trace for a user-lock path.
3. `destroy_window` reads `win->current_drawable` after `release_win_ptr`.
4. The new test waits INFINITE everywhere: a stalled thread hangs the test instead of failing it.
Not checked: winewayland/winemac callbacks, lifetime/refcount paths, hot-path cost, anything that needs running.

Second pass of the review (2026-10-04, still reading only):
- Lead 1 closed: the winex11 flush fallback `NtUserPostMessage(WM_X11DRV_SET_HIDDEN)` takes no user lock
  (get_user_entry is a lock-free read of the shared session; put_message_in_queue is the send_message server
  request). Same conclusion as the 173 worker, reached independently. Possible exception with `+msg` tracing only
  (SPY_GetMsgStuff may look up the window class): unread.
- Combined 171 + 173 order, no cycle found by reading: client surfaces_lock → win_data_mutex → user lock →
  dce.c surfaces_lock → surface mutex → leaves. It holds only because `try_set_window_hidden` is a trylock:
  pin that with a comment at winex11.drv/window.c ~2855 naming the cycle a blocking lock would close.
- Still open: everything under "Not checked" above; quickest debug build = scratch copy of wt/173-dbg
  (tests/r173/lockorder-debug.patch) with 171 cherry-picked.
