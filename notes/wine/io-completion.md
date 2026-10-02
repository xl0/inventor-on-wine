# Overlapped I/O, completion ports, named pipes (107) — checked at integ b6dab895f3e + fix/107

## Windows ground truth (Win11 VM, tests/iocp_deferred.c, ntdll:pipe test_completion_on_dequeue)
- File with a completion port and hEvent NULL ("thread agnostic I/O"): when an overlapped read that
  returned pending completes, neither the IOSB nor (for pipes) the buffer is written until a thread
  dequeues the packet (GetQueuedCompletionStatus[Ex]); the dequeuing thread writes them, also when the
  issuing thread spins, sleeps alertably or has exited. Same for a pending write, partial message reads
  (STATUS_BUFFER_OVERFLOW) and cancellation (CancelIo returns at once, IOSB still pending until dequeue).
  A dequeue from another process (duplicated port handle) still fills the issuer's IOSB and buffer.
- With an hEvent the I/O completes at once (IOSB, buffer, event), packet still queued.
- Immediate completion (data already there / write fits) writes the IOSB before ReadFile/WriteFile return.
- TCP socket (AFD): data lands in the buffer at completion, IOSB at dequeue, the handle is signaled at dequeue.
- Named pipe handles are never signaled by I/O completion (WaitForSingleObject / GetOverlappedResult(wait)
  with hEvent NULL times out, with or without a port). Wine signals them (fd sync).

## Wine (integ + fix/107)
- Pipes are server-side: NtReadFile/NtWriteFile = `read`/`write` request; immediate completion returns a
  wait handle and the client fills the IOSB, then `select`s on it so the server posts the packet / sets the
  event afterwards (2 requests; needed because the IOSB must be written before the packet is visible).
- Pending pipe I/O completing later: before 107 a system APC (APC_ASYNC_IO, SIGUSR1 if the thread runs user
  code) to the issuing thread, which fetched the data (`get_async_result`) and reported back (`select`), then
  the server posted the packet: ~5 extra requests + often a signal. Since 107 pipe asyncs are "thread
  agnostic" (async_set_thread_agnostic): with a port, no event, no APC, the packet is posted at once with
  a reference to the async; `remove_completion`/`get_thread_completion` return its client callback + IOSB
  and the dequeuing thread runs the callback (complete_async_io) and writes the IOSB. The fd is not
  signaled for those (Windows pipes never are). Foreign-process dequeue: APC to the issuing process.
- NtRemoveIoCompletion: `remove_completion` (associates the thread with the port, returns a queued packet
  or PENDING + the thread's completion-wait handle), `select` on that handle, `get_thread_completion`.
  Since 107, when the thread's last check found the port empty (thread_data.empty_port), a blocking call
  uses SELECT_WAIT_COMPLETION (associate + wait in one request) instead of the first two.
- Chromium MessagePumpForIO: GQCS(0) after every task/completion, GQCS(INFINITE) when empty; one pending
  ReadFile per Mojo channel, writes from any thread, no FILE_SKIP_* flags, hEvent NULL, byte-mode pipes.
  Still per wake: empty poll + wait + get_thread_completion (3); per immediate write: write + select.
  Next steps: port depth in shared memory (empty polls client-side), output data inline in the dequeue reply.
