# 123 CJK font linking only works with Noto Sans CJK installed
Status: open · Owner: - · Branch: - · Found in: user's laptop (squares after 119, 2026-10-02)

119 links Tahoma to "Noto Sans CJK JP/SC/TC/KR" by name. Hosts without those exact families
(other CJK fonts: WenQuanYi, Droid Sans Fallback, Source Han, IPA, Takao, AR PL, system fonts
with other names, or Noto CJK under another family name) still show boxes. The user's laptop
shows squares for CJK (font set unknown yet: `fc-list :lang=ja family`).

Task: choose the CJK link fonts via fontconfig by language coverage (ja, zh-cn, zh-tw, ko),
ordered by the ANSI code page like 119, instead of hard-coded names (keep Noto first when
present). Same care as 119: only when Tahoma exists; one face per needed coverage; Latin
metrics unchanged; DirectWrite's fallback list has the same hard-coded names (dlls/dwrite) —
check and align. Tests: gdi32:font test_font_link must pass with only a non-Noto CJK font
visible (use a private fontconfig config in the test environment to hide Noto).
