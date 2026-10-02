# 113 32-bit CreateThread fails with ERROR_NOT_ENOUGH_MEMORY under thread churn
Status: analysed, not fixed (no leak; Wine thread start/exit throughput, large) · Branch: fix/113-wow64-thread-leak (empty)
Found in: 107 review (scratchpad r107.c `stress N` with exiting issuer threads)

Not a leak, and not I/O specific. Address space is back to the start value after the threads are gone
(`tests/r113/io_threads.c`: 10000 sequential I/O-issuing threads in every mode, +0 MB on Wine and Windows).
What fails is a backlog: threads created faster than Wine starts and finishes them. With several
creators that don't wait (the stress: 4 dequeuers spawn issuers and close the handles), ~900 threads
exist at once; at ~2.2 MB each (the exe's 2 MB stack reserve + TEB/64-bit side) the 2 GB of a
non-large-address-aware 32-bit process is full -> ERROR_NOT_ENOUGH_MEMORY. A sequential create/exit
loop never has more than one.

`tests/r113/thread_churn.c 4 5000 [io]` (4 creators x 5000 threads that exit at once):
| | peak threads | failures | time |
|---|---|---|---|
| Wine i386 | 1000 (address space full) | 495-1113 | 3.4-6.4 s |
| Windows x86 (VM, 16 CPUs) | 14 | 0 | 2.7 s |
| Wine x86_64 | 3779 | 0 | 6.8 s |
| Windows x64 | 21 | 0 | 1.1 s |
Threads reach their thread function late (peak "running" 1-2 on both): they pile up between
creation and start/exit. Same with the I/O variant.

Per thread (`tests/r113/thread_vm.c`): address space Wine 2068 KB vs Windows 2316 KB (x86), so the
footprint is not the problem. Costs: CreateThread (suspended) Wine 282 us / Windows 36 us;
create+run+exit+wait 538 / 272 us (x64: 242/26, 465/227). Wine commits the whole stack
(committed = reserved), Windows ~72 KB: commit charge difference, not address space.

Where the time goes (perf, 4x5000 i386, 120-CPU host): process sys 9.0 s vs user 2.7 s; 41% of samples in
the kernel under clone3 (thread creation), 16% mmap, 8% munmap, 2.5% mprotect (thread stacks/TEBs:
map_view, virtual_alloc_thread_data); wineserver only 1.8 s CPU of 4.7 s. So ~1000 runnable threads
contend in the kernel (likely mm/thread locks) during creation and teardown; kernel symbols are not
available here (kptr_restrict), so not pinned further.

Windows: a thread's pending overlapped pipe read is cancelled when it exits (IOCP-bound and plain
handles, Win11); Wine does the same.

Possible directions (each a perf project): fewer mmap/munmap per thread (reuse thread stacks/TEBs,
as Windows keeps them cheap), cheaper NtCreateThreadEx, throttling is not Windows behaviour.
Practical impact: only 32-bit apps that spawn threads in bursts from several threads; Inventor is 64-bit.
