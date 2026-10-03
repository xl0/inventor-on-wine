# 160 riched20: numbered paragraphs: access violations in paint, then the MEPF_REWRAP assertion
Status: draft · Owner: - · Branch: - · Found in: review of 141 (fuzz seed 32, inst/141-review) · Reproduces on integ d18a5dcd1ef (pre-141 DLL) and on fix/141

## Symptom
A rich edit with a numbered paragraph (EM_SETPARAFORMAT with PFM_NUMBERING, or RTF with `\pn` bullets) that then
gets text with paragraph breaks (EM_REPLACESEL, EM_STREAMIN SF_RTF|SFF_SELECTION) throws access violations inside
window-proc callbacks (`err:seh:user_callback_handler ignoring exception c0000005`, three in the repro) and then
hits `caret.c:232 ~para->nFlags & MEPF_REWRAP`.

## Repro (Wine only; tests/r141/fuzz.c, build line at its top)
`wine fuzz.exe 32 182 0 10000004 -k tests/r141/fuzz-keep-numbering.txt` (the -k file keeps steps 155 171 172 179 181:
`setpf mask 0x30`, `replacesel 8`, `char 0x9`, `setsel 0 0`, `replacesel 7`). Other seeds that end in the assertion with
ITextRange::SetText excluded (`-x tomtext`, 300 steps, class 0 style 10000004): 16, 27, 44 (EM_STREAMIN of the
bullet-list RTF with SFF_SELECTION), 22 (EM_REPLACESEL), and seed 16 of class 1 style 10200044; 5 of 80 runs. EM_SETCHARFORMAT is never the last action.

## Guess at the component (guess)
riched20 `para_num_init` / `para_num_clear` and the paragraph split/join paths (para.c): a freed or missing
`para_num.style` / text used by paint.c and wrap.c, and a split that marks a paragraph after the handler wrapped.
