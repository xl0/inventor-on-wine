# 107 Chromium's Mojo IPC costs ~10 wineserver round trips per message (named pipes + IOCP)
Status: fixed (awaiting review; partial, see Left) · Owner: 107 worker · Branch: fix/107-mojo-pipe-iocp (wt/107 on integ b6dab895f3e, build wt/107-build) · Found in: 091 (inv3, integ bd4259df723 + fix/091)

## Symptom
An animating WebView2/Edge page (Assistant spinner, 48 frames/s) makes ~2200 server requests/s in the GPU
process and ~3000/s in the renderer after 091; ~75 % of them are Mojo channel I/O and its waits.

## Mojo's pattern (mojo/core/channel_win.cc, base/message_loop/message_pump_win.cc)
Byte-mode overlapped pipe (4096/4096), bound to the IO thread's port (concurrency 1), hEvent NULL, no
FILE_SKIP_* flags; one ReadFile always pending (buffer >= 4 KiB), WriteFile from any thread under a lock
(next write only after the previous completion); pump: DoWork, GQCS(0), GQCS(INFINITE) when empty;
cross-thread tasks = PostQueuedCompletionStatus.

## Windows ground truth (notes/wine/io-completion.md; tests/iocp_deferred.c, ntdll:pipe test_completion_on_dequeue)
Port + hEvent NULL: a pending read/write's IOSB and (pipes) buffer are written by the thread that
dequeues the packet, not at completion (issuer spinning/alertable/exited alike); CancelIo returns before
it; a dequeue in another process still fills the issuer's IOSB. With an hEvent: completes at once.
Immediate completions write the IOSB before ReadFile/WriteFile return. Pipe handles are never signaled
by completions. Wine's own test already had this as todo_wine ("Canceled asyncs with completion port and
no event do not update IOSB before removing completion").

## Request mix per message (tests/mojo_pingpong.c, server-side counts with tools/wineserver-reqstats.patch)
Base: 10.98 requests per message and side: select 4 (IOSB wait of the write, blocking wait, APC fetch
after SIGUSR1/wakeup, APC result), remove_completion 3 (write packet, empty poll, blocking start),
write, read, get_async_result (APC fetches the read data), get_thread_completion; + ~0.7 SIGUSR1 (the APC
for the completed read interrupts the running reader). Server handler time 26.3 us/message.
Fixed: 6.04: write + select (IOSB of the immediate write), read, get_async_result (dequeuer fetches the
data), remove_completion 2 (write packet, read packet), select ~1 (blocking wait, now one call).
Server handler time 13.5 us/message. No signals.

## Fixes (fix/107) -- protocol change 969 -> 971
- `server: Complete named pipe I/O in the thread that dequeues its completion packet.` Pipe asyncs are
  thread agnostic: with a port, no event and no APC, a completion after STATUS_PENDING posts the packet
  at once with a reference to the async (no APC to the issuer); remove_completion/get_thread_completion
  return the async's client callback + IOSB and the dequeuing ntdll thread runs it (complete_async_io,
  shared with the APC path; data via get_async_result, the async kept alive in thread->completion_async
  until fetched). Queue removal at dequeue (top-level request: no list changes under queue iteration),
  sync-cancel accounting at completion (CancelIo doesn't wait for the dequeue, like Windows), the fd is
  not signaled (Windows pipes never are). Dequeue from another process: APC to the issuing process (as
  before). Closing the last port handle drops the packets' async references (cycle async->port).
- `ntdll: Associate with the port and wait in one server call when its queue was found empty.`
  SELECT_WAIT_COMPLETION select op (associate thread with port + wait on its completion wait) used by a
  blocking NtRemoveIoCompletion when this thread's last check of that port found it empty
  (thread_data.empty_port, a performance hint only); otherwise remove_completion first as before.
- tests: ntdll:pipe test_completion_on_dequeue (pending read, partial message read, with event, pending
  write, GetQueuedCompletionStatusEx with two packets, blocking dequeue in another thread after an empty
  poll): Win11 pass x86_64 + i386, base Wine 7 failures, fixed Wine pass; one todo_wine_if removed.

## Results
- Repro (tests/mojo_pingpong.c 50000 64-byte round trips, pinned CPUs 40-45, busy siblings, 3 interleaved
  runs; inst/107/pp.sh): base 110-115 us/rt (8.7-9.1k rt/s), process CPU ~50 us/rt each, wineserver
  85-90 us/rt; fixed 58-62 us/rt (16.2-17.2k rt/s), ~27 us/rt each, wineserver 45-48 us/rt.
  Win11 VM: 21 us/rt (47k rt/s), ~10-12 us CPU/rt per process. Total CPU per rt ~187 -> ~100 us (Win ~22).
- Edge spin.html (Xvfb :621, --disable-gpu: GPU process crashes on Xvfb; display compositor still in the
  GPU process), server-side requests/frame (inst/107/edgestats.sh): GPU process 36.1 -> 29.5
  (remove_completion 8.7 -> 4.4, select 12.0 -> 9.9), renderer 62-77 -> 61-65 (noisy).
- Edge pinned interleaved A/B task-clock of all Edge processes + wineserver (inst/107/edgerun.sh, 20 s):
  round 1 (A base / B deferred completion, 5 pairs): median 12.6 -> 11.5 %, B lower in 5/5, wineserver
  3.4 -> 3.1 %; round 2 (A / B / C = both fixes, 5 triples): A 11.9, B 12.1, C 11.5 % (C at load 17
  excluded: 15.3). Small at this load: the IPC is a minor part of these processes' CPU without GPU work.
- Regression: tools/regress.sh on ntdll kernel32 kernelbase ws2_32 rpcrt4 ole32 oleaut32 advapi32 msi
  httpapi winhttp services combase (256 units) vs the cached integ b6dab895f3e run: 1 worse, FLAKY
  (i386 ws2_32:sock, base times out too). Edge prefix kept in /dev/shm/wine107 (3.6 GB) for re-runs.

## Left
- Per wake: empty poll (remove_completion) + wait + get_thread_completion; queue depth in shared memory
  would answer empty GQCS(0) polls client-side (~3/frame in the GPU IO thread; needs session shm in ntdll
  and a handle->locator cache invalidated in NtClose, like the fd cache).
- Output data inline in the dequeue reply would drop get_async_result (1 per pending read).
- Immediate completions stay at 2 requests (op + IOSB wait): the IOSB must be written before the packet
  becomes visible, Windows writes it before returning.
- Cross-thread wakeups (event_op, add_completion = PostQueuedCompletionStatus) are a large part of the
  renderer's requests; ntsync (kernel 6.14+) would take events/waits off the server.
- Wine signals pipe handles on completion, Windows never does (pre-existing, only non-deferred paths).
