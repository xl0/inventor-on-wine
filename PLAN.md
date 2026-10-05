# Local workstation: interactive verification

Keep the working Wine-only setup ready for user-driven testing. Implementation
and further diagnosis are handed to the main agent. Preserve the private crash
evidence and matching binaries; keep system Wine and unrelated prefixes untouched.
The server results below are reference material, not local completion status.

- [x] User reports "Everything looks good" on `b5d75449ff` after checking
  sketch text. Treat this as interactive feedback, not a scripted regression run.
- [ ] Investigate [174](issues/174-mod-resize-stale-regions.md)'s persistent
  main-window resize artifacts. Screenshot attached; debugger is not holding a fatal exception,
  and picom's known-good settings remain active. Confirmed Mod+mouse resize:
  hover redraws icons in black areas, and another resize restores the window.
  Investigate missed repaint/invalidation rather than assuming an app hang.

- [ ] Verify [124](issues/124-open-dialog-resize-coreclr-crash.md)'s merged fix:
  Open dialog, previews, breadcrumb navigation and Awesome Mod+mouse resizing.
  Matching old binaries are archived privately; new crash capture is armed.

- [x] CJK comparison and screenshot handed off in
  [123](issues/123-cjk-fallback-via-fontconfig.md).
- [ ] Verify [125](issues/125-format-text-preview-cjk-richedit.md)'s merged fixes:
  Chinese preview in the default font and re-editing without tiny dots.
  Wrapping follows the sketch text-box width; orientation remains unverified.

**Licensing:** one user-requested local Inventor session is running.
Coordinate the shared single-device seat; do not start additional instances.

- [x] Dedicated Inventor 2027.1 prefix installed, including native .NET 4.8,
  Gecko, pinned Edge/WebView2 and isolated user folders. Interactive document
  and sketch use reached.
- [x] Built `b5d75449ff` with local memory limits; 32/64-bit console and Vulkan
  readback smoke pass. Launched Inventor for interactive testing.
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

## User feedback on integ b5d75449ffe (laptop, X11, 2026-10-04) and what was started
"In my quick testing on X11 I did not really run into any issues." Two reports after that:
- 174 (Opus worker, wt/174, display :98 / inv): after an awesome Mod+mouse resize parts of the main
  window keep the OLD layout (ribbon width, browser pane height, status bar) with black/stale areas
  until hover or the next resize; the viewport does resize. Hypothesis to test: the size-move end
  (077/130) is detected before the WM's last ConfigureNotify, so the app finishes its size-move at
  an intermediate size. Regression-or-old unknown.
- 175 (Opus worker, wt/175, display :100 / inv3): moving Inventor to another virtual desktop
  (awesome tag) leaves the splitter popup's outline on the old one (owned layered popup not
  following its owner; transient-for / _NET_WM_DESKTOP / managed-or-not to be established).
Both work with probes (awesome + picom on the headless NVIDIA displays; a WPF probe in a copy of
the Inventor-free .NET prefix) and must not start Inventor until the user says the licence seat is
free again — then tell both workers (SendMessage) so they do their Inventor checks.

- 173 done on fix/173 (5 commits, tip 1b6964ae62d), in adversarial review: upstream bugs —
  NtUserDrawIconEx drew with the user lock held (deadlock vs winex11's window data; repro hangs
  10/10 on integ, 8/9 on master, 0/50 on the fix); BadWindow deaths when another thread's
  set_window_visual replaced the X window (PropertyNotify handlers and SetWindowText: 0 of 55 on
  the fix, up to 10/10 before); a xinerama/display inversion. With 171 on top the lock graph has no
  cycle (debug build, stress + an Inventor suite 13/13); the flush's posted retry takes no user lock
  (forced ~11000 times). Regress units 0 worse, 077 table identical. NOT done: Inventor on the
  plain fix build (suite, fps A/B, drag/resize) — the server got "Device limit reached" (the seat
  is on the laptop); nothing was clicked in that dialog.

- 171 review done: the race fix works, but commit 1's new edge (user lock → dce.c surfaces_lock)
  closes real deadlocks (a 3-thread probe hangs 10/10 on fix/171, 0/10 on integ: the flusher holds
  the list lock while waiting for surface mutexes whose holders take the user lock — e.g.
  UpdateLayeredWindow with a window DC as source) and stalls every USER call behind flush passes
  (GetWindowLongW up to 1.3 s). Decision: rework to the reviewer's variant A — a registration
  counter on the surface, registration stays outside the user lock (13 lines); bounded waits in
  the test. Worker applying. Reviewer's drafts: 176 (upstream: win32u updates a window DC with a
  surface locked → user → surface → user cycle on one window), 177 (Xlib: _XError in XSync from
  thread detach vs a thread holding the display lock in create_shm_image; hangs in 5-30 % of
  loaded stress runs on every build) — both X11-relevant, to be handed out after the 173 review.

- 157 verified on its final tip (integ + 7 commits, 750bb757812): VM GNOME/KDE/sway rapid +
  lockstress with pointer/key jitter 0 hangs, 0 protocol errors; wl_xowner 26/26 on KDE and GNOME;
  host session: roleflip + jitter 8/8, GL child visible after flips 5/5, first frame after show
  5/5, 132's clip/flip identical to integ, sink stress 6/6, debug build: only allowed lock pairs.
  In a focused re-review of the changed design (surface serial instead of the role-change gap).
  170: hides ≥ 5 ms fixed by the last commit; a 0-2 ms hide/show stays fatal — a wl_display.sync
  roundtrip before get_xdg_surface does not help (5/5 fatal), so a deferred show wouldn't either;
  remaining routes: a new wl_surface per show (needs re-homing of subsurface popups) or keeping the
  xdg objects of hidden windows.

- 175 fixed on fix/175 (2 commits, tip 764362956c3), in adversarial review; Inventor not checked
  (seat). Why the splitter is a window: `Autodesk.Inventor.InvDockUI.PaneBorder`, a WPF Window over
  each MFC pane divider (Inventor's frame is MFC); WS_SYSMENU is WPF's default, WS_POPUP and
  WS_EX_TOOLWINDOW are set by Autodesk; winex11's "popup with sysmenu == caption" rule (2007) makes
  it a managed client. The outline = awesome's 1 px border: awesome 4.3 doesn't copy an opacity of
  exactly 0 that is set before it manages the window (062's hiding never reached its frame).
  awesome never moves transients on a tag change (openbox moves the whole tree). Fix: winex11
  requests the owner's _NET_WM_DESKTOP for managed windows in its owner chain (EWMH; covers dialogs
  and floating panes too); opacity 1 instead of 0. "Unmanaged" was rejected: override-redirect
  windows are on every desktop and above every app. Probes: 0 left behind in 20 moves (base: all).
  Drafts: 178 (awesome: app-side restore after minimize leaves the owner iconic), 179 (openbox
  desktop switch minimizes Wine windows), 180 (win32u doesn't hide owned windows on minimize the
  way Windows does). Meets fix/173 in event.c (one handler).

- 174: no root cause yet, no fix. Ruled out with probes on :98 (awesome + picom, 96/144 DPI):
  the size-move ordering hypothesis (EXITSIZEMOVE before the last size: 2 of 200, harmless), any
  regression from 077/130 (three builds behave alike), GDI and software-WPF panes (0 stale in ~600
  resizes). Inventor's ribbon WPF is probably software-rendered. NEEDS either ~10 minutes of
  Inventor on the server (seat) or data from the laptop while the window is stale: `xwininfo -root
  -tree`, `tests/r174/wstate.exe "Autodesk Inventor" tree`, DPI settings, whether hovering the
  viewport alone repairs anything (list in the issue). The exact 2/3 and 5/6 width boundaries in
  the screenshot (= 96/144, 120/144) hint at a DPI path; not excluded.
- 181 (Opus worker, wt/181, display :98): three defects of GPU-presented child windows found by
  the 174 probes — grown strip garbage after the first present following a resize (10/10), a moved
  pane not shown at its new place, dirty-rect presents invisible until an X event arrives (7/10).
- Harness: run.sh refuses a prefix leased to another holder (a run without INV= had started
  Inventor in another worker's prefix `inv`).

- 173 review done: commits 2, 3, 5 fine. Commit 1 (DrawIconEx without the user lock) trades the
  deadlock for wrong pixels — a bitmap can be selected into one DC only, and the user lock was what
  serialized all users of an icon's bitmaps: 4 threads drawing one icon → ~60-75 % bad draws
  (integ and Windows: 0), GetIconInfo copies wrong, partial images on DestroyIcon. Rework: keep the
  lock for memory-DC destinations, draw from private bitmap copies otherwise (reviewer's prototype:
  0 bad, deadlock still gone), add a threaded cursoricon test. Worker applying, plus drafts 182
  (`_XAllocID` assertion: two threads doing CreateCompatibleDC/DeleteDC abort the process on
  integ, 4 of 4 — could hit real apps), 183 (set_window_text use-after-free, by reading), 184
  (position lost after cross-thread X window recreation under openbox).
- VMs: both run scripts are now single-instance (a second start took sockets/pid file/TPM state
  from the running VM — the Windows VM died several times while workers shared it); worker rule:
  the VMs are shared, never restart or kill them.

- 157 re-review done: all six earlier findings closed, commits 1-6 merge as is. Commit 7
  (role-less surfaces) re-flushes a hidden layered window at every idle (810 ms CPU in a 2 s probe
  vs 0 on integ): report the flush as done + expose when the subsurface role arrives. Worker
  fixing; then merge (coordinator reads the delta). Draft 185: mutter 50.1 mishandles rapid role
  flips with a presenting GL child (Clutter criticals, child drawn on the owner; compositor-side,
  the driver's requests are legal).

- 171 reworked and ON INTEG (local, 3 commits → tip 818f1e8f9b6; not pushed, build/ not rebuilt —
  the struct gained a field and WINE_GDI_DRIVER_VERSION is 112, so the next rebuild is a full one):
  registration counter on the surface, registration stays outside the user lock; a surface set
  during destroy is released in free_window_handle; bounded user32:win test (5/5 on Windows x86 and
  x64). ulwrace race 200/200 (integ 27/30 fault), the review's deadlock probe 10/10 done, no
  USER-call stall (max 0.5 ms), 8 units 0 worse. Remaining hangs in the heavier stresses are 177
  (Xlib) and 176 (window DC updated under a surface lock) — upstream, drafts. On Windows the whole
  user32:win had 2 test_topmost failures in 20 fix runs (0 in 18 integ runs), unattributed: check
  before upstreaming. Diff read by the coordinator (the reviewer's own variant; no second review).
- Windows VM: qemu 10.2 segfaults within minutes when the virtio-fs device is attached (one
  worker's observation: stable for a whole session without it). vm/run.sh: the share is now opt-in
  (VFS=1); winrun.sh works over ssh and needs none.

- 175 review done: commit 2 (opacity 1) fine — on base an alpha-0 window under awesome + picom is
  drawn fully opaque, not only bordered; "unmanaged" rejection confirmed. Commit 1 must act only on
  a real change of the owner's desktop, move only owned windows that were on the owner's previous
  desktop (else a tool window parked on another monitor jumps to the owner's screen; a dialog moved
  alone is pulled back; multi-tag dialogs collapse), and follow child-window owners via GA_ROOT.
  Reviewer's tested variant v3; worker applying. Merge after (coordinator reads the delta).

- 173 reworked and ON INTEG (local tip b8f013d4fbb = b5d75449ffe + 171 (3) + 173 (6); not pushed,
  build/ still b5d75449ffe): NtUserDrawIconEx draws from private bitmap copies on non-memory DCs
  (lock kept for memory DCs) + a threaded cursoricon test (passes on Windows; the first version
  failed it with 34 % wrong draws); BadWindow fixes; xinerama order; trylock comment. Collision
  probes 0 bad, deadlock repro 50/50 clean, 10 units 0 worse, 077 table identical. Window-DC icon
  draws cost 25-60 µs instead of 14-16. Inventor not run on it (seat). Read by the coordinator.
  Root-caused drafts from it: 182 (libX11 bug: XID refill races when threads allocate ids on the
  shared gdi_display — winex11 creates a GC per memory DC; aborts the process), 184 (ConfigureNotify
  mapped through the old host parent after an X window recreation; one-line experiment fixes it),
  183 (set_window_text use-after-free, by reading; no symptom in a stress).
- 182 + 184 (Opus worker, wt/182): Wine-side fix for the XID race (it could abort real apps),
  proper fix for 184, an upstream libX11 report text for the user to file.

- 181 fixed on fix/181 (7 commits on b5d75449ffe), in adversarial review; all three defects are
  upstream's: (1) wined3d presented to the old-size Vulkan swapchain after a resize (now recreates
  on a suboptimal acquire) + GL on NVIDIA raced the client X window's resize (XSync after a size
  change); (2) move_window_bits copied only inside the window surface (now also on the host window
  for client-surface areas; a moved same-process surface is re-presented); (3) wined3d's DC from
  swapchain creation went stale, direct X drawing was never flushed (upstream d3cb94b543e), and
  our 061 re-present on Expose restored an older frame (now limited to the exposed region).
  Probes: 0 stale in 30-run passes, both renderers, ± picom (build/: 9-17 of 30, 10 of 10, 12 of 12
  bad). fps unchanged. Inventor not run (seat). Drafts 186 (Windows repaints a child that a
  resized sibling came to overlap; Wine copies bits — a lead for 174's stale panes), 187 (GL: first
  present after show lost in ~25 % of runs on both builds).

- 157 (+ the fixable part of 170) ON INTEG (local): 7 winewayland commits. Commit 7's re-flush cost
  is gone (role-less flush reported done + `contents_skipped`; the expose for a subsurface only
  when a flush was actually skipped — an unconditional one would commit an unpainted black surface
  for every new popup). Verified on the host session (hidden-window CPU 0 ms, glhide at 5 ms 14/14,
  first frame 5/5, roleflip + jitter 5/5, wl_xowner 28/28, 132 probes, 0 protocol errors); delta
  read by the coordinator. Open: 170's 0-2 ms hide/show (compositor-side timing), an unmanaged
  layered popup hidden and re-shown without a redraw stays invisible (integ too), draft 185 (mutter).
  integ local = b5d75449ffe + 171 (3) + 173 (6) + 157 (7) — not pushed, build/ not rebuilt.

- 181 review done (2026-10-05): no regression in the per-frame path (no recreation storm, no extra
  round trips, fps equal, validation layer clean, 119 units 0 worse); three commits fine. To fix:
  a moved GPU child that never presented shows VRAM garbage (re-present needs a `presented` flag,
  also for our 061 path); the XFlush in add_device_bounds misses pen primitives and costs 7-17x on
  direct GDI (flush after the image blits only; the general "direct GDI not flushed" regression
  from upstream d3cb94b543e → draft 188); clip left on hdc_dst after an Expose re-present; the
  internal DCX flag must not be reachable from apps. Worker applying and rebasing onto integ
  (171 + 173 + 157); merge after (coordinator reads the delta). Kept deliberately: one extra
  WM_PAINT for a moved GPU child (Windows sends none) — re-measure in Inventor.

- 175 reworked and ON INTEG (local): acts only on a real change of the owner's desktop, moves only
  managed owned windows that were on the owner's previous desktop, follows child-window owners via
  GA_ROOT; a followed window is recorded on the new desktop at once (two quick owner moves: 0 of 75
  left behind, 15-17 of 25 without that). Windows of other processes are still requested
  unconditionally. awesome ± picom 0 of 20 left behind; openbox: a second-level dialog once in 10
  (draft 179, not a missed follow). Minimize/restore under awesome: base 1 of 352 incomplete,
  patched 6 of 624 (p = 0.43; draft 178's race). Opacity commit unchanged. Inventor not checked.
- integ local = b5d75449ffe + 171 (3) + 173 (6) + 157 (7) + 175 (2) = tip ed0755113c0.
  build-next/ = wine-11.18-555-ged0755113c0; full regress vs the h26 master baseline: 1648 pass /
  80 fail / 28 crash / 1 timeout — the only difference is i386 amstream:amstream timing out in the
  loaded full run (8 of 8 pass in unit mode on build-next, build/ and master: load flake).
  Regress half done; the Inventor suite needs the seat, then push and rebuild build/.
  Still to come into this round: 181 (corrections), 182/184.

- 181 corrected and ON INTEG (local, 9 commits → tip d8e4d0f72d2; integ = b5d75449ffe + 27):
  `presented` flag on client surfaces (no re-present of a surface without an image; cleared when it
  grows or switches on/offscreen), flush only after image blits (GDI speed and Xorg CPU back at
  base; direct GDI primitives on such windows stay unflushed → draft 189), clip selected
  unconditionally (an upstream bug reproduced: fullscreen frame clipped to the former window rect),
  the DCX flag off the syscall, DC invalidation only when the pixel-format flag reaches the server.
  36 acceptance runs 0 bad (awesome ± picom, both renderers, 144 DPI), 119 units 0 worse, fps equal.
  Driver version now 113. Not fixed: D3D9 + Vulkan on lavapipe after a resize. Kept: one extra
  WM_PAINT for a moved GPU child (re-measure in Inventor). Regress of the new tip
  (build-next/ = wine-11.18-564-gd8e4d0f72d2): 0 worse of 1757 (1649 pass / 80 fail / 28 crash).

- 182 + 184 fixed on fix/182 (3 commits on b8f013d4fbb; cherry-pick cleanly onto d8e4d0f72d2), first
  version (the paragraph's end has the final state).
  182: cause confirmed with an instrumented libX11 (`_XLockDisplay` refills the XID, then may wait
  for a sequence sync with the display unlocked); Wine now takes XLockDisplay around every
  id-allocating request on shared displays (42 sites via helpers). Chosen over a driver mutex
  because NVIDIA's EGL allocates ids on gdi_display inside the library. dcchurn 60 of 60 (integ:
  46 of 60 abort); cost not measurable (5.95 vs 5.94 µs per DC). libX11 report + patch ready in
  issues/attachments/182-* for the user to file. 184: the owner drops the stale host parent at the
  first position event; lost positions under openbox 70 of 720 → 0 of 720.
  Not closed: 177 (reachable on plain integ too). New drafts: 188 (BitBlt from a window DC while
  another thread replaces the surface), 190 (libX11 XOpenIM double free when threads create their
  first windows together), 191 (XIC destroyed by another thread during XFilterEvent).
  Reviewed, reworked, ON INTEG (local, 5 commits → tip cffd27540ee; integ = b5d75449ffe + 32;
  first version kept as fix/182-v1). The review (inst/182-review/) required a pre-sync in
  lock_xid_alloc: Xlib's 65K sequence sync otherwise waits for its reply inside the user lock
  (the 177 shape with a new holder). Also: `synchronous` latched at init, get_dummy_parent
  re-checked under the lock. With the pre-sync Xlib's own sync never fires (56 / 485 → 0 / 0).
  Decision: user lock kept over the reviewer's hook-only variant (misses xcb-only bursts) and over
  a driver mutex (NVIDIA EGL hole). Accepted cost: next to a thread allocating ~250K ids/s on a
  CPU-contended host, another drawing thread's p99 goes 0.35–0.5 → 2.4–6.5 ms (Xlib's locks are
  not fair; quiet host: equal). An abort of the process is worse; revisit if a real app shows it.
  Weak spots left: Xcursor's first use does round trips under the lock; glXCreateWindow/Pbuffer
  and NVIDIA GL allocators are outside it. libX11 report is now a comment for xorg/lib/libx11
  issue #10 (same assertion, open since 2010) — the user's to file.
  build-next/ = wine-11.18-569-gcffd27540ee; full regress vs the h26 master baseline: 0 real
  regressions of 1757 (9 units differed in the loaded run — the 177 worker was building — and all
  are flaky on rerun on both builds: dxgi, mf, ntdll:change/exception, wininet:http,
  cryptowinrt).
- Seat back on the server (2026-10-05). On build-next (integ cffd27540ee): suite 13/13, dwgloop
  10/10 → integ PUSHED to gh (b5d75449ffe..cffd27540ee). inv, inv2, inv3 run on build-next
  (x/prefixes.tsv); build/ is still b5d75449ffe as the 'before' reference for the checks below —
  rebuild it and set the table back when the workers are done.
  Running: Sonnet worker on inv2 doing the Inventor checks 175 / 181 / 173 still owed; the 174
  worker on inv3 (does the user's resize problem still reproduce on the new integ? bounded).
  User: wrap up — finish what is started (177 review, 191, these checks), no new issues.
- 174, Inventor round on the server (awesome + picom, 144 DPI, Vulkan): the user's persistent stale
  picture did NOT reproduce, on build/ (0 of 72) or on build-next (0 of 50). Found instead: after
  a WM-driven resize the window jumps back to an earlier size of the drag when the application
  falls behind (build/ 9 of 34 throttled; probe frame.exe rgnpost 27 of 30, build-next 20 of 20
  plus a 1 px creep per resize). Cause (traced, upstream code): a SetWindowPos that moves nothing
  (update_window_state, SetWindowRgn) between the WM's resize and the posted state change makes
  sync_window_position request the stale Win32 rect. Fix on fix/174 (d09a8c4d1ce on cffd27540ee,
  6 lines: early return when Win32 didn't move the window and no request is pending) → 0 of 20,
  Inventor 0 of 26. In adversarial review (Opus, may use inv3). Known hole: the same inside a
  state update (4 of 2854 in a trace). Whether this explains the user's picture is unproven; the
  data list to collect on the laptop is at the end of issues/174. Fact: Inventor has one client X
  window (the graphics view); ribbon, browser, tabs are in the window surface.
- Inventor checks on build-next (Sonnet worker, issues 175 / 181 / 173 have the details):
  175 PASS (owned popups follow across awesome tags, 10 of 10 shots clean; tooltips, drop-downs,
  marking menu, dialogs on the right tag), 173 PASS (orbit 56.6 vs 53.3 fps, pan equal, 11 min
  mixed session clean), 181 PASS for final content, rubber band 117–119 fps on both builds,
  splitter drag 55–60 vs 47 fps. Extra WM_PAINT: not measurable with the existing tools, no fps
  effect. FOUND: under awesome + picom, transient wrong content after a resize (white viewport,
  duplicated ribbon fragments) for 0–4.2 s, worst healed by 12 s; without picom final after 0.6 s.
  Not compared with build/ → the 174 worker is measuring build dependence and where the content
  is late (bounded, inv2). This is now the main lead for the user's "rendering issues on resize".
  Open small items: one slow rubber-band session (71–87 fps after WM swaps, not reproduced);
  `uilat wmdrag` hits toolbar widgets in this layout; splitter drag across a tag change stops.
- 174 heal-time lead, state at stop (issues/174 "Heal time after a resize", "What the main thread
  waits on"): the ~5.0 s idle state after the first large grows of a session (Win32 window at an
  intermediate size, X window final, paints pending, then a burst) has only ever appeared while
  the screen was being sampled after release (heal.py: root image + the frame's composite pixmap
  every 0.1 s; the checks worker's heal.sh grabs continuously too). Without the sampler: 0 long
  cases in 53 + ~110 drags. gdb during the wait: main thread in Inventor's ordinary message pump
  (FwUI → NtUserWaitMessage), nothing queued in Xlib, winex11's rects settled — but no capture
  is a confirmed 5 s case. No 5 s constant found on this path in winex11 / win32u / server.
  So: probably a measurement effect (correlation, not proven), and the user's persistent picture
  is still unexplained. Next step if resumed (15 min): same sequence with and without the sampler
  in fresh sessions; if only sampled runs are slow, look at Xorg / awesome delivering the final
  ConfigureNotify late (xev on the frame, winex11 +event timestamps), not at Inventor.
  Not done: WebView2 panes closed, real input during the wait, build/ with this method.
  STOPPED here (wrap-up). From the user: retest on integ; if still seen, the data list at the
  end of issues/174.
- 177 fixed, reviewed ("merge as is", no defect found), ON INTEG (local tip b1e7b97cfed = pushed
  cffd27540ee + 1; not built/regressed yet: build-next is in use by the Inventor checks).
  Cause: libX11 bug, still in master (a thread already waiting in _XReply is let past a user lock,
  but when it reads an X error _XError waits for that lock; the holder's reply is queued behind
  it; introduced by libX11 83e1ba5, 2011). Wine side: an XESetError hook on every display handles
  expected/ignored errors before _XError takes the lock. gdistress openbox 74 of 80 → 78 of 80
  (rest is 191), +synchronous 0 → all; NVIDIA GLX stress 6 of 11 → 11 of 11; no measurable cost.
  Decision: reviewer's optional leaf-mutex variant (inst/177-review/v2-leaf-mutex.patch) not
  taken (pre-existing upstream race, larger diff); it is the upgrade if upstream objects to
  LockDisplay in winex11. Still open by design: errors Wine neither expects nor ignores, and a
  lock holder inside an Xlib call with async handlers (unreachable while cross-thread requests on
  thread displays stay under win_data_mutex). libX11 report + reproducer + patch in
  issues/attachments/177-* — the user's to file. Worker is correcting the texts per the review.
- 191 fixed, reviewed, ON INTEG (local tip 12ef0899477 = pushed cffd27540ee + 177 + 2).
  Cause measured: libX11's filter list is unlocked (XFilterEvent vs XUnsetICFocus/XDestroyIC),
  and even with a library patch a cross-thread XDestroyIC crashes in the local IM's filter → the
  XIC must stay in its owner thread. Fix: a non-owner flags the XIC (`xic_invalid`), the owner
  destroys/recreates it in X11DRV_get_ic. Review: "merge after fixes" — the fix alone made the
  ToUnicodeEx-from-a-non-owner path hit a freed XIC more easily (probe 5 of 16 → 26 of 32
  hangs); the reviewer's tested follow-up (ToUnicodeEx keeps the window data locked, 0 of 16)
  went in before it. +synchronous gdistress 5–7 hangs of 40 → 0; xicrace → 0; typing identical.
  build-next/ = wine-11.18-572-g12ef0899477: full regress 0 real of 1757 (1 flaky, d2d1 timeout
  on both), Inventor suite 13/13, dwgloop 10/10 → integ PUSHED (cffd27540ee..12ef0899477).
- 190 fixed on fix/191 commit 2 (675c332fddc: recursive mutex around XOpenIM / XCloseIM / font
  sets / IM callbacks; libX11 reallocs a process-wide IM list unlocked) — NOT MERGING:
  it staggers thread starts and thereby exposes draft 193 (libXext frees its global XGE record
  when the last XInput2 display closes; process whose main thread has no window: 0 of 6 → 5 of 6
  die). Decision: hold 190 until 193 has a fix (new issue, not in this wrap-up round); the
  worker's untested one-liner is tests/r191/xge-keep-display-try.patch. 190's base rate in Wine
  is low (2 of 70 with a main window); 193 is not reachable in Inventor.
  libX11 reports for 190 and 191 in issues/attachments/ — the user's to file.
  Correction: own libX11 builds under inst/177, inst/182 have no locale data → no XIM there.

## Now (2026-10-04, after the reboot)
Resumed: prefixes and X servers up on build/; the 157 worker, the 173 worker and the 171 reviewer
continue from the items below. The user is testing on the laptop: the licence seat may leave the
server at any time → workers do Inventor-free work first and skip Inventor checks on a licensing
error (no clicks). Background, as written at the pause:
`tools/prefix.sh start inv` (inv2, inv3, inv4) brings X and the prefixes up; `vm/run.sh` and
`vmwl/run.sh` for the VMs when needed. AppArmor: programs in the sandbox no longer pick up
per-binary profiles (user's local rules), bare `hostname`/`lsblk`/`who` work.
- integ = b5d75449ffe (pushed) = build/ (wine-11.18-537): verified, regress 0 worse vs the h26
  baseline, suite 13/13. Contains 130, 131, 133, 134, 140, 141, 165, 132.
- fix/157 (integ + 7 commits, tip 750bb757812): winewayland lock order + part of 170. Code done;
  verification of the final tip half done — remaining steps in the issue ("State at pause, review
  round") and in the coordinator's resume message: KDE batch, wl_xowner, debug-build pass, user32
  tests, host-session checks (first frame after show, 132's clip/geo/flip, sink stress, seq.c),
  issue table + draft 170 update; 170's 0-2 ms hide/show: first a throwaway roundtrip experiment,
  then maybe a deferred re-show. Then a short re-review (serial instead of the gap), merge.
- fix/171 (3 commits, tip 784c189d22e): win32u window-surface race, upstream bug. Fix complete;
  review only read so far (notes at the end of the issue): no cycle found in the combined 171+173
  lock order as long as winex11's `try_set_window_hidden` stays a trylock (pin with a comment);
  everything that needs a build/run, lifetime paths, cost, the test's INFINITE waits still open.
- fix/173 (3 commits, tip 6635a80202b): winex11/win32u deadlock (NtUserDrawIconEx), BadWindow on
  replaced X windows, xinerama/display inversion — all upstream. Remaining: full stress incl.
  ulwrace, debug build with 171 (wt/173-dbg has it cherry-picked and built; force the flush
  fallback), regress units both arches, Inventor on inv2, notes; then adversarial review.
- Then: merge round (157+170, 171, 173) with regress + suite, push; 167 (Wayland z-order of
  in-process client surfaces); UI pass 3; warning fix for 41d9173ca57 at the next rebase.
- Resume the same agents with SendMessage if this session survives, else new workers from the
  issue files. Two stray confined swtpm processes die with the reboot.

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
  165: upstream wined3d bug, fixed and on integ (a present queued for a window destroyed meanwhile
  left a stale Vulkan swapchain → fault in a Vulkan call → winevulkan exits the process from the
  command-stream thread → assertion at DLL detach; also resolves draft 150). New dxgi test passes
  on the Windows VM (both arches). Drafts 168, 169. Harness sees/kills a half-dead Inventor.
- Windows VM works on the new host: vm/run.sh runs swtpm from a copy in deps/ (the system binary's
  AppArmor profile only talks to libvirt peers). Its display is 1280x800 now, so dxgi's
  display-mode lines fail differently than in older VM logs (175 vs 58 failures).
  Two stray confined swtpm processes (3008138, 3008605) can't be signalled from the sandbox.
- integ = b5d75449ffe (pushed) = e00a74f6590 + 165 (2 commits) + 132 (4 commits, winewayland only);
  build/ = wine-11.18-537-gb5d75449ffe, all four prefixes on it. Full regress vs the new-host
  master baseline: 0 worse of 1757 (1649 pass / 80 fail / 28 crash); suite on inv 13/13.
- 157 fixed on fix/157 (5 winewayland commits on e00a74f6590): rule "win32u's locks first, the
  driver's window data last, then pointer/keyboard/text_input"; win32u calls moved out of the
  locked regions (styles/title, cursor clipping, client-surface updates on role change, cursor
  info, IME rect). Stress in vmwl: 0 hangs in 100 runs on GNOME/KDE/sway (unfixed: 18 of 20 on
  GNOME), 0 protocol errors, wl_xowner 26/26. Narrow races accepted and documented (title set from
  another thread during the role change; surface re-creation gap). Rebased onto integ b5d75449ffe
  (tip 7fb257635be; old tip fix/157-v1): 132's sink code obeys the rule (debug build + reading);
  stress clean again in the VM. Review back: reordering sound, 170/171 independent; one regression
  (role-change gap: pointer focus still on the window whose surface is NULL → relative-motion
  handler faults → process dies; reviewer's roleflip + pointer jitter 4/4 dead) and smaller items.
  Worker applying, auditing all event paths for that state, adding pointer/key input to the
  stress, and fixing 170 on the same branch (driver commits a buffer to the role-less surface).
- 173 (Opus worker, wt/173, inv2): winex11 deadlock found by the 157 reviewer on Xvfb with integ —
  X11DRV_WindowPosChanged holds winex11's window data and wants the user lock, update_visible_region
  → X11DRV_GetDC under the user lock wants the window data. X11 is what the user runs: find out
  whether upstream's or ours, table + rule + fix; also two BadWindow deaths in the same stress.
  A combined stress on the host's GPU session found two older bugs: 170 (protocol error "wl_surface
  already has a buffer committed" on a quick hide/show of a toplevel with a GL child; 6 of 6 on
  unfixed integ) and 171 (win32u, driver-independent: two threads changing one window's surface →
  NULL write in register_window_surface with dce.c's surfaces_lock held, the fault is swallowed
  and the process hangs later).
- 171 fixed on fix/171 (3 commits, tip 784c189d22e), in adversarial review: upstream bug (master
  faults 10 of 10). register_window_surface() now runs before release_win_ptr(), so the surface
  list changes under the lock that protects win->surface (new edge: user lock → dce surfaces_lock);
  a surface installed during destroy is dropped; user32:win test (passes on Windows). Probe: 46 of
  50 fault on build/, 199 of 200 clean on the fix (the one death is 173's BadWindow). Windows:
  cross-thread UpdateLayeredWindow is legitimate and immediate. Draft 172: a fault inside a win32u
  syscall is swallowed by design (handle_syscall_fault) — at most a WARN is possible.
  vmwl on the new host: no nested bwrap → one read-only virtiofsd per shared path (README).
- 132: M1 built on fix/132 (4 commits, winewayland only, +998 lines; all gates pass): the presenting
  process reads frames back into a shared section, the owner's Wayland event thread attaches them
  to a subsurface; handles pulled by the owner (no names, no server change), independent of the
  owner's message pump. On the new host the Wayland session is GPU-rendered (mutter and Wine's
  EGL on NVIDIA): WebView2's hardware path survives there (162 and 163 are Mesa/llvmpipe-only);
  Vulkan stalls in vkAcquireNextImageKHR on this session (draft 166), so Wayland = renderer=gl.
  Cost at 1678x884 on NVIDIA: ~0.9-4 ms per frame in the source, 3.75 ms compositor upload.
  Inventor on Wayland: Home page, trial popup and Assistant show content. NOT mergeable alone:
  remote surfaces aren't clipped by sibling windows, so the Home page covers open documents.
  M2 done (6 commits on fix/132): geometry and clipping come from the owner (two get_visible_region
  server requests per evaluation in the sink's event thread, none per frame; rect clip + hide),
  16 sinks per source pid. Inventor on Wayland: Home page hides behind an open document and returns
  on the Home tab; Assistant docked/undocked/closed; popup; resize/maximize; hello/part/asm/drawing/
  view pass. Reviewed (memory safety sound; a source could name a window it didn't own) and
  reworked: the first-contact message carries the client window, the sink derives the source pid
  from it, refuses its own process' windows and windows not rooted in the posted top-level,
  re-checks at every evaluation; 16 sinks and 1 GiB per source process; POLLOUT in the poll loop;
  wake socket drained; 8192 px cap. Only topology B is carried (a swapchain on another process'
  window stays blank on Wayland; Windows allows it — table in the issue). On integ.
  Remains: 167, tooltips under sinks, non-rectangular regions, Vulkan (blocked by 166), mutter-only
  assumptions (buffer destroy while attached, FLIPPED_180, scale 100 %).
  Found on the way, pre-existing → draft 167: in-process GL client surfaces ignore window z-order
  on Wayland (a part's 3D view stays on top of a drawing opened after it): blocks multi-document
  work; same family as 145 (tooltip under the viewport). Next Wayland fix after 157.
- Next: adversarial reviews for 157, 132 and 165 when they report; UI pass 3 (areas pass 2 missed);
  warning fix for 41d9173ca57 at the next rebase of integ; drafts not started: 142-151, 156,
  158-163 (see issues/).
- AppArmor (user, 2026-10-04): local overrides for the `hostname` and `Xorg` profiles
  (/data/box/apparmor-local-overrides.md): bare `hostname` prints again; Xorg servers need a
  restart to stop logging — :98, :100, :101 restarted, :99 (inv2) when the 173 worker is done (stop the prefix, kill that display's Xorg/openbox/x11vnc by PID, `prefix.sh start`).
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
