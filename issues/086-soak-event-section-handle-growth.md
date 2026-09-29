# 086 Long sessions: unnamed Event and Section handles grow ~200 per suite
Status: open (draft) · Owner: - · Branch: - · Found in: soak #2 (inst/soak/2026-09-29, integ bbc7f82accb)

## Symptom
One Inventor session running `tools/invscen/run.sh all` repeatedly: kernel handles
2.5k → 12.6k over 4 h (+2.25k/h); at the end Event 7886, Section 2202 (Key 391,
Mutant 155 — those were fixed by 067/072). ~190-240 unnamed events per suite,
same rate as soak #1. RSS grows +2.8 GB/h (mappings +3k/h), not explained by COM
stubs (flat since 072). Data: inst/soak/2026-09-29/{summary,trends}.txt,
handles-iter*.txt, stubs.txt (not in git); tools/soak/.

## Task
Find who creates and never closes the events/sections (Wine component or the
application), decide whether Windows leaks the same (VM: same scenario loop,
handle counts via Task Manager-equivalent / GetProcessHandleCount — note 075),
fix Wine-side leaks, and see how much of the RSS/mapping growth they explain.
