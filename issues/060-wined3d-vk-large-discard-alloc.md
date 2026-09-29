# 060 wined3d-vk: per-frame 52 MiB vkAllocateMemory + big upload memcpy (sketch/orbit lag)
Status: fixed · Owner: worker-060 · Branch: fix/060-wined3d-vk-large-bo (6fb53c17861) · Found in: UI latency pass (tools/uilat)

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

## Findings (worker)
- Resource: one 52 MiB (0x3400000) DYNAMIC vertex buffer (bind VERTEX_BUFFER, CPU write), mapped
  WRITE_DISCARD on a **deferred context** (OGS render thread; FinishCommandList/ExecuteCommandList
  on another thread): `wined3d_deferred_context_map_upload_bo()` -> `adapter_vk_alloc_bo()` ->
  dedicated vkAllocateMemory, ~3 per rubber-band update (~60/s). Full-size DISCARD => the CS thread
  renames (COW), no copy. Other maps are small (<1 MiB, slab/chunk). Identified with a temporary
  ERR in adapter_vk_alloc_bo + addr2line (WINEDEBUG=+d3d11 has no buffer descs).
- Cause 2 (CS memcpy/page faults) does not show with the fix: CS thread 3% of orbit samples, no
  large copies (>8 MiB) through adapter_vk_copy_bo_address. It was likely an artifact of the
  chunk64 hot-patch (fresh 64 MiB chunk mappings) — not investigated further.
- Upstream: no MR/commit on this (gitlab MR search: allocator/chunk/discard/vkAllocateMemory);
  the >CHUNK_SIZE/2 bypass dates from 9529f08e1a4 (2020). wined3d-GL leaves it to the driver
  (new GL buffer per discard; NVIDIA GL copes: ~4.5 ms/iter in the repro).

## Fix
`wined3d: Reuse destroyed Vulkan BOs with dedicated memory.` context_vk keeps destroyed
dedicated-memory BOs (VkBuffer + memory + persistent mapping; unmapped on 32-bit) in a small
array (8 BOs / 256 MiB, FIFO eviction) under the allocator lock; the CS thread marks entries idle
in cleanup_resources once their command buffer completed; create_bo reuses an idle entry with the
same size and usage and a superset memory type. Inventor needs only 1 cached entry.

## Results (:98 RTX 6000 Ada, same session, integ 061fa687382 vs + fix)
tests/d3d11_discard_perf.exe (52 MiB, frame latency 3), ms/iter:

| case | master vk | fixed vk | wined3d-gl | DXVK 3.1.1 |
|---|---|---|---|---|
| immediate, memset 52 MiB | 23.2 | 3.5 | 4.6 | 7.0 |
| immediate, touch 4 KiB | 35.2 | 1.0 | 0.2 | 0.6 |
| deferred, touch 4 KiB | 35.0 | 1.2 | 13.2 | 1.5 |

uilat (`rubber hover orbit pan --setup`, tags base060 / fix060 in inst/060/uilat; DXVK/gl from above):

| scenario | vk before | vk fixed | wined3d-gl | DXVK |
|---|---|---|---|---|
| rubber step p50 | 47.7 ms | 4.9 ms | 20 ms | 4.4 ms |
| rubber fps / lag p50 | 21 / 24 ms | 119.5 / 4.1 ms | 44 / 23 ms | 117 / 1.8 ms |
| hover p50 | 12.7 ms | 4.8 ms | 16 ms | 4.9 ms |
| orbit fps | 30 | 57 | 47 | 60 |
| pan fps | 39 | 70 | 55 | 62 |

Tests: tools/regress.sh (d3d11 d3d10core d3d9 d3d8 ddraw dxgi wined3d, GL default renderer) vs
c09f08e4924: 1 worse unit, FLAKY (d3d11:d3d11 crash on base re-runs too). renderer=vulkan on
lavapipe (manual, d3d11/d3d10core/dxgi x86_64): same results as master (d3d11 aborts mid-run on
both; d3d10core 6 vs 13 failures, dxgi 61 vs 62, noise). No conformance test added: no semantic
change (DISCARD contents were already undefined; fresh Vulkan memory isn't zeroed either).
