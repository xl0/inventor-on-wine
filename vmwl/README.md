# vmwl: Linux VM for testing winewayland.drv against real compositors

Ubuntu 26.04 LTS guest (kernel 7.0, Mesa 26.0, mutter/gnome-shell 50.1, KDE Plasma/KWin 6, sway 1.11).
qemu/KVM, 8 vCPU, 16 GB, 120 GB qcow2 (`disk.qcow2`, ~5 GB used), virtio-vga (KMS, software GL = llvmpipe),
usb-tablet (absolute pointer), user networking. Everything listens on 127.0.0.1 only: SSH :2223, VNC :5911.
Not the Windows VM (`vm/`); nothing here touches it.

## Use
- `vmwl/run.sh` starts it (first run: `fetch.sh` + disk + cloud-init seed + key `id_ed25519`, git-ignored). Stop: `vmwl/ssh.sh sudo poweroff`.
- `vmwl/ssh.sh [cmd]` ssh as `xl0` (uid 1000, passwordless sudo, hostname = host's, like the host user so Wine's user name matches).
- `vmwl/session.sh gnome|kde|sway|stop`: tty1 autologins xl0, whose `~/.bash_profile` execs the compositor named in `~/.wl-session`
  (gnome: `gnome-session --session=gnome`, GNOME sends Escape once to leave the overview; kde: `startplasma-wayland`; sway).
  Log: `~/.wl-session.log` in the guest. The socket is `wayland-N` in /run/user/1000 (sway gets `wayland-1`), `wl.sh` finds it.
- `vmwl/wl.sh CMD...` runs CMD in the guest with XDG_RUNTIME_DIR, WAYLAND_DISPLAY, DBUS_SESSION_BUS_ADDRESS set and DISPLAY unset.
- `vmwl/shot.sh [out.png]`, `vmwl/input.py click X Y|dclick|key COMBO|type TEXT`: QEMU screendump / QMP absolute pointer (1280x800),
  compositor independent. They are `vm/shot.sh` / `vm/input.py` with `VM_DIR` pointing here.
- Project subset exported read-only over virtio-fs (virtiofsd inside bwrap, only these paths exist): guest `/host/{wt/wayland-build,tests,wine-src/nls,wine-src/fonts}`
  (the build's symlinks point into wine-src). Add more with `VMWL_BIND="wt/foo-build" vmwl/run.sh` (project-relative, same path under /host).
  `prefixes/` is not shared. Prefixes live on the guest disk (`/home/xl0/...`).
- Wine: `vmwl/wl.sh 'export WINEPREFIX=$HOME/wp-x WINEDEBUG=-all; B=/host/wt/wayland-build; WINEDLLOVERRIDES="mscoree,mshtml=" $B/wine wineboot -i; $B/wine notepad'`
  (the override only on wineboot: exported, it reaches the app and breaks every .NET program, see Inventor below; long-running apps:
  `setsid nohup ... &`; stop with `$B/server/wineserver -k`). The host's glibc-2.35 build runs as is on the guest.
  X11 instead of Wayland (GNOME/KDE have Xwayland): `export DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/.mutter-Xwaylandauth.*); unset WAYLAND_DISPLAY`,
  then `wineboot -u` (Wine picks winex11.drv; check with `WINEDEBUG=+loaddll`).
- `vmwl/wl_xowner.sh COMPOSITOR [BUILD]`: all wl_xowner cases with PASS/FAIL, protocol-error and xdg-foreign counts; results for fix/134 in `results-134.md`.
- `vmwl/probes.sh NAME`: notepad, `wl_xowner.exe self` (+ click into the owner), `wl_xswap.exe` in the current session ->
  `vmwl/shots/NAME-*.png`, fresh prefix `~/wp-NAME`.
- Snapshot: internal qcow2 snapshot `provisioned` (clean guest, no compositor running, no prefixes). Back to it with the VM stopped:
  `qemu-img snapshot -a provisioned vmwl/disk.qcow2`. New one: `-d provisioned` then `-c provisioned`. Full reset: delete `disk.qcow2`, `seed.iso`, `id_ed25519*`, run.sh again (re-provision: `ssh.sh 'sudo bash -s' < provision.sh`, then `provision2.sh`).

## Image provenance
`https://cloud-images.ubuntu.com/releases/26.04/release/ubuntu-26.04-server-cloudimg-amd64.img` (825 MiB, published 2026-09-29, release build
20260918 per `...amd64.release.20260918.20260927.image_changelog.json`), sha256
`8800651811af9a85465ad1d552add729947bb16488dddb4a9b5305a3d97332b2`. `fetch.sh` verifies `SHA256SUMS.gpg` with `gpgv` and the host's
`/usr/share/keyrings/ubuntu-cloudimage-keyring.gpg` (good signature, key D2EB44626FDDC30B513D5BB71A5D6C4C7DB87C81 "UEC Image Automatic Signing Key"),
then the image against SHA256SUMS. Packages: Ubuntu archive only (`provision.sh`). Provisioning: cloud-init NoCloud seed (`user-data.in`).

## Installed
GNOME (gnome-session/shell/mutter, gsd, nautilus), KDE (plasma-desktop, kwin-wayland), sway + foot, xwayland, wayland-utils (`wayland-info`), grim, slurp,
wl-clipboard, mesa-utils (`eglinfo`), mesa-vulkan-drivers (lavapipe), libwayland/xkbcommon/xkbregistry/freetype/fontconfig/gnutls/vulkan loader/alsa/pulse runtime,
build-essential, mingw-w64, wayland/xkbcommon/xkbregistry/freetype/vulkan/gnutls dev headers, gdb, strace, rsync. No display manager.

## Autodesk licensing: the guest is a second device (issue 152)
Do not start Inventor or the `prefixes/inv4` copy in the guest: every start is a checkout as a different device than the host (one active device per
account). No forwarder: the old 39683 forward was removed (nothing connects to that port, on the host either).
- Discovery: the SDK in Inventor reads `C:\ProgramData\Autodesk\AdskLicensingService\AdskLicensingService.data` (`{"Addr":"127.0.0.1:PORT"}`, written
  by the prefix's own service) and opens a WebSocket there, so each prefix is served by its own AdskLicensingService.
- The device ID is not the service's: the service has the AdskLicensingAgent started by the client compute it (monitor.dll) from what Wine reports for
  the disk serial (Win32_DiskDrive, else the system volume serial), the system UUID (Win32_ComputerSystemProduct, from the Unix machine-id) and the
  user name. Guest and host differ in the first two, whichever service answers. Pointing the guest at a host service would only make that host
  service register the guest's ID.

## Protocol support (wayland-info, files `globals-<compositor>.txt`; gnome = mutter 50.1, kde = KWin 6, sway 1.11 / wlroots)
| protocol | gnome | kde | sway |
|---|---|---|---|
| xdg_wm_base | v7 | v6 | v5 |
| xdg_wm_dialog_v1 | v1 | v1 | - |
| zxdg_exporter/importer_v2 (xdg-foreign v2) | v1 | v1 | v1 |
| zxdg_exporter/importer_v1 | v1 / - | - | v1 |
| xdg_toplevel_drag_manager_v1 | v1 | v1 | - |
| wp_fractional_scale_manager_v1 | v1 | v1 | v1 |
| zwlr_data_control_manager_v1 | - | - | v2 |
| ext_data_control_manager_v1 | - | v1 | v1 |
| xdg_activation_v1 | v1 | v1 | v1 |
| zwp_pointer_constraints_v1 / relative_pointer_manager_v1 | v1 / v1 | v1 / v1 | v1 / v1 |
| zwp_text_input_manager_v3 | v1 | v1 | v1 |
| zxdg_decoration_manager_v1 | - | v1 | v1 |
| xdg_toplevel_icon_manager_v1 | - | v1 | - |
| wp_cursor_shape_manager_v1 | v2 | v2 | v1 |
| wp_color_manager_v1 | v2 | v1 | - |

## Baselines (host wt/wayland-build @ 04293594c50, fresh prefix; screenshots in `shots/`)
- notepad: renders and works under all three (sway tiles it full-screen; Wine draws its own caption).
- `wl_xowner.exe self` (issue 134): gnome: B (green) disappears behind A after clicking A (bug reproduced); kde and sway: B stays above A.
- `wl_xswap.exe` (issue 132): no magenta in any of them, A stays blank (bug reproduced everywhere).

## GL=1 (virgl) experiment, 2026-10-03: does not work for Wine
`GL=1 vmwl/run.sh` (optional `GL_NODE=/dev/dri/renderD129`, default renderD130) uses `virtio-vga-gl` + `-display egl-headless` on the host's NVIDIA render node.
qemu 6.2 starts fine, the guest gets virgl (`eglinfo`: `renderer: virgl`, OpenGL 4.3 core / ES 3.2), gnome-shell and sway run and screendump/VNC work.
Retried with renderD128: same, no qemu error on stderr. Per the coordinator, NVIDIA's GBM backend does not initialise here (nvidia-drm without modeset), so the host side of
virgl is most likely Mesa llvmpipe, i.e. not GPU-accelerated in any case (not verified; qemu was not seen in nvidia-smi's compute list, which would not list graphics clients anyway).
But any Wine process in a Wayland session hangs at startup with virgl: `wineboot -i` never finishes (explorer.exe /desktop and rundll32 idle after loading uxtheme,
no error), `notepad` shows no window. The same prefix works with `LIBGL_ALWAYS_SOFTWARE=1` set for Wine, so it is winewayland/EGL on virgl (not investigated
further; same with sway as with GNOME). Default (no GL=1) is unchanged: llvmpipe, d3d11_present 390 fps.

## Wine's graphics stack on 26.04 (GNOME session, fresh prefix, host wt/wayland-build)
`tests/d3d11_present.exe`: Wayland gl 317 fps, Wayland vulkan (lavapipe) 75, Xwayland gl 279-410, Xwayland vulkan 371, pixel readback correct in all four.
Libraries the 22.04 build wants and the guest lacks: `libavcodec.so.58`/`libavformat.so.58`/`libavutil.so.56` (winedmo.so: ffmpeg 4, 26.04 has a newer
major, needs a rebuild there), `libpcsclite.so.1` and `libodbc.so` (not installed). The other 25 dlopen'ed sonames of config.h resolve.

## Inventor in the VM (2026-10-03): not run; the earlier crash was the launch environment (issue 152)
The first attempt (prefix copy of inv4, `rsync -aX`, 45.5 GB, still in `/home/xl0/prefixes/inv4`) crashed 7 s after the licence checkout, no window.
Cause: Inventor inherited `WINEDLLOVERRIDES="mscoree,mshtml="` from the wineboot line. Without mscoree Wine cannot load IL-only DLLs, .NET 10's
`System.Runtime.dll` fails, the main frame's WM_CREATE throws a CLR exception (`err:seh:user_callback_handler ignoring exception e0434352`) and
Inventor then dereferences a NULL channel builder: access violation reading 0 at `CommonUI.dll+0x60b90`. Same dump signature on the host under X11
with the override set, and none without it. So not 26.04, the VM or winewayland. Whether Inventor runs in the guest is untested: it needs a licensing
decision first (above). To drop the prefix copy: delete `~/prefixes/inv4` in the guest (+ `fstrim`) or restore the `provisioned` snapshot (it predates
the copy and still has the old forwarder unit, harmless).
