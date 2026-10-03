# 146 riched20: format of text typed after deleting everything, and other format differences (probe leftovers of 141)
Status: draft · Owner: - · Branch: - · Found in: 141 (tests/r141/scf_word.exe, VM vs wt/141-build) · Priority: low

## Symptom / evidence
Differences between Wine's riched20 and Windows (msftedit and riched20.dll agree) seen while probing 141.
None is needed for 141's fix; listed so they aren't rediscovered. VM output `inst/141/vm-scf_word.txt`, Wine
`inst/141/wine-final-scf_word.txt`.
1. Text typed after deleting the whole text takes the final paragraph mark's format on Wine; Windows keeps the
   format of the deleted text. Probe `eop`: text size 200, final mark 600, `EM_SETSEL(0,-1)`, `EM_REPLACESEL ""`,
   `EM_REPLACESEL "X"` -> X is 200 on Windows, 600 on Wine. In Inventor's Edit Dimension this showed as 8 pt text after
   select all + Delete before 141's third commit made the mark carry the right font.
2. `WM_SETTEXT` keeps the character format of the final paragraph mark on Wine (a mark made italic stays italic
   after the text is replaced); Windows resets it. Seen in riched20's `test_EM_SETCHARFORMAT_word`, which therefore
   runs its paragraph-mark case last.
3. `EM_SETCHARFORMAT(SCF_DEFAULT)` with a new size: on Windows text that has the default format follows
   (EM_POSFROMCHAR of the text end 34 -> 98), on Wine existing text keeps its size (probe `caret`).
4. Word breaking has no punctuation class (FIXME in `ME_WordBreakProc`): for `ab, cd.` Windows treats `,` and `.`
   as words of their own, so SCF_WORD / Ctrl+arrows / double-click selections differ.
5. `EN_PROTECTED` is not implemented (TODO list in editor.c). Inventor's Edit Dimension asks for ENM_PROTECTED; on
   Wine the `<<>>` value token can be deleted. Whether Windows prevents that there is unknown.

## Guess at the component (guess)
riched20 `ME_GetInsertStyle` / delete paths for 1, `WM_SETTEXT` handler for 2, `ME_SetDefaultCharFormat` for 3.
