# Local workstation

Ubuntu 24.04 x86_64, 16 logical CPUs, 58 GiB RAM. Local setup is Wine-only:
no reference VM. The server environment and populated prefixes described below
are not present here.

- `wine-src/`: partial clone of `xl0/wine`, `integ` at `47e296ffde4d`.
  Tracks `gh/integ`; `origin` points to WineHQ. No rebase onto newer upstream.
- `build/`: `../wine-src/configure --enable-archs=i386,x86_64` succeeds with
  GCC/MinGW 13. Only notice: legacy OSS audio unavailable (ALSA/Pulse work).
  Logs: `configure.out`, `config.log`, `make.log`, `build-resource-usage.txt`.
  Full build succeeded in 29m14s, peak cgroup memory 6.86 GiB, no swap.
  Version: `wine-11.18-360-g47e296ffde`.
- `prefixes/smoke`: local prefix; 32/64-bit `cmd.exe` and the existing
  `tests/custom_caption.c` GUI probe pass (`build/smoke.log`). User-folder
  symlinks replaced with prefix-local directories. Mono/Gecko not installed;
  smoke runs disable them and winemenubuilder. Test wineserver stopped.
  Local D3D11 Vulkan smoke also passes on desktop `:0` (clear/present/readback,
  `inst/local/d3d11-vulkan.log`); this does not validate Inventor's viewport.
- `prefixes/inv`: native .NET 4.8 (Release 528049, both C# compilers run),
  Windows 11, Gecko 2.47.4 for both architectures, Edge and WebView2
  154.0.4258.37 installed. Browser downloads match the server's pinned hashes;
  `tools/edge.sh` sets Edge as default for the OAuth callback.
  Uses distro Winetricks 20240105
  with explicit `WINE=build/wine` and `WINESERVER=build/server/wineserver`
  (absolute paths). User-folder links isolated; winemenubuilder disabled to
  avoid changing the host desktop. Setup log: `inst/local/prefix-setup.log`.
  Installer provided by the user:
  `~/Downloads/Autodesk_Inventor_Professional_2027_1_English_en-US_setup_webinstall.exe`
  (SHA256 `1b843e5db368d07c68494da2610db8d9c98f5ae41b821efc31c9de49386162cc`).
  Extracted unmodified to `inst/webinstall/`; entry point `Setup.exe`.
  ODIS reports Inventor core + 2027.1 update, TrueView and Content Libraries
  INSTALLED. Electrical Catalog Browser failed: its CA MSI's
  `AceUnzipZipFiles` action returns 1603; optional add-on rolled back
  ([050](issues/050-electrical-catalog-unzip-msi.md)).
  Main Inventor launch/sign-in not yet verified. Installer was launched on `:0`
  with `setsid nohup`, nice 10, idle I/O, 14 GiB cap/no swap in
  `inventor-install.scope`. Uses `WINE_D3D_CONFIG=renderer=vulkan`.
  Launcher PID: `inst/local/installer.pid`; output: `inst/local/installer.log`.
  Browser setup log: `inst/local/browser-setup.log`.
  Setup verification uses `wine winecfg -v`, not `HKCU\Software\Wine\Version`
  (winecfg sets NT version keys and removes that override).
- Local Xvfb crashes during GLX initialization in NVIDIA EGL/GBM.
  `xvfb-run -a -s '-screen 0 1024x768x24 -nolisten tcp -extension GLX'`
  works for 2D smoke tests; verify with `xdpyinfo` before running Wine.
  The host desktop `DISPLAY=:0` is accessible.
- Missing dependencies were installed by the user outside the sandbox:

  ```sh
  sudo apt-get install --no-install-recommends \
    g++-mingw-w64-i686-posix g++-mingw-w64-x86-64-posix \
    libgnutls28-dev libasound2-dev libpulse-dev \
    libxkbregistry-dev libxxf86vm-dev libusb-1.0-0-dev \
    libsdl2-dev libcups2-dev libkrb5-dev libpcap-dev \
    libpcsclite-dev libsane-dev libv4l-dev libva-dev \
    ocl-icd-opencl-dev libcapi20-dev samba-dev
  ```

  The MinGW C++ packages also bring C compilers and binutils for both Windows
  architectures; no additional i386 Linux libraries are needed. Wine bundles its
  PE-side media libraries. Run in place, leaving system Wine 9 and its prefixes
  untouched.
- Local builds use nice 15, idle I/O, 8 jobs, and an aggregate cgroup memory
  limit (8 GiB reclaim threshold, 12 GiB hard cap, no swap). From `build/`:

  ```sh
  nice -n 15 ionice -c 3 systemd-run --user --scope \
    --unit=inventor-wine-build \
    -p MemoryHigh=8G -p MemoryMax=12G -p MemorySwapMax=0 \
    -p CPUWeight=25 -p IOWeight=10 make -j8 > make.log 2>&1
  ```

  User-manager scope creation works inside the sandbox and preserves its
  filesystem restrictions. Never use the server's `-j120` locally.

# Layout (server setup unless noted above)

- This dir is a git repo with an allowlist `.gitignore` (scripts, docs,
  tests only), pushed to github.com/xl0/inventor-on-wine. `wine-src/` is its
  own repo: fixes on local `fix/NNN-*`, cherry-picked onto `integ` (linear patch stack
  on master; `git format-patch master..integ` = our series), force-pushed to
  the fork github.com/xl0/wine (remote `gh`; origin = gitlab.winehq.org).
- `wine-src/` — upstream Wine git (gitlab.winehq.org), our patches go here.
- `build/` — out-of-tree build, `--enable-archs=i386,x86_64` (new WoW64,
  no 32-bit host libs). Run in place: `build/wine`, `build/server/wineserver`.
  Full `make -j120` ≈ 2.5 min. Wine's own conformance tests are in
  `build/dlls/*/tests/x86_64-windows/`.
- `issues/NNN-slug.md` — one per Wine bug (brief + memory for its worker).
- `notes/wine/*.md` — Wine internals reference by subsystem. `notes/worker.md`
  — worker rules, reporting, build/test recipe (the brief every worker reads).
- `wt/NNN/`, `wt/NNN-build/`, `wt/NNN-prefix/` — per-issue wine-src worktree
  (branch `fix/NNN-slug`), its build, its prefix.
- `prefixes/<name>/` — Wine prefixes. New prefixes symlink `users/<you>/Documents` to the
  host home (read-only in the sandbox): replace it with a real dir. Create with
  `WINEDLLOVERRIDES="mscoree,mshtml=" build/wine wineboot -u`
  (otherwise Mono/Gecko download dialogs hang on a headless display).
- `tests/` — our small mingw-built test programs (run on Wine and the VM).
  `d3d11_present.c`: device + swapchain + clear/present + readback, exit 0 = ok.
  Build: `x86_64-w64-mingw32-gcc -O2 -o X.exe X.c -ld3d11 -ldxgi -luuid`.
  `p7x_winverifytrust.c` (+`p7x_gen.py`): msix-SDK-style signature checks on an
  AppxSignature.p7x. `regloadkey_hive.c`: RegLoadKey of a binary hive + tree dump.
  `dcomp_create.c`: DCompositionCreateDevice/2/3(NULL) (Win11: S_OK).
  `dcomp_qi.c`: desktop device QI for Edge's undocumented {4ca97a18-...} (Win11: S_OK).
  `dxgi_comp_swapchain.c`: GetDC-painted composition swapchain, buffer order after Present1
  and dirty-rect Present1. `layered_child_gpu.c`: cross-process D3D child in a colour-keyed
  layered window. `syscommand_hidden.c`: SC_MAXIMIZE/MINIMIZE on hidden windows.
  `wofset.c`: WofSetFileDataLocation / FSCTL_SET_EXTERNAL_BACKING.
  `lowbox_token.c`: NtCreateLowBoxToken / CreateAppContainerToken token properties.
  `hkcu_proto.c`: per-user URL protocol via HKCR / AssocQueryString. `hkcr_merge.c`: HKCR merged
  view probe (which side backs open/create/query/enum; needs admin).
  `tlb_cache.c`: typelib cache + PSDispatch/PSOAInterface proxy/stub probe and benchmark (032).
  `com_peruser.c`: COM vs per-user classes (CLSID/ProgID/Interface/OleRegGetUserType,
  RegOpenUserClassesRoot); run elevated and non-elevated, modes as argv[1] for fresh processes.
  `dosdev_name.c`: DOS device names in paths (035). `wintext.c` (`wintext.exe [TITLE]`):
  dump visible top-level windows + children (class, text, rect, pid, styles).
  `dragdrop_revoke.c`: RegisterDragDrop/RevokeDragDrop across threads/apartments/processes (034).
  `droptargets.c`: list windows with OLE drop-target props, flag cross-process ones (read-only).
  `ctrlchar_text.c [FONT..|show]`: per-API table of how control chars are drawn/measured
  (GDI, DrawText, GCP, Uniscribe); `show`: "Pan\r" in ExtTextOut/DrawText/combo/listbox (039).
  `dtp_short.c`: short-date DateTimePicker layout (044, screenshot it).
  `custom_caption.c` (`[max|plain|plainmax] [secs]`): client over the caption via
  WM_NCCALCSIZE, own blue caption strip; prints rects (040, screenshot it).
  `ebrowser_events.c`: ExplorerBrowser host like Inventor's file dialogs: SIGDN names of the folder chain,
  ICommDlgBrowser / IExplorerBrowserEvents / DShellFolderViewEvents log, clicks the first file (041, 043).
  `subclass_probe.c`: comctl32 v5/v6 SetWindowSubclass props, nested removal, destroy in a
  callback, cross-thread/process calls (046).
  `syslink_attr.c`: SysLink `<a>` parsing with extra attributes (049).
- `x/` — headless display. `x/start.sh` runs Xorg :98 on the NVIDIA GPU at
  ca:00.0 (card4). ac:00.0 carries the host console / gdm, avoid it.
  `x/start.sh 99 PCI:52:0:0` = second display :99 on 34:00.0 (`DRI_PRIME=pci-0000_34_00_0`),
  `x/vnc.sh 99` → VNC 5903, `x/shot.sh out.png 99`. :99 + `prefixes/inv2` (copy of inv,
  signed in) is a second Inventor setup, independent of :98/inv.
  Third: `x/start.sh 100 PCI:22:0:0` (GPU 16:00.0, `DRI_PRIME=pci-0000_16_00_0`),
  `x/vnc.sh 100` → VNC 5904, `prefixes/inv3` (copy of inv minus the installer Temp cache).
  Licensing: one AdskLicensingService on 127.0.0.1:39683 (host network) serves
  the Inventors of all prefixes, whichever prefix's service bound it first. Never
  wineserver -k an Inventor prefix while other Inventor sessions run; if you must,
  restart the other Inventors afterwards (they show "Licensing error" and quit).
- `vm/` — Windows 11 Pro reference VM (qemu/KVM, not libvirt).
  `run.sh [install]`, `shot.sh [png]` (screendump via HMP `mon.sock`).
  SSH: `ssh -i vm/id_ed25519 -p 2222 -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null -o BatchMode=yes dev@127.0.0.1`
  (PowerShell default shell; host key changes with snapshots).
  VNC 127.0.0.1:5901. Install is unattended (`autounattend.xml` + `setup.ps1`
  on a generated ISO): local admin dev/dev, autologon, OpenSSH, no sleep.
  Unactivated (generic Pro install key).
  `winrun.sh X.exe [args]` runs an exe in the desktop session (scheduled task;
  SSH is session 0, no desktop) and returns its output + exit code.
  `WINRUN_ID` namespaces concurrent callers (task + `C:\t\ID`),
  `WINRUN_TIMEOUT` (600 s) kills hung runs (exit 124).
  Snapshots: qcow2 internal (`clean` = fresh install, `base` = + virtio-fs) with
  matching `snap/<name>/{vars.fd,tpm}`. Restore: VM off, `qemu-img snapshot -a
  NAME win.qcow2`, copy vars.fd + tpm back.
- virtio-fs share: host `vm/share/` = guest `Z:`. Host side: Rust virtiofsd
  (unprivileged, `--sandbox none`; `VFS_LOG=debug vm/run.sh` for tracing),
  guest RAM is a shared memfd. Guest side: WinFsp + viofs driver + VirtioFsSvc.
  Cached reads of big files fail ~10% ("Error performing inpage operation");
  use unbuffered copies (`robocopy Z:\ C:\dst FILE /j`, from the desktop
  session via winrun — Z: isn't visible to SSH/session 0; over SSH plain `scp`
  is fast: 3.4 GB in ~22 s) for bulk data, then
  run things from C: (never from Z:, it isn't NTFS).
- `iso/` — Windows ISO, Inventor 2027.1 web installer.
- `inst/` — installer work: `webinstall/` (7z-extracted web installer), logs.
- :98 runs openbox (started by x/start.sh); without a WM Wine never unmaps
  hidden windows (issue 029).
- `x/shot.sh [png [N]]` screenshots :98 (or :N) with a plain root XGetImage;
  `x/vnc.sh` (re)starts x11vnc on 127.0.0.1:5902 (-noxdamage) and turns off
  :98 screen blanking. Never `xwd -root`: Wine's per-process X colormaps make
  xwd take its multi-colormap path (packed 24 bpp) and draw Wine dialogs black (036).
  `xwd -id WIN` of a single window is fine.
- `vm/input.py click X Y | key COMBO | type TEXT` — VM input over QMP.
- `deps/` — third-party downloads, pinned (hash in the fetch cmd or below).
  virtiofsd 1.14.0 static zip sha256 2e4fe957…8978e (GitLab upload, no
  upstream hash), virtio-win-0.1.302.iso 303f7ae4…67949d (only viofs/w11 used),
  winfsp-2.1.25156.msi 073a70e0…9f7a (matches GitHub digest).
  MicrosoftEdgeWebView2RuntimeInstallerX64.exe: WebView2 Evergreen Runtime
  154.0.4258.37, sha256 771042db…582c1b. Microsoft's standalone installer
  (fwlink 2124701) always serves the latest version; on 2026-09-28 that
  matched the VM. The file is the pin, so keep it.
  MicrosoftEdgeEnterpriseX64-154.0.4258.37.msi: Edge Stable x64, sha256
  4d8d922c…258246 (matches edgeupdates.microsoft.com/api/products?view=enterprise).
- `tools/tar.sh` (WINEPREFIX=...): Windows' inbox tar.exe (bsdtar 3.8.8, as on Win11) into
  system32/syswow64; builds static MinGW bsdtar.exe (zlib only) into deps/bsdtar-3.8.8/ if missing.
  libarchive-3.8.8.tar.xz sha256 3873a888…efb918 (GitHub digest), zlib-1.3.2.tar.xz d7a06547…a792f3
  (zlib.net). Installed in inv-net48, inv, inv2, inv3; transplant.sh runs it (050).
- Wine Gecko 2.47.4 (the version wine-src's appwiz.cpl expects), x86 + x86_64
  MSIs in deps/, sha256 = GECKO_SHA in dlls/appwiz.cpl/addons.c; installed with
  `wine msiexec /i wine-gecko-2.47.4-<arch>.msi /qn` into inv-net48, inv, inv2
  (prefixes are created with mshtml= to skip the prompt; mshtml needs Gecko).
- `deps/dxvk.sh PREFIX` — pinned + sha256-checked DXVK 3.1.1 and vkd3d-proton
  3.0.1, copied into the prefix with native DllOverrides. Tarballs cached in deps/.
- Ghidra 12.1.4 (`deps/ghidra_12.1.4_PUBLIC`, zip sha256 ddac49f9…d2d4db, from
  release notes) + Temurin JDK 21.0.12.1+1 (`deps/jdk-21.0.12.1+1`, tarball
  sha256 ce79869e…aee94, Adoptium). Only used by `tools/decomp.sh`.
- `tools/decomp.sh BIN funcs|decomp|xrefs|strings|imports [ARG]` — headless
  Ghidra queries via `tools/Decomp.java`. Project cached per binary sha256 in
  `deps/ghidra-cache/` (+ Ghidra's XDG config/cache), flock per binary; first
  use analyzes (mostly 1 core: 7 MB exe 1.5 min, 44 MB Installer.exe 35 min;
  cached queries 4-6 s, `strings` ~30 s). Clean-room guard refuses MS system paths and
  PEs whose version-info CompanyName or Authenticode O= says Microsoft
  (also refuses Wine builtins, whose CompanyName is Microsoft). The check
  lives in `tools/msbin.py FILE...` (also used by transplant.sh).
- Prefixes: `smoke` (plain), `dxvk` (smoke + DXVK/vkd3d-proton).
- `tools/transplant.sh [footprint|fetch|build]` — prefix `inv-vm`: copy of
  pristine `inv-net48` + Inventor 2027.1 transplanted from the VM (diff against
  snapshot `base`, via a temporary second VM): Autodesk/FlexNet files as shipped
  (incl. app-local MS DLLs), junctions as symlinks, filtered registry delta
  (C:\Users\dev rewritten, Run keys dropped); VC++ 14.50 + .NET 10.0.9 via the
  MS redists Autodesk ran. Data + manifest in `inst/transplant/`. Stopgap until
  the Wine install works: bugs seen only there may be transplant artifacts.
  Test display: own Xorg :107 on GPU 34:00.0 (`inst/transplant/xorg-107.conf`,
  `DRI_PRIME=pci-0000_34_00_0`).
  WebView2 Runtime (in-box on Win11, AdskLicensingAgent's sign-in UI) and
  Microsoft Edge are installed from deps/ by `tools/edge.sh` (WINEPREFIX=...;
  also run on prefixes/inv). Edge is the default browser (its
  installer sets HKCR http/https). The Autodesk sign-in form opens there, and the
  OAuth code comes back through a custom URI scheme registered in the prefix.
  Session: `[B=<build>] inst/transplant/run-inv.sh [log]`
  restarts the prefix and launches Inventor on :107 (licensing service auto-starts since 014).
- `tools/invscen/run.sh [--vm] SCENARIO|all` — Inventor COM API scenarios. Compiles
  `Harness.cs` + `SCENARIO.cs` with the prefix's .NET 4.8 csc (Inventor interop types
  embedded via /link, so the exe also runs on the VM), attaches to the running
  Inventor (GetActiveObject; starts Inventor.exe in prefixes/inv if none runs; the
  VM side never starts it), closes all docs, prints PASS/FAIL/SKIP per step with
  values + timings (step timeout aborts the run; a failure skips the rest of its
  section, `H.Reset()` starts an independent one). Harness helpers: NewPart, Box,
  Vol/Mass (analytic volume checks), EdgeAt/FaceAt, Save, Translate (export via a
  translator add-in).
  Artifacts: inst/invscen/SCENARIO/; `--vm`: C:\t\scen\SCENARIO copied to
  inst/invscen/ref/ (the VM reference). `all` runs the suite (hello tlb part asm
  drawing feat params sheetmetal asmcon asmbig drawing2 script export; asm/drawing
  use part/box.ipt, the rest are self-contained), logs to inst/invscen/results/S.txt
  (`--vm all`: ref/S.txt), prints a PASS/FAIL table and steps >3x slower than
  ref/S.txt; when a crashed Inventor sits in winedbg (034) it kills that prefix's
  Inventor/winedbg/CER dialog by PID (never wineserver -k, see licensing below).
  The harness aborts a step at once on Inventor's "Licensing error" dialog or when
  Inventor is gone (RPC unavailable); timeouts list Inventor's visible dialogs.
  Expectations are analytic (volumes, centroids, flat-pattern lengths, view extents)
  or structural counts taken from the VM once (STEP/IGES/SAT entities, STL
  triangles, SaveAsBitmap light-pixel share). Never pipe run.sh into another
  command: wine children inherit the pipe and the reader never sees EOF.
  Other setup: `INV_PREFIX=prefixes/inv2 DISPLAY=:99 DRI_PRIME=pci-0000_34_00_0`
  (artifacts in inst/invscen/inv2/). `view`: reopens part/box.ipt visible, SaveAsBitmap,
  leaves it open for a viewport screenshot (037). csc wants backslash paths; `using Inventor` clashes with System names
  (File, Environment, Attribute): qualify them. Embedded interop types don't
  inherit (PartDocument is not a Document): cast at runtime (`(Document)obj`).
  `samples` / `samples2016` (symlink): Autodesk's official sample sets
  (inst/samples/, C:\t\samples\{2022,2016} in the prefix and on the VM; open, mass,
  BOM, rebuild, save-as copy, reopen per top-level doc; no expectations, diff vs VM).
  autodesk_inventor_2022_samples.zip sha256 1eeb4164…09292b,
  autodesk_inventor_2016_samples.zip 9d3096f6…41f501c, both from damassets.autodesk.net
  (links on the "Inventor Sample Files" support article; www.autodesk.com zips 403 curl).
- `tools/regress.sh run BUILD` / `compare BASE.txt NEW.txt` — sharded full
  conformance-suite run (32 jobs, own prefix in /dev/shm + Xvfb :120+ per shard,
  software GL/Vulkan, no Gecko/Mono; ~4 min for both arches, 1755 units) and
  diff with re-run check: REAL / NEW (not in baseline) / FLAKY.
  Results cached in `deps/regress/<built commit>/`. Master baseline builder:
  `wt/regress-master` (detached worktree) + `wt/regress-master-build`.

# Running Wine with GPU

    DISPLAY=:98 DRI_PRIME=pci-0000_ca_00_0 WINE_D3D_CONFIG=renderer=vulkan build/wine app.exe

Long-running app sessions (installers, Inventor): launch with
`setsid nohup ... &` so a Claude Code restart doesn't kill part of the process
tree (ODIS helpers died once, leaving Installer.exe spinning on dead COM peers).

# Host notes (Ubuntu 22.04, 120 threads, 1 TB RAM, 4× RTX 6000 Ada, drv 580)

- NVIDIA Vulkan can't present to Xvfb ("Queue family does not support
  presentation") → D3D device creation fails. Hence the NVIDIA Xorg.
- Xorg: run `/usr/lib/xorg/Xorg` directly (Xorg.wrap is console-only).
  Rootless Xorg can't get DRM master → `UseDisplayDevice none` (no modesetting).
- Vulkan device order is ac, llvmpipe, 34, 16, ca; wined3d takes the first.
  Mesa's implicit device_select layer + `DRI_PRIME=pci-0000_ca_00_0` fixes it.
- Wine's full d3d11 test suite still hits VK_ERROR_INITIALIZATION_FAILED on some
  swapchain and crashes mid-run on this setup; single-swapchain apps work.
- DXVK reports the RTX as AMD (1002:73df) by default; set
  `dxgi.hideNvidiaGpu = False` (dxvk.conf) if an app checks for a certified GPU.
- QEMU 6.2: q35 SATA ports hold one CD each (`bus=ide.N`). Its bundled C
  virtiofsd needs root; hot-installing the viofs driver made the Rust
  virtiofsd exit (InvalidMessage) — a VM restart fixed it.
