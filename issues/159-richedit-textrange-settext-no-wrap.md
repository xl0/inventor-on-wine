# 159 riched20: ITextRange::SetText leaves the paragraph unwrapped (MEPF_REWRAP assertion at the next caret update)
Status: draft · Owner: - · Branch: - · Found in: review of 141 (fuzz, inst/141-review) · Reproduces on integ d18a5dcd1ef (pre-141 DLL) and on fix/141

## Symptom
Same assertion as 141 (`caret.c:232 ~para->nFlags & MEPF_REWRAP`) from another path: after `ITextRange::SetText`
the next focus change, selection change or ITextRange::ScrollIntoView asserts.

## Repro (Wine only; probe sources in tests/r141/, build lines at their top)
`wine rescript.exe text:abc tomtext:0,1,X focus:1` -> assertion. (`tomtext:A,B,STR` = ITextDocument::Range(A,B) +
SetText; `focus:1` = SetFocus on the control.) With `fuzz.exe SEED 300 0 10000004` it is the last action before the assertion for 39 of seeds 1-40.

## Evidence
`ITextRange_fnSetText` (richole.c ~1590-1634) calls `ME_InternalDeleteText` / `ME_InsertTextFromCursor` and returns:
no `ME_WrapMarkedParagraphs`, no scroll bar or caret update, no `ME_CommitUndo`. It also saves `pCursors[0]` before
the edit and puts it back afterwards; the saved cursor can point into a run that the edit split or freed.
`ITextSelection::SetText` goes through the same function.

## Inventor
Not reached in the sessions traced for 141 (`WINEDEBUG=+richedit`, inst/141/trace-final.log: Edit Dimension with
typing, a symbol, delete all; Format Text with typing, CJK paste, re-edit, Bold, typing over a selection). The only
TOM calls Inventor made there are ITextDocument::Range, ITextRange::GetFont/GetStart/GetEnd and ITextFont
Reset/SetUnderline (125). Other Format Text features (stacked text, parameters, symbols) were not traced.

## Guess at the component (guess)
riched20 richole.c: wrap + update like `ME_ReplaceSel` does, and convert the saved cursor through a character
offset instead of copying the ME_Cursor.
