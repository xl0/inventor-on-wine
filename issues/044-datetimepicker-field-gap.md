# 044 DateTimePicker short date drawn with a gap: "9 /28/2026" (low)
Status: open (draft, low) · Owner: - · Branch: - · Found in: UI test campaign (iProperties > Project)

## Symptom (integ d53133a66a1)
iProperties > Project > Creation Date shows "9 /28/2026"; Windows shows
"9/28/2026". Reproduced with a plain comctl32 DateTimePicker
(`tests/dtp_short.c`: DTS_SHORTDATEFORMAT, with and without DTS_SHOWNONE,
date 2026-09-28, en-US short date "M/d/yyyy").
- Wine (left) vs VM (right): ![dtp](attachments/044-dtp-wine-vs-vm.png)
- Inventor: ![iprops](attachments/044-iprops-date-wine.png)

## Suspect
comctl32 datetime.c lays out fields with a fixed width per field (month
sized for two digits) instead of the text's actual extent, so a 1-digit
month leaves a gap before the separator. Cosmetic.
