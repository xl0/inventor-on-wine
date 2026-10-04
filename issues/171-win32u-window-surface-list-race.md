# 171 win32u: two threads changing the surface of one window corrupt the window surface list (NULL write in register_window_surface, lock leaked, later hang)
Status: fixed (branch, not on integ) · Owner: worker 171 · Branch: fix/171 (3 commits on integ b5d75449ffe) · Found in: 157 (lockstress on the host Wayland session) · win32u, driver independent · upstream bug

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

## Fix (fix/171)
- 49738642551 `win32u: Update the window surface list with the window lock held.` `register_window_surface` moves in
  front of `release_win_ptr` in `apply_window_pos` (5 lines up), `destroy_window` and `destroy_thread_windows`. The list
  now changes under the lock that protects `win->surface`, like every other field of the window: a surface is in the
  list exactly while it is a window's surface. New nesting: user lock -> dce.c `surfaces_lock`. Safe because nothing
  under `surfaces_lock` takes the user lock: `flush_window_surfaces` only calls `window_surface_flush` (surface lock,
  NtGdi, driver flush), and a driver flush that took the user lock would already deadlock against
  `update_surface_region`, which flushes with the window pointer held. Read: winex11 `x11drv_surface_flush` (X calls,
  `try_set_window_hidden` = trylock, NtUserPostMessage = server call), winewayland `wayland_window_surface_flush`
  (win_data + NtGdi), scaled / offscreen surfaces (NtGdi only). winemac not read.
  The call sits at the end of the locked section, not at the swap: registering earlier lets a flushing thread pick up
  the new surface while its owner still sets it up (measured: +11 % on a contended resize, gone at the end).
- af812239de0 `win32u: Release a window surface that was set while the window is destroyed.` Found with the destroy
  stress: UpdateLayeredWindow between `destroy_window` dropping the surface and `free_window_handle` installs a new one
  that nobody releases (81 list entries left after 1500 create/destroy rounds, each flushed forever). `free_window_handle`
  now drops whatever is there, under the lock that removes the handle.
- 784c189d22e `user32/tests: Test UpdateLayeredWindow on a window of another thread.` (`test_layered_window_threads`):
  update with move + resize while the owner thread doesn't process messages (deterministic; guards against "fixing" this
  by forwarding to the owner), then 500 owner resizes against updates from the main thread with a third thread painting.
  Windows: 6 of 6 runs pass (x64 and x86). On unfixed Wine the race part fails or hangs in 14 of 20 runs (it is a
  stress, not deterministic).

Rejected: forwarding UpdateLayeredWindow to the owner thread (Windows doesn't; breaks callers whose owner doesn't pump);
a registration counter per surface under `surfaces_lock` alone (works out of order without the new nesting, but adds a
field to the driver-visible `struct window_surface` and leaves the list out of step with the windows).

## Verification (final build wt/171-build = 784c189d22e; Xvfb :1400/:1401, prefixes under inst/171/, logs inst/171/TAG/)
`tests/r171/run.sh BUILD TAG RUNS TIMEOUT EXE ARGS` (own Xvfb, watchdog, gdb backtraces of stuck runs).
| probe | build/ (integ b5d75449ffe) | master 4e819f054dd | fix |
|---|---|---|---|
| `ulwrace race 1500 1` (owner resizes, other thread updates, 1 painting thread) | 46 of 50 runs FAULT + hang | - | 199 of 200 ok, 0 faults, 0 hangs (1 run: BadWindow death, 173) |
| `ulwrace race 5000 1` | 10 of 10 FAULT | 10 of 10 FAULT | 10 of 10 ok (first fix build) |
| `ulwrace race 5000 0` (two threads only) | 10 of 10 ok | - | - |
| `ulwrace destroy 1500/2000 1` (windows created and destroyed under updates) | 6 ok, 4 stuck (flush of a freed surface) of 10 | - | 20 of 20 ok |
| surfaces left in the list after `destroy 1500 1` (`tests/r171/leak.sh`) | - | - | 0 (81 with the first commit only) |
| `ulwrace exit 2000 1` (windows left to the thread exit) | 10 of 10 ok | - | 10 of 10 ok (first fix build; weak probe: the updating thread rarely replaces the surface) |
| `tests/r157/lockstress.exe 3000 1 nogl` | 8 ok, 2 BadWindow deaths (173) of 10; 15000 ops: 5 ok, 1 BadWindow of 6, no hang | - | 10 of 10 ok (first fix build: 9 ok, 1 BadWindow) |
| new user32:win subtest alone, unfixed vs fixed | 14 of 20 runs fail/hang | - | 20 of 20 pass |
lockstress on Xvfb does not trigger 171 at these sizes (on the host Wayland session it was 5 of 32 at 15000 ops).

Conformance (`tools/regress.sh unit`, both arches, vs deps/regress/b5d75449ffe...-h26): user32:win fail 4 (base 4; the same
two lines, win.c:10998/10999 = base 10890/10891), user32:msg fail 1 (1), user32:sysparams fail 6 (6), gdi32:dc fail 3 (3),
user32:dce, user32:input, win32u:win32u, d3d9:device pass. 0 worse.

Inventor (inv4, :101, fix build): suite 12 PASS + export failing only "IGES export: 80-column records" (known state of that
prefix), Home page and Assistant pane render, no dialogs. inv4 is back on build/, Inventor closed, lease released.

Cost: single thread, no contention (`ulwrace bench 2000 0`, 5 interleaved runs each, loadavg 10-16): resize with a new
surface 393 us on build/ vs 394 us, UpdateLayeredWindow 147 vs 149 us, create + destroy 1215 vs 1217 us. With one thread
painting and pumping all the time (`bench 2000 1`): resize 512 vs 528 us (median 514 vs 515), update 171 vs 167 us,
create + destroy 1682 vs 1641 us. Inventor suite, part / asm scenario totals, warm runs interleaved F B F B (loadavg
10-17): fix part 2.8 2.8 2.8 3.0 2.9 s, asm 3.8 3.9 3.7 4.1 4.2 s; build/ part 3.1 3.0 3.0 2.9 s, asm 3.9 3.9 4.0 4.2 s.

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
