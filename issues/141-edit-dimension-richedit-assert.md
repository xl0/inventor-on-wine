# 141 Placing a dimension in a drawing: riched20 assertion in an endless OK-loop dialog
Status: fixed on fix/141 (wt/141, 3 commits on integ d18a5dcd1ef), not merged · Owner: issue-141 worker · Found in: UI pass 2 (inst/ui2, inv :98, build/ 04293594c50)

## Symptom
In a drawing (.idw), General Dimension: pick two edges, click to place the dimension. Instead of Inventor's "Edit
Dimension" box a Wine message box appears: "Assertion failed! File: ../wine-src/dlls/riched20/caret.c Line: 232
Expression: ~para->nFlags & MEPF_REWRAP". Clicking OK ("exit the program") does not exit: the box disappears and a new
one (new HWND each time) appears at once. Inventor never gets back to the user; the only way out is killing the process.
![assert](attachments/141-edit-dimension-assert.png)

## Result
- **Not a regression**: an old upstream bug (code unchanged since 2019, 03a93eb80e2 and before). Same assertion with
  riched20.dll from the upstream master build (wt/regress-master-build, 4e819f054dd) swapped into the test build
  (`inst/141/trace-master.log`: `_wassert ... ../regress-master/dlls/riched20/caret.c,232`). None of our 5 riched20
  commits is involved; no bisect needed.
- Cause: `EM_SETCHARFORMAT(SCF_WORD | SCF_SELECTION)` with an empty selection. Fixed, plus two differences in the
  same handler that the probe showed. After the fix: ![fixed](attachments/141-edit-dimension-fixed.png)

## Root cause
The Edit Dimension dialog (MFC `#32770`, RichEdit50W) fills its rich edit in WM_INITDIALOG while the control already
has the focus. Last steps (`inst/141/trace-integ.log`): `EM_GETCHARFORMAT(SCF_DEFAULT)`, `EM_EXSETSEL(4,4)` (end of
the text `<<>>`), `EM_SETCHARFORMAT(SCF_WORD | SCF_SELECTION, Tahoma 240 ...)`, `EM_EXSETSEL(4,4)`.
`handle_EM_SETCHARFORMAT` formatted "the word at the caret" with `ME_SetCharFormat` (marks the paragraph
MEPF_REWRAP) but only wrapped `if (changed)`, and `changed = ME_IsSelection()` is FALSE for an empty selection. Every
other path of the handler, and every other message that marks paragraphs, wraps before returning; this one returned
with a marked paragraph. The next caret computation asserts: `EM_EXSETSEL -> set_selection -> update_caret ->
cursor_coords` (first box, `inst/141/bt-first.txt`), then `WM_SETFOCUS -> create_caret -> cursor_coords` (every later box).
The reporter's four standalone variants never sent SCF_WORD with an empty selection.

## The OK loop (not a Wine-side difference, nothing filed)
It is recursion, not a failing abort: msvcrt's assert box is owned by the active window (the Edit Dimension dialog).
On OK, `EndDialog` of the box re-activates the owner, `DefDlgProc` restores the focus to the rich edit, its WM_SETFOCUS
asserts again, inside the first box's `EndDialog`, before `_wassert` gets to `raise(SIGABRT)` (frames 47-68 of
`inst/ui2/assert1/bt.txt`). Any assert in a focus handler does that, on Windows too.

## Windows ground truth (VM, `tests/r141/scf_word.exe`, output `inst/141/vm-scf_word.txt`)
RICHEDIT50W (msftedit) and RichEdit20W (riched20.dll), `EM_SETCHARFORMAT(SCF_WORD | SCF_SELECTION)`:
- The layout follows at once (EM_POSFROMCHAR of the text end moves) and so does the caret of a focused control
  (also for SCF_ALL and SCF_DEFAULT). No paint needed, hidden or visible.
- Non-empty selection: only the selection is formatted (SCF_WORD has no effect).
- Empty selection, text `one two  three`: caret inside a word's letters: the word without the spaces after it; caret
  among the spaces after a word: the word and the spaces; caret at a word start: nothing (modify flag stays 0);
  caret between the letters and the spaces: nothing on msftedit, word + spaces on riched20.dll.
- Caret in front of a paragraph mark (end of text, end of a line): the mark gets the format, the text before it is
  untouched (msftedit: modify flag and undo; riched20.dll: neither). This is Inventor's call: it gives the final
  paragraph mark the dimension style's font.
- Wine before: formatted the word (with its spaces) at every caret position including the word before a paragraph
  mark, and with a selection the word after the selection's end too; no wrap, no caret update.

## Fix (fix/141, wt/141)
- fb34cb8cd17 `riched20: Wrap the paragraphs after formatting a word with EM_SETCHARFORMAT.` The assertion: SCF_WORD
  counts as a change, so the handler wraps like its other paths. Test `test_EM_SETCHARFORMAT_word` (fails + asserts
  without the fix).
- 3466158191d `riched20: Update the caret after EM_SETCHARFORMAT.` One line; the caret stayed at its old position
  after SCF_ALL / SCF_WORD size changes until the next selection change.
- 71568c127a8 `riched20: Only apply SCF_WORD to the word the caret is inside of.` msftedit's rules above
  (`set_word_char_format`). In Edit Dimension Wine reformatted `<<>>` and left the final paragraph mark in the dialog
  font (MS Shell Dlg 8 pt): after select all + Delete the typed text was 8 pt instead of the style's Tahoma 12 pt.
  The first commit alone removes the assertion; the other two can be dropped independently.
- Tests: riched20 editor on the VM x86_64 + i386: 10980 tests, 0 failures (one i386 run failed in the unrelated
  `cursor position set` link tests, mouse-position dependent; the rerun passed). Wine x86_64 + i386: 10977 tests,
  0 failures. `tools/regress.sh run wt/141-build -m '^(riched20|riched32|msftedit)$'` (wt/141-regress): 10 units pass,
  failures/todos equal to the 04293594c50 baseline (0 worse).
- Probe on Wine after the fix vs the VM (`inst/141/wine-final-scf_word.txt`): RICHEDIT50W word/selection/paragraph
  mark maps equal except `ab, cd.` (Wine's word breaking has no punctuation class) and the leftovers below.

## Verification in Inventor (inv :98 on wt/141-build)
- Issue steps by mouse (Annotate > Dimension, two edges, place): Edit Dimension opened 3 of 3 (first dimension of the
  session included), 0 assertions. Typing, a symbol from the palette (Ø), select all + Delete + retype, tolerance
  method Symmetric (drawing shows `9.71 ± .00`), appended text (`6.06 MAX` in the drawing): work.
- Format Text (125): typed + pasted `中文测试` in Tahoma shows glyphs; re-edit by double-click keeps size and font;
  typing over a selection and Bold work (see 147 for the bold display).
- Repro without the mouse: `INVSCEN_DIALOGS=off INVSCEN_UI=1 INVSCEN_CMD=DrawingDimensionToleranceCtxCmd
  tools/invscen/run.sh dim141` (API-made dimension, then the context command that opens Edit Dimension on it);
  without INVSCEN_CMD it leaves the drawing open for the manual steps. WINEDEBUG=+richedit shows the sequence.

## Leftovers
- Deleting the `<<>>` token is possible on Wine (the dimension then shows its plain value). Whether Inventor on
  Windows prevents that is unknown: the dialog asks for ENM_PROTECTED and Wine has no EN_PROTECTED; the run was not
  CFE_PROTECTED when checked. The grey box behind the text is there with and without the fix. Not verified on Windows.
- Other rich edit differences the probe showed, outside this issue: draft 146. Format Text doesn't show bold: draft 147.
