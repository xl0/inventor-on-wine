# 161 riched20: undo stack assertion (undo.c:424) after a link format at an insertion point
Status: draft · Owner: - · Branch: - · Found in: review of 141 (fuzz seed 19, inst/141-review) · Reproduces on integ d18a5dcd1ef (pre-141 DLL) and on fix/141

## Symptom
`Assertion failed: undo->type == undo_end_transaction || undo->type == undo_potential_end_transaction, file
dlls/riched20/undo.c, line 424` in EM_UNDO.

## Repro (Wine only; tests/r141/fuzz.c, build line at its top)
`wine fuzz.exe 19 342 0 10000004 -k tests/r141/fuzz-keep-undo.txt` (keeps steps 190 339 340 341): EM_AUTOURLDETECT,
EM_SETCHARFORMAT(SCF_SELECTION, CFM_LINK | CFM_ITALIC) with an empty selection, EM_REPLACESEL (can undo), EM_UNDO.
Same with `-w` (the fuzzer's SCF_WORD bit removed), so it is not the SCF_WORD path of 141.

## Guess at the component (guess)
riched20 undo.c / link handling: an undo item pushed outside a transaction (ME_UpdateLinkAttribute after the
commit, or the insert style's link bit) so the stack top is not an end-of-transaction marker.
