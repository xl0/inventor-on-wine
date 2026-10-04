# Local workstation: handoff and deferred verification

Keep the working Wine-only setup ready for user-driven testing. Implementation
and further diagnosis are handed to the main agent. Preserve the private crash
evidence and matching binaries; keep system Wine and unrelated prefixes untouched.
The server results below are reference material, not local completion status.

- [ ] Triage [124](issues/124-open-dialog-resize-coreclr-crash.md): Open-dialog
  Awesome-resize repaint/navigation failure and captured CoreCLR crash.
  Keep the symptoms distinct until linked; preserve private evidence and binaries.

- [x] CJK comparison and screenshot handed off in
  [123](issues/123-cjk-fallback-via-fontconfig.md).
- [ ] Main agent to investigate [125](issues/125-format-text-preview-cjk-richedit.md):
  preview-only boxes, early wrapping and tiny dots after re-editing. Text
  orientation is not yet distinguished from the camera/sketch orientation.

**Licensing:** Inventor is closed. Coordinate the shared single-device seat
before another user-requested launch; no further local test campaign is requested.

- [x] Dedicated Inventor 2027.1 prefix installed, including native .NET 4.8,
  Gecko, pinned Edge/WebView2 and isolated user folders. Interactive document
  and sketch use reached.
- [x] Built `d7799da4d5` with local memory limits; 32/64-bit console, GUI and
  COM proxy checks, Vulkan readback and DComp/VideoContext1 queries pass.
- [x] NVIDIA-primary single-X-screen desktop resolved reported lag; picom
  GLX/VSync with `--no-use-damage` eliminated reported viewport tearing.
- [x] Verified 084 fix on the laptop's original missing-ShellFolder state:
  shell and WinSupport.dll probes now return Documents, without registry repair.
- [ ] When local testing resumes, explicitly verify the Content Center warning,
  [085](issues/085-trial-popup-stays-white.md)'s popup/cursor fixes and
  [083](issues/083-wpf-keytip-font-fallback.md)'s ribbon key tips after corefonts.
  Successful launches alone do not verify those exact cases.
- [ ] Optional Electrical Catalog Browser remains uninstalled
  ([050](issues/050-electrical-catalog-unzip-msi.md)); the server's missing-tar
  workaround has not been applied locally.

# Server plan: Autodesk Inventor on Wine, agent-driven

Intent: get Inventor working under Wine by closing a tight loop:
scripted scenario fails → triage traces → minimal repro / conformance test →
ground truth on a real Windows VM → patch Wine → rerun scenario.
Upstreamable fixes preferred; a patched fork is an acceptable first deliverable.
Clean-room: black-box observation of Windows only, no disassembly of MS binaries.

Milestones (Inventor): install → licensing service → sign-in → main window →
new part → sketch/extrude → save/reopen → assemblies → drawings → export.
Drive via Inventor COM API where possible, screenshots only for UI/render checks.

Licensing: 30-day trial per Autodesk account. User approved starting the clock
(2026-09-27). First launch needs the user's Autodesk sign-in (credentials are
the user's, in `login.txt` — git-ignored, mode 600; only the coordinator
types them, via xdotool on :98; never paste them into prompts, logs, issues,
commits or screenshots). Read-only mode may remain after expiry.

## TODO
- [x] Wine built from source (new WoW64), prefix boots
- [x] Headless NVIDIA display: D3D11 (wined3d-vk) renders + presents on RTX
- [x] Windows 11 VM: unattended install, SSH, snapshots, virtio-fs share
- [x] Test bridge: `vm/winrun.sh` (VM desktop session) vs `build/wine`
- [x] DXVK / vkd3d-proton installed and presenting on RTX
- [x] Prefix `inv` with native .NET 4.8 (winetricks), win11; pristine copy `inv-net48`
- [x] Inventor 2027.1 install (web installer, no optional components)
- [ ] Test campaign (user request, after outstanding issues): extensive API +
  UI tests via subagents. Wine-only by default with analytic expectations; the
  VM only when Wine fails/looks off, or once to cache reference outputs
  (exports, UI screenshots, occasional timings). API (tools/invscen): features
  (fillet/chamfer/hole/pattern/shell/revolve/sweep/loft, sheet metal), file I/O
  and export/import (STEP/IGES/STL/DWG/DXF/PDF), assemblies (constraints, BOM,
  large generated asm), drawings (section/detail/dims/parts list), iLogic/VBA,
  per-step timings. UI (:98): ribbon tabs + main dialogs, manual sketch/extrude,
  viewport navigation + visual styles screenshot-diffed vs VM (wined3d, then
  DXVK), browser/context menus/undo, Assistant panel, help links.
  - API suite done (`tools/invscen/run.sh all`, 13 scenarios, ~130 steps; VM
    reference logs + outputs in inst/invscen/ref/). integ 91495f487ad: all
    modelling, parameters/iProperties/materials, sheet metal + flat pattern,
    constraints/BOM/interference, 200-occurrence asm, drawings (section/
    detail/dims/parts list/sheet formats, .idw), STEP/IGES/SAT/Parasolid/STL
    round trips, PDF/DWG/DXF export and iLogic PASS with the VM's values.
    Wine-only failures: SaveAsBitmap unshaded (037), view extents/cold-start
    ActiveView (037), con.iam (035, fixed),
    Inventor crashes in RevokeDragDrop (034, 3 in ~1 h of suite runs).
    Infra: prefix has no Wine Gecko; Inventor's first STEP import per session
    loads mshtml → "install Gecko" prompt blocks Inventor (export runs last).
    Not testable: VBA (not installed), Apprentice (not registered) — same on VM.
    Speed: suites 1.1-2.5x the VM; constraints 5-10x (mate/flush 2.7 s vs
    0.4 s, angle 2.0 vs 0.2), placing 200 occurrences 19 s vs 7 s.
    2026-09-28 rerun, integ 228616fa47c, cold + warm: all campaign PASSes hold
    except cold asmbig save (crash in ogsdevicedx11, 047, also in campaign dumps);
    one cold start hung on the first doc (048); view images still 037.
    Real-world data (`samples`/`samples2016`: Autodesk's official 2022 + 2016 sample sets,
    69 docs each incl. 1313-leaf Buffer Prep Skid; inv3/:100): everything matches the VM
    (counts, BOM, mass, migration, save/reopen, older DWG) since 053/054/056 (integ
    c09f08e4924; samples2016 full PASS) except last-digit rebuild volumes (055) and a
    full-2022-run sequence: Buffer Prep Skid save-as E_FAIL, then Inventor dies in Fan Cover
    Mold Rebuild2 (throw from catch, 056 notes). ~1.9x the VM (057); big-asm open 2.5-7x.
  - UI pass by hand (:98, integ d53133a66a1; worker report has details):
    | area | result |
    |---|---|
    | ribbon tabs part/asm/drawing, File menu, New dialog | OK |
    | App Options (all tabs), Doc Settings, iProperties, fx, Styles, Projects | OK; CR shown as box 039, date gap 044 |
    | title bar: QAT/InfoCenter cut off by WM frame | broken 040 |
    | Open/Save As/Place file dialogs | save/typed name OK; breadcrumb blank 041, file click ignored 043 |
    | sketch rect (typed dims), extrude, params, rename, drag, undo/redo, ctx menus | OK |
    | viewport render, ViewCube, styles, orbit; picking (dim/fillet/drag) | broken 037 |
    | asm place (typed name) + constraint via browser picks; drawing base view | OK |
    | tooltips/popups | OK, black shadow 042 |
    | Assistant (WebView2) Q&A; Help → Edge | OK (Edge ~50 s, 022) |
    Env: WPF browser lacks symbol glyphs ([●] grounded mark shows as box: no
    Segoe UI Symbol-type font in C:\windows\Fonts); :98 Xorg uses xfree86
    keycodes, Wine expects evdev → wrong GetKeyNameText ("Super_L" for End).
- [ ] Extra coverage (queued, subagents): real-world sample data on inv3/:100
  (running) → soak loop (run.sh all for hours, crashes/memory/timing drift) on
  :98 after 047 → specialised environments (Nastran, Frame Generator, Design
  Accelerator, Tube&Pipe/Cable if content allows, Anark 3D PDF, print) →
  environment variations (non-ASCII/long paths, locale, HiDPI, clipboard,
  file associations) → after 037: visual styles/Studio/ray tracing/viewport
  perf/picking → installer repair/modify/uninstall. GPU passthrough to the VM
  (real NVIDIA reference instead of WARP) after the user enables IOMMU.
  - Soak (inv/:98, integ c036c687c47, tools/soak, inst/soak/2026-09-28): one session 4.2 h, 21 suites +
    6 samples: all PASS (bar 055); 1 crash at 4 h 10 min (074); no licensing errors; UI latency flat;
    suite time doubles (130 → 250-300 s) from COM stub build-up (072) + heap free-list scans (073);
    RSS +2.5 GB/h, handles +2.2k/h (Explorer key leak 067, proxy mutexes 072).
    Soak #2 (integ bbc7f82accb, inst/soak/2026-09-29): 31 suites + 6 samples, no crash (074 not seen); suite time
    flat (late 135 s vs 122 s), ntdll 1.9 % / combase ~0 % late; stub managers flat (~50), no key/mutex leak.
    Still: RSS +2.8 GB/h, handles +2.2k/h (unnamed events ~200/suite, sections), mappings +3k/h. New: Activate of
    Default.ipj E_FAIL after 3.4 h, then asmcon SaveAs E_INVALIDARG every run (082).
  - Specialised environments (inv3/:100, integ c036c687c47, rechecked 7f6770b075c; API where possible, else UI):
    | area | result |
    |---|---|
    | Stress Analysis ("Inventor Nastran" is not a separate add-in here or on the VM; the built-in env uses Bin\NASTRANSolver) | env + study + loads OK since 063 (7f6770b075c); Mesh View kills Inventor: NdrStubCall3 unimplemented (068). VM on `beam`: 1.522 mm / 119.8 MPa (theory 1.52 / 120) |
    | Content Center (desktop libraries installed on Wine, none on the VM) | OK since 064 (7f6770b075c): 10 categories, Place from CC generates + places DIN EN ISO 4017 |
    | Frame Generator | profiles + preview + naming dialogs OK; final OK creates no members, silently (069, unconfirmed vs Windows) |
    | Design Accelerator shaft / spur gears | OK: dialogs, calc, 2D preview + graphs; parts generated (gear ODs 6.35/14.98 cm, reactions 78.7+46.0 lbf = load + weight). Previous component dropped when the next generator starts (066, unconfirmed) |
    | Bolted connection | OK: CC fastener picker, bolt generated + placed |
    | Tube & Pipe / Cable & Harness | both envs enter, run/route/harness created, route start pick works, C&H library 491 wires; no full route (fittings need CC) |
    | Anark 3D PDF (`publish`) | OK, PRC 3D PDF as on the VM |
    | Printing (`publish` + File › Print) | no printer on Wine by default (DrawingPrintManager.Printer E_FAIL); with tests/addprinter.exe: print dialog, Print-to-File prompt, PostScript output match the VM's PDF content |
    | Inventor Studio | render OK (32 iterations, 4 s; CPU raytracer, Inventor ~190% CPU) |
    | viewport after closing the Assistant panel | OK since 065 (7f6770b075c); black areas under closed dialogs (061) |
- [x] Modelling via COM (tools/invscen): part (sketch/extrude/mass props/
  save/reopen), assembly (2 parts + mate), drawing (views) all PASS on Wine,
  matching the VM step for step (integ 6adad910faf). Speed is the gap (032).
- [x] First launch + sign-in. inv-vm (transplant, now idle; :107 stopped): WebView2 renders the
  licensing page. Sign-in is OAuth in the system browser (IdentityManager
  LaunchNativeBrowser; code returns via a custom URI protocol) → install Edge
  in the prefix as default browser, keep the round trip inside Wine.
  Status: Edge renders the Autodesk sign-in; email+password accepted, then an
  hCaptcha — needs the user. Same on the real install (prefixes/inv, :98,
  VNC 5902): credentials entered, hCaptcha waiting. :98 is the primary path.
  Rendering fixes found: 023 dxgi Present1 dirty rects; layered colorkey
  window hides GPU child content.
  prefixes/inv (real install, integ 220b08678ea): Autodesk's installer ran its
  own WebView2 143 (missing on Wine; in-box 154 on the VM, so skipped there);
  tools/edge.sh upgraded it to 154 + Edge. Launched from the Start-menu .lnk on
  :98: licensing page renders, "Sign in with your Autodesk ID" opens Edge
  (first-run wizard, then the sign-in form). Next: coordinator signs in on :98.
  Signed in (028 workaround). integ 6c63dc3c499: Start-menu .lnk → trial welcome
  dialog → Inventor Home ("Inventor 2027.1", Recent, Open/New) in ~75 s, responsive.
  - VM: installed; signed in 2026-09-28 13:15 (user solved hCaptcha); Inventor
    Home up, running in the desktop session (reference for scenarios).
  - COM scenarios (tools/invscen, 2026-09-28): VM passes part, asm, drawing.
    Wine: connect + new part OK, sketch blocked by 031; asm (with the VM's box.ipt)
    blocked by 031 at Occurrences; drawing (VM box.ipt) passes: base + projected
    view, save/reopen .dwg. All API calls slow on Wine (032).
  - Wine (integ ec5464293b0): "Install and update complete" 2026-09-28 02:03;
    base bundle + 2027.1 Update bundle INSTALLED (incl. DWG TrueView as
    Essential, as on the VM). UI left at the Start screen (not clicked).
    Only deviation: Genuine Service MSI 1603 (PowerShell CA, 020), ODIS treats as
    success; Content Libraries installed from an earlier run.
  - Prefix fixes: users/xl0/Documents was a symlink to the read-only host
    ~/Documents (RSA Engine "REP_Init: Cannot create ...\Documents\Autodesk
    \Output"); now a real dir. A stray `WindowsUpdate\Auto Update\RebootRequired`
    key (created 00:29:07 during a rollback, writer unknown; ODIS then demands a
    restart) was deleted.
- [ ] Wine bugs: one file each in `issues/`. M = merged into integ,
  P = parked, O = open, X = not a Wine bug / wontfix, L = low priority.
  - M 001 SxS app config privatePath · M 002 QueryActCtx USE_ACTIVE fallback
  - P 004 CreateProcess 14001 on missing manifest dep (needs built-in assembly audit)
  - M 005 expose repaint of GPU content · M 006 empty surface clip
  - M 007 tasklist no-match INFO line · M 008 wintrust p7x blobs
  - M 009 wintrust SHA-256 chains + RFC 3161 · M 010 regf hives + RegLoadAppKey
  - M 011 crypt32 partial chains · L 012 crypt32 basic constraints
  - M 013 crypt32 array decoder skip · M 014 services in session 0 (+ 015)
  - M 016 GeneralizedTime fractions · M 017 dcomp (Wine-Staging series + fixes)
  - M 018 session-0 follow-ups · M 019 imagehlp certs past 2 GB
  - L 020 Genuine Service MSI 1603 (PowerShell 5.1 missing?) · L 021 large PE mappings
  - L 022 Edge GPU path undocumented dcomp iface (30 s delay; clean-room)
  - M 023 dxgi Present1 dirty rects · M 024 wofutil · M 025 SC_MAXIMIZE no-op
  - M 026 AppContainer tokens (Edge sandbox) · M 027 layered colorkey + GPU child
  - M 028 merged HKCR view · X 029 no WM on :98 (openbox now) · M 030 COM per-user classes
  - M 031 typelib offsets > 32 KB · M 032 typelib load speed
  - L 033 Wow6432Node marker keys · M 034 RevokeDragDrop wrong thread
  - M 035 DOS device names (Win11) · X 036 black dialogs (xwd artifact)
  - M 037 3D viewport: vkd3d-shader fx statics in $Globals + d3d11 FORMAT_SUPPORT_DISPLAY
  - L 038 NTFS stream names · M 039 unmapped control chars zero-width (+ DrawText tab break)
  - M 040 custom title bar vs WM decorations · M 041 file dialog breadcrumb
  - L 042 popup shadows black · M 043 file dialog selection
  - L 044 DateTimePicker gap · O 045 maximized undecorated offset (draft)
  - M 046 comctl32 subclass: cross-thread/process, nested removal UAF, v5/v6 split
    (probable cause of an Inventor startup crash; not confirmed)
  - L 050 Electrical Catalog Browser unzip custom action (MSI 1603)
  - M 047 d3d11 CreateDeferredContext raced with ClearState (Inventor crash) — merged
  - O 048 cold-start hang, trial popup "We're having trouble" (maybe shared licensing)
  - L 049 SysLink rejects <a> with extra attributes (raw markup shown)
  - X 050 Electrical Catalog CA needs inbox tar.exe → tools/tar.sh (bsdtar 3.8.8)
  - M 051 msi removed pre-existing empty folders on uninstall/rollback (data loss)
  - X 052 junctions lost when copying prefixes without xattrs (inv2/inv3 repaired)
  - O 053 packager: Package object lacks IViewObject2/IOleCache/IDataObject (Arm_Rest, Fan Connector)
  - O 054 DeleteFile/RemoveDirectory lack POSIX semantics (name stays while handles open)
  - L 055 ucrtbase math last-bit differences; sample rebuild volumes differ 5e-6..5e-5 (unconfirmed link)
  - O 056 COM stub lets C++ (noncontinuable) exceptions escape: Inventor crashes instead of RPC_E_SERVERFAULT
  - M 053 packager IViewObject2 + IOleObject + IPersistStorage::Save (Arm_Rest.ipt)
  - M 054 POSIX delete semantics in DeleteFile/RemoveDirectory (+3 server fixes)
  - L 055 ucrtbase math 1–2 ULP vs MS (tiny volume diffs)
  - M 057 GetWindow Z-order links in shared memory (19.9 µs → 0.1 µs; samples open/save/reopen 1294 → 714 s, VM 498)
  - X 058 crash is Autodesk's own RxDispatch __except + CER after an FWSrv throw (follows 059)
  - M 059 user handle generation wraps at 0x7ffe like Windows (bit-31 HWNDs broke MFC → SaveAs E_FAIL, 058 crash)
  - M 060 wined3d-vk: reuse retired large buffers (sketch 47.7 → 4.9 ms, = DXVK)
  - M 061 re-present offscreen client surfaces on expose (black drag trail 67% → 0%)
  - M 062 WPF splitter popup: hidden under a compositor (all-faint layered windows); black without one (X can't blend, wontfix)
  - M 063 manifest threadingModel case-insensitive + defaults (Stress Analysis; Inventor retest pending)
  - M 064 GetWindowsAccountDomainSid non-account SIDs → 1257 (Content Center; Inventor retest pending)
  - M 065 win32u DC region recompute for cross-process child windows (stale Assistant image)
  - X 066 Design Accelerator part dropped by the next generator: same on Windows (pending Place)
  - M 068 rpcrt4 NdrStubCall3 → NdrStubCall2 (NDR syntax)
  - M 070 in-process cross-apartment calls run in the caller's actctx (+ MTA calls bypass rpcrt4) — Stress Analysis = VM
  - L 071 cross-process calls into an STA: OLE-window creation context
  - L 075 GetProcessHandleCount stub returns 0
  - O 069 Frame Generator OK creates no members (Windows unchecked: VM lacks CC libraries)
  - M 067 SHAddToRecentDocs leaked the Explorer key per call
  - O 072 COM: proxy mutex → CS + rb-tree stub lookups merged; ping-based rundown of dead
    clients' objects deferred (design in issue; harness now releases refs)
  - M 073 ntdll heap: more free-list size classes + bounded walk (verified in soak #2)
  - O 074 Inventor crash after 4 h soak, CLR exception in place-occurrences (draft; not in soak #2)
  - O 082 soak: DesignProject.Activate E_FAIL after 3.4 h, then SaveAs E_INVALIDARG (draft)
  - M 076 SC_MOVE with any hittest low bits moves the window (caption drag under awesome did nothing)
  - M 077 WM-initiated moves send WM_ENTERSIZEMOVE/EXITSIZEMOVE (stale splitters after Mod4+drag); follow-up 130
  - M 078 first present of a new offscreen client surface never reached the screen
  - L 079 awesome: restoring a maximized window leaves X maximized (seen once)
  - L 080 WindowFromPoint ignores per-pixel alpha of layered windows
  - M 081 ~500 wineserver requests per COM call (hooks, registry, FreeLibrary, GetProp): 9 ms vs 2 ms
  - M 056 COM server exceptions → RPC_E_SERVERFAULT (combase channel + rpcrt4 stub)
  - X 003 installer "hang" (harness artifact)
  - Pending chores:
    crash-capture .reg applied to inv (inv2 when its Inventor stops).
- [x] Integration branch `integ` in wine-src: linear stack of our fix commits
  on master; `build/` runs it. Tracks upstream master tip: periodically
  `git rebase master integ` (upstreamed patches drop out), rebuild, retest,
  force-push; rerun tests + the Inventor steps.
- Later: virtio-fs cached-read flakiness (newer QEMU/viofs? guest debug log),
  GPU passthrough (needs intel_iommu=on), data disk on spare NVMe,
  wined3d d3d11 suite crash on NVIDIA headless (only if it bites real apps)

## New host (2026-10-04): Ubuntu 26.04.1, kernel 7.0, reinstalled — survey
Project data is intact (same path and user; ZFS now, 1.4 T free): prefixes, VM disks, builds, worktrees,
login.txt, git state as in the pause section below. Nothing of ours is running.
- Works as is: the existing Wine builds (build/, build-next/, wt/*-build) run — all dlopen'ed
  libraries are present except libodbc and ffmpeg 4 (winedmo.so). Xorg + nvidia_drv, x11vnc, qemu 10.2
  + OVMF + swtpm, /dev/kvm, gdb, gnome-shell 50.1, our own virtiofsd in deps/.
- GPU: NVIDIA 595.91.07 (open module), nvidia-drm.modeset=1 → KMS connectors exist and
  `tests/gpu/gbmtest.py` passes: NVIDIA GBM backend, dma-buf export, EGL on GBM. GPU-composited
  headless Wayland and virgl/Venus in vmwl/ (qemu 10.2) are now worth trying. gdm's greeter
  (gnome-shell on Wayland) runs on one GPU.
- Build deps and tools now come from a project-local prefix (user's choice; no root, no sandbox
  change): `tools/sysroot.sh` unpacks 233 pinned Ubuntu packages into deps/sysroot (822 MB), list in
  tools/sysroot.pkgs; regress.sh, prefix.sh, run.sh, x/start.sh pick it up themselves; builds need
  `eval "$(tools/sysroot.sh env)"`. A fresh integ build from it (build-s/) matches the old feature
  set and gains native winewayland.drv, winedmo on ffmpeg 8 and ntsync headers (no /dev/ntsync in
  the sandbox). Xvfb, openbox, awesome, picom, xdotool, python3-xlib, mingw run from it.
  x/wayland.sh works on gnome-shell 50 (private dbus config without AppArmor mediation).
  Nested bwrap/user namespaces are blocked inside the sandbox.
- integ (local) = e00a74f6590: 232278be38e + 141 (4 commits, read by the coordinator) + 134 (6).
  Worker (Sonnet) running: new-host master build + regress baseline (old-host results are not
  comparable: ZFS, kernel 7, Mesa 26), integ build + compare, host-drift table. No Inventor yet.
- kernel.yama.ptrace_scope: was 1 after the reinstall, the user set it back to 0 (gdb attach works).
- Disk layout (user, 2026-10-04): everything stays in the project dir on /home (ZFS, compressed ~2x);
  only the big images live in /data/users/xl0/wine (XFS on RAID0, no redundancy, no symlinks):
  win.qcow2, vmwl-disk.qcow2, iso/. vm/run.sh, vmwl/run.sh, transplant.sh use $WINE_DATA (default
  that path). Pruned: prefixes/inv-lic and inv-vm, merged worktrees (branches kept), all old-host
  builds under wt/ and build-next/. The user allowed deleting prefixes as needed and using the licence.
- New-host builds: wt/regress-master-build-h26 (wine-11.18-218-g4e819f054dd) and build-s
  (wine-11.18-531-ge00a74f6590, with winewayland.so). wt/132 and wt/157 have no build (rebuild
  natively when resumed). Next: master baseline
  (`tools/regress.sh run wt/regress-master-build-h26 -o deps/regress/4e819f054dd2d9ee855ee3f1e30d8c1bb8f80fcf-h26 -f`),
  integ run (`-o deps/regress/<e00a74f6590 full hash>-h26`) + compare, host-drift table, warning scan;
  then start the prefixes (first Inventor launch on the new device id) and the Inventor checks.
- First Inventor run on the new host (inv, :98, old-host binary build/): headless NVIDIA Xorg comes
  up with modeset=1 (GL 4.6 NVIDIA 595), the licence works without sign-in (trial, 25 days left;
  the new machine-id did not cause a device-limit or sign-in prompt), `hello` passes (connect 20.6 s).
  [164](issues/164-new-host-webview2-children-crash.md), fixed in the environment: the host has no
  mesa-vulkan-drivers, so no device_select Vulkan layer; DRI_PRIME was ignored, wined3d took GPU
  16:00.0 and every D3D11CreateDevice on :98 failed (WebView2 gone → Home page black; Inventor's
  view init → "Encountered an improper argument."). Package added to deps/sysroot: Home page,
  Assistant and trial popup render, `hello` passes without dialogs. Anything started without the
  sysroot env still fails (host fix: `apt install mesa-vulkan-drivers`).
  Follow-ups: regress.sh's `VK_ICD_FILENAMES=…/lvp_icd.x86_64.json` does not exist here (file is
  lvp_icd.json, now in deps/sysroot), so the h26 baselines ran with no Vulkan driver;
  x/shot.sh does not load the sysroot env (numpy/PIL); `sysroot.sh add` wipes the prefix like rebuild.
- New-host regression baseline done: master (wt/regress-master-build-h26) 1641 pass / 86 fail /
  28 crash of 1755 → deps/regress/4e819f054dd…-h26/; integ e00a74f6590 (build-s) 1648 / 81 / 28 of
  1757 → deps/regress/e00a74f6590…-h26/; compare: 0 REAL, 0 NEW, 3 FLAKY (i386 crypt32:store,
  user32:input, quartz:filtergraph). Old-host results kept, not comparable. Host drift (23 units
  changed state between hosts; table in the worker report, inst/h26/): mostly improvements from
  Mesa 26 and fonts (opengl32, d3d9/d3d8 visual, ddraw7, gdi32:font pass now); worse: ntdll:info
  and kernel32:thread (CPU affinity), x86_64 kernel32:debugger, ntoskrnl, rpcrt4:server,
  user32:input (timing). kernel32:path fails on both hosts.
  gcc 15: one warning in our stack, a false positive (win32u/input.c:868 `clip` maybe uninitialized,
  commit 41d9173ca57; the loop always runs once) — initialise it at the next rebase of integ.
- regress.sh pointed at a lavapipe ICD file that doesn't exist on this host (Mesa 26 renamed it):
  the first new-host baselines ran without any Vulkan driver. Fixed (ICD from the local prefix);
  both baselines rerun with lavapipe (old copies: inst/h26/*-novk-results.txt): master 1644 pass /
  81 fail / 30 crash, integ 1648 / 80 / 28 (+1 timeout), compare 0 REAL, 0 NEW, 2 FLAKY
  (i386 amstream, kernel32:thread). With a Vulkan driver mfplat crashes on master again (as on the
  old host) and kernel32:debugger, rpcrt4:server, x86_64 user32:input pass — those three were
  flaky, not host drift. These two result dirs are the reference for this host.
  `tools/sysroot.sh add` no longer wipes the prefix; x/shot.sh loads the prefix env itself.
- Round finished (2026-10-04): integ e00a74f6590 pushed to gh; build/ rebuilt natively at that tip
  (wine-11.18-531, with winewayland.so); inv, inv2, inv3, inv4 run it (all four headless NVIDIA X
  servers come up, :101 too). Inventor half on inv3, new build vs old binaries (inst/round2/):
  suite 13/13 (also 13/13 on the old build), dwgloop 10/10, samples 464 PASS / 2 FAIL (known 055);
  141: Edit Dimension opens 3 of 3 (old build: assertion); 140: no hang 3 of 3 (old build: hangs);
  130: 117-119.5 fps fresh and aged; 131: "Static" windows 4 → 4 after two suites (old build 10 → 81);
  133 probes as expected; connect 20.5-22 s on both; identical err-line sets; WebView2 helpers run.
  One open item → draft 165 (Opus worker, inv3): a wined3d `wined3d_not_from_cs` assertion box,
  seen once on the new build, not reproduced, cause unknown (ours with 131, or upstream/host).
- 157 (winewayland lock order) resumed in the VM.
- 132: M1 built on fix/132 (4 commits, winewayland only, +998 lines; all gates pass): the presenting
  process reads frames back into a shared section, the owner's Wayland event thread attaches them
  to a subsurface; handles pulled by the owner (no names, no server change), independent of the
  owner's message pump. On the new host the Wayland session is GPU-rendered (mutter and Wine's
  EGL on NVIDIA): WebView2's hardware path survives there (162 and 163 are Mesa/llvmpipe-only);
  Vulkan stalls in vkAcquireNextImageKHR on this session (draft 166), so Wayland = renderer=gl.
  Cost at 1678x884 on NVIDIA: ~0.9-4 ms per frame in the source, 3.75 ms compositor upload.
  Inventor on Wayland: Home page, trial popup and Assistant show content. NOT mergeable alone:
  remote surfaces aren't clipped by sibling windows, so the Home page covers open documents.
  Worker continues with M2 (owner-side clipping/visibility, idle source, slot limits); review after.
- Next: adversarial reviews for 157, 132 and 165 when they report; UI pass 3 (areas pass 2 missed);
  warning fix for 41d9173ca57 at the next rebase of integ; drafts not started: 142-151, 156,
  158-163 (see issues/).
- ssh from inside the sandbox fails on the system config ("Bad owner or permissions on
  /etc/ssh/ssh_config.d/20-systemd-ssh-proxy.conf": root-owned files look unowned in the user
  namespace). Fixed on our side: both repos use `core.sshCommand = ssh -F /dev/null`, and
  vm/winrun.sh, vmwl/ssh.sh, run.sh, transplant.sh pass `-F /dev/null`.
- Claude Code 2.1.289.

## Paused for a reboot (2026-10-03) — resume from here
Everything is stopped cleanly: inv, inv2, inv3, inv4 (wineserver -k), both VMs (guest shutdown), the
host Wayland session, all workers. Only inv-lic (unused, 152) was left running; don't restart it.
State:
- wine-src `integ` (local, NOT pushed) = 232278be38e: gh/integ d18a5dcd1ef (has 130; build/ is still
  04293594c50) + 131 (3 commits) + 140 (4) + 133 (3), all reviewed.
- Verification round, half done: `build-next/` = wine-11.18-521-g232278be38e; full regress
  (deps/regress/232278be38e…/, compare outputs there): 0 REAL, 0 NEW, 4 FLAKY (i386 kernel32:thread,
  ntdll:info both arches, x86_64 ntoskrnl — fail the same on build/), nothing worse than 04293594c50;
  user32:win win.c:12747 seen 0 of 30 runs on either build. NOT done: the Inventor half.
- Ready to cherry-pick after the round: fix/134 (6 commits, 5d59ae5fddf, winewayland only, reviewed)
  and fix/141 (4 commits, 467f5b54e5d; coordinator still to read the two new small commits).
- Checkpointed, no fix code yet: fix/157 (repro in vmwl on GNOME 50, lock order rule decided, call
  table and remaining steps in the issue; wt/157-build needs `make dlls/win32u/all
  dlls/winewayland.drv/all`), fix/132 (M0 answered, M1 design in the issue; wt/132-build built).
After the reboot:
1. `tools/prefix.sh start inv` (then inv2, inv3, inv4): Xorg, VNC, prefix, all on build/.
   Windows VM: `vm/run.sh` (Inventor there only with the host's Inventors stopped). Linux VM: `vmwl/run.sh`.
2. Finish the round on inv with build-next (lease, set inv's build column in x/prefixes.tsv to
   build-next, restart): suite all, dwgloop, 131 window counts + Home screenshot, 130 rubber fps
   (inst/130/m.sh, age.sh), 140 repro, 133 probes, connect times; restore the table. If clean:
   push integ, rebuild build/ (taskset -c 20-59,80-119), restart prefixes.
3. Cherry-pick 141 and 134, rebuild, riched20 subset + drawing-dimension repro; then resume 157 and
   132 (SendMessage to the same agents if this session survives, else new workers from the issue
   files' "State at pause"). Every new fix: independent Opus review before merge.
4. UI pass 3 for the areas pass 2 didn't reach; drafts filed today and not started: 142, 143-151,
   156, 158-163 (162: WebView2 hardware-path GPU process dies at a Chromium check on software GL,
   X too — content only after ~25-30 s; 163: swap with interval > 0 on a never-mapped Wayland surface
   blocks forever).
5. Open questions for the user: Inventor in the Linux VM (separate device + seat juggling, or not at
   all); retire prefixes/inv-lic; MCP server later; host packages after the 26.04 reinstall
   (then also modeset=1: tests/gpu/gbmtest.py, GPU compositor, virgl/Venus).

## Now (2026-10-03)
- build/ = integ 04293594c50 (wine-11.18-510, PROTOCOL 971), pushed to gh. Last full round clean:
  regress vs master 4e819f054dd 0 REAL/NEW (1 FLAKY, x86_64 user32:input); invscen all 13/13;
  dwgloop N=10 PASS; CJK glyphs in all 8 fonts; 124 Open-dialog crash repro survives 12 rounds.
- Prefixes: inv, inv2, inv3, inv4 on build/; inv-lic (frozen wt/lic-build) is unused (152).
  Claude Code was relaunched 2026-10-03 in a new sandbox: inv-lic's processes are hidden from
  /proc (other user namespace) but work; leave it running. prefix.sh now finds servers via /proc/locks.
- In progress:
  - 130 merged on integ d18a5dcd1ef (pushed), not yet in build/: winex11 selects raw button releases
    on the root only during a WM grab (077's per-config-change root XISelectEvents made Xorg recompute
    all XI2 masks; rubber fps 119 → 47 after 12 suites, flat 117-119 with the fix). Worker ran the
    077 size-move table (awesome/openbox ± picom: same) and user32/win32u/winex11/dinput regress (0 worse).
    Decisions: no separate adversarial review (5 lines, same logic with an early query; read by the
    coordinator); build/ rebuild + full regress + suite wait until the UI and Wayland passes end
    (a regress run at load 60-120 would spoil their timing).
  - 133 fixed on fix/133 (3 commits), in review. My premise was wrong: on Windows 11 a process is
    input idle as soon as ANY thread waits for messages (as on Wine). The real bugs: two Wine-internal
    waits set the idle event — the clipboard manager thread (winewayland's per-process one, explorer's)
    and a thread sitting in WaitForInputIdle itself. No protocol change. Not yet confirmed on Wayland
    (`tests/r133/idle.exe main_getmsg helper_win_getmsg`, then drawing2/sheetmetal). Remaining Windows
    differences (PeekMessage loops count as idle, later calls track one thread, +500 ms timeout) → draft 143.
  - Verification round running (Sonnet): integ 232278be38e = 130 + 131 + 140 + 133 built in
    build-next/, full regress vs master and vs 04293594c50, suite + dwgloop + per-fix checks on inv.
    integ is not pushed and build/ not rebuilt until it is clean. 141 and 134 follow when reviewed.
  - Wayland track, two Opus workers on top of fix/134:
    157 (wt/157, tests in vmwl/): lock-order deadlock user lock ↔ win_data_mutex — two threads
      showing/hiding windows hang 4 of 7 runs on integ; the blocker for real use.
    132 M0+M1 (wt/132, host session, inv2): unknowns first (does the GPU process block in
      eglSwapBuffers on a never-mapped surface; why its hardware path dies), then design A, GL path:
      renderer reads back into a shared section, owner attaches it to a proxy subsurface.
  - Reviews (independent Opus) and follow-ups:
    131 window-less composition swapchains: reviewed, follow-ups done (explicit
      WINED3D_SWAPCHAIN_WINDOWLESS flag, so d3d8/d3d9 NULL-window swapchains are as before; Windows
      parity for 5 calls) → on integ eb8829b9fe1 (not pushed until the round is complete).
      Drafts: 149 (Present(1) doesn't pace on these), 150 (Vulkan crash after the window of a
      swapchain is destroyed), 151 (FLIP_DISCARD composition buffers read back black).
    134/135: review follow-ups done on fix/134 (5d59ae5fddf, rebased onto integ 0b77b17a942):
      foreground fallback (owner's last active popup → thread's active window → the window),
      no redundant set_parent(nil), foreign owner decided by process id, cross-process loop guard.
      wl_xowner 28/28 on mutter 42, Inventor dialogs and trial popup stay above, typing reaches the
      dialog. To be cherry-picked onto integ once the verification round is done (winewayland only).
      Remains: keys to a dialog in another thread/process than its disabled owner; stale-import loop;
      owned windows of a minimized owner invisible (156); 158 (stale owner handle after thread exit).
    133: review follow-ups done (surface flush before the wait, test robustness, QS_SMRESULT case)
      → on integ. Wayland case verified by the reviewer (+69 ms → +1590 ms). One i386 user32:win
      run of 16 had an extra failure (win.c:12747, cross-thread destroy order); frequency is being
      measured on both builds in the round.
    140: reworked after review → on integ: the accelerator search ends when it wraps a second time
      (3 lines; every case that returned is unchanged; 0 hangs of 1785 probe cases, Windows hangs in 25)
      + no search when the message window is the dialog (Windows sends nothing; Wine clicked buttons
      of the dialog's sibling) + tests (pass on the VM). Inventor repro 3 of 3 without a hang.
      Not re-reviewed: the rework is the reviewer's own tested variant, diff read by the coordinator.
      One Inventor start of four died in .NET startup right after the build switch (not investigated;
      watch for it in the merge round). Remaining Windows differences → draft 148.
    141 (riched20 EM_SETCHARFORMAT SCF_WORD; upstream bug since 2019 per the worker — being
      re-checked, the reviewer found that a DLL dropped next to the exe is not what gets loaded):
      review: commits 1 (wrap) and 2 (caret) fine; commit 3 (word selection rewrite) hangs with an
      EDIT-style word-break proc, pushes an undo item undo can't apply, and moves RichEdit20W apps away
      from riched20.dll's behaviour at word end. Decision: replace commit 3 by the minimal rule
      Inventor needs (caret in front of a paragraph mark formats the mark) + the undo.c one-liner;
      no word-logic rewrite. Worker applying; drafts 159 (ITextRange::SetText never wraps → same
      assertion), 160 (numbered paragraphs: AV in paint), 161 (undo stack assertion). Not on integ
      yet (wine-src must not move under the running verification round).
- UI pass 2 done (Sonnet, inv, build/ 04293594c50, about half of the areas; inst/ui2/results.md):
  nothing regressed vs the first pass; picking, viewport, ViewCube, breadcrumbs (041), file clicks
  (043) now work; 042 shadow still there. New: 140, 141 (above), 142 (low: stale pixels in the
  Render Output dialog's top strip). Not covered yet: pattern/section/measure, interference, .ipn,
  section/detail views, balloons, parts list, print preview, docking/floating/splitters, tile/cascade,
  Frame Generator, Tube & Pipe, Cable & Harness, Weldment, Stress Analysis, Add-In Manager,
  Customize, Styles editor, Content Center, Pack and Go → UI pass 3 when a prefix is free.
- Wayland pass done (Sonnet, inv4, wt/wayland-build at 04293594c50, software rendering): Inventor
  starts, ribbon/dialogs/menus/tooltips/typing/3D viewport work, invscen 10/13. Blockers, by impact:
  132 WebView2 content blank (a swapchain made by another process on a window is never shown),
  134 owned/modal dialogs sink behind their owner (no xdg_toplevel_set_parent),
  135 cross-process owned popup (trial popup) maps behind the main window and blocks its input,
  133 (DWG/DXF export: WaitForInputIdle returns at once, above), 136 activated popups/dialogs are placed by the compositor, not at Win32 coordinates.
  User (2026-10-03): doesn't use Wayland, but would like it to work → second-priority track
  behind X11. Started: 134/135 fix (Opus worker, wt/134, inv2, owns the Wayland session):
  xdg_toplevel_set_parent for owned windows, xdg-foreign for cross-process owners; reports on 136.
  132 design study done (in the issue): WebView2 renders on its own child of a foreign top-level and
  the Wayland driver has no way to show it. Decision: fund design A (renderer reads back each frame
  into a shared section, the owner attaches it to a proxy subsurface; winewayland only, ~500 lines
  GL, software copy per frame) after 134/135 land and the Wayland session is free; first M0
  (does the GPU process block in eglSwapBuffers on a never-mapped surface; why its hardware path
  dies; topology-B probe). Zero-copy dmabuf is out of scope here (not verifiable without a GPU session).
  133 is core (above).
- Wayland VM (`vmwl/`, Ubuntu 26.04 guest; GNOME 50, KDE, sway; qemu screenshots/input; README there):
  built. Baseline: notepad fine on all three; 132 reproduces on all three; 134 only on GNOME/KDE.
  fix/134 verified there: no FAIL on GNOME and KDE (unfixed: 17/16), cross-process goes through
  xdg-foreign v2, no protocol errors incl. wlroots' strict cases (sway's one chain-3 FAIL predates it).
  Inventor in the VM (issue 152): not run there since; two findings.
  - Licensing: every prefix is served by its OWN AdskLicensingService (port in the service's .data
    file); nothing has used inv-lic's 39683 since 09-29 → inv-lic is not needed (left running; retire
    with the user's OK). The device id is computed client-side from the disk serial, the machine
    UUID (Unix machine-id) and the user name: all host prefixes are one device, the guest is another.
    Routing the guest to a host service can't change that. The first VM worker's 5 guest launches
    registered a second device (cached licence, no prompt); host licensing still works.
    Open, user's call: guest as a separate device with host Inventors stopped (as for the Windows VM),
    or not at all. Not doing: cloning the host's machine-id/disk serial into the guest.
  - The "crash" was a launch error, same on the host: WINEDLLOVERRIDES="mscoree,mshtml=" (meant for
    wineboot) was exported to Inventor → CLR exception in the main frame's WM_CREATE → NULL deref at
    CommonUI.dll+0x60b90. Not 26.04, not the VM, not winewayland. run.sh now refuses that env.
    Whether Inventor runs on the 26.04 userland is still untested; Wine probes do
    (d3d11 on Wayland/Xwayland, GL and lavapipe). Only winedmo.so (ffmpeg 4 sonames) can't load there.
  virgl (`GL=1`): guest reports "virgl" but every Wine process hangs at start; not investigated.
  Declined: cracked/pirated Inventor (skews the licensing/WebView2 paths we test, untrusted binaries).
- Harness: full regress runs pin themselves to CPUs 20-59,80-119 (REGRESS_CPUS) and builds use the
  same taskset, so merge rounds no longer wait for UI/timing workers. Next merge round is batched:
  130 (on integ) + 131 + 133 after their reviews, built and regressed in `build-next/`, then build/.
  run.sh: a crash in a prefix outside the table no longer restarts `inv`; WAYLAND_DISPLAY set = Wayland run.
  Proposed, not done: golden prefix + `prefix.sh reset` (UI workers leave state behind: user name,
  closed Assistant pane, ribbon split buttons); must keep the live sign-in/licensing state.
- Host reinstall coming (Ubuntu 26.04, a more minimal base; date open). User: nvidia-drm
  modeset=1 will come with it → then re-run tests/gpu/gbmtest.py, try a GPU-composited headless
  compositor on the host and virgl/Venus in vmwl/ (notes/wine/wayland.md). Until then Wayland is llvmpipe. Decision deferred until it
  lands: how our host packages (build deps, mingw, Xorg/Xvfb, WMs, qemu, gdb...) get provided.
  User ruled out re-rooting the shared bwrap sandbox. Must survive the reinstall (not in git):
  prefixes/ (sign-in state), login.txt, VM disks, deps/, ~/.claude; keep user name and project path.
- inv4 state artifact: Inventor's user name is a non-ASCII test string (118 campaign), which
  breaks the export scenario's IGES 80-column check on any build; reset it via the API.
- Waiting on the user's laptop (awesome + picom, 144 DPI): retest at 04293594c50 the trial popup
  (white for a while), splitter overlay lag, CJK in Format Text, the Open dialog crash.
- Held on branches (not merged):
  091 cross-process geometry caches (protocol 970; no measured gain, broad surface);
  108 wined3d CS adaptive spin (0.5-0.8 % of a core); 092 lock-free GetProcAddress (widens the
  Inventor 048 race); 105 optional ProcessDebugFlags setter.
- Not Wine bugs (closed): 048 (Inventor lock-order race; harness waits out the trial popup), 066,
  long-session memory/handle growth (Windows grows as much), long-path SaveAs, Format Text wrap.
- Wine vs Windows VM: place 200 occurrences 7.7 vs 7.3 s; cold first part view ~8 vs 6.3 s;
  trial popup content ~7 vs 6.5 s; AdskIdentityManager idle CPU ~3.5 vs 4 %.
- Soaks: #4 (4.0 h, d7799da4d5c) no crash, no hang, DWG export 32/32; only finding was 130.
- Perf method: single suite sums are noise. Claims need interleaved A/B on one prefix with
  /proc/loadavg < ~25 (a regress run pushes it to 60-120); for stable numbers pin cores and keep
  sibling hyperthreads busy (088: C6 exit latency).
- Licence: ONE active device (server, VM, laptop each count). VM Inventor needs the server's
  Inventors stopped. Never click Pause product.
- Decisions (user delegated 2026-09-30): every non-trivial fix gets an independent adversarial
  review before merge; Sonnet for UI/test/chore workers, Opus for fixes and reviews;
  089 per-process timer resolution on by default (WINE_TIMER_RESOLUTION overrides);
  062 scoped to compositor-only; merged worktrees/builds are pruned, branches kept (last: 2026-10-03).
- Queue (low priority): 117 (msedge.dll image sharing), 121 (HiDPI layout; VM can't do 150 %),
  113 (32-bit thread churn), 042 (shadows/splitter without a compositor), 069 (blocked: VM lacks
  Content Center libraries), 080, 082 (not reproduced), 096 (DXVK), 103 (vkd3d upstream),
  126 (special user APC), 127-129 (Wayland). Rebase integ when upstream master moves.
- Later / another session (user, 2026-10-03): an MCP server for Inventor on our Wine setup
  (run C#/iLogic snippet, view image, inspect tree/parameters, API lookup). Existing third-party
  ones: ipt-mcp, inventor-mcp, Inventor AI (not audited; don't install into our prefixes unread).
- Environments: x/prefixes.tsv + tools/prefix.sh status; leases in x/leases; user WM reference
  x/awesome-rc.lua. Worker scratch is capped at ~10 GB (notes/worker.md).

## Process (agreed with user)
Coordinator drives Inventor, files issues, spawns one worker per issue
(briefed by `notes/worker.md` + the issue file), reviews, cherry-picks onto `integ`,
rebuilds `build/`, deletes merged worktrees (branches kept).
After each `integ` update: a subagent runs `tools/regress.sh` (full suite,
differential vs cached master baseline) and reports only regressions. Workers report
new bugs as draft issues and infra breakage instead of routing around it.
Disassembling third-party (Autodesk) code is OK; Microsoft code never.
Review again after the next batch of workers. Notes so far:
- Adversarial review of risky code (parsers in wineserver) paid off: 010 had
  a server crash, memory/CPU exhaustion and privilege-free key deletion.
  Do it for any code parsing app-supplied data in shared processes.
- Workers leave Wine processes behind (010 reviewer: 24 procs for a day);
  guide now says to clean up.
- 014 ran `pkill -x Xvfb`, killing the regress runner's shard displays mid-run
  (reported it promptly). Guide: kill by PID/display only.
- Workers basing on integ need an integ regress baseline: the coordinator's
  post-rebuild regress run caches deps/regress/<integ-commit>/ — point
  workers at the latest cached one (004 had to build its own).
- Coordinator mistake: `git add -A` swept a worker's in-progress edit
  (decomp.sh without msbin.py) into a commit. Stage explicit paths only while
  workers are editing the project repo.
- 011 also left a gap unfiled (basic constraints; filed by coordinator as 012).
- 006 noticed two possible bugs (cross-process child swapchain doesn't update
  the owner's clip until SetWindowPos; R/B swap on lavapipe) but didn't file
  drafts as the guide asks.
- UI worker (campaign) noted, not filed: :98 used xfree86 keycodes (now evdev
  in start.sh); missing Windows symbol font (browser grounded marks show a box);
  ~13 leftover dll*.tmp theme copies in the prefix (7.5 MB each).
