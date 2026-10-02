# Rich edit (dlls/riched20; msftedit forwards to it)
Checked against integ d7799da4d5c + fix/125 (issue 125).

- One implementation for RichEdit20A/W and RICHEDIT50W: nothing in the editor knows which class it
  is (only `bEmulateVersion10`), so msftedit-only behaviour (tomApplyTmp) applies to both.
- Layout: `wrap.c` itemizes each paragraph (ScriptItemize), `shape_run` shapes every run with
  ScriptShape/ScriptPlace, `paint.c` draws with ScriptTextOut. Glyph-index shaping never uses GDI
  font linking; fix/125 reshapes non-complex LTR runs that got a default glyph with
  `fNoGlyphIndex`, which makes Uniscribe place and draw by character (GetCharABCWidthsW /
  ExtTextOutW, both linked, see fonts.md). `script_analysis` is rewritten by `itemize_para` on every
  wrap, so the flag doesn't outlive a font change.
- Windows binds fonts at insertion (IMF_AUTOFONT: CJK in Tahoma becomes Microsoft YaHei / SimSun
  runs) and links at display when binding is off. Wine does neither binding nor
  EM_GET/SETLANGOPTIONS (unsupported, 0).
- `EM_SETTARGETDEVICE(dc, twips)`: the DC is ignored, the width is converted with the window DC's
  DPI. Right for a screen DC (same line breaks as Windows for CJK); wrong for printer DCs.
- TOM (`richole.c`): an ITextFont on a range reads/writes the range directly unless caching is on
  (`Reset(tomCacheParms)` for gets, `Reset(tomApplyLater)` for sets). `props[]` is filled once at
  creation and is stale afterwards; values are points (twips_to_points / points_to_twips at the
  CHARFORMAT boundary). fix/125: `set_mask` records what was set in apply-later mode, tomApplyNow
  applies only that; tomApplyTmp ignores setters. Spacing is in points too (sSpacing twips).
  mingw's C CHARFORMAT2W has no pad word before wWeight: probes built with mingw read sSpacing
  at wWeight's place (use Wine's headers or offsets).
- A selection made with (0,-1) ends after the final paragraph mark (cpMax = len + 1). The mark can't
  be deleted, so delete paths must collapse the cursors themselves (`ME_DeleteSelection` does;
  WM_CLEAR and cut didn't before fix/125). A format set at an insertion point never reaches the
  final paragraph mark, on Windows neither.
- Debug channels: `+richedit` (messages with hwnd/wParam/lParam, itemize/layout),
  `+richedit_style` (formats), `+richedit_lists`, `+richedit_check`.
