# Rich edit (dlls/riched20; msftedit forwards to it)
Checked against integ d7799da4d5c + fix/125 (issue 125), d18a5dcd1ef + fix/141 (issue 141).

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
- Layout invariant: a message handler that marks paragraphs (`para_mark_rewrap`, MEPF_REWRAP) has to wrap them
  before it returns (`ME_UpdateRepaint`, or `ME_WrapMarkedParagraphs` + `ME_UpdateScrollBar` + `update_caret`).
  `cursor_coords()` (caret creation/update, ITextRange::ScrollIntoView) asserts on a marked paragraph;
  EM_POSFROMCHAR doesn't, it returns stale positions. Not every path keeps it: 141 (`EM_SETCHARFORMAT(SCF_WORD |
  SCF_SELECTION)` with an empty selection, fixed), 159 (ITextRange::SetText), 160 (numbered paragraphs).
  An assert in a focus handler recurses: msvcrt's box is owned by the active window, closing it restores the
  focus, WM_SETFOCUS asserts again before abort() is reached ("OK doesn't exit").
- SCF_WORD (tests/r141, riched20 `test_EM_SETCHARFORMAT_word`): Windows ignores it with a selection; with an empty
  one it formats the word the caret is in (riched20.dll: also at the end of its letters, then with the spaces;
  msftedit.dll: nothing there), nothing at a word start, and the paragraph mark when the caret is in front of one
  (msftedit.dll: modify flag + undo; riched20.dll: neither). Inventor's dialogs use the last case to give the
  final paragraph mark the text style's font. Wine (fix/141) does the paragraph mark case like msftedit.dll and
  otherwise still formats the word after the caret / selection end with its spaces (todo_wine rows; 146).
- Trap when testing another riched20.dll: a Wine-built DLL put next to the exe with `riched20=n,b` is still loaded
  from the build tree unless its "Wine builtin DLL" marker is patched (inst/141-review/build-scratch.sh).
  Overwriting the file in the build tree works (the assert message then shows that build's source path).
- Fuzzer for the invariant: `tests/r141/fuzz.c` (random messages, checks every paragraph with ScrollIntoView after
  each); `tests/r141/rescript.c` runs small message scripts on Wine and the VM.
- Debug channels: `+richedit` (messages with hwnd/wParam/lParam, itemize/layout),
  `+richedit_style` (formats), `+richedit_lists`, `+richedit_check`.
