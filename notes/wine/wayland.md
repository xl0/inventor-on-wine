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
pointer), xdg_wm_dialog_v1 (mutter 47+), xdg-foreign v2 (mutter 44+). Present: linux_dmabuf, viewporter,
relative-pointer, pointer-constraints, text-input-v3, xdg-wm-base v4/zxdg_shell_v6, xdg-foreign **v1 only**
(zxdg_exporter_v1/zxdg_importer_v1), gtk_shell1 v5 (GTK-private; its set_modal is what makes a mutter 42
window a modal dialog), xdg_activation_v1. Dump: `WINEDEBUG=+waylanddrv` prints `interface=... version=...`
per global (no wayland-info here).

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

Drafts: 127 (opengl hang), 128 (VK_PROCESSKEY on every key), 129 (present rectangles); Inventor pass (below): 132-136.
- A swap with interval > 0 never returns while the compositor does not show the surface (hidden window, own child of a foreign
  top-level): Mesa waits for a frame callback inside eglSwapBuffers (163; `tests/r132/xp.exe hidden interval=1`).
- WebView2 with wined3d GL on llvmpipe (the only renderer on this Wayland host): each hardware-path GPU process dies at a Chromium
  CHECK ~2 s after start, 3 rounds per browser process, then software compositing (162; same on X with GL). Web content therefore
  appears ~25-30 s late even where it can be shown; under Wayland it is blank until 132 is fixed (state and design in the issue).

## Behaviour seen
- Window placement is the compositor's (requests at 100,100 end up centred); Wine only learns the size.
- Wine draws its own caption/frame (notepad title bar is Wine's); the shell shows the exe name in the top bar.
- Keyboard layout becomes 0xE0010409 once the window is focused (IME layout), see 128.
- Source FIXMEs worth knowing: wayland_keyboard.c (modifier state sync with XKB, foreground update,
  WM_INPUTLANGCHANGEREQUEST wParam), wayland_pointer.c:842 dpi scaling of rects.

## Owned windows (134, 135; branch fix/134, not on integ yet)
- Same process: an owned managed window gets `xdg_toplevel.set_parent(owner's toplevel)`. window.c keeps the owner
  root per window (`wayland_win_data.owner`, taken at WindowPosChanged) and what the compositor was told
  (`wayland_surface.parent_hwnd`). Rules from xdg-shell / wlroots: only a *mapped* toplevel (first buffer committed) can be
  a parent, an unmapped one counts as NULL (wlroots really drops it; mutter keeps it); when a parent unmaps wlroots
  re-parents to the grandparent, mutter 42 leaves non-modal children pointing at the dead window, so the driver sends `set_parent(NULL)` itself; a loop is a fatal
  `invalid_parent` on wlroots (mutter only logs "would create a loop" in shell.log). Hence: parent set at the owned window's
  WindowPosChanged if the owner is mapped, else when the owner maps (`wayland_surface_mapped` -> `update_owned_toplevels`);
  a stale link that would close a loop is unset first.
- "A stale link" is the only loop source in one process. Across processes (parents set elsewhere are invisible) the driver
  also walks GA_ROOT(GW_OWNER) from a foreign owner and does not import it if the chain comes back to the window: owners are
  mapped to their root window, so "A owned by B, B owned by a child of A" is legal Win32 and a toplevel loop. On wlroots the
  `invalid_parent` error for a foreign loop is posted on the parent's toplevel = kills the *owner's* process connection.
  Still open: stale imports in two processes (135).
- Owner changes via SetWindowLongPtr(GWLP_HWNDPARENT) reach no driver entry (same on X11: WM_TRANSIENT_FOR follows at the
  next style/pos update): the parent follows at the owned window's next WindowPosChanged.
- Other process: the owner's process exports each mapped toplevel (xdg-foreign v2, else v1) and publishes the handle on the
  window: property `__wine_wayland_exported_handle` = global atom whose name is the handle; a second property *named* by the
  handle holds the atom reference, so wineserver frees it with the window (plain NtAddAtom would leak one of ~16k atoms per
  window of a killed process). The owned window's process reads it (NtUserGetProp + NtQueryInformationAtom), imports and
  calls `set_parent_of`; it re-reads only when it has no live import (`destroyed` event = owner unmapped). mutter 42 handles
  are 32 random printable ASCII chars (spaces, quotes, `#`...; atom names are case-insensitive, fine for random handles),
  wlroots uses 36-char tokens.
  Not covered: nothing tells the owned window's process when a foreign owner maps later, it retries at its next
  WindowPosChanged (win32u moves owned popups with their owner, which was enough in the probe and for Inventor's trial popup:
  first owned by the splash window, then by the main window, imported 65 ms after the main window's handle appeared).
- mutter 42 does not centre a child toplevel on its parent (place.c centres only DIALOG/MODAL_DIALOG types, a Wayland
  window only becomes one through gtk_shell1.set_modal): dialogs still land near the top-left (136).
- Keyboard focus: mutter focuses the clicked surface even if the Win32 window is disabled (owner of a modal dialog). fix/134
  then makes its last active popup (visible, enabled) foreground, else the thread's active window, else the window itself,
  so typing keeps going to a same-thread dialog. A dialog in another thread/process becomes foreground but gets no keys: the
  driver sends keys for the compositor-focused (disabled) window.
- "Other process" is decided by pid, not by "no win_data in this process": an in-process owner can lose its win_data while
  owned windows live on (thread exit without DestroyWindow; the server even keeps the dead owner handle, 158).
- Under win_data_mutex only plain server requests are safe; anything taking win32u's user lock (GetWindowLong, window text,
  GW_OWNER...) can deadlock against a surface flush (157, already present on integ: `rv rapid 300`).
- mutter hides the children of a minimized parent; the driver's minimize state desyncs from the compositor (156), so an owned
  window shown while its owner is "minimized" stays invisible.
- `tests/wl_xowner.sh` (WINE_BUILD=...): all wl_xowner cases with pixel checks; 28 PASS on fix/134, `self` fails on integ.
  Current compositors (GNOME 50, KDE, sway; xdg-foreign v2): vmwl/results-134.md. Review probes: inst/134-review/rv.c.

## Inventor 2027 under Wayland (wine-src 04293594c50, prefix inv4, 2026-10-03)
Result: starts in ~15-20 s to an idle main window, ribbon/dialogs/menus/tooltips/3D viewport (wined3d GL on llvmpipe) work;
`invscen all` 10/13 (drawing2 DWG + sheetmetal DXF fail: 133; export IGES fails on X too). Blockers: WebView2 content blank
(132: Home page, Assistant pane, trial popup), modal dialogs fall behind their owner on a click (134), the trial popup is
stacked behind the main window and blocks it (135), toplevel popups placed by mutter (136). 128 (extra 0xE5) had no visible
effect in Inventor typing (dialog edit fields, file name box). Crash: none seen. FPS not measured (orbit tracks input).
- Run it: `PREFIX_HOLDER=x tools/prefix.sh lease inv4 x; tools/prefix.sh stop inv4 --holder x`, then with the Wayland env
  (`eval "$(x/wayland.sh env)"`, DISPLAY unset, `WINEPREFIX=$PWD/prefixes/inv4`) `wt/wayland-build/wine wineboot -u` and
  `WINE_D3D_CONFIG=renderer=gl wt/wayland-build/wine 'C:\Program Files\Autodesk\Inventor 2027\Bin\Inventor.exe'` (vulkan
  has no WSI here). Switching a prefix between X and Wayland needs only stop + `wineboot -u` with the other env (no prefix update).
  `tools/prefix.sh start` always uses build/ + DISPLAY. Back: `x/wayland.sh stop; tools/prefix.sh start inv4`.
- Harness: run.sh drops DISPLAY itself when WAYLAND_DISPLAY is set, so with the Wayland env:
  `INV_PREFIX=prefixes/inv2 WINE_BUILD=wt/134-build WINE_D3D_CONFIG=renderer=gl tools/invscen/run.sh SCEN` (verified: hello, part,
  drawing, view; output in inst/invscen/inv2). Run scenarios one by one: `all` restarts prefix `${INV:-inv}` after a crash.
  Never pipe `wine wineboot -u` (or any wine command that starts the prefix) into `tail`: the services inherit the pipe and
  the pipeline never ends. The dialog-screenshot hook (INVSCEN_SHOT -> x/shot.sh) does not work on Wayland (waits 30 s per dialog).
- With `renderer=gl` Inventor shows a "DirectX 12 is not supported or installed on this PC" box when Application Options is
  opened the first time (OK is harmless).
- The trial popup (AdskLicensingAgent `webview` window) is invisible and modal on integ (135; on fix/134 it is above the main
  window, content still blank: 132): close it with WM_CLOSE (`tests/wl_winctl.exe HWND close`);
  the harness already does. Find windows with `tests/wl_winlist.exe` (all visible top-levels: hwnd, rect, owner), `tests/wl_wintree.exe
  [exe-substring]` (with children, hidden too). Wine rects of toplevels are NOT screen positions here (mutter places them).
- Probes: wl_xswap (cross-process swapchain, 132), wl_idle (WaitForInputIdle, 133), wl_xowner + wl_xowner.sh (owned windows z-order, 134/135),
  wl_childswap (same-process child/layered/popup swapchain; works), wl_winctl (close/hide/show/top/move a window by HWND).
- Input quirks of x/wshot.sh: `click` before any `move` hits (0,0) = the Activities hot corner; `rel` motion while a button is held does not
  drag, use absolute `move` steps; orbit = `keydown Shift_L; down 2; move...; up 2; keyup Shift_L` (F4 does not orbit on X either);
  `type` drops non-ASCII (mutter: "No keycode found for keyval", harness limit, not Wine); Super_L does not open the overview.
  Screenshots of Inventor show the account name top right: crop before keeping.
- Observed fine on Wayland: window resize/move by the compositor (Wine sees the new size), maximize/restore (maximized main window
  is 1920x1048 at (-4,-4): mutter keeps the 32 px panel), menus/context menus/Marking Menu/tooltips at the pointer (subsurfaces;
  but a tooltip over the 3D viewport is drawn under it: 145),
  text entry, clipboard between Wine processes (not with the host: no zwlr_data_control).
- An explorer stub window ("Shell_TrayWnd", 166x52) is always visible top-left; DBXBridge/other helper processes each log the three
  `wayland_process_init` capability errors (noise, ~130 lines per Inventor start).

## Other compositors: vmwl (2026-10-03)
`vmwl/` is a qemu VM (Ubuntu 26.04) where GNOME 50 (mutter 50.1), KDE Plasma 6 (KWin) and sway can be started with
`vmwl/session.sh gnome|kde|sway`, and the host's `wt/wayland-build` runs inside it (`vmwl/wl.sh`, read-only share at /host);
screenshots/input by QEMU, same for all. Usage, protocol tables and baselines: `vmwl/README.md`, `vmwl/globals-*.txt`.
Baseline: 134 (`wl_xowner self`) reproduces on mutter 50 but not on KWin or sway; 132 (`wl_xswap`) reproduces on all three.
xdg_wm_dialog_v1 exists on mutter 50 and KWin (not sway); xdg-foreign v2 and xdg_activation on all three.
Inventor has not run there: the one attempt crashed because of the launch environment (mscoree override, not Wayland or
26.04), and a guest start is a second licensing device, so it waits for the user's decision (issue 152, `vmwl/README.md`).
On 26.04 (Mesa 26.0.8 llvmpipe/lavapipe, LLVM 21) `d3d11_present` works under winewayland and under winex11 on Xwayland,
with the GL and the Vulkan renderer; of the 22.04 build's unix libs only winedmo.so (ffmpeg 4 sonames) cannot load there.

## Host GPU for Wayland / VMs: GBM needs nvidia-drm KMS (checked 2026-10-03, driver 580.178.04)
- GBM (libgbm, Mesa's buffer API: `gbm_device` from a DRM fd, `gbm_bo` buffers exportable as dma-bufs, `gbm_surface`
  as an EGL native window) is what Wayland compositors, wlroots' headless backend, Xwayland/GLAMOR and qemu's
  `egl-headless,rendernode=` use to render on a GPU. Mesa's libgbm picks a backend by DRM driver name:
  `/usr/lib/x86_64-linux-gnu/gbm/nvidia-drm_gbm.so` for `nvidia-drm`.
- Here it does not work: `tests/gpu/gbmtest.py /dev/dri/renderD128` → the NVIDIA backend is loaded and fails
  (`__NV_GBM_TRACE_ENABLED=1`: `nv_common_gbm_create_device failed`), libgbm falls back to Mesa's built-in backend
  (llvmpipe), buffer creation fails. NVIDIA README ch. 41B: "DRM KMS must be enabled" (ch. 36: `nvidia-drm modeset=1`,
  off by default). KMS is off on this host: card1-4 have no connectors in /sys/class/drm, DRM_IOCTL_MODE_GETRESOURCES
  → ENOTSUP (the parameter file itself is root-only). Not a sandbox limit.
- What does work without KMS: Vulkan, GLX on our headless Xorg servers, and EGL on the device platform
  (EGL_EXT_platform_device: all four RTX 6000 Ada render) — but no Wayland compositor or qemu path uses that.
- So GPU-composited Wayland sessions and virgl need `modeset=1` (host-wide module parameter, reboot or module
  reload); until then Wayland testing is llvmpipe (host mutter 42, vmwl/ guests).
