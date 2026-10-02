# 118 Coverage campaign: environment variations (paths, locale, HiDPI, clipboard, shell, UI-mode opens)
Status: done (report) · Owner: coverage worker · Found in: inv4 :101, build/ 43790927731, 2026-10-02

Baseline on inv4: `hello part asm drawing drawing2 params export` all PASS in the normal environment
(the first `hello` after an Inventor start FAILs on a visible "IPM Content" window, see 120).
Tooling added (not in `all`): `tools/invscen/{paths,locale,cjk,docs}.cs`.

| # | variation | what ran | result | drafts |
|---|---|---|---|---|
| 1 | non-ASCII + long paths | `paths`: part (box, sketch text, iProperty) SaveAs / reopen / STEP+STL+PNG export / STEP import / assembly + drawing SaveAs and reopen under `C:\Users\<user>\Документы\Проект 測試 ä`, `a b&c#d%e[1]'f;g,h!~@$^(x)=+`, 206-char dir (222-char file) | PASS; names, text, iProperties, refs and volumes round-trip. 258- and 310-char dirs (file 274 / 326 chars): see note 1 | - |
| 2 | locale de_DE.UTF-8, ja_JP.UTF-8 | locales built with `localedef` into a scratch LOCPATH (not installed); `hello part asm drawing drawing2 params export` + `locale` | PASS in both (only the first-start window of 120). Inventor follows the user locale: decimal comma in expressions/Expression strings ("12,5 mm", "2 ul * 3,5 mm"), ja_JP dates `2026/3/4`; `12.5 mm` also accepted; `1.234,5 mm` rejected; STL ASCII, STEP, IGES, params XML stay invariant (`.`); `App.Locale` stays 0x409. CJK glyph issue under ja_JP: 119 | 119 |
| 3 | HiDPI 144 (150 %) | LogPixels in `HKCU\Control Panel\Desktop`, prefix restarted; `hello part drawing` (PASS, `INVSCEN_DIALOGS=log`), UI screenshots of ribbon, browser, dialog, Assistant pane; restored to 96 (value deleted, prefix restarted, re-verified) | API PASS; UI scales 1.5x; layout problems in the Application Options footer and the Assistant toast. Changing LogPixels without restarting the prefix leaves a half-painted main window (system-aware logical screen 2880x1620 on a 1920x1080 X screen), expected for a live change | 121 |
| 4 | clipboard | X <-> Wine <-> Inventor, text only (xclip absent; Tk `clipboard_get/append` on :101 and a throwaway Win32 CF_UNICODETEXT tool) | PASS: X -> Inventor edit field (Application Options > User name) with `Grüße 日本語 Привет`; Inventor -> X/Wine intact (formats 13/16/1/7). Not covered: images, Inventor private formats (Ctrl+C on a part face copies nothing; no cheap UI path to a viewport image copy) | - |
| 5 | file associations / shell | `.ipt` -> `Inventor.PartDocument\shell\open\command` `Inventor.exe /dde`, `ddeexec [open("%1")]`; `wine start /unix X.ipt` with Inventor running (opened in the same instance, no second Inventor.exe), with a non-ASCII file name, and with Inventor not running (starts and opens it) | PASS | - |
| 6 | UI-mode opens | `INVSCEN_UI=1 INVSCEN_DIALOGS=log run.sh samples` on 2022 samples | scenario RESULT FAIL only for the 2 Speedometer.ipt volume checks the VM reference also fails. 75 dialogs: 41 Resolve Link (Engine MKII, Buffer Prep Skid ...), 4 Moldflow server unavailable (Mold Design), 30 WPF boxes (assembly out-of-date "update now?", drawing "components need updating", "views out of date" lists); all as expected from earlier VM-side notes, none Wine-specific | - |

## Notes
1. Paths > 259 chars: `SaveAs` to a file path of 274 or 326 chars returns success after 14 s (vs 1.3 s) but writes the
   document (and `OldVersions\`) into an ancestor folder whose file path stays < 260 chars (strace: Inventor stat/opens
   the requested path, then writes `...\<ancestor>\name.newVer.ipt` and renames); `Documents.Open` of the requested
   path then fails E_INVALIDARG, `GetFileAttributesW` on it fails. Wine accepts plain >259-char paths (a
   mingw probe created/queried a 399-char path with and without `\\?\`), so Inventor decides this itself; the Windows
   behaviour is not known (VM Inventor off limits), so no draft. Worth a VM check if Windows differs.
2. A part kept open, a same-stem STEP imported and closed, then an assembly referencing the part saved and closed:
   the assembly close also closes the part (Documents.Count 0) and the harness' part RCW answers RPC_E_DISCONNECTED
   (reproducible, `paths` and a throwaway scenario). Looks like Inventor merging the imported `p.ipt` document with
   the open `p.ipt` (same name); not shown to be Wine-related.

## Environment changes made and restored
inv4 started/stopped (lease `coverage` released); LogPixels 144 added then deleted (user.reg again only has the
Fonts value 0x60); scratch dirs `Документы`, `Проект 測試 ä`, the special-character dir, `C:\lp` and `inst/assoc`
removed. Locales live only under the session scratchpad.
