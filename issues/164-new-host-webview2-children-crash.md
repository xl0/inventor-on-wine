# 164 new host: every D3D11 device creation fails on the NVIDIA displays (no Mesa device-select Vulkan layer) → WebView2 dies, Inventor shows "Encountered an improper argument."
Status: fixed (environment; no Wine change) · Owner: 164 worker · Branch: none · Found in: first Inventor run on the Ubuntu 26.04 host (inv, :98, build/ = 04293594c50)

## Symptom
1. Home page black, Assistant pane blank; no `msedgewebview2` processes left ~40 s after start, fresh Crashpad dumps in all
   three WebView2 user-data dirs (licensing agent, ADP, Home page).
2. Modal "Encountered an improper argument." (MFC CInvalidArgException) once a document was created (`hello`: first part).

Both are one bug: on this host `D3D11CreateDevice` fails with E_OUTOFMEMORY for every process on :98.

## Cause
wined3d's Vulkan adapter is the FIRST physical device the loader returns (`get_vulkan_physical_device`, adapter_vk.c; upstream
TODO "Create wined3d_adapter for each device"). Only the GPU that owns the X screen can present to it. The project selected
that GPU with `DRI_PRIME=pci-0000_<bus>_00_0`, which is implemented by Mesa's implicit Vulkan layer
`VK_LAYER_MESA_device_select` (package `mesa-vulkan-drivers`). The reinstalled host has the NVIDIA ICD only: no
mesa-vulkan-drivers, so no layer, DRI_PRIME is ignored and the order is plain PCI order: 16:00.0, 34:00.0, ac:00.0, ca:00.0.
:98 is on ca:00.0 (last), wined3d takes 16:00.0:

    err:d3d:wined3d_swapchain_vk_create_vulkan_swapchain Queue family does not support presentation on this surface, vr VK_SUCCESS.
    err:dxgi:dxgi_device_init Failed to create implicit swapchain, hr 0x80004005.      -> D3D11CreateDevice 0x8007000e

(The same message as "NVIDIA Vulkan can't present to Xvfb" in CODE.md's host notes.) :100 (16:00.0 = device 0) would have
worked by luck; :99 and :101 not (predicted from the order, not run).

### Symptom 1 chain (Chromium side, from the Crashpad reports, `strings -a`)
- GPU process (`ptype gpu-process`, 9 threads): breakpoint at msedge.dll+0x4a201f5, rax = 0x8007000e,
  `ERROR:components\viz\service\display_embedder\output_device_backing.cc:150] D3D11CreateDevice failed: Out of memory. (0x8007000E)`,
  switches `--gpu-recent-crash-count=0 --use-gl=disabled`: the software compositor's presenter also wants a D3D11 device
  (WARP; Wine falls back to hardware). `gpu-venid 0x0000`. Before it the hardware path fails without a dump: in the
  err log of run a-nolayer 21 more processes (7 per browser) fail two device creations each and just exit; the 9 dumped
  ones fail one.
- After three of those per browser (4-5 s apart) the browser process itself (50-60 threads) dies at msedge.dll+0xb05f40d:
  `FATAL:content\browser\gpu\gpu_data_manager_impl_private.cc:436] GPU process isn't usable. Goodbye.`
  So the GPU process dies first and takes everything with it; renderers/utilities are never the cause.
- `mdmp.py` on all 12 dumps of one run: 9 x the first site, 3 x the second. Nothing else.

### Symptom 2 chain
Stack of the box (gdb + winesyms/sehbt on Inventor.exe while it was up; the catch block runs on top of the throw frames, so
`stackscan` + `sehfrom <slot of _CxxThrowException's return address>` recovers them; mfc140u names from Microsoft's public
PDB with `tools/pdbpub.py`):

    MessageBoxW <- CWinApp::ShowAppMessageBox <- AfxMessageBox <- CException::ReportError <- FwUI.dll+0x47c4da (catch block)
    thrown by: _CxxThrowException <- AfxThrowInvalidArgException+0x21 <- COleDocument::OnIdle+0xce <- CDocTemplate::OnIdle+0x51
      <- CWinApp::OnIdle+0x77 <- FwUI.dll+0x1eb438 (app OnIdle override) <- FwUI.dll+0x1e88b1 (message loop) <- FwUI.dll+0x459b6c (Run)

`COleDocument::OnIdle` walks the document's views and does `pView->EnsureParentFrame()` (ENSURE = this exception): a view
of the document has no frame window. In the log of the same run Inventor.exe itself fails six D3D11CreateDevice calls while
`hello` creates its first part, then `err:seh:user_callback_handler ignoring exception c0000005` 3.4 s later, then the box.
Read: the view's graphics init fails, an access violation in a window callback is swallowed, a half-made view stays in
the document (inferred; the ends of the chain are proven by the A/B below, the middle is not traced).
The box is thrown from idle, so it comes back after OK.

The `err:dialog:EndDialog got invalid window handle` lines in `inst/invscen/inventor.log` are NOT from this host: their
timestamps (101536 s) are above this boot's uptime (the log is appended; they are the last lines of the previous run on
the old host, at Inventor's exit after two `RevokeDragDrop invalid hwnd`). The first new-host run logged nothing (WINEDEBUG=-all).

## Evidence / bisect
`inst/164/probe/d3dcreate.c` (adapter list + D3D11CreateDevice HARDWARE/WARP) and `tests/d3d11_present.exe` on :98.
"layer" = libVkLayer_MESA_device_select.so + its JSON visible to the Vulkan loader.

| changed | D3D11CreateDevice on :98 | Inventor (hello + 45 s) |
|---|---|---|
| nothing (build/, vulkan, no layer) | 0x8007000e (HARDWARE and WARP) | no msedgewebview2 left, 12 dumps, "improper argument" (run a-nolayer) |
| + layer, DRI_PRIME=ca (only change) | S_OK, d3d11_present ok | Home page, Assistant, trial popup render; 3 gpu + 3 renderer + 7 utility alive; no dumps, no box (b-layer, c-sysroot) |
| layer, DRI_PRIME=16 / 34 / ac | 0x8007000e each | – |
| layer, NODEVICE_SELECT=1 | 0x8007000e | – |
| Wine build: build-s (e00a74f6590, built on this host), scratch prefix | same: fails without, works with the layer | – |
| renderer=gl, no layer | S_OK (GL context comes from the X screen) | not run (GL has 162) |
| Chromium switches, filesystem (ZFS), kernel/glibc/AppArmor/seccomp | not tested: nothing in the traces points there and the layer alone flips both symptoms | |

`vulkaninfo --summary` first device: no layer 864dfccf (16:00.0) whatever DRI_PRIME says; with the layer DRI_PRIME=ca
→ 219a849e (ca), 34 → 514d9995, 16 → 864dfccf, ac → 5e3d569e, unset on :98 → 219a849e (the layer asks the X server).

## Fix
`mesa-vulkan-drivers=26.0.8-1ubuntu0.3` (the one package missing, same version as the installed Mesa) added to the local
package prefix: deb in deps/sysroot-debs, unpacked into deps/sysroot, recorded in tools/sysroot.pkgs. Everything that loads
the sysroot env (prefix.sh, invscen/run.sh, x/start.sh, regress.sh, `tools/sysroot.sh run`) now finds the layer through
XDG_DATA_DIRS + LD_LIBRARY_PATH; lavapipe (llvmpipe) is a Vulkan device again, as on the old host.
Unpacked by hand (`dpkg-deb -x` of the one deb) instead of `tools/sysroot.sh add`: `add` ends in `unpack`, which wipes and
re-extracts the whole prefix like `rebuild` (not safe while a regress run uses it). End state is what `add`/`rebuild` produce.

Still needs the env: a bare `DISPLAY=:98 DRI_PRIME=... build/wine app.exe` (without `tools/sysroot.sh run` / `eval env`)
fails as before. The host-side alternative that removes that trap: `sudo apt install mesa-vulkan-drivers`.

## Open / not done
- Wine could pick the device itself (prefer a physical device that can present on the display, or one adapter per device):
  not done, the layer restores the documented setup. winex11's GPU matching (vkGetRandROutputDisplayEXT over RandR
  providers) would not help here: the headless screen's own provider (NVIDIA-0) has no outputs, the two others listed are
  other GPUs.
- tools/regress.sh sets `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`: that file does not exist on this
  host (Mesa 26 names it lvp_icd.json; now in deps/sysroot/usr/share/vulkan/icd.d/), so regress runs have NO Vulkan
  driver ("Found no drivers"): expected host drift in every Vulkan-dependent test until the path is fixed.
- x/shot.sh needs numpy/PIL from the sysroot but does not load its env (`tools/sysroot.sh run x/shot.sh ...` works).
- `tools/sysroot.sh add` wipes the prefix (see above).
