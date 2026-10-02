# 119 CJK text shows as boxes in Inventor unless the font itself has CJK glyphs (no GDI font fallback)
Status: draft · Owner: - · Found in: 118 environment campaign (inv4, build/ 43790927731)

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
