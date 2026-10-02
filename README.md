# Autodesk Inventor on Wine

An experiment in agent-driven Wine development: get **Autodesk Inventor
Professional 2027** — a large, never-before-working CAD application — running on
Linux under Wine, by closing a tight loop:

> scripted scenario fails → triage traces → minimal repro / conformance test →
> ground truth on a real Windows VM → patch Wine → rerun the scenario.

The work is done by a coordinating Claude agent that drives the application,
files issues, and hands each issue to a separate worker agent (own git
worktree, own build, own Wine prefix). Workers write a Wine fix plus a
conformance test that must pass on both Windows and Wine; the coordinator
reviews, cherry-picks the fix onto an integration branch, rebuilds, and runs a
differential regression pass over Wine's whole test suite.

Wine patches live in the fork **[xl0/wine, branch `integ`](https://github.com/xl0/wine/tree/integ)**:
a linear stack of commits on upstream master (`git format-patch master..integ`).
This repo holds the harness, notes, issue write-ups and test programs.

## Status (2026-10-02)

- **Install:** Autodesk's own web installer installs Inventor 2027 + the
  2027.1 update under Wine, matching the Windows install (optional Electrical
  Catalog needs Windows' inbox `tar.exe` — provided by `tools/tar.sh`).
- **Launch & licensing:** Autodesk sign-in (OAuth via Edge inside the prefix),
  30-day trial, licensing services as real session-0 services; the trial popup
  (WebView2) shows content ~7 s after launch, like Windows (was ~17 s).
- **3D viewport:** renders like Windows; sketch rubber-band ~5 ms, orbit ~60 fps.
- **Modelling via the COM API:** 13 scripted scenarios pass; Autodesk's 2022 and
  2016 sample sets (up to 1,313-part assemblies) open, rebuild, save and reopen with
  the same results as Windows. Placing 200 occurrences 7.7 s (Windows 7.3 s);
  cold first part view ~8 s (Windows 6.3 s).
- **Long sessions:** 4-hour soaks without crashes; remaining memory/handle growth
  is Inventor's own (Windows grows as much); DWG export hang fixed.
- **Idle CPU:** Autodesk identity manager 14–21 % → ~3.5 % of a core (Windows 4 %)
  via Windows' default 15.6 ms timer granularity.
- **Window managers:** tested under openbox and awesome (the user's WM),
  incl. WM-driven moves and caption drags.
- **Known gaps:** a few cosmetic issues (popup shadows/splitter bars without a
  compositor); WebView2 animation costs more CPU than on Windows.

## Environment

One headless Linux server (120 threads, 1 TB RAM, 4× NVIDIA RTX 6000 Ada,
Ubuntu 22.04) accessed over SSH/VNC:

- **Wine** built from source (new WoW64, out-of-tree), `integ` branch.
- **Windows 11 reference VM** (QEMU/KVM, OVMF Secure Boot + swtpm, unattended
  install, OpenSSH, snapshots, virtio-fs share). `vm/winrun.sh X.exe` runs a
  test program in the VM's interactive desktop session and returns its output
  and exit code — the source of ground truth. The same Inventor is installed
  and signed in there as a behavioural reference.
- **Headless GPU displays:** rootless NVIDIA Xorg per GPU (`x/start.sh N BUS`,
  `UseDisplayDevice none`), openbox as window manager, x11vnc for watching
  (`x/vnc.sh N`). Two independent signed-in Inventor environments run in
  parallel (`:98`, `:99`).
- **Prefixes:** `inv` (real install through Autodesk's installer), `inv2`
  (copy for parallel work), `inv-vm` (transplant of the VM's install, used
  early to test Inventor before the installer worked), plus a pristine
  .NET 4.8 template. Prerequisites that Windows ships and Wine doesn't are
  installed from pinned official packages: WebView2 runtime, Edge (default
  browser), Wine Gecko.
- **Tools:**
  - `tools/invscen/` — Inventor COM scenario harness (C#, compiled in the
    prefix with .NET 4.8 `csc`; `run.sh [--vm] SCENARIO|all`).
  - `tools/regress.sh` — sharded differential runner for Wine's ~1750
    conformance test units (~4 min on 32 jobs), REAL/NEW/FLAKY classification
    against a cached upstream-master baseline.
  - `tools/decomp.sh` — Ghidra headless decompiler for third-party binaries,
    with a guard that refuses Microsoft code.
  - `tools/transplant.sh`, `tools/edge.sh` — prefix construction.
- **Process docs:** `notes/worker.md` (worker rules and build/test recipe),
  `notes/wine/*.md` (Wine internals learned along the way), `issues/NNN-*.md`
  (one file per bug: symptom, Windows ground truth, cause, fix, outcome).

**Clean-room rule:** Microsoft binaries are observed black-box only (no
disassembly); Autodesk's own code may be decompiled to understand what it
calls.

## What we ran into (and fixed)

About 115 issues so far; ~190 of our own commits on `integ` plus Wine-Staging's
DirectComposition series. Highlights, roughly in the order the application hit
them:

| Area | Problem | Fix |
|---|---|---|
| Installer bootstrap | SxS private assemblies found only via `<exe>.config` `privatePath` | ntdll: app config probing (001, 002) |
| Installer | `tasklist` printed an empty table where Windows prints an `INFO:` line; the installer greps for `:` | tasklist (007) |
| Installer | Package signatures: time-stamped signatures from an expired cert, AppxSignature.p7x blobs, RFC 3161 tokens, SHA-256 chains, partial chains, GeneralizedTime fractions, CMS attribute certificates, certificate tables past 2 GB | wintrust / crypt32 / imagehlp (008, 009, 011, 013, 016, 019) — incl. closing two verification holes |
| Installer | Binary registry hives (`regf`) and `RegLoadAppKey` | wineserver hive loader, adversarially reviewed (010) |
| Licensing | Go-based Autodesk services check that `services.exe` runs in session 0 | services in session 0 + follow-ups (014, 018) |
| Sign-in | WebView2 needs DirectComposition; Edge crashes, never starts sandboxed renderers, drops the OAuth `adsk.idmgr:` callback | Wine-Staging dcomp series + fixes (017, 023–027), AppContainer tokens (026), merged `HKCR` view incl. COM per-user classes (028, 030) |
| Rendering | Lost GPU content after expose; empty surface clip; colour-keyed layered windows; custom title bars covered by WM decorations | win32u / winex11 (005, 006, 027, 040) |
| COM API | Typelib type offsets beyond 32 KB read as negative; typelib loading quadratic (COM 10–500× slower) | oleaut32 (031, 032) |
| Stability | Crash in `RevokeDragDrop` releasing another process's pointer | ole32 (034) |
| Rendering | Wine's HLSL compiler put `static` globals into effect constant buffers; d3d11 never reported displayable formats, so no swapchain | vkd3d-shader, d3d11 (037) |
| Performance | Fresh 52 MiB GPU allocations per buffer discard (sketch latency 48 → 5 ms) | wined3d-vk (060) |
| Stability | Deferred-context creation raced with state resets; C++ exceptions escaping COM calls; comctl32 subclass list corruption | d3d11, combase/rpcrt4, comctl32 (047, 056, 046) |
| Long sessions | Window handle generations past 0x7fff produced two different 64-bit HWND values → MFC lost track of windows → save failures and crashes | wineserver, win32u (059) |
| Files/installers | POSIX delete semantics (Windows 10+); MSI rollback deleting pre-existing folders; OLE Packager objects | kernelbase/server, msi, packager (054, 051, 053) |
| Files | `C:\dir\con.iam` treated as the console device (Windows 11 rules changed) | ntdll (035) |
| File dialogs | No folder names in the path bar; clicking a file did nothing | shell32 (041, 043) |
| COM performance | ~500 wineserver round trips per cross-process call (hooks, GetProp, registry, GetWindow) | win32u/server/combase caches, QI shortcuts (057, 081, 097) |
| COM correctness | Event leaks, rpcrt4 connection per interface, lost wakeup in RpcServerUnregisterIf (DWG export hang), cancelled-call use-after-free, STA shutdown with queued calls, 64-apartment limit | combase/rpcrt4 (086, 087, 099, 109, 110, 112, 114, 116) |
| Timing | Waits returned early (coarse clock), no Windows timer granularity | ntdll/win32u/server (088, 089, 093) |
| Loader | Lookups waited for other threads' DllMain; TLS setup asked the server about every thread | ntdll (092, 098) |
| WebView2/Edge | GPU-process crash loop, invisible cursor, missing AppContainer APIs, SECURITY_CAPABILITIES, slow first content | dcomp/d3d11/dxgi, win32u/winex11, userenv/kernelbase/advapi32, ntdll (085, 090, 094, 095) |
| Window management | Caption drag under awesome did nothing; WM-started moves sent no size-move messages; black trails after drags | win32u/winex11 (061, 076, 077, 078) |
| Wine's own test suite | Flaky units (desktop close race, display-mode race, debugger inheritance, signal handling on host threads) | server/win32u/ntdll (101, 102, 104, 105) |

Several "bugs" turned out to be harness artifacts (a squashed screenshot
format, `xwd -root` drawing Wine windows black under a WM, a busy-loop that
Windows has too) — which is why workers must validate their tools on a known
case before trusting them.

Every fix comes with a conformance test checked on the Windows VM and on Wine,
and every `integ` update gets a full differential run of Wine's test suite
(so far: no real regressions).

## Layout

See `CODE.md` for the detailed layout and host notes, `PLAN.md` for the plan,
milestones and issue index, and `issues/` for the individual write-ups.
