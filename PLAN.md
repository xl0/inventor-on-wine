# Plan: Autodesk Inventor on Wine, agent-driven

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
- [ ] Wine bugs: one file each in `issues/` (index below). Coordinator drives
  Inventor and finds bugs; workers fix one issue each in a wine-src worktree.
  - 001 SxS app config privatePath probing — merged
  - 002 QueryActCtxW USE_ACTIVE fallback to process context — merged
  - 003 installer "hang" — not a Wine bug (our x/shot.sh was wrong; spin is Autodesk's, same on Windows)
  - 004 CreateProcess fails on missing manifest dep — fixed but PARKED: needs
    built-in assembly audit first (else breaks exes Windows starts)
  - 005 lost GPU window content after expose (win32u) — merged
  - 006 winex11 empty surface clip = no clip — merged
  - 007 tasklist no-match output (installer Error 101) — merged
  - 008 wintrust p7x blob verification (installer Error 4005) — merged
  - 009 wintrust SHA-256 chain check + RFC 3161 token verification — merged
    (build/ rebuild pending: 010 worker uses it)
  - 011 crypt32 partial chains + AUTHENTICODE_TS policy — merged
  - 012 crypt32 base policy lacks basic constraints check (low)
  - 010 regf hive loading + RegLoadAppKey (installer Error 4000) — merged
    after adversarial review (4 bugs found+fixed, folded in)
  - 013 crypt32 array decoder skip bug (exposed by 009; .NET runtime
    Error 15) — merged
  - 014 services in session 0 (Go-based Autodesk licensing service) — merged
    after review (1 regression found+fixed: cross-session device broadcasts)
  - 018 session-0 follow-ups (global locks, wineboot SeTcb, WTS sessions,
    Service-0x0-3e7$ winstation) — merged
  - 015 ServicesPipeTimeout read as DWORD — fixed with 014 (default kept 10 s)
  - 017 dcomp: Wine-Staging DirectComposition series (66 patches, CodeWeavers)
    + our cross-process target fix — merged; WebView2 renders the licensing
    page in inv-vm. Sign-in window itself still black (017 worker on it)
  - 023 dxgi Present1 dirty rects on flip swapchains — merged
  - 024 wofutil WofSetFileDataLocation stub crashed Edge setup — merged
  - 025 SC_MAXIMIZE on maximized hidden window showed it (Edge) — merged
  - 026 Edge sandbox: CreateAppContainerToken/NtCreateLowBoxToken — merged;
    --no-sandbox dropped from tools/edge.sh + prefixes/inv HKCR (inv-vm HKCR still has it).
  - 027 GPU child content hidden in colour-keyed layered windows — merged
  - 028 HKCR lacks merged HKCU\Software\Classes (Edge drops adsk.idmgr: OAuth
    callback; workaround: copy scheme keys to HKLM\Software\Classes) — fixed
    (full merged view, kernelbase) — merged with 030 as one series.
    HKLM-copy workaround removed from prefixes/inv (merged view resolves it)
  - 031 oleaut32 typelib typedesc offsets > 32 KB (Inventor parts/assemblies
    via COM) — merged
  - 032 typelib load quadratic + PSDispatch loading typelibs (COM 10–500x
    slower) — merged; COM now ~VM speed (asm 163 s → 4 s)
  - 034 Inventor crash: RevokeDragDrop from another thread/process released a
    foreign pointer — merged (build/ rebuild pending). Pending: apply the
    crash-capture .reg (issue 034) to inv/inv2 when Inventor is stopped
  - 035 DOS device names in full paths (Win11 rules; con.iam save) — merged
  - 036 "black" Inventor dialogs — not a Wine bug: `xwd -root` artifact (Wine's
    per-process colormaps); x/shot.sh now uses a plain XGetImage, dialogs render
  - 037 3D viewport: offscreen render edges-only, stale Home page — in progress
    (+ second Inventor env inv2 on :99)
  - 038 NTFS alternate data stream name syntax unsupported (low)
  - 030 COM reads merged per-user classes (not for elevated) — merged
  - 033 32-bit per-user class redirection relies on deletable marker keys (low)
  - 031 oleaut32 MSFT typelib typedesc offsets > 0x7fff read as negative: Inventor API
    PartDocument.ComponentDefinition / ComponentDefinition.Occurrences broken — blocks
    part + assembly modelling via COM (tools/invscen part/asm) — open
  - 032 typelib marshaler re-parses Inventor's 4 MB typelib per new dispinterface
    proxy/stub (~2.2 s each; perf) — open
  - 029 winex11: hidden managed windows stay mapped without a WM (waits for WM_STATE
    forever; trial welcome dialog leaves a white box on :98) — infra: run openbox on :98
  - 022 Edge GPU path queries undocumented dcomp interface → 30 s GPU-process
    retry delay before software fallback (low; clean-room blocks implementing)
  - 016 crypt32 GeneralizedTime with 1-2 fraction digits (VC++ redist /
    aspnetcore time-stamp tokens; installer Error 15) — merged
  - 019 imagehlp certificate offsets > 2 GB (2027.1 update exe) — merged
  - 020 Genuine Service MSI 1603 in killBeacon (non-blocking, low)
  - 021 whole-file mappings for large PEs in 32-bit (low)
  - uninvestigated: AceInvAddIn-ca.msi 1603 (Electrical Catalog Browser,
    optional, now unticked); TrueView failed only via .NET Error 15 (013)
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
