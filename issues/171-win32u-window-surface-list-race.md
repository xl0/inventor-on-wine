# 171 win32u: two threads changing the surface of one window corrupt the window surface list (NULL write in register_window_surface, lock leaked, later hang)
Status: draft · Owner: - · Branch: - · Found in: 157 (lockstress on the host Wayland session) · win32u, not driver specific by reading

## Symptom
A thread hangs forever in `pthread_mutex_lock(&surfaces_lock)` (dce.c: `register_window_surface` or
`flush_window_surfaces`), the owner of the mutex is the same or another live thread that is not inside the locked
region any more; soon every UI thread of the process waits there. Seen in 5 of 32 runs of
`tests/r157/sinkstress.sh` / `lockstress.exe 15000` on the host session (fix/157 build; with and without 132's sources),
never in ~150 shorter runs in the vmwl guest.

## Evidence (`inst/157/evidence/171-fault.txt`, `171-hang.txt`; WINEDEBUG=+seh)
```
01e4:trace:seh:handle_syscall_fault code=c0000005 flags=0 addr=0x7cf5eace8f50 ip=7cf5eace8f50
01e4:trace:seh:handle_syscall_fault  info[0]=0000000000000001  info[1]=0000000000000008        write to NULL + 8
01e4:trace:seh:handle_syscall_fault returning to user mode ip=00006ffffd6b7644 ret=c0000005     NtUserUpdateLayeredWindow
```
ip = `list_remove` inlined in `register_window_surface` (dce.c:789, include/wine/list.h:100), rdi = &surfaces_lock.
gdb on the later hang: `'dce.c'::surfaces_lock.__data.__owner` = the main thread, which itself waits in
`register_window_surface <- apply_window_pos <- NtUserSetLayeredWindowAttributes`; user lock, client surfaces lock and
the driver's win_data_mutex are free.

## Cause (by reading)
`apply_window_pos` swaps `win->surface` under the user lock, but calls `register_window_surface( old_surface,
new_surface )` after `release_win_ptr`. Two threads in `apply_window_pos` for the same window (here: the owner thread's
SetWindowPos / ShowWindow and another thread's UpdateLayeredWindow / SetLayeredWindowAttributes, which win32u does not
forward to the owner thread): thread B takes thread A's new surface as its `old_surface` before A has added it to the
list, and `list_remove` runs on an entry that is still {NULL, NULL}. The SIGSEGV happens inside a syscall with
`surfaces_lock` held; ntdll's `handle_syscall_fault` returns c0000005 to the caller and the thread carries on with the
mutex locked. (A's later `list_add_tail` of a surface that B already released is the other half: a stale list entry.)

## Repro
`tests/r157/sinkstress.sh wt/157-build TAG 15000 4` on the host session (`NOSRC=1`: without 132's sources, same result),
`STRESSDEBUG=+seh` to see the fault; about 1 run in 6 hangs. Needs the 157 fix: unfixed builds deadlock earlier.
The trigger in lockstress is UpdateLayeredWindow / SetLayeredWindowAttributes on a window of another thread.

## Open
Component: win32u (window.c apply_window_pos, dce.c). Register the surface while the window pointer is held, or make
the list update tolerate the order. Separate, general: a fault in a syscall silently leaks every Unix mutex held
(handle_syscall_fault), which turns such bugs into hangs with no message unless +seh is on.
