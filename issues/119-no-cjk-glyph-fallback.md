# 119 CJK text shows as boxes in Inventor unless the font itself has CJK glyphs (no GDI font fallback)
Status: fixed on fix/119-cjk-font-link (wt/119), not merged · Owner: issue-119 worker · Found in: 118 environment campaign (inv4, build/ 43790927731)

## Symptom
Drawing notes (and the UI edit fields, e.g. Application Options > User name) with Chinese/Japanese
characters render as empty boxes in Arial, Tahoma, Segoe UI, MS Gothic and SimSun. Cyrillic and Latin-1 are
fine. Picking a CJK-capable font explicitly works ("Noto Sans CJK JP" / "Noto Serif CJK JP" render the
same text correctly), so fontconfig exposes the fonts (30 CJK faces in `fc-list`) and Inventor stores the
text correctly (a UTF-16 round trip through X clipboard, iProperty, sketch text and STEP/file names is intact).

## Windows
GDI/DirectWrite fall back per character (font linking via
`HKLM\Software\Microsoft\Windows NT\CurrentVersion\FontLink\SystemLink`, MS Gothic / Yu Gothic /
Microsoft YaHei), so a Japanese or Chinese note in Arial is readable. Not verified on the VM (Inventor
must not be started there); this is standard Windows text behaviour. A Japanese or Chinese user sees
boxes in every note/dimension/title block with the default fonts.

## Evidence
`issues/attachments/119-cjk-no-fallback.png` (top: Arial/Tahoma/Segoe UI lines, bottom: explicit Noto CJK).
Also seen under `LC_ALL=ja_JP.UTF-8` (the locale does not change it).

## Repro
`INV=inv4 INVSCEN_KEEP=1 tools/invscen/run.sh cjk` leaves a drawing with the notes open; edit the `fonts`
array in `tools/invscen/cjk.cs` to compare. Check what Wine's FontLink key holds in the prefix
(`reg query "HKLM\Software\Microsoft\Windows NT\CurrentVersion\FontLink\SystemLink"`) and whether
win32u/freetype.c has any per-character fallback when a face lacks the glyph.

## Windows ground truth (VM, 2026-10-02)
`run.sh --vm cjk` (Windows 11 Pro VM, Inventor 2027.1, fonts Arial, Tahoma, Segoe UI, MS Gothic, Yu Gothic,
Microsoft YaHei, SimSun, isocp; same note text as the scenario). Screenshot:
`issues/attachments/119-vm-cjk-fallback.png`.

![VM CJK notes](attachments/119-vm-cjk-fallback.png)

- **Arial: boxes on Windows too** (Chinese/Japanese shown as empty squares). Also the scenario's Noto fonts
  (not installed on the VM, Inventor substitutes Arial) and the SHX-like `isocp`: boxes.
- **Tahoma and Segoe UI: CJK renders** (font linking per character), glyphs from a linked CJK face.
- MS Gothic, Yu Gothic, Microsoft YaHei, SimSun render CJK themselves (MS Gothic/SimSun/Yu Gothic show Cyrillic
  with wide spacing, i.e. their own Cyrillic glyphs).
- So the Wine symptom is the same for Arial; the real difference is Tahoma/Segoe UI (the UI fonts), which have
  SystemLink entries. UI edit fields were not checked on the VM.
- VM CJK fonts in C:\Windows\Fonts: msgothic.ttc (MS Gothic/PGothic/UI Gothic), YuGoth{B,L,M,R}.ttc, msyh*.ttc
  (YaHei), msjh*.ttc (JhengHei), mingliub.ttc, simsun.ttc + simsunb + SimsunExtG, malgun*.ttf; the FontLink
  list also names Meiryo, Batang, Gulim, Dotum, MS Mincho (Windows Fonts folder holds only those present as files).
- SystemLink (`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontLink\SystemLink`, full dump in
  `issues/attachments/119-vm-systemlink.txt`):
  - **Arial: no entry**, **MS Sans Serif: no entry**.
  - Tahoma: MSGOTHIC.TTC,MS UI Gothic; MINGLIU.TTC,PMingLiU; SIMSUN.TTC,SimSun; GULIM.TTC,Gulim;
    YUGOTHM.TTC,Yu Gothic UI; MSJH.TTC,Microsoft JhengHei UI; MSYH.TTC,Microsoft YaHei UI;
    MALGUN.TTF,Malgun Gothic; SEGUISYM.TTF,Segoe UI Symbol
  - Segoe UI: TAHOMA.TTF,Tahoma; MEIRYO.TTC,Meiryo UI,128,96; MEIRYO.TTC,Meiryo UI; MSGOTHIC.TTC,MS UI Gothic;
    MSJH.TTC (128,96 and plain); MSYH.TTC (128,96 and plain); MALGUN.TTF (128,96 and plain); MINGLIU.TTC,PMingLiU;
    SIMSUN.TTC,SimSun; GULIM.TTC,Gulim; YUGOTHM.TTC,Yu Gothic UI (128,96 and plain); SEGUISYM.TTF
  - Microsoft Sans Serif: MSGOTHIC,MS UI Gothic; YUGOTHM,Yu Gothic UI; MINGLIU,PMingLiU; SIMSUN; GULIM;
    MSJH,JhengHei UI; MSYH,YaHei UI; MALGUN; SEGUISYM
  - MS Gothic: MINGLIU,MingLiU; SIMSUN; GULIM,GulimChe; YUGOTHM; MSJH; MSYH; MALGUN; SEGUISYM
  - SimSun: MICROSS.TTF,Microsoft Sans Serif,108,122; MINGLIU,PMingLiU; MSMINCHO,MS PMincho; BATANG; MSYH; MSJH;
    YUGOTHM; MALGUN; SEGUISYM
  Implication: a Wine fix (if wanted) matters for Tahoma/Segoe UI/MS Sans Serif text; Arial needs nothing
  beyond what Windows does (boxes), unless GDI font association (not SystemLink) differs. Wine's SystemLink
  would need these keys for the fallback to exist at all.

## Findings (worker, 2026-10-02)
Rendering paths in Inventor: drawing notes / title block go through OGSGDIFontDevice.dll
(GDI `GetGlyphOutlineW` by character: linked on Windows and Wine), UI edit fields are plain
`Edit` controls (Uniscribe `ScriptStringAnalyse` SSA_LINK|SSA_FALLBACK). Wine internals: notes/wine/fonts.md.

Causes:
1. Wine's link lists (registry SystemLink by file name, built-in defaults by family name) name only
   Windows CJK fonts (MS UI Gothic, SimSun, ...), absent here: Tahoma's link is empty, so every font's
   fallback chain (own links, Microsoft Sans Serif, Tahoma) has no CJK font.
2. Linked fonts were realized with the base LOGFONT; for cell heights (lfHeight > 0, Inventor's case)
   Noto's large ascent/descent made CJK ~30% smaller than Windows (Windows uses the base em height).
3. Edit controls: Uniscribe's SSA_FALLBACK picks "SimSun" (ANSI_CHARSET) -> resolves to Arial without
   the glyphs, so SSA_LINK never got a chance.

Windows ground truth (tests/cjk_link.c on the VM, `cjk_link.exe [HEIGHT]`): Tahoma, Segoe UI,
Microsoft Sans Serif, MS Shell Dlg (2), Lucida Sans Unicode link CJK (GetGlyphOutline returns MS UI
Gothic's outline, GetGlyphIndices 0xffff); Arial/Times/Courier/Verdana/Calibri don't (default glyph).
Tahoma lfHeight +32: linked advance 26 = Tahoma's em (MS UI Gothic alone at +32: 32). Tahoma with
SHIFTJIS/HANGEUL charset stays Tahoma. Bitmap bases (MS Sans Serif, System) return no outline at all.

Fix (fix/119-cjk-font-link, 3 commits on integ 43790927731):
- `win32u: Size linked fonts to the em height of the base font.`
- `win32u: Link Tahoma to the Noto CJK fonts.` (JP/SC/TC/KR, first by ACP; same names as dwrite's
  fallback). Only Tahoma: a link on a missing font name makes the name resolve to its first link
  (Microsoft Sans Serif would become Noto CJK instead of Arial). All fonts fall back to Tahoma's
  links, so Arial/Segoe UI/MS Gothic/SimSun now show CJK too (Wine differs from Windows for Arial
  by design of its global fallback; MS Gothic etc. render Latin in their substitute, CJK linked).
- `gdi32/uniscribe: Don't use a fallback font that lacks the glyphs.`
- Tests: gdi32 font `test_font_link` (Tahoma -32/+32: linked outline bigger than .notdef, advance =
  em, GetGlyphIndices 0xffff), usp10 `test_ScriptString_fallback` (SSA_FALLBACK|SSA_LINK widths
  full-width). Pass on the VM and Wine (x86_64, i386); without the fixes they fail (got widths 24).
  Skip without MS UI Gothic/SimSun/Noto Sans CJK JP. regress.sh (gdi32 gdiplus usp10 user32 comctl32
  comdlg32 dwrite riched20 win32u uxtheme mlang d2d1 msftedit riched32 shell32) vs integ: 0 REAL
  (1 FLAKY d2d1 timeout).

Screenshots (inv4, cjk scenario, default fonts list):
before ![before](attachments/119-wine-before.png) after ![after](attachments/119-wine-after.png)
Application Options user name (left: win32u fix only, right: + uniscribe fix):
![edit](attachments/119-wine-options-edit.png)

Regression-relevant: every font's missing glyphs now come from Noto CJK when installed (lazy load of
a ~20 MB TTC on first miss per font instance); charset selection of Tahoma for CJK charsets;
linked glyph size for positive heights (all linked fonts, also real MS UI Gothic installs);
Uniscribe fallback for non-complex scripts. Leftovers: bitmap base fonts (MS Sans Serif, System)
still return linked outlines and differ in size from Windows at scaled sizes; hosts without Noto Sans CJK (e.g. only
WenQuanYi or Droid Sans Fallback) still get boxes; a fontconfig lookup by language would cover them.
