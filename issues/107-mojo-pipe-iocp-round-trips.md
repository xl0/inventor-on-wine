# 107 Chromium's Mojo IPC costs ~10 wineserver round trips per message (named pipes + IOCP)
Status: open (draft) · Owner: - · Branch: - · Found in: 091 (inv3, integ bd4259df723 + fix/091)

## Symptom
An animating WebView2/Edge page (Assistant spinner, 48 frames/s) makes ~2200 server requests/s in the GPU
process and ~3000/s in the renderer after 091; ~75 % of them are Mojo channel I/O and its waits:
GPU IO thread per frame: remove_completion 10, select 9, read 4, write 3, event_op 2,
get_thread_completion 1-2, get_async_result 1; renderer compositor + IO threads similar.
Wineserver round trips are ~10-20 us of CPU (client + server) each, the cores' C-state wakeups make it
worse (unpinned the same work costs ~2x the CPU of pinned runs with busy siblings, see 091).

## Where the round trips come from (mojo/core/channel_win.cc: overlapped ReadFile/WriteFile, every
## completion through the IO thread's completion port, no FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)
- WriteFile that completes at once: `write`, then `select` on the async wait handle (wait_async, so the
  server posts the completion after the client filled the IOSB), later `remove_completion` to dequeue it.
- ReadFile: `read` (pending); data arrives -> system APC interrupts the reader's wait (`select` returns),
  `get_async_result` copies the data, `select` reports the APC result, the completion wakes the waiter,
  `get_thread_completion`.
- GetQueuedCompletionStatus that blocks: `remove_completion` (pending) + `select` + `get_thread_completion`;
  polls with timeout 0 (MessagePumpForIO after each task) are one `remove_completion` each.
- Windows: ~2 syscalls per message on each side.

## Ideas (each needs Windows ground truth on ordering / IOSB / completion semantics)
- Completion port depth in shared memory: empty timeout-0 polls answered client-side.
- Fold get_thread_completion into the wait reply of the completion wait.
- Writes completing at once on a handle with a completion port: post the completion from the write
  request when the client doesn't need to fill the IOSB first.
- Repro: tests/ ping-pong of overlapped named pipe messages between two processes with an IOCP each
  (Mojo's pattern), round trips and us per message, Wine vs VM.
