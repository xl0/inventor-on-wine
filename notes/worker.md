# Worker guide

You are a worker on the Inventor-on-Wine project. You own one issue
(`issues/NNN-*.md`, your brief) and report to the coordinator (the agent that
spawned you). Read `CODE.md`, `issues/README.md`, your issue file and the
relevant `notes/wine/*.md` first.

## Rules
- Clean-room: never disassemble/decompile Microsoft binaries; observe Windows
  black-box only. Third-party app code (Autodesk etc.) may be disassembled:
  `tools/decomp.sh BIN funcs|decomp|xrefs|strings|imports` (Ghidra, cached
  per binary; refuses Microsoft binaries — don't work around the guard).
  Running Microsoft redistributables is fine (app-local copies the app ships,
  redist installers, winetricks): keep prefixes faithful to a real install.
- Upstreamable Wine style: match surrounding code, smallest correct diff, no
  speculative fallbacks. Tests only for what realistically regresses; they
  must pass on the VM and on Wine (`todo_wine` only for deliberate leftovers).
- Commit finished work on your `fix/NNN-*` branch, Wine-style subject
  (`ntdll: Do X.`), no Co-Authored-By / attribution lines.
  Body: briefly state the motivation where it isn't obvious from the subject —
  what Windows does / what broke (name the app if an app needs it). Skip it for
  pure refactors and test-only commits when the subject says it all. The user approved
  this workflow; the git hook wants the approval quoted: prefix the command with
  `GIT_OK='worker commits on fix branches (user-approved workflow)'`. Never
  push, never commit outside your branch.
- Resources you don't own unless your brief says so: display :98,
  `prefixes/`, `build/`, the Autodesk installers. Never launch Inventor or click
  in the VM (Inventor is installed there; launching starts the 30-day trial).
  No package installs. Share the CPU: `make -j40`.
- Launch long-running Wine apps with `setsid nohup ... &`.
- The issue file is your memory: keep Status / findings current so another
  worker can resume from it. Put reusable, non-obvious Wine knowledge in
  `notes/wine/<topic>.md` (small targeted edits; others edit them too).
- Screenshots: add one when the screen state is the evidence (errors,
  rendering bugs, before/after of a UI fix). Crop to the window, save 256-colour
  PNG as `issues/attachments/NNN-short-name.png`, link it from the issue file.
  The repo is public: never capture Autodesk sign-in/account pages or anything
  showing email, license or machine identity.
- Brief statements are observations or suspicions; verify before building on them.

## Check the harness first
Before trusting a tool's output for a conclusion (screenshots, click coords,
exit codes, "rebuilt" binaries), confirm it on a known case once. Past traps:
squashed screenshots, `make dlls/ntdll` being a no-op, `%errorlevel%` always 0.

## Report, don't route around
- Broken shared tooling or infra (VM, winrun, display, docs): report it to the
  coordinator right away (SendMessage if you can, else in your final report)
  instead of silently working around it. A local workaround to keep going is
  fine, but say so.
- New Wine bugs outside your issue: don't fix them. Write a short draft
  `issues/NNN-slug.md` (take the next free number; symptom, evidence, repro if
  cheap) and mention it in your report. If it blocks you, report and stop; the
  coordinator gets it fixed and merged, then asks you to rebase.

## Build and test
- Worktree + out-of-tree build:
  `git -C wine-src worktree add ../wt/NNN -b fix/NNN-slug`, then in
  `wt/NNN-build`: `../NNN/configure --enable-archs=i386,x86_64 && make -j40`.
  Rebuild only what changed: `make -j40 dlls/ntdll/all dlls/kernel32/tests/all`
  (the `/all` matters; plain `dlls/ntdll` is a no-op directory target).
- Switching the Wine build used on an existing prefix triggers a prefix update;
  run `WINEDLLOVERRIDES="mscoree,mshtml=" <build>/wine wineboot -u` first, or a
  rundll32 error dialog hangs the next app launch.
- Prefixes with auto-start Autodesk services: on builds with 014 (services in
  session 0) wineserver idles once user processes exit; older builds hang on
  `wineserver -w` — use `wineserver -k` to be safe.
- Own prefix: `WINEPREFIX=$PWD/wt/NNN-prefix WINEDLLOVERRIDES="mscoree,mshtml="
  wt/NNN-build/wine wineboot -u` (own prefix = own wineserver).
- Conformance test on Wine: `wt/NNN-build/wine
  wt/NNN-build/dlls/kernel32/tests/x86_64-windows/kernel32_test.exe actctx`.
- Same exe on Windows: `WINRUN_ID=NNN vm/winrun.sh path/to/X.exe args`
  (own task + `C:\t\NNN`; `WINRUN_TIMEOUT` default 600 s, exit 124).
- i386 variants live in `.../tests/i386-windows/`.
- Regression check of touched modules: `tools/regress.sh run wt/NNN-build -o
  wt/NNN-regress -j 8 -m '^(ntdll|kernel32)$'`, then `compare` against the same
  filter run on `wt/regress-master-build`. Full runs are the coordinator's.

Before finishing: `wineserver -k` every prefix you started and kill your
Xvfb/Xorg servers by PID or display number (never `pkill Xvfb` / `pkill wine`:
other agents run their own). Beware `pgrep -f PATTERN` / `pkill -f`: the
pattern also matches your own shell's command line — match exact argv with
`ps -eo pid,args | awk ...` instead (unless the brief says to leave something running).

## Final report
Concise: result (root cause / fix), new Windows ground truth, commits, test
summary lines (VM + Wine), new issues filed, infra problems hit, open questions.
