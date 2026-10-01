# 102 Fault on a host (non-Wine) thread kills the process via a second SEGV in the handler
Status: fixed (2 commits on fix/102), dxgi part -> 103 · Owner: worker 102 · Branch: fix/102-segv-host-thread (wt/102, wt/102-build)
Found in: 100 (rows 7, 8, 14: mfplat both arches, dxgi crash 2/30, d3d10core exit hang 1/30)

## Brief
d3d12 tests on lavapipe (regress Xvfb): native NULL deref in libvulkan_lvp.so, then Wine's SEGV
handler faults again in server_call_unlocked on a thread without Wine thread data; kernel kills
the process. Two things: (1) Wine handler must behave sanely on non-Wine threads, (2) why lvp faults.

## Setup
Scratch: /tmp/i102 (harness run.sh/measure.sh, templates, logs; prefixes in /dev/shm/i102), Xvfb :340-:355.
Careful: per-run prefix copies (1.8 GB each) in /tmp filled the disk once; reuse one per build+display.
Env as regress.sh (WINEDEBUG=-all, lavapipe ICD, LIBGL_ALWAYS_SOFTWARE=1).
Mesa: mesa-vulkan-drivers 23.2.1-1ubuntu3.1~22.04.4 (lvp stripped; dbgsym ddeb in /tmp/i102/dbg).

## Findings
### 1. Wine: signals on host threads (ntdll, signal_x86_64.c)
Host-library threads (lavapipe's vk_queue submit thread, llvmpipe, GLib gmain/gdbus, gstreamer)
have no sigaltstack, no thread_data, no TEB. The handlers compute thread data as
`rsp & ~signal_stack_mask`, i.e. some address on the host thread's own stack; its `teb` field is
whatever lies there (usually 0 = looks like a TEB-less "system thread", PsCreateSystemThread, upstream
5155018f6f8). segv/trap/fpe/abrt then reach send_debug_event's `!data->teb` branch:
ERR + NtTerminateProcess -> server call -> `get_thread_data()` (pthread_getspecific) is NULL ->
SEGV inside the handler while SIGSEGV is blocked -> kernel kills (rc 139, core of the 2nd fault).
If the garbage `teb` were nonzero, init_handler would write through it.
Fix (commit "ntdll: Don't raise exceptions for signals in threads not created by Wine."):
`handle_host_thread_signal()` at the top of segv/trap/fpe/abrt handlers. Linux: the kernel stores
the thread's altstack in `uc_stack`; Wine threads always have theirs (set in init_thread_pipe before
handlers are installed), so `uc_stack.ss_sp != data->signal_stack` = host thread. Page faults
still go to virtual_handle_fault(NULL, ...) (write watches on memory a host driver thread writes,
e.g. imported host pointers); anything else: ERR, SIG_DFL, raise() -> native crash semantics
(core shows the real fault). Other OSes: unchanged. Not done: int/quit handlers on host threads
(process-directed SIGINT/SIGQUIT can land there and still crash), signal_i386.c (32-bit unix side).
Result: `err:seh:handle_host_thread_signal signal 11 at 0x...b272 (addr 0x40) in a non-Wine thread`.

### 2. Why lavapipe faults: Wine mfplat bug (use after free on the GPU timeline)
Backtrace (glibc backtrace() from the handler, offsets resolved with Ubuntu's
mesa-vulkan-drivers-dbgsym 23.2.1-1ubuntu3.1~22.04.4 ddeb, extracted to /tmp/i102/dbg, not installed):
handle_copy_buffer_to_image (lvp_execute.c:2207, `lvp_buffer_from_handle(srcBuffer)->bo->width0`,
bo NULL) <- lvp_execute_cmd_buffer <- lvp_execute_cmds <- lvp_queue_submit <- vk_queue_submit_final
<- vk_queue_submit_thread_func (Mesa's threaded vk_queue submit thread, a host thread).
The source VkBuffer was destroyed before the queued copy ran. Source: mfplat buffer.c
d3d12_surface_buffer_copy_transfer_resource(Write) (Unlock2D after a write lock) queues
upload->texture copy and doesn't wait; memory_buffer_Release then releases upload/list/allocators
immediately. Fix: wait for the buffer's fence in Release (commit "mfplat: Wait for pending copies
before releasing D3D12 transfer resources."). 0/20 crashes after (was ~90%).
Not lavapipe's fault. Under gdb (ptrace) the race never hit (0/6).
Separate: "Unexpected texture color" garbage = vkd3d doesn't zero committed resources (D3D12
guarantees zeroed memory unless CREATE_NOT_ZEROED); lavapipe memory comes from malloc, GPU drivers
give zeroed pages. Not fixed (vkd3d).

### 3. dxgi crash: vkd3d, see 103
Same host-thread crash family, different object: vkd3d destroys a fence's timeline VkSemaphore while
a submitted ID3D12CommandQueue_Wait on it is still queued in lavapipe's submit thread
(vk_queue_submit_final, `sync->type` NULL). Test-side wait before the Release: 0/96 vs 4/96.
Not fixed here (vendored vkd3d, needs an upstream vkd3d change); draft issues/103.

### 4. d3d10core timeout (100 row 14): not reproduced
0/64 runs (16 parallel). The one history log (c036c687) has no "tests executed" line: it hung
mid-run, not at exit; nothing ties it to d3d12/lavapipe. Note: a pre-fix mfplat run hung 120 s
after glibc "malloc(): unsorted double linked list corrupted" (heap damage from the same UAF), so
lavapipe UAFs can also show up as timeouts.

## Measurements (16 parallel streams, Xvfb, lavapipe, WINEDEBUG=-all; 32 runs each)
| unit | build/ (integ 4f92c92ace1) | fix/102 |
|------|-----------------------------|---------|
| mfplat x86_64 | 26 crash + 1 timeout (heap corruption abort) /32 | 0/32 |
| mfplat i386   | 22 crash /32 | 0/32 |
| dxgi x86_64   | 1/32 | 1/32 (103) |
| dxgi i386     | 1/32 | 0/32 |
mfplat still fails (3..14 failures, varying): 3 deterministic (1645/1665/1695, missing decoder) plus
the vkd3d-not-zeroed garbage; compare will keep calling it worse/flaky until vkd3d zeroes resources.
