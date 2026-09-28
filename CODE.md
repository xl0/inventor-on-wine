# Layout

- This dir is a git repo with an allowlist `.gitignore` (scripts, docs,
  tests only), pushed to github.com/xl0/inventor-on-wine. `wine-src/` is its
  own repo: fixes on local `fix/NNN-*`, merged into `integ`, which is pushed to
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
- `prefixes/<name>/` — Wine prefixes. Create with
  `WINEDLLOVERRIDES="mscoree,mshtml=" build/wine wineboot -u`
  (otherwise Mono/Gecko download dialogs hang on a headless display).
- `tests/` — our small mingw-built test programs (run on Wine and the VM).
  `d3d11_present.c`: device + swapchain + clear/present + readback, exit 0 = ok.
  Build: `x86_64-w64-mingw32-gcc -O2 -o X.exe X.c -ld3d11 -ldxgi -luuid`.
- `x/` — headless display. `x/start.sh` runs Xorg :98 on the NVIDIA GPU at
  ca:00.0 (card4). ac:00.0 carries the host console / gdm, avoid it.
- `vm/` — Windows 11 Pro reference VM (qemu/KVM, not libvirt).
  `run.sh [install]`, `shot.sh [png]` (screendump via HMP `mon.sock`).
  SSH: `ssh -i vm/id_ed25519 -p 2222 dev@127.0.0.1` (PowerShell default shell).
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
  use unbuffered copies (`robocopy Z:\ C:\dst FILE /j`) for bulk data, then
  run things from C: (never from Z:, it isn't NTFS).
- `iso/` — Windows ISO, Inventor 2027.1 web installer.
- `inst/` — installer work: `webinstall/` (7z-extracted web installer), logs.
- `x/shot.sh [png]` screenshots :98; `x/vnc.sh` (re)starts x11vnc on
  127.0.0.1:5902 (-noxdamage) and turns off :98 screen blanking.
  Root GetImage there is packed 24 bpp despite a 32 bpp header (NVIDIA
  headless quirk); shot.sh handles it, generic xwd readers squash x by 0.75.
- `vm/input.py click X Y | key COMBO | type TEXT` — VM input over QMP.
- `deps/` — third-party downloads, pinned (hash in the fetch cmd or below).
  virtiofsd 1.14.0 static zip sha256 2e4fe957…8978e (GitLab upload, no
  upstream hash), virtio-win-0.1.302.iso 303f7ae4…67949d (only viofs/w11 used),
  winfsp-2.1.25156.msi 073a70e0…9f7a (matches GitHub digest).
- `deps/dxvk.sh PREFIX` — pinned + sha256-checked DXVK 3.1.1 and vkd3d-proton
  3.0.1, copied into the prefix with native DllOverrides. Tarballs cached in deps/.
- Ghidra 12.1.4 (`deps/ghidra_12.1.4_PUBLIC`, zip sha256 ddac49f9…d2d4db, from
  release notes) + Temurin JDK 21.0.12.1+1 (`deps/jdk-21.0.12.1+1`, tarball
  sha256 ce79869e…aee94, Adoptium). Only used by `tools/decomp.sh`.
- `tools/decomp.sh BIN funcs|decomp|xrefs|strings|imports [ARG]` — headless
  Ghidra queries via `tools/Decomp.java`. Project cached per binary sha256 in
  `deps/ghidra-cache/` (+ Ghidra's XDG config/cache), flock per binary; first
  use analyzes (-max-cpu 16). Clean-room guard refuses MS system paths and
  PEs whose version-info CompanyName or Authenticode O= says Microsoft
  (also refuses Wine builtins, whose CompanyName is Microsoft).
- Prefixes: `smoke` (plain), `dxvk` (smoke + DXVK/vkd3d-proton).

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
