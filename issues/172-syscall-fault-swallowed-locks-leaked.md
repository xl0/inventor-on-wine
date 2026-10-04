# 172 ntdll: a page fault inside a syscall / unix call is turned into a return status, silently; Unix mutexes held at that point stay locked
Status: draft · Owner: - · Branch: - · Found in: 171 (and 157's stress) · ntdll unix side, upstream behaviour

## Symptom
A bug in Unix-side code (win32u, a display driver, any `__wine_unix_call`) that faults while it holds a pthread mutex does
not crash the process. The call returns the exception code as its result (0xc0000005; for a BOOL API that reads as
success), nothing is printed, and the process hangs at the next taker of that mutex, in another call and often another
thread. 171 looked like a lock-order problem for that reason: the gdb dump shows a mutex whose owner is not inside the
locked region any more.

## Where
`dlls/ntdll/unix/signal_x86_64.c` `segv_handler` -> `handle_syscall_fault()` (same function in signal_i386.c,
signal_arm.c, signal_arm64.c). When the faulting stack pointer is on the thread's syscall (kernel) stack
(`is_inside_syscall`):
- `data->jmp_buf` set (`ntdll_set_exception_jmp_buf`: explicit probes of user memory): longjmp to it;
- otherwise: rewrite the signal context to `__wine_syscall_dispatcher_return` with `rax = ExceptionCode`, i.e. unwind
  the whole Unix-side call and return the code to the PE caller. Unix calls (`__wine_unix_call_dispatcher`) run on the
  same stack and get the same treatment. No destructors, no unlock: C has no unwinding.
Output: `TRACE_(seh)` only (`WINEDEBUG=+seh` shows `handle_syscall_fault code=... addr=... ip=...`, registers,
`returning to user mode ip=... ret=c0000005`).

## Upstream or ours
Upstream, by design: 0b3db9dfa28 (Alexandre Julliard, 2020-08-24) "ntdll: Handle page faults happening during a syscall
without dispatching the exception." It is how Wine emulates the kernel's answer to bad user pointers (a syscall given an
invalid buffer returns STATUS_ACCESS_VIOLATION instead of raising in the caller); syscall implementations dereference
user pointers directly and rely on it, and conformance tests check for that status. Our tree doesn't touch the function.

## Can it be made loud
- Terminating the process: no. It would break every application (and test) that passes a bad pointer to a syscall and
  expects a failure status.
- Telling a bad user pointer from an internal bug: not possible in the handler. Both fault in the same code (a win32u
  syscall reading its argument vs. a NULL list entry), the faulting address says nothing (apps pass NULL + offset too),
  and the handler doesn't know which locks are held.
- An `ERR` in the second branch (no jmp_buf): one line per swallowed fault with code, faulting address, ip (+ module) and
  the PE return address. Cheap, changes no behaviour. Upstream would likely object to an ERR for something that is
  legitimate when the pointer came from the application (WARN at most, which nobody sees); as a local debugging aid on
  `integ` it is useful if it is quiet in normal runs. Measured for Inventor (build/, inv4, `WINEDEBUG=+seh`, start + hello,
  part, asm, drawing scenarios; 87986 seh trace lines): 0 `handle_syscall_fault`. Not measured: the conformance suite
  (tests that pass bad pointers to syscalls on purpose would each print one).
- Smaller alternative that upstream could take: make the first `TRACE_(seh)` line of `handle_syscall_fault` a
  `WARN_(seh)`, so `WINEDEBUG=warn+seh` shows swallowed faults without the cost of the full +seh trace.

## Task
Decide between the local ERR (after measuring its noise) and the WARN. Don't touch the control flow. For hangs on a
Unix mutex meanwhile: re-run with `WINEDEBUG=+seh` and look for `handle_syscall_fault` before reading lock orders.
