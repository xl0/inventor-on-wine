# Current goal: local Inventor setup (no VM)

Reproduce the patched Wine + native Autodesk installer setup on this workstation.
Keep system Wine untouched; run from `build/` with a separate prefix.
The server results below are reference material, not local completion status.

- [x] Clone `xl0/wine` into `wine-src/`, tracking `gh/integ` (`47e296ffde4d`).
- [x] Build dependencies installed; new-WoW64 configure passes (only legacy OSS
  audio unavailable).
- [x] Local build complete: 8 jobs, nice 15, 12 GiB cap; 29m14s, peak 6.86 GiB,
  no swap. Separate smoke prefix passes 32/64-bit console and GUI probes.
- [x] Inventor prefix: native .NET 4.8 (32/64-bit runtime checks), Windows 11,
  Gecko, isolated user folders. Local Vulkan D3D11 smoke passes.
- [x] Pinned Edge/WebView2 installed; Autodesk installer launched on the desktop
  in `prefixes/inv`, using the patched build.
- [x] Main Inventor + 2027.1 update installed. Electrical Catalog Browser
  failed in `AceUnzipZipFiles` (MSI 1603);
  [issue 050](issues/050-electrical-catalog-unzip-msi.md), low priority.
- [ ] Verify local Inventor launch/sign-in.

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
  - O 049 SysLink `<a>` with extra attributes shown as raw markup (draft)
  - L 050 Electrical Catalog Browser unzip custom action (MSI 1603)
  - O 047 Inventor crash: NULL read in ogsdevicedx11 on TBB thread (in progress)
  - O 048 cold-start hang, trial popup "We're having trouble" (maybe shared licensing)
  - L 049 SysLink rejects <a> with extra attributes (raw markup shown)
  - X 050 Electrical Catalog CA needs inbox tar.exe → tools/tar.sh (bsdtar 3.8.8)
  - M 051 msi removed pre-existing empty folders on uninstall/rollback (data loss)
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
