# Direct3D / d3dcompiler / vkd3d-shader — checked at master 4e819f054dd

- The HLSL compiler (vkd3d-shader + vkd3d-utils, libs/vkd3d) is linked into
  wined3d.dll; d3dcompiler_43/47 import vkd3d_D3DCompile2VKD3D etc. from it.
  After editing libs/vkd3d rebuild with `make dlls/wined3d/all`
  (`make libs/vkd3d/all` has no rule, and `make ... | grep error` hides that).
- ntdll version heuristic (unix/loadorder.c version_heuristics): an app-local DLL whose
  CompanyName is Microsoft gets the default order, i.e. Wine's builtin wins. So apps
  shipping d3dcompiler_47 use vkd3d's compiler; `WINEDLLOVERRIDES=d3dcompiler_47=n`
  forces theirs (handy A/B test: identical output ⇒ bug is in the compiler).
- DXVK replaces d3d11/dxgi only; it still uses Wine's d3dcompiler, so compiler bugs
  show on wined3d-vk, wined3d-GL and DXVK alike.
- fx_4/fx_5 effects (fx.c): write_fx_4_buffer writes numeric globals of each buffer;
  static globals must be skipped (037). Effects11 (FX11, often linked into apps)
  binds everything through D3DReflect of the embedded shader blobs.
  Inspect an fx blob: vkd3d_shader_compile(SOURCE_FX → TARGET_D3D_ASM) (prints the
  variable/buffer layout; no container flags).
- d3d11 CheckFormatSupport (device.c) is built from wined3d_check_device_format per
  bind flag. Swapchain format pickers look for D3D11_FORMAT_SUPPORT_DISPLAY (037).
  Still missing vs Windows: MIP_AUTOGEN, BLENDABLE, CPU_LOCKABLE, BACK_BUFFER_CAST.
- CreateSwapChain with DXGI_FORMAT_UNKNOWN is E_INVALIDARG on Windows too
  (flip models: DXGI_ERROR_INVALID_CALL on Windows, E_INVALIDARG on Wine).
- The d3d11 conformance suite has no unit selection (argv) and is unstable on the
  NVIDIA headless display; run it via tools/regress.sh (lavapipe).
- wined3d-vk memory: allocations > WINED3D_ALLOCATOR_CHUNK_SIZE/2 (32 MiB) bypass the chunk
  allocator (own vkAllocateMemory, freed when retired, never reused). A DISCARD map allocates
  a new bo on the app thread (`adapter_vk_alloc_bo`), so per-frame discards of big resources
  cost a fresh allocation each (~15-30 ms for 52 MiB host-visible on NVIDIA; issue 060).
