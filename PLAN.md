# Plan: Autodesk Inventor on Wine, agent-driven

Intent: get Inventor working under Wine by closing a tight loop:
scripted scenario fails → triage traces → minimal repro / conformance test →
ground truth on a real Windows VM → patch Wine → rerun scenario.
Upstreamable fixes preferred; a patched fork is an acceptable first deliverable.
Clean-room: black-box observation of Windows only, no disassembly of MS binaries.

Milestones (Inventor): install → licensing service → sign-in → main window →
new part → sketch/extrude → save/reopen → assemblies → drawings → export.
Drive via Inventor COM API where possible, screenshots only for UI/render checks.

Licensing: 30-day trial per Autodesk account — don't launch Inventor (starts
the clock) until the harness is ready. Read-only mode may remain after expiry.

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
  - Wine: original web installer (inst/webinstall) runs on integ, UI renders;
    at the first page. Next: install location → install.
- [ ] Wine bugs: one file each in `issues/` (index below). Coordinator drives
  Inventor and finds bugs; workers fix one issue each in a wine-src worktree.
  - 001 SxS app config privatePath probing — merged
  - 002 QueryActCtxW USE_ACTIVE fallback to process context — merged
  - 003 installer "hang" — not a Wine bug (our x/shot.sh was wrong; spin is Autodesk's, same on Windows)
  - 004 CreateProcess should fail (14001) on missing manifest dependency (low)
  - 005 lost GPU window content after expose (win32u) — merged
  - 006 winex11 empty surface clip = no clip — merged (build/ not yet rebuilt)
  - 007 tasklist no-match output (installer Error 101) — merged
  - 008 .adix package reader fails (installer Error 4005)
- [x] Integration branch `integ` in wine-src (001, 002, 005 merged); `build/`
  runs it. Tracks upstream master tip: periodically recreate integ on current
  master + open fix branches, rerun tests + the Inventor steps.
- Later: virtio-fs cached-read flakiness (newer QEMU/viofs? guest debug log),
  GPU passthrough (needs intel_iommu=on), data disk on spare NVMe,
  wined3d d3d11 suite crash on NVIDIA headless (only if it bites real apps)

## Process (agreed with user)
Coordinator drives Inventor, files issues, spawns one worker per issue
(briefed by `notes/worker.md` + the issue file), reviews, merges into `integ`,
rebuilds `build/`, deletes merged worktrees (branches kept). Workers report
new bugs as draft issues and infra breakage instead of routing around it.
Disassembling third-party (Autodesk) code is OK; Microsoft code never.
Review again after the next batch of workers. Notes so far:
- 006 noticed two possible bugs (cross-process child swapchain doesn't update
  the owner's clip until SetWindowPos; R/B swap on lavapipe) but didn't file
  drafts as the guide asks.
