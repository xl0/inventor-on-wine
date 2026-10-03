# 158 wineserver: a window keeps the handle of its owner after the owner died with its thread
Status: draft · Found in: review of fix/134 (inst/134-review, `rv texit`) · predates fix/134 · Windows ground truth missing

## Symptom
`rv.exe texit` (inst/134-review/rv.c): G main window; thread T creates P owned by G; the main thread creates D owned by P;
T exits without DestroyWindow. Afterwards (inst/134/rv/texit.log, same in inst/134-review/out/texit.log):
```
after thread exit: IsWindow(P)=0 GW_OWNER(D)=000000000001006e D visible=1     <- 1006e is the dead P
now G owned by D: prev=0000000000000000 err=1400                              <- SetWindowLongPtr(G, GWLP_HWNDPARENT, D)
GW_OWNER(G)=0000000000000000
```
D survives its owner, `GetWindow(D, GW_OWNER)` still returns the destroyed window's handle, and making another window owned
by D fails with ERROR_INVALID_WINDOW_HANDLE (presumably the server's owner-loop check walking into the dead handle).

## Open
Component (guess): server/window.c, thread-exit window destruction (owned windows of other threads are neither destroyed
nor un-owned, unlike DestroyWindow). What Windows does (destroy D, or clear its owner) needs a VM run of `rv texit`.
Drivers see the stale handle too: GA_ROOT of it fails, so winewayland treats D as unowned at its next position change.
