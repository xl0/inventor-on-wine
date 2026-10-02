# 123 CJK font linking only works with Noto Sans CJK installed
Status: fixed on fix/123 (wt/123, 4 commits on integ d7799da4d5c, review fixes done), not merged; laptop preview boxes/wrapping still unexplained (comparison below; probe output pending) · Owner: issue-123 worker · Found in: user's laptop (squares after 119, 2026-10-02)

119 links Tahoma to "Noto Sans CJK JP/SC/TC/KR" by name. Hosts without those exact families
(other CJK fonts: WenQuanYi, Droid Sans Fallback, Source Han, IPA, Takao, AR PL, system fonts
with other names, or Noto CJK under another family name) still show boxes.

## Fix (fix/123)
- `win32u: Link Tahoma to the East Asian fonts fontconfig prefers.` (b472d8e2445): when none of the four
  Noto families is installed (119's by-name link stays first, so hosts with Noto are unchanged; non-fontconfig
  platforms keep it too), new backend call `get_language_fonts`: per language in ACP order (ja, zh-cn, zh-tw,
  ko; 936/949/950 put theirs first) `FcFontMatch("sans", lang, scalable)`; a match counts only if its FC_LANG
  has the language exactly (otherwise fontconfig hands back a Latin font), and a language already covered by
  an earlier match adds nothing. font.c maps file + face index to Wine's face (family names may differ from
  fontconfig's; `@` vertical families skipped) and appends the family to Tahoma's links, after registry
  SystemLink entries and real Windows fonts. Still only when Tahoma exists.
- `win32u: Link Tahoma to an East Asian font even when Tahoma has a substitute.` (ddd64feed24): see below.
- Tests: gdi32:font `test_font_link` and usp10 `test_ScriptString_fallback` skip on "no font with
  SHIFTJIS_CHARSET" instead of font names.

## Review fixes (2026-10-02)
- `win32u: Don't use linked fonts for C1 control characters.` (0f916853d8b): U+0080-009F are never looked up
  in links (C0 already wasn't); Unifont has glyphs for them and made them visible in Tahoma text
  (gdi32:font test_control_chars, 6 failures with Unifont as the only CJK font; now 0).
- `win32u: Find the East Asian link font also through replacements and by name.` (65707e3dc72):
  - crash in every process when "Noto Sans CJK JP" is a `HKCU\Software\Wine\Fonts\Replacements` name (such a
    family has no faces of its own): go through `get_family_face_list`, link the replaced family.
  - backend call is now `get_language_font` (one language: file, index, FC_FAMILY, mask of covered languages);
    when no face has that file + index (Wine kept the copy from C:\windows\Fonts: winetricks-installed
    font that is also on the host) the family is looked up by name; a language only counts as covered
    once a font was linked.
  - tests skip unless a scalable SHIFTJIS_CHARSET font really has the test character (bitmap JIS fonts
    under ACP 932, "Phetsarath OT" claiming the JIS code page).
- Matrix after the fixes (gdi32:font / usp10, x86_64 + i386, "9" = the host todo-successes also on integ):
  Noto visible 9/0 (r119 `fm` probe output identical to build/ d7799da4d5c); no Noto 9/0 (WenQuanYi);
  Droid+IPA 9/0; Unifont only 9/0; Replacements "Noto Sans CJK JP" -> WenQuanYi 9/0 (no crash, links
  WenQuanYi); wqy-zenhei.ttc copied into C:\windows\Fonts before the first start, same font on the host 9/0
  (linked by name). ja_JP locale without any CJK outline font: both tests skip (42 other failures there,
  same on build/). VM: usp10 0, font 9 (lines 2872/2894/5195, not in test_font_link).
  regress.sh subset vs integ: 0 REAL (FLAKY i386 user32:win, fails on base too).
- Not solved, documented: a prefix that still loads host fonts from its registry (HKLM Fonts, sticky) after
  the fontconfig set shrank: links follow what fontconfig offers now (IPAGothic only -> Hangul not
  linked although WenQuanYi is still loaded). The name fallback doesn't help: fontconfig never names that font.

## Fonts on the server and results
`fc-list :lang=…`: Noto Sans/Serif/Mono CJK (all four), WenQuanYi Zen Hei (all four), Unifont (all four),
Droid Sans Fallback (ja, zh-tw only), IPAGothic/IPAPGothic (ja only). `fc-match sans:lang=X`: Noto Sans CJK
JP / SC / TC / JP(ko).
Private `FONTCONFIG_FILE` configs, each with its own fresh prefix (host fonts are sticky in a prefix,
notes/wine/fonts.md):

| config | fontconfig picks | Tahoma links | gdi32:font / usp10 (x86_64 + i386) |
|---|---|---|---|
| default (Noto visible) | - (by name) | Noto Sans CJK JP | 9 host todo-successes as on integ / 0 failures |
| all system dirs except opentype/noto | WenQuanYi Zen Hei for ja (covers all) | WenQuanYi Zen Hei | same / 0 |
| same without wqy + unifont | Droid Sans Fallback for ja (covers zh-tw); zh-cn, ko: DejaVu Sans, rejected | Droid Sans Fallback | same / 0 |
| only IPAGothic + Droid + DejaVu | IPAGothic (ja), Droid (zh-tw) | IPAGothic, Droid Sans Fallback | 0 / 1 (Latin SSA_FALLBACK check; no Arial-like font in this set, not CJK) |

`test_font_link` ran (no skip) in all of them; VM (Win11): font 9 failures (ntmCellHeight / line 5195, unrelated,
none in test_font_link), usp10 0. Locales ja_JP / zh_TW (LOCPATH): ACP 932 links JP, 950 links TC.
Edit controls (Uniscribe: the SimSun fallback is dropped by 119's rule, GDI links): top WenQuanYi, bottom Droid:
![edit](attachments/123-edit-wqy-droid.png)
regress.sh subset (gdi32 usp10 user32 comctl32 dwrite gdiplus riched20 win32u) vs integ d7799da4d5c: 0 REAL
(FLAKY i386 user32:win, fails on base too).

## DirectWrite
`dlls/dwrite/analyzer.c` `system_fallback_config`: every range maps to Noto family names, looked up by name in
the system collection; a missing family = no fallback font (boxes). dwrite has no fontconfig access (FreeType
unixlib only), a by-language lookup needs a new path through win32u or its unixlib: not small, not done.

## Laptop: squares although Noto Sans CJK is installed and 119 is in the build
Checked on the server (same family set, Noto 2.004 TTC: faces 0-4 JP KR SC TC HK, 5-9 Mono):
- TTC faces / HK / localized names: Wine registers each face under its name-table family (JP, KR, SC, TC, HK
  all present; TC's only family record is tagged zh-tw, still "Noto Sans CJK TC"). Works under ACP 1252, 932, 950.
- Existing SystemLink\Tahoma: every Wine prefix has it (MSGOTHIC.TTC,MS UI Gothic ...); missing files are
  skipped and the Noto link is appended. Entries that resolve (real fonts, Replacements) come first.
- Tahoma: Wine's own tahoma.ttf is always loaded from the build's fonts dir; a real one has the same family name.
- Font cache: volatile, rebuilt per wineserver session.
- **Found**: a `FontSubstitutes` value named `Tahoma` (any target) made 119 drop the link: all fonts showed
  boxes although GetTextFace still says Tahoma. Reproduced with `reg add ...\FontSubstitutes /v Tahoma /d Arial`;
  fixed by ddd64feed24. Unknown whether the laptop prefix has such a value.
- Not checkable here: text not drawn by GDI. WPF (Inventor's HwndWrapper UI) and DirectWrite/WebView2 have their
  own fallback lists (Windows / Noto font names) and never see GDI links. Also possible: an earlier child in
  the fallback chain ("Microsoft Sans Serif" resolved to some host font, then Tahoma) that maps the character
  to a box glyph; the probe shows which font the glyph comes from.

Diagnostics for the laptop: `build/wine tests/cjk_link.exe > cjk_link.txt` (rebuild line in the file). The
first block prints ACP/locale, SystemLink / FontSubstitutes / Wine Replacements entries with
`[family: installed|missing]`, Tahoma and Noto registry font paths, families per East Asian charset, and per
character whether Tahoma's glyph is linked and which installed font has the same outline ("NOT LINKED" =
default glyph). Wine's own list: `WINEDEBUG=+font build/wine tests/cjk_link.exe 2>&1 | grep -a "linked Tahoma"`
(119 build: `grep -a 'SystemLink for L"Tahoma"'`). If the probe says linked, the squares are in a non-GDI path:
say where they appear.

Regression-relevant: hosts without Noto CJK now load whatever fontconfig prefers per language on a missing
glyph (up to 4 faces; Unifont if it is the only one); a few FcFontMatch calls per process start in that case;
Tahoma links with a Tahoma substitute; the two tests' skip condition.

## Laptop interactive comparison and screenshot (2026-10-02)

Tested in the existing `prefixes/inv` on **wine-11.18-494-gd7799da4d5**:
119 is included, the two fix/123 commits above are not. Same 144-DPI
Awesome/NVIDIA/Vulkan/picom setup as [124](124-open-dialog-resize-coreclr-crash.md).
No font installation, registry change, prefix repair or rebuild was applied
for this comparison.

### Font availability and result

- Host `fc-list` confirms **Noto Sans CJK SC Regular and Bold**
  (`/usr/share/fonts/opentype/noto/NotoSansCJK-{Regular,Bold}.ttc`), plus other
  Noto CJK families, Noto Serif CJK, Droid Sans Fallback and WenQuanYi Zen Hei.
- Offline inspection of the prefix's HKLM
  `Software\Microsoft\Windows NT\CurrentVersion\FontSubstitutes` shows **no
  value named `Tahoma`**. `MS Shell Dlg` and `MS Shell Dlg 2` map to Tahoma.
  This does not inspect every font-replacement/cache mechanism.
- The user had not deliberately selected a font when initially seeing boxes;
  the original/default font name was not recorded.
- In sketch text's **Format Text** dialog, pasting `中文测试` and explicitly
  selecting **Noto Sans CJK SC** worked.
- The user then clarified: with other fonts, Chinese **also rendered in the
  sketch, but not in the preview**. The other font names were not recorded.
  Here "preview" refers to the editing/preview area inside Format Text,
  not a DWG thumbnail or Open-dialog preview.

So the host is not simply missing Noto, the characters survive the paste, and
the remaining squares depend on the UI rendering path/font selection.
Do not assume the no-Noto fix alone addresses this report. The preview
control class/rendering API and the expanded `cjk_link` output are still unknown.

### Additional layout/orientation observations

![Laptop Format Text preview and sketch](attachments/123-laptop-sketch-text-preview.png)

The user supplied this screenshot with **Noto Sans CJK SC**, size **0.120 in**:

1. The last character moves to a new line in the preview: `中文测` then `试`,
   despite the large visible editor area. Both copies show the same split.
   Whether this is a real paragraph break, soft wrapping, a text-box width
   constraint or incorrect text metrics has not been established.
2. The user describes the sketch text as "upside-down". The screenshot shows
   rotated text and a rotated **TOP** ViewCube label; camera/sketch/text
   orientation has not been normalized. This is not yet proof of inverted
   glyph outlines or a CJK-specific bug.
3. **After finishing the text and reselecting its area**, the user reports
   that the preview collapses to tiny, approximately one-pixel dots.
   Selecting those dots changes the displayed font selection to
   **Noto Sans Mono CJK** (regional suffix not reported). The text remains
   selectable/present, but is no longer readable in the preview.
   This is a subsequent observation, not shown in the screenshot above.
   It is unknown whether stored font/size data changed, the font selector is
   merely reporting the selected run, or preview font metrics/state were lost.

Keep these observations separate from the preview's missing glyphs until
linked. Next checks for the owner: identify the preview control/API and fonts
that fail; obtain the requested `cjk_link` output; distinguish stored newlines
from wrapping; compare Latin and Chinese text at the same sketch/view
orientation and against Windows. Also compare font family, size and text
formatting before accepting the text versus after reselecting/re-editing it,
to locate the one-pixel preview regression. Split follow-up issues if needed.

No additional agent-driven tests were run; the user supplied the subsequent
reselection observation and requested handoff to the main agent.
The screenshot is the user-provided image,
with metadata stripped; it contains no sign-in page or email.
