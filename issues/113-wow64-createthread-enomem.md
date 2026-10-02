# 113 32-bit CreateThread fails with ERROR_NOT_ENOUGH_MEMORY after ~4000 short-lived I/O threads
Status: open (draft, low) · Owner: - · Branch: - · Found in: 107 review (scratchpad r107.c `stress N` with exiting issuer threads)

In a WoW64 (32-bit) process, after ~4000 short-lived threads that issue overlapped pipe I/O and
exit, CreateThread fails with ERROR_NOT_ENOUGH_MEMORY. Same on base (not 107). A plain
create/exit loop of 20000 threads doesn't hit it, so something per I/O-issuing thread leaks 32-bit
address space (stacks/TEBs not freed, async/APC bookkeeping pinning thread data?). Inventor is
64-bit; matters for 32-bit apps with thread churn. Find what leaks, compare with Windows.
