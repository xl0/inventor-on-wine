# 162 wined3d GL renderer: WebView2's hardware-path GPU process dies at a Chromium CHECK (skia_output_surface_impl.cc:1277)
Status: draft · Found in: 132 M0 (old host: wined3d GL on llvmpipe, Wayland and X; does NOT happen with wined3d GL on NVIDIA EGL, new host 2026-10-04) · Component (guess): wined3d GL backend on Mesa/llvmpipe, d3d11 format caps

## Symptom
With `WINE_D3D_CONFIG=renderer=gl` on llvmpipe every WebView2 GPU process started on the hardware path dies ~2 s after start;
the browser process restarts it every ~8 s and gives up after three crashes (`--gpu-recent-crash-count=0..2`), the fourth uses
software compositing (`DCompositionCreateDevice(NULL)`). Inventor has three WebView2 browser processes (licensing agent, ADP,
Home page): 9 crashes, and web content shows up 25-30 s late. With the Vulkan renderer on the NVIDIA X displays the hardware
path lives. On Wayland (no Vulkan WSI on the host, so always GL) this is the normal case.

## Evidence
- `+seh`: `EXCEPTION_BREAKPOINT` at the same address in all nine processes (`inst/132/inv/m0-inventor.log` Wayland,
  `m0x-inventor.log` Xvfb + winex11, same build wt/134-build and prefix inv2).
- Crashpad report (`...\EBWebView\Crashpad\reports\*.dmp`, `strings -a`):
  `FATAL:components\viz\service\display_embedder\skia_output_surface_impl.cc:1277  surface_size=860x500 format=4 color_type=4
  backend_format.isValid()=0 backend_format.backend()=5 GrBackendFormats::AsGLFormat(backend_format)=0 sample_count=1
  surface_origin=0 willGlFBO0=1`, `egl-display-type angle:D3D11`.
  I.e. Skia's caps for the ANGLE (GLES on D3D11) context have no texturable + renderable default format for kRGBA_8888.
- Last Wine messages of the crashing thread: `wined3dformat_from_dxgi_format Unhandled DXGI_FORMAT 0x68` (P010), `dxgi_output_WaitForVBlank stub`.

## Open
Which D3D11 answer makes ANGLE drop RGBA8 as a render target (CheckFormatSupport / CheckMultisampleQualityLevels /
feature level on the GL backend?). Not tried: wined3d GL on the NVIDIA X displays (tells GL backend from llvmpipe),
`+d3d11,+d3d` of one GPU process. Chromium's source (skia_output_surface_impl.cc, ANGLE's renderer11 format tables) can be read.
