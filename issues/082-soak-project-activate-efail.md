# 082 Inventor: DesignProject.Activate E_FAIL after ~3 h, then every SaveAs E_INVALIDARG
Status: open (draft) · Owner: - · Branch: - · Found in: soak #2 (inst/soak/2026-09-29, integ bbc7f82accb)

## Symptom
Single Inventor session (tools/soak/soak.sh, inv/:98), 3.4 h in. The 6th `samples` run (iter 18) completed all
docs (463 PASS, 1599 s), then its final "restore project" step (`DesignProject.Activate(true)` of Default.ipj,
switching back from samples.ipj) failed with E_FAIL. From then on:
- every later `samples` run fails at once in "restore pristine samples" (Activate of Default: E_FAIL);
- from iter 22 on, asmcon "make parts" fails every run: `Document.SaveAs` → E_INVALIDARG (other scenarios'
  SaveAs keep passing: part, asm, drawing, asmbig, export). Also in the late suite ~3 h later.
Iterations 19-21 (samples.ipj still active) passed asmcon, so it isn't just "wrong project active".
Soak #1 (integ c036c687c47) left samples.ipj active after its timed-out samples runs and never hit this.

## Evidence
inst/soak/2026-09-29/iter18/samples.txt, iter21/samples.txt, iter22/asmcon.txt (stack: H.Save → Document.SaveAs),
events.txt. Not in git.

## Next steps
- Reproduce: long session, or Activate Default after a samples run; check Inventor's log / a dialog, `+file` on
  the Activate call (sharing violation on samples.ipj or the workspace?).
- Check the asmcon save path under the active project (box.ipt/plate.ipt in the scenario dir).
- Unclear whether Wine or Inventor state; Windows unchecked.

## Soak #3 (2026-10-01, integ 492d5679270)
Not reproduced: one session ran 4.1 h / 33 suites / 11 samples runs, "restore project" (Activate Default.ipj) passed
every time, including at 3.6 h and 4.1 h (soak #2: failed in the 6th samples run, 3.4 h); asmcon SaveAs stayed green.
Another session was lost at 2.6 h to 109 (unrelated). Possibly timing-dependent (samples runs now take ~1000 s instead of
~1500 s); keep open as not reproducible.
