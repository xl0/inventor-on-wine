# Local workstation (the user's laptop — NOT the server; skip this section on the server)

Ubuntu 24.04 x86_64, 16 logical CPUs, 58 GiB RAM. Local setup is Wine-only:
no reference VM. The server environment and populated prefixes described below
are not present here.

Inventor is running for user-requested interactive retesting on `b5d75449ff`,
with private GDB crash capture armed. The sketch-text check looked good, but
the user subsequently reported persistent main-window resize artifacts:
misplaced/stale image fragments and black bands. Inventor was not stopped by
the debugger; picom still uses GLX/VSync/`--no-use-damage`.
Confirmed Awesome Mod+mouse resizing: hovering over black areas redraws icons,
and another resize restores the full window after transient glitches.
This suggests missed repaint/invalidation, not a confirmed root cause.
Report and reviewed screenshot:
[174](issues/174-mod-resize-stale-regions.md).
This build includes the server fixes for
[124](issues/124-open-dialog-resize-coreclr-crash.md)'s breadcrumb-triggered
crash and [125](issues/125-format-text-preview-cjk-richedit.md)'s preview issues.
The original core's 126 mapped Wine binaries were copied and hash-verified in
`inst/local/debug-20261002-142809-crashcapture/matching-binaries-d7799da4d5/`
before rebuilding. Keep that archive and all crash data private.

**Licensing:** the laptop and server share one active-device seat. Coordinate
launches; previous device-limit errors do not authorize pausing another device.

- `wine-src/`: partial clone of `xl0/wine`, `integ` at `b5d75449ff`.
  Tracks `gh/integ`; `origin` points to WineHQ. No rebase onto newer upstream.
- `build/`: `../wine-src/configure --enable-archs=i386,x86_64` succeeds with
  GCC/MinGW 13. Only notice: legacy OSS audio unavailable (ALSA/Pulse work).
  Configuration logs: `configure.out`, `config.log`.
  Version: `wine-11.18-537-gb5d75449ff`. Latest rebuild took 1m48s
  with the resource limits below.
  Log/resource report: `build/rebuild-20261004-203437{.log,-resources.txt}`.
  Issue 084's shell and WinSupport.dll probes pass against the unchanged prefix.
  Current 32/64-bit console and Vulkan D3D11 present/readback smoke pass:
  `build/smoke-b5d75449ff.log`. Prior caption GUI, DComp interface and
  cross-process/cross-apartment COM checks passed on `d7799da4d5`.
  Both local prefixes were stopped before rebuilding.
- `prefixes/smoke`: isolated user folders, no Mono/Gecko; smoke runs disable
  them and winemenubuilder. Test wineserver stopped. Graphics probe success
  does not validate Inventor's viewport or dialog rendering.
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
  Inventor has reached sign-in and interactive sketch/document use.
  DPI is 144; a full prefix restart was needed after changing DPI to restore
  winecfg layout/input. Restart only when no documents are open.
  Setup logs: `inst/local/{installer,browser-setup}.log`.
  Corefonts is installed; Arial files/registration verified, Segoe UI absent.
  [083](issues/083-wpf-keytip-font-fallback.md) records the earlier .NET 10.0.9
  WPF key-tip FailFast and installation workaround. The server's fix is now
  in the build; explicit local key-tip verification remains pending.
  [046](issues/046-startup-crash-setwindowsubclass.md) has the distinct old-build
  subclass crash evidence.
  [084](issues/084-content-center-documents-shell-path.md) passes both local
  Documents-path probes without repairing the missing `ShellFolder` key;
  explicit UI warning verification remains pending.
  [085](issues/085-trial-popup-stays-white.md) contains the licensing-popup
  screenshot/log and cursor/GPU fixes now in the build. The captured "having
  trouble" run reported `ALLOW`, not an explicit device-limit denial.
  CJK host fonts include Noto Sans/Serif CJK and WenQuanYi; no HKLM
  FontSubstitutes `Tahoma` value. Explicit Noto Sans CJK SC works in Format Text;
  other fonts render the sketch but not its preview. Evidence is in
  [123](issues/123-cjk-fallback-via-fontconfig.md), follow-up work in 125.
  Setup verification uses `wine winecfg -v`, not `HKCU\Software\Wine\Version`
  (winecfg sets NT version keys and removes that override).
- Launches use `WINE_D3D_CONFIG=renderer=vulkan`, errors-only Wine logging
  (`-all,err+all,+timestamp,+pid`), and retained
  `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--disable-gpu`. The latter did not
  improve lag or eliminate NVIDIA contexts; it is not proven software rendering.
  `inst/local/current-debug-run` names the private directory containing
  `inventor.log`, `version.txt`, and `settings.txt`.
  Issue 124 documents the successful GDB second-chance exception capture and
  sparse ELF core. In the later CJK run Inventor exited 0, but GDB itself hit
  `linux-nat.c:1807: resume: Assertion signo == GDB_SIGNAL_0` during shutdown.
  That debugger failure is not another Inventor crash.
- Local Xvfb crashes during GLX initialization in NVIDIA EGL/GBM.
  `xvfb-run -a -s '-screen 0 1024x768x24 -nolisten tcp -extension GLX'`
  works for 2D smoke tests; verify with `xdpyinfo` before running Wine.
  The host desktop `DISPLAY=:0` is accessible.
- Desktop WM is Awesome on X11, now PRIME `nvidia`; GLX reports RTX 3080.
  Old `/etc/X11/xorg.conf` forced two separate X screens, and local GPU
  snippets conflicted with PRIME. User resolved this after backing out the
  overrides; `xdpyinfo` now reports one screen. User-reported desktop lag
  resolved after the NVIDIA-primary switch; earlier samples implicated
  Xorg/WebView2 GPU activity, not memory pressure or disk I/O.
  Picom v10 is running with `--config /dev/null --backend glx --vsync
  --no-use-damage`. User reports `--no-use-damage` eliminated viewport tearing.
  Earlier transient resize artifacts cleared on release. The later Open-dialog
  failure after Awesome Mod+mouse resize is recorded separately in issue 124.
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
- `deps/sysroot/` — local package prefix (git-ignored): Ubuntu archive packages the host lacks (-dev
  headers, mingw-w64, bison/flex, Xvfb, openbox, awesome, picom, xdotool, vulkan-tools, wayland-info,
  cabextract, ccache), unpacked without root. `tools/sysroot.sh add PKG...` resolves the not-installed
  closure (`apt-get -s`), downloads it pinned (.debs in `deps/sysroot-debs/`) and records
  `name=version sha256` in `tools/sysroot.pkgs`; `rebuild` recreates the prefix from that list;
  `eval "$(tools/sysroot.sh env)"` or `tools/sysroot.sh run CMD` sets PATH (appended), PKG_CONFIG_PATH,
  CPATH, LIBRARY_PATH, LD_LIBRARY_PATH, PYTHONPATH, BISON_PKGDATADIR, M4, LUA_PATH, XDG_*.
  regress.sh, prefix.sh, invscen/run.sh and x/start.sh source it themselves; a Wine build
  (configure and every later `make`) needs it in the shell. Rough edges: .pc files are rewritten to
  absolute prefix paths (not relocatable; `rebuild` after moving); dangling -dev symlinks are repointed
  to the system's runtime libs; mingw is the win32-thread variant (Debian's default alternative); only the
  mingw gcc/g++/cpp alternatives are recreated; CPATH also reaches mingw gcc (hasn't hurt Wine's
  build); ffmpeg 8 (winedmo.so NEEDED) and libodbc exist only in the prefix, so winedmo's unix side
  fails and ODBC is absent when the env is not set; `x/awesome-rc.lua` finds its themes via
  `$SYSROOT_ENV`; the Wayland session needs `x/dbus-session.conf` (AppArmor query fails in the sandbox).
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
  `dcomp_qi.c`: QIs and pointer identity of dcomp devices (v1, v3, v3 NULL) incl. Edge's undocumented
  {4ca97a18-...} (Win11: == IDCompositionDevice3, v3 only) and ID3D11VideoContext1 (085).
  `xproc_cursor.c [bitmap|arrow|none]`: child process's cursor on its child window in our toplevel;
  look at the screen cursor with `x/xcur.c` (XFixes) (085).
  `dxgi_comp_swapchain.c`: GetDC-painted composition swapchain, buffer order after Present1
  and dirty-rect Present1. `layered_child_gpu.c`: cross-process D3D child in a colour-keyed
  layered window. `syscommand_hidden.c`: SC_MAXIMIZE/MINIMIZE on hidden windows.
  `wofset.c`: WofSetFileDataLocation / FSCTL_SET_EXTERNAL_BACKING.
  `lowbox_token.c`: NtCreateLowBoxToken / CreateAppContainerToken token properties; `lowbox_args.c`: their argument
  validation + TokenAppContainerSid sizes (095).
  `hkcu_proto.c`: per-user URL protocol via HKCR / AssocQueryString. `hkcr_merge.c`: HKCR merged
  view probe (which side backs open/create/query/enum; needs admin).
  `tlb_cache.c`: typelib cache + PSDispatch/PSOAInterface proxy/stub probe and benchmark (032).
  `heap_stress.c [OPS_M LIVE MAXSIZE KEEP_PERMILLE private]`: random-size/lifetime HeapAlloc/Free stress,
  ns/op + commit per million ops, HeapCompatibilityInformation (073; fragmenting: `60 20000 262144 2`).
  `com_peruser.c`: COM vs per-user classes (CLSID/ProgID/Interface/OleRegGetUserType,
  RegOpenUserClassesRoot); run elevated and non-elevated, modes as argv[1] for fresh processes.
  `dosdev_name.c`: DOS device names in paths (035). `wintext.c` (`wintext.exe [TITLE]`):
  dump visible top-level windows + children (class, text, rect, pid, styles); "NOT RESPONDING" = thread not pumping.
  `dragdrop_revoke.c`: RegisterDragDrop/RevokeDragDrop across threads/apartments/processes (034).
  `droptargets.c`: list windows with OLE drop-target props, flag cross-process ones (read-only).
  `ctrlchar_text.c [FONT..|show]`: per-API table of how control chars are drawn/measured
  (GDI, DrawText, GCP, Uniscribe); `show`: "Pan\r" in ExtTextOut/DrawText/combo/listbox (039).
  `cjk_link.c [HEIGHT]`: GDI font linking of CJK chars per font (glyph index, outline size, advance;
  119); first a diagnostics block: registry SystemLink/FontSubstitutes/Replacements with installed checks,
  East Asian families, which installed font Tahoma's linked glyphs come from (123). `cjk_edit.c [SECS]`: CJK text in Edit controls + DrawText/ExtTextOut per font, screenshot it (119).
  `dtp_short.c`: short-date DateTimePicker layout (044, screenshot it).
  `xproc_hidden_present.c`: other process presents D3D11 on its child of our container; we hide the
  container, screen pixel must stop showing its frames (065; Win11 pass, exit 0 = ok).
  `expose_present.c [nopump|show]`: D3D11 child presents, first frame must show, another window
  covers/uncovers it, screen pixel must keep the frame (061, 078; exit 0 = ok).
  `present_lag.c [N] [SYNCINTERVAL]`: N presents on a D3D11 child, the last must be on screen 500 ms
  later (078: offscreen copy raced NVIDIA's present; exit 0 = ok). `layered_alpha.c`: ULW_ALPHA popup with alpha bands
  0..255: screen colour, WindowFromPoint, SendInput click per band; `kinds`: LWA_ALPHA / colour key / opaque ULW (062).
  `layered_splitter.c [SECS|auto]`: WPF-free stand-in for Inventor's splitter popup (alpha 3-6 bar dragged to
  move a pane border) + a tooltip with a shadow; `layered_splitter.sh drag|move|cycle` drives it with xdotool on Wine,
  `auto` (SendInput) on the VM (062).
  `layered_popup_probe.c
  [TITLE] [drag X|mdrag X Y DX|hit|alpha|max]`: another app's layered popups: rects, owner, screen
  grab, composited alpha (over own white/black windows), window drag with SendInput (062).
  `sc_move_hittest.c`: SC_MOVE|n from WM_LBUTTONDOWN + SendInput drag, does it move (076).
  `sizemove_log.c` + `sizemove_scen.sh`: window logging ENTER/EXITSIZEMOVE, MOVING, POSCHANGED
  (top 40 client px = caption); the script drives WM moves/resizes with xdotool (077; PROBE/TITLE env
  = another probe). `filedlg_sizemove.c`: modal GetOpenFileName dialog logging the same + WM_SIZE (124).
  `custom_caption.c` (`[max|plain|plainmax] [secs]`): client over the caption via
  WM_NCCALCSIZE, own blue caption strip; prints rects (040, screenshot it).
  `ebrowser_events.c`: ExplorerBrowser host like Inventor's file dialogs: SIGDN names of the folder chain,
  ICommDlgBrowser / IExplorerBrowserEvents / DShellFolderViewEvents log, clicks the first file (041, 043).
  `subclass_probe.c`: comctl32 v5/v6 SetWindowSubclass props, nested removal, destroy in a
  callback, cross-thread/process calls (046).
  `d3d11_deferred_race.c [N] [warp]`: CreateDeferredContext while another thread loops
  immediate ClearState; counts failures (Windows 0, unfixed Wine ~14%) (047).
  `syslink_attr.c`: SysLink `<a>` parsing with extra attributes (049).
  `packager_ole.c`: OleLoad of a Package embedding + QI set (053).
  `delete_open_dir.c`: delete a tree while handles are open (POSIX delete semantics, 054).
  `delete_posix.c [DIR\\]`: DeleteFile/RemoveDirectory/disposition classes with other handles open,
  what those handles see, mappings, locks (054; `-lntdll`).
  `crt_math.c`: ucrtbase math results bit-for-bit (writes crt_math.bin to compare, 055).
  `stub_exception.c [MODE]`: exception raised in a COM server method: cross-apartment IDispatch/IPersist
  calls, the stub called directly, a custom stub; per case in a child (056).
  `cxx_catch_throw.c` + `cxx_catch_throw_eh.cpp` (clang MSVC-target C++ EH, build line in the file):
  throw/rethrow from a catch block under C++ catch, __except, cross-apartment COM (058).
  `d3d11_discard_perf.c [MiB] [iters] [deferred] [touch=B] [latency=N]`: Map(WRITE_DISCARD) of a
  big dynamic buffer per iteration, ms/iter (060; vk vs gl vs DXVK: app-local DXVK dlls + d3d11,dxgi=n).
  `cs_spin_bench.c FPS DRAWS SECS [sync|-] [GAP_US]`: D3D11 frame loop for wined3d CS thread costs: fps,
  process/main-thread CPU, sync-readback latency; GAP_US = busy work before each draw (108).
  `owner_blocked.c`: popup owned by a window of a process that doesn't pump, resized/moved (Win11 0 ms; 085b).
  `bigmap_perf.c [DLL [hold]]`: map a big unaligned-image DLL (msedge.dll) twice, 1 TB placeholder/plain
  reservations + split/free, ms per step; `hold` keeps it mapped in another process (085b, 117).
  `mojo_pingpong.c [N] [SIZE] [skip]`: two processes ping-pong over an overlapped pipe + IOCP like Chromium's Mojo
  (rt/s, us/rt, CPU/rt); `iocp_deferred.c`: when a port-bound pipe read's IOSB/buffer are written (dequeue) (107).
  `junction_mklink.c DIR`: junction via FSCTL_SET_REPARSE_POINT like AdskLicensingInstHelper,
  then paths/CreateProcess through it (052).
  `hwnd_signext.c [N]`: churn child windows, compare every HWND the app sees for one window (059).
  `user_handle_uniq.c [N]`: per USER type (window, menu, icon, accel, hook, HDWP, HIMC) max HIWORD,
  wrap point, bit-31 handles (059; `-limm32`).
  `com_rundown.c run kill|exit|uninit|release [clients] [objs] [secs]`: STA server, client processes
  take object refs and end without Release; prints the server's live objects over time (072).
  `recentdocs_leak.c`: handle count around 100 SHAddToRecentDocs per flag (067; `-lntdll`).
  `addprinter.c NAME PPD`: local wineps printer on FILE: + default (print tests; wine-src's
  dlls/winspool.drv/generic.ppd works). `actctx_tmodel.c`: comClass threadingModel spellings
  → actctx model (063). `actctx_comcall/` (probe.dll with a resource-2 manifest + exe; build lines in
  actctx_comcall.c): active context / manifest CLSID lookup in direct and cross-apartment/-process
  COM calls; `mfc_state.c`: AFX_MANAGE_STATE of Inventor's FEA DLL (070). `account_domain_sid.c`: GetWindowsAccountDomainSid/EqualDomainSid
  per SID (064).
  `getwindow_perf.c [N] [ITERS]`: ns per GetWindow/GetParent/GetProp/... over a tree of N children (057, 081).
  `xproc_geometry.c [ITERS]`: helper process owns a child of our toplevel; its GetAncestor/MapWindowPoints/
  GetWindowRect/GetDCEx region must follow our moves/resizes/hides at once, then ns/call (091; Win11 pass).
  `wait_granularity.c [phase|loops|periods|nt|running|periodic|pool|mmtimer]`: timer resolution semantics of
  timed waits/timers, timeBeginPeriod/NtSetTimerResolution (089; Win11 ground truth in the issue).
  `abs_timeout.c [rel|bench|clock]`: absolute wait/timer deadlines (system vs precise base, past,
  early-return counts), relative condvar waits, GetSystemTime(Precise)AsFileTime cost, wall-clock steps (093).
  `hook_chain.c [ITERS]`: hooks added/removed while a chain runs (same/other thread) + cost of a
  3-hook CallMsgFilter/PeekMessage (081). `qi_remote.c`: which QIs on a cross-process proxy reach the
  object, IRpcOptions Query/Set, cost of a remote QI (097). `mapview_perf.c`: map/unmap view,
  mutex wait+release, SetEvent, VirtualProtect costs (097). `freelib_perf.c`: LoadLibrary/FreeLibrary of a loaded DLL
  with 300 extra modules (081). `com_cancel.c [sta|mta|psta|pmta]`: COM call cancelled by the message
  filter (slow server in another apartment/process): return time, late reply, CoTestCancel (099).
  `r109/disc_probe.c MODE`: object disconnects itself in a call (newcall double rel relmd remarshal);
  `r109/stress.c run SECS [SAFE]` (or R109_SAFE=0..4): disconnect/release/call stress, AVTRACE on server
  crashes (109, 110); 3 = no disconnects, live objects at exit; 4 = also release every objref, live
  objects after all releases (112). `r112/uninit_pending.c [N [release]]`: STA uninitializes with N calls queued (112);
  `r112/uninit_race.c [N]` (RACE_NOCALL, RACE_VERBOSE): in-process STA uninit racing a call (112, 114).
  `r114/sta_endpoints.c [N]`: N short-lived STAs marshal + get a call; process handles by type (114).
  `r116/listen_restart.c [auto|slow|autoslow]`: listen / AUTOLISTEN register right after
  RpcMgmtStopServerListening (held connection, running call), 5 s timeout per step (116).
  `r113/io_threads.c [N] [iocp|event|none|done|imm|immwait]`: short-lived threads issuing one pipe read,
  address space per N/10; `r113/thread_churn.c [CREATORS] [PER] [io]`: threads created without waiting,
  peak thread count + CreateThread failures; `r113/thread_vm.c [N]`: per-thread address space and
  create/run costs (113).
  `appcontainer_sid.c`: userenv DeriveAppContainerSidFromAppContainerName vs SHA-256 formula;
  `appcontainer_register.c`: kernelbase AppContainerRegisterSid/Unregister/LookupMoniker + HKCU Mappings key;
  `ac_profile_dir.c`: Chromium's AppContainer profile dir security steps (090, 094).
  `seccaps_probe.c [run EXE]`: CreateProcess with SECURITY_CAPABILITIES: attribute sizes, child token/env/access
  (`run EXE`: start EXE `process seccaps` in the container, for kernel32_test) (095).
  `setsecinfo_access.c`: access rights each *_SECURITY_INFORMATION flag needs (NtSetSecurityObject per handle
  right; SetNamedSecurityInfo via OWNER RIGHTS-only DACLs; label readback) for files/dirs/keys/events (094).
  `mydocs_path.c`: CSIDL_PERSONAL PIDL, SHGetPathFromIDList, desktop GetDisplayNameOf per SHGDN,
  SHGetNameFromIDList; `inv_mydocs.c [BINDIR]`: Inventor's own WinSupport OSxFolder::GetMyDocumentsDir (084).
  `mapcycle_perf.c [N] [THREADS] [VIEWS]`: CreateFileMapping/MapViewOfFile/Unmap cycle cost (098).
  `wic_enum.c`: WIC metadata-reader enumeration cost + run-time registration visibility (098).
  `desktop_owner_thread.c`: thread exits in the desktop's explorer must not close the desktop (Wine only, 101).
  `clipcursor_warp.c`: ClipCursor-moved cursor still reported after >100 ms (Win11: yes, 101).
  `fake_mousemove.c`: which window changes post WM_MOUSEMOVE to the window under a still cursor (120).
  `hover_tooltip.c [move] [hook] [poke] [slide=MS]`: Edge app window with an HTML title opened under a
  parked cursor, logs Edge's windows, exit 1 = tooltip shown; `hook` + `hover_tooltip_hook/` (global
  WH_GETMESSAGE hook DLL, `hookhost.exe SECS` for any app) log Edge/WebView2 mouse messages (120).
  `r120/fmm2.c MODE` (cases loops lat perf menu combo): fake-move rules, latency/coalescing, hover-toggle
  loops, menu/combo selection under a still cursor; `r120/mip.c [mip]`: extra info / WM_POINTERUPDATE of
  fake and SetCursorPos moves (120, 122; VM vs Wine outputs in inst/120/).
  `dbg_wow64_order.c [CMDLINE|-inherit|-ntinherit]`: debug events of a DEBUG_PROCESS child in order
  (default syswow64 msinfo32; pid, module names); `-inherit`: debuggee clears ProcessDebugFlags, is its
  child debugged; `-ntinherit`: NtCreateUserProcess with/without NO_DEBUG_INHERIT (105, 106).
  `displaychange_sync.c [W H|list]`: ChangeDisplaySettingsEx delivery of WM_DISPLAYCHANGE to own/other
  thread/process windows and mode visibility after return (104; the VM has only 1024x768).
  `r124/xstate_ctx.c [cfg|init|loc|mat|apc|exc]`: extended context (CONTEXT_EX/xstate) seen by vectored
  handlers and special user APCs, xstate config, RtlLocateExtendedFeature argument validation;
  `r124/nre_barrier.cs` / `gc_suspend.cs`: .NET 10 tests (build on the VM with the 4.8 csc, run with
  `dotnet X.exe` + X.runtimeconfig.json): NRE in the JIT write barrier, GC suspension of a spinning thread (124, 126).
  `r083/wpf_fallback.cs [png=OUT] [FAMILY..]`: WPF (4.8) text measuring/rendering in missing font families;
  exit 35 = WPF's FailFast for a system without Arial (083).
  `r125/re_probe.c [tom|bind|wrap|sel|eop]`: rich edit probes for RICHEDIT50W and RichEdit20W: ITextFont on a
  range (units, Reset modes), CJK font binding per insertion method and EM_SETLANGOPTIONS (+ BMPs),
  EM_SETTARGETDEVICE line breaks, selections over the final paragraph mark, insertion-point formats; `r125/clip.c [TEXT]`:
  CF_UNICODETEXT on the clipboard; `r125/ctl.c TITLE [ID STRING]`: list / select in another process's
  combo boxes (125; VM outputs in inst/125/).
  `r141/scf_word.c [word|sel|layout|caret|eop]`: rich edit EM_SETCHARFORMAT(SCF_WORD): characters formatted per caret
  position / selection, modify + undo, layout and caret right after, format typed after deleting everything (141, 146).
  `r141/rescript.c [class:N] CMD...`: rich edit message scripts (text, sel, cf, undo, map, sweep, tomtext, wbproc...)
  for Wine and the VM; `r141/fuzz.c SEED STEPS CLASS STYLEHEX [-x acts] [-k keepfile]`: random rich edit messages with
  a layout-invariant check after each (MEPF_REWRAP assertion; 141 review, drafts 159-161).
  `r130/xisel.py DISPLAY [N]`: us per XISelectEvents on the root (grows with the windows that have XI2 selections);
  `r130/xiwins.py DISPLAY N hold`: N stand-in XI2 windows (aged-session repro); `r130/xres.py DISPLAY [MIN]`: X resources
  per client pid (X-Resource) (130). `r131/winlist.c [IMAGE] [raw]`: all windows of the session per process/thread/class/
  parent, hidden too; `r132/xp.c`: cross-process present probe (`xp.exe host [busy=N flip=N pw= ph=]`, then
  `foreign|child|hidden|visible [HWND] interval=N cycle=N follow=1 hold=N ...`: quadrant image + moving bar, ms per Present, STALL
  watchdog, handle counts; 132, 163, 166); `r132/evil.c`: hostile source for winewayland's cross-process surfaces (bad handles, sizes,
  rects, floods; the owner must survive; 132); `r132/{run2,geo,clip,leak}.sh` + `pix.py`: two source processes / geometry and lifetime
  steps / owner-side clipping with idle sources / 200-cycle leak check on the host Wayland session, judged by colour boxes in screenshots;
  `r132/backpressure.sh`, `burst.sh` (+ `burst.c`): requests queued while the compositor is SIGSTOPped; `r132/xwin.c`: what Windows
  shows when process B draws / presents on a window of process A (GDI, blt and flip swapchains; table in 132); `r131/comp_windows.c`: thread windows around CreateSwapChainForComposition (Win11 none; 131);
  `r131/comp_probe.c`: what a composition swapchain (D3D11, D3D12) answers to the window-related calls and invalid
  descs; `r131/comp_threads.c`: process windows when it is created/released/outlived across threads (131).
  `r165/deadwin.c [PRESENTS] [keep|detach]` + `detach.c` (DLL): D3D11 swapchain presented after its window is destroyed,
  a buffer released at process detach (150, 165: Vulkan renderer exit code 3 + wined3d_not_from_cs() assertion; `detach`: 169).
  `r171/ulwrace.c` (`race|destroy|exit [N] [FLUSHERS] [hold]`, `bench`, `basic`): UpdateLayeredWindow on another thread's
  window while its owner resizes / destroys it (win32u surface list race; FAULT = a call returned an NTSTATUS), cost of the
  surface-changing calls, Windows semantics of cross-thread layered calls; `r171/run.sh BUILD TAG RUNS TIMEOUT EXE ARGS`:
  any probe N times on an own Xvfb with a watchdog + gdb backtraces; `r171/leak.sh`, `surfaces.py`: window surfaces left
  in win32u's list (gdb) (171).
  `r133/idle.c [-v] [-x EXE] [NAME..]`: WaitForInputIdle scenarios, child threads scripted per scenario (which
  thread / which wait makes a process input idle, later calls, console children; `wine_*` = Wine-only stand-in for a
  driver's clipboard manager thread); the child is a GUI/console-patched copy of the exe (133, 143).
  `r140/idm.c [N [M]|list]`: table-driven IsDialogMessage / GetNextDlgTabItem / GetNextDlgGroupItem probe (window
  trees x key messages, message log per case, child process + 3 s watchdog so a hang is a result);
  `r140/brief.py` (one line per case), `fold.py`, `cmp2.py VM WINE [-v LAYER]` (layered diff); `r140/wtree.c
  IMAGE|0xHWND|text:T`: focus chain / subtree with styles of another process, sends no messages (140, 148).
  `r140/self.c`: IsDialogMessage with msg.hwnd = the dialog itself (plain, real, child, DS_CONTROL dialogs).
  `isdialogmsg_hidden.c`: 140's first single-case probe.
  `loader_dllmain/` (`loader_dllmain.exe MODE|all`, DLL build lines in dm.c): which loader calls
  of thread B wait while thread A sits in a DllMain (048, 092); `stress.c`: threads load/free/look up
  and call DLLs (refcount/lookup races show as crashes or modules left loaded) (092).
- `x/wayland.sh start|stop|env`, `x/wshot.sh` (screenshot + input), `x/winj.py`: headless mutter Wayland
  session for winewayland.drv tests; build in `wt/wayland-build` (build/ lacks the driver). See notes/wine/wayland.md.
  Inventor pass on Wayland: issues 132-136; probes `tests/wl_{winlist,wintree,winctl,xswap,xowner,childswap,idle}.c` (window
  listing/control, cross-process swapchain, owner z-order, WaitForInputIdle). `tests/wl_xowner.sh` (WINE_BUILD=DIR): all
  wl_xowner cases (modal, chain, late owner, hide/destroy, re-owner + loop, cross-process) with screenshot pixel checks (134, 135).
- `x/` — headless displays. `x/prefixes.tsv` is the single source of truth: per prefix
  display, Xorg PCI bus, DRI_PRIME, VNC port, build dir, role (inv :98, inv2 :99, inv3 :100,
  inv4 :101). `tools/prefix.sh status|env|start|stop|kill-inventor|lease|release`
  (kill-inventor: Inventor.exe + helpers by image name, since Wine processes all have Linux ppid 1;
  spares services) works from it (status: wineserver/build/procs/port holder/lease; servers are mapped to
  prefixes by the holder of the server dir's lock file in /proc/locks). After Claude Code is
  relaunched, processes from the old sandbox are in another user namespace: signals and sockets work,
  but /proc/PID/{cwd,exe,environ,fd} don't, so status shows them as "other sandbox" and gdb/strace
  can't attach — restart the prefixes to manage them again. `x/leases` (git-ignored, flock'd) records who
  uses a prefix; start/stop refuse on someone else's lease. `INV=inv3 tools/invscen/run.sh S`
  uses the table. `x/start.sh N BUS` = headless NVIDIA Xorg (needs DRI_PRIME=pci-0000_<bus>_00_0
  so Vulkan picks the GPU that owns the screen), `x/vnc.sh N`, `x/shot.sh out.png N`.
  ac:00.0 also carries the host console/gdm greeter (headless Xorg there works, :101);
  prefer the other GPUs. The user's WM (awesome 4.3, no compositor): kill that display's
  openbox by PID, `DISPLAY=:N awesome -c x/awesome-rc.lua`; `awesome-client`
  (DBus) can script it (e.g. `c.maximized = false`). awesome lacks _NET_WM_MOVERESIZE (061, 076).
  Licensing (152, observed 2026-10-03): every prefix is served by its OWN AdskLicensingService. The service
  listens on the address saved in `C:\ProgramData\Autodesk\AdskLicensingService\AdskLicensingService.data`
  (`{"Addr":"127.0.0.1:PORT"}`; port busy at start -> new port, file rewritten), and the SDK in Inventor
  (AdskLicensingSDK_10.dll) reads that file and connects by WebSocket; if that fails it runs
  `AdskLicensingInstHelper servicectl start`. Ports now: inv 45691, inv2 37683, inv3 46231, inv4 46809.
  39683 was the port of the original install; copies shared it only until each rewrote its file (09-28/29).
  The separate licensing prefix inv-lic (port 39683) was never needed and is gone (2026-10-04).
  So a `wineserver -k` of one
  prefix only takes down that prefix's Inventor (follows from the connections; not tested by killing).
  Per prefix there is also AdskIdentityManager (two loopback ports; sign-in state) used by Inventor and the agents.
  Device = what the AdskLicensingAgent started by the client computes (monitor.dll): SHA-256 of Wine's disk
  serial (Win32_DiskDrive; fallbacks down to the system volume serial), of the system UUID
  (Win32_ComputerSystemProduct = Unix machine-id) and of the user name. Nothing from the prefix, hostname
  or MACs: all prefixes of one host and user are one device, a VM or another host is another one.
  Licence = ONE active device (server "hong-Precision-7960-Tower", the VM and the user's laptop
  "mafa" each count, and so would Inventor in the Linux VM `vmwl/`: don't start it there (152);
  a running session keeps its seat until its next check, so two can look concurrent). "Device limit reached": another device holds the seat. To use the VM's Inventor,
  stop all server Inventors (kill-inventor); the licensing services alone don't hold the seat
  (verified 2026-10-02), then start Inventor in the VM. Never
  click Pause product (account action — the user's call).
  Copy prefixes with `cp -a` / `rsync -aX`: junctions live in the
  user.WINEREPARSE xattr of `name?` dirs (052); plain copies break them.
- `vm/` — Windows 11 Pro reference VM (qemu/KVM, not libvirt).
  26.04 host: `vm/run.sh` starts swtpm from a copy (`deps/swtpm`, made from /usr/bin/swtpm if missing): the system binary's
  AppArmor profile only talks to libvirt-labelled peers (qemu: "tpm-emulator: Failed to send CMD_SET_DATAFD"), and a confined
  swtpm cannot be signalled from the sandbox (a stray one stays until the user kills it).
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
  NAME /data/users/xl0/wine/win.qcow2`, copy vars.fd + tpm back.
- virtio-fs share: host `vm/share/` = guest `Z:`. Host side: Rust virtiofsd
  (unprivileged, `--sandbox none`; `VFS_LOG=debug vm/run.sh` for tracing),
  guest RAM is a shared memfd. Guest side: WinFsp + viofs driver + VirtioFsSvc.
  Cached reads of big files fail ~10% ("Error performing inpage operation");
  use unbuffered copies (`robocopy Z:\ C:\dst FILE /j`, from the desktop
  session via winrun — Z: isn't visible to SSH/session 0; over SSH plain `scp`
  is fast: 3.4 GB in ~22 s) for bulk data, then
  run things from C: (never from Z:, it isn't NTFS).
- `/data/users/xl0/wine/` (XFS on RAID0, no redundancy; `WINE_DATA` overrides): the big images only —
  `win.qcow2` (Windows VM), `vmwl-disk.qcow2` (Linux VM), `iso/` (Windows ISO, Inventor 2027.1 web
  installer). Everything else, incl. prefixes and builds, stays in the project dir (ZFS, compressed ~2x).
- `inst/` — installer work: `webinstall/` (7z-extracted web installer), logs.
- :98 runs openbox (started by x/start.sh); without a WM Wine never unmaps
  hidden windows (issue 029).
- `x/shot.sh [png [N]]` screenshots :98 (or :N) with a plain root XGetImage;
  `x/vnc.sh` (re)starts x11vnc on 127.0.0.1:5902 (-noxdamage) and turns off
  :98 screen blanking. Never `xwd -root`: Wine's per-process X colormaps make
  xwd take its multi-colormap path (packed 24 bpp) and draw Wine dialogs black (036).
  `xwd -id WIN` of a single window is fine.
- `vm/input.py click X Y | key COMBO | type TEXT` — VM input over QMP.
- `vmwl/` — Linux Wayland test VM (Ubuntu 26.04; GNOME 50 / KDE Plasma 6 / sway; qemu, SSH :2223, VNC :5911,
  scripts reuse `vm/shot.sh` / `vm/input.py` via `VM_DIR`). Runs host builds (`build/` by default, more with `VMWL_BIND`) from
  read-only virtio-fs shares, one virtiofsd per path. See `vmwl/README.md`; protocol tables `vmwl/globals-*.txt`.
  `tests/r157/`: winewayland lock-order tools (157): `lockstress.c` (multi-thread window state stress), `batch.sh` /
  `g-run.sh` / `g-utest.sh` (stress matrix and conformance units in the guest, watchdog + gdb backtraces),
  `lockorder-debug.patch` + `lockorder.py` (debug build that reports win32u locks taken under driver mutexes).
  `r132.sh` (132's probes on any build, host session), `sinkstress.sh` (lockstress with 132's sources in its windows).
  `jitter.py host|vm SECS [X Y [RATE]]`: relative pointer motion + key presses during a stress (winj.py / QMP).
  `tests/r173/`: the same for winex11 (173): `iconlock.c` (user lock vs window data deadlock), `visual_race.c` (X window
  recreated by another thread: BadWindow), `flushpost.c` (forces the surface flush's posted retry), `lockorder-debug.patch` +
  `lockorder.py` + `cycles.py` (all win32u + winex11 mutex pairs with call chains, cycle check, X errors with backtraces),
  `run.sh` / `batch.sh` / `ls.sh` / `x.sh` / `sm.sh` (watchdog runner, batches without a WM and with openbox, lockstress loop,
  own Xvfb displays, 077's size-move table without the exported WINEDLLOVERRIDES; they live in inst/173/).
  `r182/xallocid.c` (plain Xlib: threads creating GCs on one Display, libX11 `_XAllocID` assertion), `r183/settext_race.c`
  (two threads setting one window's text), `r184/vstate.c` (X window recreated from another thread in each window state,
  `loop N` counts lost positions; 184).
  `tests/r175/`: owned windows vs the window manager (175): `owned.c` (owner + owned layered popup / dialogs / tool
  window / unmanaged popups, cross-process dialog; commands from a file; `auto` = Windows semantics around minimize),
  `scen.sh` (owner moved between desktops, who follows, what stays on screen; `pix.py`), `matrix.sh` (4 WM configs,
  driver .so A/B), `minloop.sh`, `barpix.sh` (WM border around the invisible popup), `wm.sh` / `ac.sh` / `movetag.sh`
  (swap the WM of a display, awesome-client), `xinfo.sh` (X properties per window), `xtransient.c` / `xopacity.c`
  (plain X clients: WM policy for transients and frame opacity).
  `tests/r174/`: what stays stale after a WM resize (174, 181): `frame.c` (frame with ribbon / browser / status children +
  D3D11 view; options: layout deferred to WM_EXITSIZEMOVE, slow / pumping layout, children painted by another thread, DPI
  aware), `wpf.cs` + `build-wpf.sh` (the same in WPF 4.8: one window or a WinForms frame hosting child HwndSources; `sw`,
  `partial` = dirty-rectangle present after each resize), `drive.sh` (N Mod / Alt + right-button drags of a corner or
  `xdotool windowsize` steps on any display, screenshot after each), `check.py` (screen vs. the layout for the X window's
  size, per child), `wstate.c` (another process' top-level: rects, region, children with pending update rects; `redraw`).
  Never export `WINEDLLOVERRIDES="mscoree,..."` to a .NET app (wineboot only): Inventor then dies at
  `CommonUI.dll+0x60b90` 6 s after start (152).
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
- `tools/gdb/winesyms.py`, `tools/gdb/sehbt.py`: gdb on a hung Wine process (winedbg can't attach
  under the loader lock): symbols despite the preloader, .pdata backtraces per syscall frame (048).
  `tools/gdb/bpbt.py`: `bpbt NtFoo N SKIP` = PE callers of an Nt* call on a live process (098).
- `tools/mdmp.py DUMP [-m] [-n N]`: minidump (Autodesk CER `Temp\Inventor<ts>.dmp`) -> exception, registers,
  module+RVA stack scan of the faulting thread (152).
- `tools/pdbpub.py FILE.pdb RVA...`: nearest public symbol per RVA from a Microsoft symbol-server PDB (164).
- `tools/wineserver-reqstats.patch` (debug only): SIGHUP to wineserver dumps request counts and
  handler time per client thread (+ view counts) to /tmp/wineserver-reqstats.txt (098).
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
  At exit (also on timeout/abort) it drops App + scenario statics and GCs so the RCWs release
  Inventor's objects: Wine has no DCOM rundown for dead clients (072; was ~1350 stubs/suite).
  Artifacts: inst/invscen/SCENARIO/ (prefix P other than prefixes/inv: everything under inst/invscen/P/:
  SCENARIO/, bin/scen-SCENARIO.exe (prefixed so `cmd` can't shadow system cmd.exe), results/, inventor.log); `--vm`: C:\t\scen\SCENARIO copied to
  inst/invscen/ref/ (the VM reference). `all` runs the suite (hello tlb part asm
  drawing feat params sheetmetal asmcon asmbig drawing2 script export; asm/drawing
  use part/box.ipt, the rest are self-contained), logs to results/S.txt of that root
  (`--vm all`: ref/S.txt), prints a PASS/FAIL table and steps >3x slower than
  ref/S.txt; when a crashed Inventor sits in winedbg (034) it kills that prefix's
  Inventor/winedbg/CER dialog by PID (never wineserver -k, see licensing below).
  The harness aborts a step at once on Inventor's "Licensing error" dialog or when
  Inventor is gone (RPC unavailable); timeouts list Inventor's visible dialogs.
  It WM_CLOSEs the licensing agent's trial welcome popup once visible 15 s (connect + step polls;
  logs "dismissed trial welcome"; clicking its X broke the next ActiveView; closing during WebView2
  init spins the agent, 088), and connect waits until it is gone: a first document during Inventor's
  startup can deadlock it (048, app race).
  The watcher ignores FwUI's 2x2 hidden modal dialog and Chromium tooltips (120).
  Expectations are analytic (volumes, centroids, flat-pattern lengths, view extents)
  or structural counts taken from the VM once (STEP/IGES/SAT entities, STL
  triangles, SaveAsBitmap light-pixel share). Never pipe run.sh into another
  command: wine children inherit the pipe and the reader never sees EOF.
  Dialog watcher (Harness.cs): a thread polls visible top-level windows of Inventor.exe and
  its child processes; anything not in `Benign` (class list; untitled `HwndWrapper[` hosts too)
  that stays >= 1 s is printed as `DIALOG [step] 'title' class WxH text [...] shot PATH`,
  cropped-screenshotted (`dshot.sh` via INVSCEN_SHOT, `Out\dialog-N.png`; WPF box texts are
  unreadable, the shot is the evidence) and dismissed (WM_CLOSE, Esc, first button).
  `INVSCEN_DIALOGS=fail` (default) fails the running step and RESULT; `log` only reports; `off`.
  `INVSCEN_UI=1`: SilentOperation off, so Inventor shows its real prompts (084: a silent
  open never showed the "locations unavailable" warning). Scenario `uiopen` (INVSCEN_OPEN,
  INVSCEN_PROBE=<command> to provoke a dialog, INVSCEN_PROJECT=0) opens a sample part + assembly
  that way. Silent samples/part/asm: 0 dialogs in ~470 steps. Under UI mode samples legitimately
  prompt: Resolve Link (Engine MKII, Buffer Prep Skid; the VM has the same missing refs),
  "out of date" update prompts, "Moldflow server not available" (Mold Design); so use `log`.
  Other setup: `INV_PREFIX=prefixes/inv2 DISPLAY=:99 DRI_PRIME=pci-0000_34_00_0`
  (artifacts in inst/invscen/inv2/). `view`: reopens part/box.ipt visible, SaveAsBitmap,
  leaves it open for a viewport screenshot (037). csc wants backslash paths; `using Inventor` clashes with System names
  (File, Environment, Attribute): qualify them. Embedded interop types don't
  inherit (PartDocument is not a Document): cast at runtime (`(Document)obj`).
  `samples` / `samples2016` (symlink): Autodesk's official sample sets
  (inst/samples/; pristine copies in C:\t\samples\{2022,2016}.orig in the prefix and on
  the VM, mirrored to the work dir each run since saves migrate dependents in place; open, mass,
  BOM, rebuild, save-as copy to C:\t\scen\S, reopen per top-level doc; diff vs the VM).
  autodesk_inventor_2022_samples.zip sha256 1eeb4164…09292b,
  autodesk_inventor_2016_samples.zip 9d3096f6…41f501c, both from damassets.autodesk.net
  (links on the "Inventor Sample Files" support article; www.autodesk.com zips 403 curl).
  Specialised environments (not in `all`): `publish` (Anark 3D PDF via the add-in's
  Automation.Publish, late-bound with InvokeMember since C# `dynamic` fails on Windows too;
  DrawingPrintManager.PrintToFile on the default printer — needs one: inv3 has
  "Wine PostScript File" on FILE: from tests/addprinter.exe), `content` (CC tree, 064),
  `beam` (steel cantilever for the Stress Analysis UI, 063), `frame` (skeleton + frame.iam
  for Frame Generator / Design Accelerator UI; `INVSCEN_FRAME=check` lists generated parts).
  Environment probes (118): `paths` (non-ASCII/special/long dirs, `INVSCEN_CASES`), `locale` (run with LOCPATH/LC_ALL set;
  `localedef -i de_DE -f UTF-8 DIR/de_DE.UTF-8`, no install), `cjk` (notes in several fonts, `INVSCEN_FONTS` ;-list), `docs` (lists open documents).
  `place`: asmbig's "place 200 occurrences" alone, `INVSCEN_N` rounds, per call kind split;
  `INVSCEN_PLACE=hidden|cheap`, `INVSCEN_SYNC` like openbench (097).
  `docbench`: `INVSCEN_N` rounds of visible Documents.Add + Close per type of `INVSCEN_TYPES`
  (part asm drw); round 1 after an Inventor start = one-time costs; `INVSCEN_SYNC` (098).
  `openbench`: activates samples.ipj, opens/closes `INVSCEN_OPEN` (;-list) `INVSCEN_N` times and times
  a file-reference walk (per-COM-call cost, 057). `INVSCEN_SYNC=C:\dir`: handshake files around each
  walk (walk.start -> wait walk.go, walk.end -> wait walk.done) to attach strace to one walk (081).
  `dim141`: drawing + base view of a sample part + one API-made dimension, left open; `INVSCEN_CMD=
  DrawingDimensionToleranceCtxCmd` opens Edit Dimension on it (use `INVSCEN_DIALOGS=off INVSCEN_UI=1`; 141).
  `r140`: new part + box + iLogic Browser pane, left open (140's repro setup).
  UI helpers: `INVSCEN_KEEP=1` keeps open docs at connect; `cmd` runs a command by internal
  name (`INVSCEN_CMD`, `list:PATTERN`; `INVSCEN_OPEN` opens a doc first), `tx` prints
  transactions + occurrences, `addins` lists add-ins. Inventor's own add-ins run on .NET 10
  in-proc (their exceptions: EventPipe trick in notes/wine/debugging.md).
- `tools/pixgrab.py WINID OUT.png`: a composite-redirected X window's pixmap (what a GPU client last
  presented into an offscreen client surface; 078).
- `tools/uilat/uilat.py SCEN... [--setup] [--tag T] [--record] [--perf ROLES]` — UI latency:
  XTest input, XDamage on root + XGetImage of a watched rect (window moves make no damage:
  polled). Step latency (isolated moves) and drag fps/lag (tracked rubber-band end / window
  shift). Scenarios rubber hover orbit pan (need `--setup` = invscen `uilat`: part in sketch
  edit, maximized), wmdrag superdrag (Super held: WM's own move) xmove (Inventor restored),
  self (tool check, ~0.5 ms). Also /proc CPU of Inventor/wineserver/Xorg (`--prefix`, default
  prefixes/inv); --record: X requests per client pid/op/window (RECORD).
  CSV + summary in inst/uilat/. Numbers and A/B vk/gl/DXVK: issue 060; WM drags: 061.
- `tools/soak/soak.sh OUT HOURS` — soak loop in one Inventor session (inv/:98): `run.sh all` per
  iteration, `samples` every 3rd, uilat rubber+orbit every 10th + at the end; per-iteration logs,
  events (crash/restart/licensing), a 30 s sampler (Inventor/wineserver/Xorg /proc) and
  `resprobe.c` in the prefix (kernel handles by type, GDI objects, windows; also `dump` = every
  handle's type + name, `threads` = threads per start module, `mods`; runs on the VM too).
  Stub managers/proxies per apartment after odd iterations (`stubs.sh PID`, gdb; stubs.txt).
  `plot.py OUT`: resources.png, timings.png, trends.txt. Runs 2026-09-28 (067, 072-074), 2026-09-29 (082), 2026-10-01 (109; soak.sh can be restarted on a running
  Inventor with a new OUT, merge the parts by renumbering before plot.py).
  Leak attribution (086): `handle-trace.patch` (debug-only Wine patch: server log of unnamed
  event/section handles + kernelbase creation backtraces) and `handle-attr.py` (live handles by stack);
  see notes/wine/debugging.md. RSS/mapping growth in soaks comes from `samples` iterations only.
- `tools/regress.sh run BUILD` / `compare BASE.txt NEW.txt` — sharded full
  conformance-suite run (32 jobs, own prefix in /dev/shm + Xvfb :120+ per shard,
  software GL/Vulkan; ~4 min for both arches, 1755 units) and
  diff with re-run check: REAL / NEW (not in baseline) / FLAKY.
  Template prefix has Wine Gecko 2.47.4 (deps/ MSIs, sha256 vs appwiz.cpl's GECKO_SHA, then
  `regsvr32 mshtml.dll` for both arches: wineboot ran with mshtml disabled so the classes and the
  text/html MIME handler were missing) and no Mono. Xvfb is 1920x1200 set to 1024x768 with
  extra RandR modes (tools/xvfb-modes.c: holder process; modes die with their client, and
  `xrandr --newmode` can't attach them on Xvfb; startup waits for xdpyinfo, not the socket, then for the
  holder to print "ready" (modes verified on the output), up to 3 attempts per display) Xvfb's built-in 1920x1200 mode has 0 Hz: dxgi 1674/1683 fail constantly.
  Runs hold flock /tmp/regress.lock via `flock -o` (children never inherit it), so concurrent
  invocations queue. Units with fixed localhost ports (webservices proxy/channel,
  winhttp notification/winhttp, wininet:http, httpapi) also serialize on /tmp/regress-ports.lock.
  `tools/regress.sh unit DLL:TEST [-a ARCH] [-n N]`: one unit N times, same environment, no run
  lock (displays :152-:199, shares the port lock); ~40 s template setup.
  Results cached in `deps/regress/<built commit>/`. Master baseline builder:
  `wt/regress-master` (detached worktree) + `wt/regress-master-build`.

# Running Wine with GPU

    tools/sysroot.sh run env DISPLAY=:98 DRI_PRIME=pci-0000_ca_00_0 WINE_D3D_CONFIG=renderer=vulkan build/wine app.exe

The sysroot env is required (26.04 host): DRI_PRIME only works through Mesa's device_select Vulkan layer, which
lives in deps/sysroot (mesa-vulkan-drivers). Without it wined3d takes Vulkan device 0 (16:00.0) and every
D3D11CreateDevice on another GPU's display fails with 0x8007000e ("Queue family does not support presentation"):
WebView2 dies ("GPU process isn't usable"), Inventor shows "Encountered an improper argument." (164).
Check: `tests/d3d11_present.exe`.

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
  26.04 host: order 16, 34, ac, ca (+ llvmpipe with the sysroot env); the layer is not installed
  system-wide, only in deps/sysroot (164).
- Wine's full d3d11 test suite still hits VK_ERROR_INITIALIZATION_FAILED on some
  swapchain and crashes mid-run on this setup; single-swapchain apps work.
- DXVK reports the RTX as AMD (1002:73df) by default; set
  `dxgi.hideNvidiaGpu = False` (dxvk.conf) if an app checks for a certified GPU.
- QEMU 6.2: q35 SATA ports hold one CD each (`bus=ide.N`). Its bundled C
  virtiofsd needs root; hot-installing the viofs driver made the Rust
  virtiofsd exit (InvalidMessage) — a VM restart fixed it.
