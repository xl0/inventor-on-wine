# 146 riched20: format of text typed after deleting everything, and other format differences (probe leftovers of 141)
Status: draft · Owner: - · Branch: - · Found in: 141 (tests/r141/scf_word.exe, VM vs wt/141-build) · Priority: low

## Symptom / evidence
Differences between Wine's riched20 and Windows (msftedit and riched20.dll agree) seen while probing 141.
None is needed for 141's fix; listed so they aren't rediscovered. VM output `inst/141/vm-scf_word.txt`, Wine
`inst/141/wine-final-scf_word.txt`.
1. Text typed after deleting the whole text takes the final paragraph mark's format on Wine; Windows keeps the
   format of the deleted text. Probe `eop`: text size 200, final mark 600, `EM_SETSEL(0,-1)`, `EM_REPLACESEL ""`,
   `EM_REPLACESEL "X"` -> X is 200 on Windows, 600 on Wine. In Inventor's Edit Dimension this showed as 8 pt text after
   select all + Delete before 141 made SCF_WORD format the mark, which now carries the right font.
2. `WM_SETTEXT` keeps the character format of the final paragraph mark on Wine (a mark made italic stays italic
   after the text is replaced); Windows resets it. todo_wine at the end of riched20's `test_EM_SETCHARFORMAT_word`.
3. `EM_SETCHARFORMAT(SCF_DEFAULT)` with a new size: on Windows text that has the default format follows
   (EM_POSFROMCHAR of the text end 34 -> 98), on Wine existing text keeps its size (probe `caret`).
4. Word breaking has no punctuation class (FIXME in `ME_WordBreakProc`): for `ab, cd.` Windows treats `,` and `.`
   as words of their own, so SCF_WORD / Ctrl+arrows / double-click selections differ.
5. `EN_PROTECTED` is not implemented (TODO list in editor.c). Inventor's Edit Dimension asks for ENM_PROTECTED; on
   Wine the `<<>>` value token can be deleted. Whether Windows prevents that there is unknown.

6. More from the review of 141 (probes `tests/r141/rescript.c`, outputs inst/141-review/out/):
   - The caret of a focused control keeps its old position after EM_SETPARAFORMAT, EM_SETFONTSIZE, WM_SETFONT and
     ITextFont setters (EM_SETCHARFORMAT got its caret update in 141).
   - EM_SETCHARFORMAT sends no EN_SELCHANGE / EN_CHANGE; Windows does when the format of text changes (notif-vm.txt
     vs notif-wine.txt).
   - EN_SELCHANGE reports SEL_MULTICHAR for an empty selection (Windows: SEL_EMPTY, seltyp 0).
   - EM_SETCHARFORMAT with flag combinations Wine doesn't know (SCF_WORD alone, SCF_USEUIRULES, SCF_ASSOCIATEFONT...)
     falls into the SCF_DEFAULT branch.
   - A windowless host that has the focus but isn't active still gets the caret calls.
   - SCF_WORD on read-only controls, protected text and hidden text differs from Windows (return value 0 on a
     read-only control when a word would be formatted; cases T14-T18, T22 of inst/141-review/cases-word.txt).
   - SCF_WORD word selection: riched20's `test_EM_SETCHARFORMAT_word` has every caret position of five texts for
     riched20.dll; the todo_wine rows are Wine formatting the word at a word start, the spaces after a word when
     the caret is in the letters, a word next to a selection, and punctuation. msftedit.dll additionally formats
     nothing with the caret between a word's letters and the spaces after it (riched20.dll and Wine: word + spaces).
   - SCF_WORD with an EDIT-style word-break proc (only WB_ISDELIMITER/WB_LEFT/WB_RIGHT): Windows formats the whole
     text, Wine nothing (the modify flag is set in both).
   - EM_UNDO after SCF_ALL: Windows returns 1 and keeps the format (redo becomes available); Wine reverts it.

## Guess at the component (guess)
riched20 `ME_GetInsertStyle` / delete paths for 1, `WM_SETTEXT` handler for 2, `ME_SetDefaultCharFormat` for 3.
