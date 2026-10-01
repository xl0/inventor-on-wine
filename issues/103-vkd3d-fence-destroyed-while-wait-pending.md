# 103 vkd3d destroys a fence's VkSemaphore while a submitted queue wait still uses it
Status: draft (not fixed) · Owner: - · Found in: 102 (dxgi:dxgi crash 2/30 in regress runs)

## Symptom
dxgi:dxgi (both arches) dies with rc 139 after `dxgi.c:5613 ... d3d12: Test 1` in ~4% of runs when
16 run in parallel on lavapipe (4/96; 0/36 with 12 streams). With 102's ntdll fix the log shows
`err:seh:handle_host_thread_signal signal 11 at ... (addr 0x10) in a non-Wine thread`.

## Evidence
Backtrace (host thread, Mesa 23.2.1 dbgsym): vk_queue_submit_final (vk_sync_timeline.h:117,
`vk_sync_as_timeline(wait->sync)` with `sync->type` NULL) <- vk_queue_submit_thread_func.
I.e. lavapipe's threaded submit processes a queued submit whose timeline wait semaphore was freed.

Test: test_swapchain_backbuffer_index (dxgi.c ~5760): `ID3D12CommandQueue_Wait(queue, fence, 1)`,
presents, `ID3D12Fence_Signal(fence, 1)` (CPU), then `ID3D12Fence_Release(fence)` at once.
vkd3d: d3d12_command_queue_Wait holds an internal fence ref only until the op is flushed;
d3d12_command_queue_wait_locked then vkQueueSubmit()s a wait on `fence->timeline_semaphore` and the
op (and ref) is dropped. The release then calls vkDestroySemaphore while the submitted wait has not
executed: VUID-vkDestroySemaphore-semaphore-05149. Kernel-syncobj drivers tolerate it; Mesa's
threaded vk_queue (lavapipe always uses it) dereferences the freed vk_sync.
Confirmation: adding `wait_queue_idle(d3d12_device, queue)` before that Release in the test: 0/96
crashes (vs 4/96). Same pattern in test_frame_latency_event (dxgi.c ~7983) and
test_swapchain_present_count (~8676). Upstream vkd3d master (6ee3db29, 2026-09-29) is unchanged.

## Fix direction (vkd3d upstream)
Keep the fence (its VkSemaphore) alive until the wait submission completes, e.g. submit the wait
with a VkFence and hand it to the queue's fence worker with an internal fence ref that is dropped
when the VkFence signals (a "release only" waiting entry), or keep waited fences on the command queue
and drop them on the next completed signal / queue destruction.

## Mitigation for tools/regress.sh
Nothing Wine-side to change in the harness except accepting the rare crash; running dxgi on a real
GPU driver hides it (kernel refcounts the semaphore payload).
