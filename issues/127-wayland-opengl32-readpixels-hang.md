# 127 winewayland: opengl32:opengl hangs in glReadPixels on the DIB memory pbuffer (Mesa llvmpipe)
Status: draft · Found in: wayland test pass (notes/wine/wayland.md)

## Symptom
`wine opengl32_test.exe` (wine-src dlls/opengl32/tests/opengl.c, test_bitmap_rendering(TRUE)) never finishes
on winewayland.drv: after the two "DIB: got wglGetExtensionsString*" todo lines the main thread sleeps
forever, 32 llvmpipe worker threads idle in futex wait. `+relay` shows the last call is
`glReadPixels(0,0,1,1,GL_RGBA,...)` (after wglMakeCurrent on the 4x4 pbuffer that `flush_memory_pbuffer`
created through `wayland_pbuffer_create`). Same exe on winex11.drv (Xvfb, software GL) finishes in 1.3 s.

## Repro
`x/wayland.sh start; eval "$(x/wayland.sh env)"; WINEPREFIX=... wt/wayland-build/wine
dlls/opengl32/tests/x86_64-windows/opengl32_test.exe` (compositor mutter 42.9 headless; GL renderer is
Mesa 23.2.1 llvmpipe: NVIDIA EGL on Wayland finds no pixel format here, see notes/wine/wayland.md).

## Open
Wine-side (EGL pbuffer on a wayland-platform display) or Mesa 23.2 bug? Try WINEDEBUG=+wgl,+waylanddrv,
EGL_PLATFORM=surfaceless-style pbuffer, newer Mesa. Not investigated further.
