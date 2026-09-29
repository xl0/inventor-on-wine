# 060 wined3d-vk: per-frame 52 MiB vkAllocateMemory + big upload memcpy (sketch/orbit lag)
Status: open (draft) · Owner: - · Branch: - · Found in: UI latency pass (tools/uilat)

## Symptom (integ 061fa687382, :98 RTX 6000 Ada, WINE_D3D_CONFIG=renderer=vulkan)
User report (laptop, RTX 3080): sketch rubber-band preview and hover highlight lag.
Measured with `tools/uilat/uilat.py rubber hover orbit pan --setup --tag T`
(XTest in, XDamage/XGetImage out; 120 Hz input; raw data in inst/uilat/):

| scenario | wined3d-vk | wined3d-gl | DXVK 3.1.1 | vk + threshold patch |
|---|---|---|---|---|
| rubber band: step latency p50 | 53 ms | 20 ms | 4.4 ms | 28 ms |
| rubber band: fps (120 Hz input) / lag p50 | 21 / 23 ms | 44 / 23 ms | 117 / 1.8 ms | 38 / 31 ms |
| hover preselect: latency p50 | 43 ms | 16 ms | 4.9 ms | 17 ms |
| orbit (Shift+MMB): fps | 35 | 47 | 60 | 43 |
| pan (MMB): fps | 40 | 55 | 62 | 54 |

Rubber band on vk updates every ~50 ms regardless of input rate (main thread only ~27%
busy: it sleeps in NVIDIA ioctls).

## Cause 1: dedicated VkDeviceMemory per DISCARD map (main thread)
- perf (DWARF, main thread): 77% of samples (rubber band), 70% (orbit) under
  `thunk64_vkAllocateMemory` -> NVIDIA ioctls + a 52 MiB mmap. strace: 3 alloc ioctls of
  20-30 ms wall each per update.
- gdb on win32u_vkAllocateMemory: every call is 54525952 bytes (52 MiB), memory type 2
  (host-visible), no dedicated-allocation pNext, main thread.
- Path: `wined3d_cs_map_upload_bo()` (WINED3D_MAP_DISCARD) -> `adapter_vk_alloc_bo()` ->
  `wined3d_context_vk_create_bo()` -> `wined3d_context_vk_allocate_memory()`: size >
  WINED3D_ALLOCATOR_CHUNK_SIZE / 2 (32 MiB) bypasses the chunk allocator and calls
  vkAllocateMemory directly; the retired bo is vkFreeMemory'd later. No reuse, so every
  discard of this 52 MiB resource costs a fresh pinned allocation (~15-30 ms on NVIDIA).
  (What the resource is, buffer or texture subresource, not identified; Inventor/OGS.)
- Experiment: patched the `cmp $0x2000000` in the running process to CHUNK_SIZE (52 MiB
  then takes a whole 64 MiB chunk, which the allocator keeps): table column 4. Not a fix
  proposal as is (wastes up to half a chunk per bo, chunks never shrink); a proper fix
  probably recycles retired large bos (size/usage/memory type) once their fence passed,
  like wined3d-GL's retired bo reuse, or suballocates large host-visible buffers.

## Cause 2: large sysmem upload copy on the CS thread
With cause 1 patched, the wined3d_cs thread becomes the bottleneck (30% rubber, 80% orbit).
perf: ~90% kernel page faults inside ucrtbase `sse2_memmove` reading freshly allocated
source memory (+ munmap): per frame the CS thread copies a large, newly malloc'd upload
buffer (non-discard map / update_sub_resource path: `malloc(size)` + UPLOAD_BO_FREE_ON_UNMAP,
or similar). Needs a trace of which d3d11 calls (Map flags / UpdateSubresource box sizes)
OGS makes per frame (`WINEDEBUG=+d3d11` on a short run).

## Task
Make wined3d-vk handle per-frame DISCARD of large resources and large uploads without
fresh allocations (compare DXVK: 1.8 ms lag at 117 fps on the same scene). Re-measure with
tools/uilat (rubber/hover/orbit/pan). Workaround for users: DXVK (deps/dxvk.sh) or
renderer=gl.
