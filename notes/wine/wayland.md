# Wayland driver (winewayland.drv) on the headless server
Checked against wine-src d7799da4d5c (mutter 42.9 / gnome-shell 42.9, Mesa 23.2.1, NVIDIA 580).

## Build
`build/` has no winewayland.drv: configure lacks `xkbregistry` (headers/.pc of libxkbregistry-dev are not
installed; only libxkbregistry.so.0). Workaround without installs: separate tree `wt/wayland-build`
(same wine-src, `--enable-archs=i386,x86_64`) configured with a hand-written 10-line
`wt/wayland-inc/xkbcommon/xkbregistry.h` and a `libxkbregistry.so -> .so.0` symlink:
`XKBREGISTRY_CFLAGS=-I.../wt/wayland-inc XKBREGISTRY_LIBS="-L.../wt/wayland-inc/lib -lxkbregistry" ../../wine-src/configure ...`
(the header dir/symlink are not in git; recreate from the rxkb_* prototypes used in wayland_keyboard.c).
Full build ~10 min with -j40. Rebuild after wine-src moves: `make -j40` in the tree.

## Start
`x/wayland.sh start` -> `gnome-shell --headless --wayland --no-x11 --wayland-display wayland-wine
--virtual-monitor 1920x1080` inside `dbus-run-session`, with a private pipewire and
XDG_RUNTIME_DIR=/tmp/wl-xdg (wiped on start), then x/winj.py (input). Never touches the user's real
gnome-shell/bus. `eval "$(x/wayland.sh env)"` exports XDG_RUNTIME_DIR, WAYLAND_DISPLAY=wayland-wine,
the private DBUS_SESSION_BUS_ADDRESS and unsets DISPLAY. `x/wayland.sh stop` kills the process group
(+ the shell's ibus-daemon). Only one such session at a time (fixed paths).
Wine needs no registry setting: with DISPLAY unset and WAYLAND_DISPLAY set the graphics driver list
(`x11,wayland` style fallback) ends on winewayland.drv. Prefix: `/dev/shm/wl-prefix` (fresh `wineboot -i`,
`WINEDLLOVERRIDES="mscoree,mshtml="`), boot takes 15 s; boot with DISPLAY unset from the start.

## Screenshots and input (no extra packages)
- `x/wshot.sh [out.png]`: org.gnome.Shell.Screenshot refuses unknown callers (`Screenshot is not allowed`,
  no Eval/unsafe mode). Shell 42 allow-lists bus *names*: we `RequestName org.gnome.Screenshot` on our
  private bus and then call it. 1920x1080 PNG incl. top bar (window area starts at y=32).
- `x/wshot.sh move X Y|click [BTN]|down|up|scroll N|key SYM|type TEXT`: x/winj.py holds a
  org.gnome.Mutter.RemoteDesktop+ScreenCast session (absolute pointer needs the screencast stream; pipewire
  is just required to exist) and serves commands on $XDG_RUNTIME_DIR/winj.sock. Verified: click into
  notepad, type text, Return.
- Ubuntu's shell starts in the Activities overview; wayland.sh sends Escape once.
- Not available: grim/ydotool/wtype/weston-info/eglinfo (not needed). `vkcube` is X11 only.

## GPU
mutter composites in software: it creates gbm renderers on all four /dev/dri/renderD128-131 but logs
"Not hardware accelerated" and disables dma-buf sharing (nvidia-drm modeset is off; /sys/module
parameter unreadable). Consequences for clients:
- EGL/GL: Mesa EGL's wayland platform -> llvmpipe (`GL_RENDERER llvmpipe`, wined3d reports the fake
  "GeForce GTX 470"). `__EGL_VENDOR_LIBRARY_FILENAMES=.../10_nvidia.json` makes ChoosePixelFormat fail
  (D3D11CreateDevice 0x887a0004). No hardware GL on Wayland here; `libEGL warning: egl: failed to create
  dri2 screen` on every start is harmless noise.
- Vulkan: NVIDIA ICD enumerates the real GPUs but VK_KHR_wayland_surface gives zero surface
  formats/present modes (vulkan-1:vulkan fails 10 checks, swapchain creation -> OUT_OF_DATE), so DXVK
  (tested: prefix dlls copied from prefixes/dxvk) creates its device on the RTX 6000 and then
  page-faults in d3d11.dll at the first swapchain. lvp (llvmpipe Vulkan) presents fine, only 3 checks fail
  (issue 129).
Hardware rendering under Wayland would need the compositor on an NVIDIA GPU with nvidia-drm.modeset=1
(root: kernel module param) or a compositor built for EGLStreams/GBM on NVIDIA. For the GPU Inventor
test keep using the X displays.

## Compositor globals (mutter 42)
Missing: zwlr_data_control (clipboard limited), xdg_toplevel_icon, wp_fractional_scale,
wp_alpha_modifier, cursor_shape, pointer_warp (-> SendInput/SetCursorPos mouse moves cannot warp the host
pointer). Present: linux_dmabuf, viewporter, relative-pointer, pointer-constraints, text-input-v3,
xdg-wm-base/zxdg_shell_v6.

## Test results (wt/wayland-build, fresh prefix; X column = same build, Xvfb with no WM, so noisy)
| test | wayland | X (Xvfb) |
|---|---|---|
| notepad, typing, screenshot | ok | |
| tests/d3d11_present.exe | ok (wined3d GL on llvmpipe, 1100 fps) | |
| tests/custom_caption.exe | ok; `max` maximizes to 1920x1080 but mutter keeps the 32 px panel, bottom clipped | |
| tests/expose_present.exe | FAIL: reads screen with GetDC(0)/GetPixel -> always 0xffffff (no screen DC readback on Wayland); use x/wshot.sh instead | |
| user32:win | 12 failures (focus/foreground 3905-3907, 4780/4783 activate msgs, 13573-13655 SC_MOVE/size via cursor, 1787 z-order) | 2 |
| user32:msg | 54 (VK_PROCESSKEY 0xe5 in keyboard tests, ShowWindow/SetWindowPos message sequences) | 1 |
| user32:input | 51 (layout change E0010409, SendInput mouse not delivered input.c:4459, focus 6931) | 0 |
| user32:monitor / cursoricon | 0 / 0 | 60 / 0 |
| user32:sysparams | 9 (dmFields/dmDisplayFrequency of virtual monitor) | 8 |
| d3d11:d3d11 | dies silently (rc 5) after ~75 lines on both drivers: not wayland specific, not investigated | same |
| vulkan-1:vulkan | NVIDIA 10+ failures (no surface formats), lvp 3 (issue 129) | 80 |
| opengl32:opengl | HANG in glReadPixels (issue 127); X finishes in 1.3 s | 30 failures (Xvfb) |
| DXVK d3d11_present.exe | device on RTX 6000 ok, page fault at swapchain (NVIDIA WSI, see GPU) | |

Drafts: 127 (opengl hang), 128 (VK_PROCESSKEY on every key), 129 (present rectangles).

## Behaviour seen
- Window placement is the compositor's (requests at 100,100 end up centred); Wine only learns the size.
- Wine draws its own caption/frame (notepad title bar is Wine's); the shell shows the exe name in the top bar.
- Keyboard layout becomes 0xE0010409 once the window is focused (IME layout), see 128.
- Source FIXMEs worth knowing: wayland_keyboard.c (modifier state sync with XKB, foreground update,
  WM_INPUTLANGCHANGEREQUEST wParam), wayland_pointer.c:842 dpi scaling of rects.
