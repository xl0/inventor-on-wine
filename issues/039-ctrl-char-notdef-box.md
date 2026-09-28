# 039 Control characters (CR) drawn as .notdef boxes in text output
Status: open (draft) · Owner: - · Branch: - · Found in: UI test campaign (Application Options)

## Symptom (integ d53133a66a1, :98)
Inventor's combo box strings end in `\r` (read with CB_GETLBTEXT: "Smooth\r",
"Medium\r", "Pan\r", "Orbit\r"; also "Default - Gray", "1 Color", "DirectX 11"
in Colors/Hardware). Wine draws the CR as an empty box after the text in
Application Options > Display (Display quality, MMB/Ctrl+MMB/Shift+MMB),
Colors (Section Capping texture, preview type), Hardware (Graphics Driver).
Windows shows the plain text.
- Wine: ![wine](attachments/039-appoptions-display-wine.png)
- VM: ![vm](attachments/039-appoptions-display-vm.png)

## Windows ground truth (Win11 VM, `tests/ctrlchar_text.c`)
"Pan\r|" in Tahoma: ExtTextOutW and DrawTextW(DT_SINGLELINE) draw "Pan|" (no
glyph, no advance visible for CR); a user32 ComboBox and ListBox item "Pan\r"
shows "Pan". Wine draws a box in all four. Windows' own tahoma.ttf has no cmap
entry for U+000D either (checked with fontTools), so Windows GDI handles
control characters without a glyph specially rather than via the font.
- VM: ![vm](attachments/039-cr-text-vm.png)
- Wine: ![wine](attachments/039-cr-text-wine.png)

## Task
Find what GDI does for unmapped C0 control chars (zero-width / blank glyph? which
code points; GetGlyphIndicesW / GetTextExtentPoint results for them), match it
in win32u font code, test in gdi32 tests.
