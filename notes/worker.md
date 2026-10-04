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
  No package installs. Share the CPU: `taskset -c 20-59,80-119 make -j40` (CPUs 0-19 and their
  hyperthreads 60-79 stay free for Inventor/UI timing; full regress runs pin themselves the same way).
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

## Tracing
perf works for your user (perf_event_paranoid=1) and ptrace is unrestricted
(ptrace_scope=0; strace -p / gdb -p on running processes). Attach only to
Wine/Inventor/Xorg processes of this project — never to anything else the user
runs (ssh-agent, browsers, editors, other agents).

## Check the harness first
(Screenshots: use x/shot.sh [display] — never `xwd -root`, which draws Wine
windows black under openbox with multiple colormaps. Input: prefer xdotool
clicks; after `xdotool key`, check `xinput query-state` for stuck keys.)
Before trusting a tool's output for a conclusion (screenshots, click coords,
exit codes, "rebuilt" binaries), confirm it on a known case once. Past traps:
squashed screenshots, black xwd -root windows (036), stuck XTEST keys, `make dlls/ntdll` being a no-op, `%errorlevel%` always 0.

## Report, don't route around
- Broken shared tooling or infra (VM, winrun, display, docs): report it to the
  coordinator (in your final report; SendMessage to "coordinator" doesn't reach it —
  if blocked, stop and report)
  instead of silently working around it. A local workaround to keep going is
  fine, but say so.
- New Wine bugs outside your issue: don't fix them. Write a short draft
  `issues/NNN-slug.md` (take the next free number; symptom, evidence, repro if
  cheap) and mention it in your report. If it blocks you, report and stop; the
  coordinator gets it fixed and merged, then asks you to rebase.

## Build and test
- The host has no -dev headers, mingw or bison: builds need the local package prefix in the shell,
  `eval "$(tools/sysroot.sh env)"` (or `tools/sysroot.sh run make ...`) before `configure` and `make`
  (CODE.md). regress.sh, prefix.sh, invscen/run.sh and x/start.sh load it themselves.
- Worktree + out-of-tree build:
  `git -C wine-src worktree add ../wt/NNN -b fix/NNN-slug`, then in
  `wt/NNN-build`: `../NNN/configure --enable-archs=i386,x86_64 && make -j40`.
  Rebuild only what changed: `make -j40 dlls/ntdll/all dlls/kernel32/tests/all`
  (the `/all` matters; plain `dlls/ntdll` is a no-op directory target).
- Switching the Wine build used on an existing prefix triggers a prefix update;
  run `tools/prefix.sh start NAME` (= `WINEDLLOVERRIDES="mscoree,mshtml=" <build>/wine wineboot -u`
  with the right DISPLAY/DRI_PRIME) first, or a
  rundll32 error dialog hangs the next app launch.
  Run it with DISPLAY (and DRI_PRIME) set for GUI prefixes: without DISPLAY the
  prefix's explorer records DriverError permanently ("graphics driver is
  missing" in every app) until the next `wineserver -k`.
- Prefixes with auto-start Autodesk services: on builds with 014 (services in
  session 0) wineserver idles once user processes exit; older builds hang on
  `wineserver -w` — use `wineserver -k` to be safe.
- Prefixes: `x/prefixes.tsv` (prefix, display, GPU, build) and `tools/prefix.sh`
  (`status`, `env NAME` to eval, `start`/`stop`) replace ad-hoc setups. Lease your prefix
  before using it, release it when done: `tools/prefix.sh lease NAME you`,
  `... release NAME you` (or set `PREFIX_HOLDER`). Never use or stop a prefix leased
  to someone else. `INV=NAME tools/invscen/run.sh S` runs a scenario on it.
- Licensing: every Inventor prefix is served by its own AdskLicensingService (CODE.md has the
  mechanism). The device is the host + user, so all host prefixes are one device; a VM is another
  (one active device per account: never start Inventor in a VM unless your brief says so).
  prefixes/inv-lic is a leftover of an older theory and unused, but leave it alone.
  Kill Inventor with `tools/prefix.sh kill-inventor NAME [--holder H]` (Inventor.exe plus its
  AdskLicensingAgent/msedgewebview2/... helpers, not the prefix's services; `--orphans`: helpers
  only, when Inventor is already gone). Plain `kill` of Inventor.exe leaves helpers spinning.
- `WINEDLLOVERRIDES="mscoree,mshtml="` is for `wineboot` only: set it on that one command, never
  `export` it. Inventor started with mscoree disabled dies ~6 s after start (CommonUI.dll+0x60b90,
  issue 152); `tools/invscen/run.sh` refuses to start it that way.
- X display numbers: :98-:101 are the Inventor GPU displays (x/prefixes.tsv), tools/regress.sh
  uses :120–:151 for its shards (and cleans them up; runs queue on /tmp/regress.lock, so concurrent invocations are safe;
  `tools/regress.sh unit DLL:TEST -n 10` runs one unit repeatedly without queueing, on :152-:199) — pick your own Xvfb
  display from :200 up. Don't edit tools/regress.sh in place while a run is active (bash reads it lazily): write a new file and `mv` it.
- Disk is shared and finite (one 3.7 TB volume; /tmp lives on it). Keep scratch prefixes and
  copies under ~10 GB total: reuse prefixes instead of copying one per run, delete finished ones
  by explicit path, and never copy inv* prefixes (38 GB each) for scratch work.
  Delete with `/home/xl0/projects/wine/tools/del` (plain `rm` under another name, same options),
  never with a raw `rm` in a command line or in `sh -c`: Claude Code stops for a confirmation on `rm`
  with globs or paths it can't resolve, even in bypass mode, and a background worker then hangs
  until the user answers. No safety net in it: look at the target first. Worktrees: `git worktree remove`.
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

After killing a wineserver, check for orphaned Wine processes of that prefix
(its WINEPREFIX in /proc/PID/environ, started before the new server) — they can
survive and hold windows/ports (74 of them once).
Before finishing: `wineserver -k` every prefix you started and kill your
Xvfb/Xorg servers by PID or display number (never `pkill Xvfb` / `pkill wine`:
other agents run their own). Beware `pgrep -f PATTERN` / `pkill -f`: the
pattern also matches your own shell's command line — match exact argv with
`ps -eo pid,args | awk ...` instead (unless the brief says to leave something running).

## Final report
Concise: result (root cause / fix), new Windows ground truth, commits, test
summary lines (VM + Wine), new issues filed, infra problems hit, open questions.
