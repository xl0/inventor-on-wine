# Fonts: font linking and fallback (win32u, uniscribe, dwrite)
Checked against integ d7799da4d5c + fix/123 (issues 119, 123).

## GDI font linking (dlls/win32u/font.c)
- `load_system_links` builds `font_links` (name -> list of family entries) from
  `HKLM\...\FontLink\SystemLink` (entries `FILE,Face`, resolved by **file name**
  via `find_face_from_filename`: Windows file names like MSGOTHIC.TTC never match host
  fonts), then from `font_links_defaults_list` (by family name, keyed by the
  `MS Shell Dlg` substitute) for Lucida Sans Unicode / Microsoft Sans Serif / Tahoma.
  The registry values are written by `update_font_system_link_info` (ACP-dependent).
- fix/119: Tahoma also links to the first of Noto Sans CJK JP/SC/TC/KR (order by ACP; same
  coverage, each linked face ~1 MB per font instance, FT faces aren't shared), if Tahoma exists; only Tahoma,
  because a link on a *missing* font name makes that name resolve to its first link
  (`find_family_from_font_links` in `find_matching_face_by_name`): links on
  Microsoft Sans Serif would turn WinForms text into Noto CJK instead of Arial.
- fix/123: without any of those Noto families, `font_funcs->get_language_fonts` (freetype.c) asks
  fontconfig for `sans:lang=L:scalable=true` per language (ja, zh-cn, zh-tw, ko; ACP's first), keeps a
  match only if its FC_LANG has L exactly (FcFontMatch returns a Latin font otherwise) and skips
  languages an earlier match covers. font.c finds the face by NT path + face index (FC_INDEX is the
  index Wine loaded the face with; skip `@` families) and links Tahoma to its family.
- Host fonts are sticky per prefix: `update_external_font_keys` writes them to HKLM `...\Fonts` with full
  paths and `load_registry_fonts` loads those in later sessions even if fontconfig no longer lists the
  directory. To test with a private `FONTCONFIG_FILE`, create the prefix under that config.
- Wine's family name = the font's name-table name (English, or the system language's); fontconfig's
  FC_FAMILY list may differ, hence file + index. `find_family_from_name` only sees primary names.
- `populate_system_links` ignores a link name that is itself a FontSubstitutes entry.
- `create_child_font_list`: every font (not SYMBOL/OEM charset) gets its own links, then
  Microsoft Sans Serif + its links, then Tahoma + its links. So Tahoma's links are a
  global fallback; Wine links Arial too, Windows doesn't (Arial CJK = boxes there).
- Child fonts load lazily (`get_glyph_index_linked`) for wchar paths: ExtTextOut,
  GetGlyphOutline (chars), GetTextExtent, GetCharABCWidths. GetGlyphIndices never links
  (also on Windows: 0xffff). Windows realizes linked fonts at the base font's em height
  (fix/119 does the same: `child->lf.lfHeight = -font->ppem`). With lfWidth, freetype.c
  `get_transform_matrices` scales a child by the base font's tmAveCharWidth (fix/119).
- A link's FONTSIGNATURE counts for charset selection (`can_select_face`): Tahoma with
  SHIFTJIS_CHARSET stays Tahoma (as on Windows) once it links to a JIS font.

## Uniscribe (dlls/gdi32/uniscribe/usp10.c)
- gdi32's ExtTextOutW only goes through BIDI_Reorder/ScriptShape (glyph indices, no
  linking) for RTL or complex-script text (U+0900-0FFF etc.); plain CJK text links.
- `ScriptStringAnalyse` SSA_FALLBACK: per-script `fallbackFont` by name (SimSun for Han
  and Kana, created with ANSI_CHARSET; override `HKCU\Software\Wine\Uniscribe\Fallback`).
  fix/119: dropped again when it has none of the glyphs the original font lacks, so SSA_LINK (fNoGlyphIndex) uses GDI
  linking. Edit controls use SSA_LINK|SSA_FALLBACK.

## DirectWrite
- dlls/dwrite/analyzer.c `system_fallback_config` hard-codes Noto family names per range
  (Noto Sans CJK SC/TC/KR/JP by locale): upstream's direction for missing Windows fonts.
  All ~150 ranges are Noto names, matched by family name in the system collection (built from the
  registry Fonts key); `MapCharacters` returns no font when the family is missing. dwrite has no
  fontconfig access (its unixlib is FreeType only), so a by-language lookup needs a new win32u/gdi32
  or unixlib path: not done (123).
