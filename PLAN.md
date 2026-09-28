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
- [ ] Inventor 2027.1 install (web installer, no optional components)
  - VM: installed, was finalizing when paused. Do NOT click Start/launch
    (starts the 30-day trial).
  - Wine (build/ integ 77645e2b221, optional components unticked): base
    product INSTALLED (all core packages, Summary.log 01:34); the bundled
    2027.1 update fails (WinVerifyTrust NOSIGNATURE on the 3.3 GB update exe,
    cert table past 2 GB) → "Installation incomplete" → 019. Dialog left open.
    Autodesk Genuine Service msi 1603 in custom action killBeacon, ODIS treats
    it as success (uninvestigated).
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
  - 004 CreateProcess should fail (14001) on missing manifest dependency (low)
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
  - 018 session-0 follow-ups (low)
  - 015 ServicesPipeTimeout read as DWORD — fixed with 014 (default kept 10 s)
  - 017 dcomp is stubs → WebView2 GPU process dies → sign-in blank (inv-vm)
  - 016 crypt32 GeneralizedTime with 1-2 fraction digits (VC++ redist /
    aspnetcore time-stamp tokens; installer Error 15) — merged
  - 019 imagehlp certificate offsets > 2 GB (2027.1 update exe) — open, blocks
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
- Coordinator mistake: `git add -A` swept a worker's in-progress edit
  (decomp.sh without msbin.py) into a commit. Stage explicit paths only while
  workers are editing the project repo.
- 011 also left a gap unfiled (basic constraints; filed by coordinator as 012).
- 006 noticed two possible bugs (cross-process child swapchain doesn't update
  the owner's clip until SetWindowPos; R/B swap on lavapipe) but didn't file
  drafts as the guide asks.
