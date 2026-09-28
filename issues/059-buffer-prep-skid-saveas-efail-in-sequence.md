# 059 Buffer Prep Skid.iam save-as fails (E_FAIL after 55–90 s) in the full samples run
Status: open (triage) · Owner: - · Branch: - · Found in: samples campaign (inv3)

In 2 of 3 full runs of `tools/invscen/run.sh samples` (2022 set), "save as
Buffer Prep Skid.iam" fails with E_FAIL after 55–90 s; the same step passes when
run alone and on the VM. Inventor then crashes a few scenarios later (058).
State-dependent: accumulated resources over the run? (handles, GDI/USER objects,
memory, temp files, file locks from earlier documents, the 054 POSIX-delete
change?, a timeout inside Inventor's save). Triage: run the full sequence with
resource monitoring (handle/GDI counts via a small probe, RSS, open fds of the
Inventor process), Inventor's own logs around the save, WINEDEBUG=+file errors
during the save; bisect the sequence to the minimal set of prior documents.
