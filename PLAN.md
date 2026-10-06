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

## State (2026-10-05, wrap-up; nothing is running)
The user asked to wrap up: finish what was started, no new issues. All workers and reviewers are done.
Everything is stopped (user, 2026-10-05): the four prefixes, their Xorg / VNC / openbox, the Windows
VM (guest shutdown), the Wayland session; the Linux VM was not running. To resume:
`tools/prefix.sh start inv` (inv2, inv3, inv4); `vm/run.sh`, `vmwl/run.sh`, `x/wayland.sh` when needed.

**integ** (wine-src, fork remote `gh`): pushed tip da01d16c73b (wine-11.18-576) = `build/` =
`build-next/`; all four prefixes are set to `build/`. Final round on that tip: full regress 0 real of
1757 vs the master baseline (2 flaky timeouts: amstream, d2d1), Inventor suite 13/13, dwgloop
10/10, uilat PASS. Known gcc 15 false-positive warning (`clip`, win32u/input.c) still there.
Since b5d75449ffe (what the user tested on the laptop: "did not really run into any issues"), all
reviewed (independent Opus review each; follow-ups from the reviews included):
- 171 win32u window-surface registration race (counter on the surface).
- 173 NtUserDrawIconEx vs winex11 window data deadlock, BadWindow on replaced X windows.
- 157 winewayland lock order (+ part of 170: no buffers on role-less surfaces).
- 175 owned windows follow their owner's desktop (splitter outline left on the old awesome tag);
  no window opacity of exactly 0.
- 181 GPU-presented child windows after move/resize (swapchain recreation, moved bits, presented
  flag, DC invalidation, flush after image blits). GDI driver version 113.
- 182 X resource id race (libX11 bug; XLockDisplay around id-allocating requests + pre-sync),
  184 position lost after a cross-thread X window recreation.
- 177 Xlib hang (libX11 bug; XESetError hook handles expected/ignored errors before _XError
  takes the user lock).
- 191 XIC destroyed from another thread (flag, owner recreates) + ToUnicodeEx keeps the window
  data locked.
- 174's jump-back: after a WM-driven resize the window returned to an earlier size of the drag
  (4 commits: _MOTIF_WM_HINTS notify posts the state change; no re-request of an unchanged Win32
  rect; the same inside a state update; only for mapped windows with a WM_STATE).
Inventor checks on the server for 173 / 175 / 181 passed (details in the issue files).

**Open, user-facing**
- 174: the user's persistent stale picture after an awesome Mod+mouse resize ("hover redraws
  icons in black areas, another resize restores") did NOT reproduce on the server (awesome +
  picom, 144 DPI, NVIDIA; build/ and integ, ~185 drags). A ~5 s wrong picture after the first
  large grows of a session was only ever seen while the screen was being sampled after release
  (probably a measurement effect; unproven). Next step on the server, 15 min: same sequence with
  and without the sampler in fresh sessions; if only sampled runs are slow, look at Xorg /
  awesome delivering the final ConfigureNotify late. From the user: retest on the new integ; if
  it still happens, the data list at the end of issues/174. Leads from the review: draft 194
  item 1 (Inventor's own restore button under awesome 4.3 never un-maximizes the X window) and
  item 2 (stale _NET_WM_STATE request), draft 186.
- For the user to file upstream (texts, reproducers, patches in issues/attachments/): libX11
  182 (as a comment on xorg/lib/libx11 issue #10), 177, 190, 191. Trackers could not be searched
  from the sandbox: check for existing reports first.
- Laptop retests still owed from earlier rounds: see the first section of this file.

**Held on branches (not merged)**
- fix/191 commit 2 (675c332fddc, 190: mutex around XOpenIM etc.): exposes draft 193 (libXext
  frees its global XGE record when the last XInput2 display closes; a process whose main thread
  has no window dies 5 of 6). Merge only together with a 193 fix (untested one-liner:
  tests/r191/xge-keep-display-try.patch). Not reachable in Inventor.
- inst/177-review/v2-leaf-mutex.patch: optional variant for 177 (leaf mutex instead of
  LockDisplay); take it if upstream objects to Xlibint.h macros in winex11.
- Older: 091 cross-process geometry caches, 108 wined3d CS adaptive spin, 092 lock-free
  GetProcAddress (widens the 048 race), 105 ProcessDebugFlags setter. Kept old tips: fix/*-v1.

**Queue (drafts unless noted; nothing started)**
- X11: 194 (window state leftovers from the 174 review), 186 (overlapped sibling not
  invalidated), 187 (GL first present after show lost), 188 (BitBlt from a window DC while the
  surface is replaced), 189 (direct GDI on a window not flushed; upstream d3cb94b543e), 176
  (window DC updated under a surface lock), 183 (set_window_text UAF, by reading), 193, 192 (GLX
  opengl unit fatal X error; exit 137 after DONE on NVIDIA), 178–180 (minimize / restore under
  awesome / openbox), 142–151, 156, 158–163. Not fixed: D3D9 + Vulkan on lavapipe after a
  resize; a splitter drag across a tag change stops; one slow rubber-band session after WM swaps
  (71–87 fps, not reproduced).
- Wayland (second priority; the user doesn't use it): 167 (in-process GL client surfaces ignore
  z-order: blocks multi-document work), 170 remainder (0–2 ms hide/show fatal), 185 (mutter
  role flips), 166 (Vulkan stalls on the host session → renderer=gl), tooltips under sinks, keys
  to a dialog in another thread/process, 136 (popup placement), 127–129.
- UI pass 3 (areas pass 2 missed: pattern/section/measure, interference, .ipn, section/detail
  views, balloons, parts list, print preview, docking/floating, tile/cascade, Frame Generator,
  Tube & Pipe, Cable & Harness, Weldment, Stress Analysis, Add-In Manager, Customize, Styles
  editor, Content Center, Pack and Go).
- At the next rebase of integ: initialise `clip` in win32u/input.c (gcc 15 false positive,
  41d9173ca57); fold / reorder the 174 and 191 commits as the reviews suggest for upstream.
- Low priority: 117, 121 (HiDPI layout), 113, 042, 069 (VM lacks Content Center), 080, 082, 096,
  103, 126. Golden prefix + `prefix.sh reset` (proposed). virgl in vmwl hangs every Wine process
  (not investigated). An X11 session mode for vmwl/ (offered; marginal benefit).
- Later / another session (user): MCP server for Inventor on this setup.

**Standing facts and decisions**
- Licence: ONE active device (server, each VM, laptop each count; all host prefixes are one
  device). The user holds the seat while testing on the laptop: no Inventor on the server then
  (run.sh refuses leased prefixes; workers stop on any licensing dialog, click nothing). No
  Inventor in a VM without the user's decision. Trial started 2026-09-27 (about 3 weeks left).
  Declined: cracked Inventor; cloning the host's device fingerprint into a VM.
- Reviews: every non-trivial fix gets an independent Opus adversarial review before merge
  (they found real bugs in nearly every fix this round); a rework that is the reviewer's own
  tested variant is read by the coordinator and merged. Sonnet for UI/test/chore workers.
- Rounds: fixes are cherry-picked onto local integ, built in build-next/, full regress vs the
  master baseline of this host (deps/regress/4e819f054dd…-h26; full runs under load show a few
  flaky units — rerun before calling one real; user32:win is run-dependent) + Inventor suite
  13/13 + dwgloop, then push, then build/.
- Perf method: single suite sums are noise; claims need interleaved A/B on one prefix at low
  load, pinned cores. Regress and builds pin to CPUs 20-59,80-119.
- Lock order rules (notes/wine/window-surfaces.md, xlib-locking.md): winex11 = client
  surfaces_lock → win_data_mutex → user lock → surface mutex → leaves (the flush's
  try_set_window_hidden stays a trylock); display user lock only around id allocations, never
  across a reply wait; an XIC belongs to its owner thread. winewayland = win32u's locks first.
- Environment (details in CODE.md): Ubuntu 26.04 host, build deps from tools/sysroot.sh (env
  required), big images in /data/users/xl0/wine, VMs shared between workers (never restart),
  the sandbox launcher and AppArmor overrides are the user's. Deletions via tools/del; kill by
  PID only (self-matching ps/pgrep patterns bit four workers); never export WINEDLLOVERRIDES;
  awesome-client goes over the shared session D-Bus name (reaches another worker's awesome).
- Not Wine bugs (closed): 048, 066, long-session memory/handle growth, long-path SaveAs.
- 089 per-process timer resolution on by default; 062 scoped to compositor-only.

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
