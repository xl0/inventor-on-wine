# 059 HWND generation reaches 0x8000: sign-extension mismatch leaves MFC with a stale HWND (Buffer Prep Skid save-as E_FAIL, then 058 crash)
Status: open (root cause found, needs fix) · Owner: - · Branch: - · Found in: samples campaign (inv3)

## Symptom
In 2 of 3 full runs of `tools/invscen/run.sh samples` (2022 set), "save as Buffer Prep Skid.iam" fails with
E_FAIL after 40–95 s; the same step passes when run alone and on the VM. Inventor then crashes a few scenarios
later (058). The state is sticky for the Inventor session: afterwards every big-assembly open/reopen fails with
E_FAIL in 0.4 s (so the failed "reopen" is not a corrupt copy), and Fan Cover Mold Rebuild2 crashes (058).

## Cause
Inventor's status bar (FwUI.dll `FWxStatusBar`) creates a read-only child EDIT (id 0x25af, a CEdit member at
+0x280, `m_hWnd` at +0x2c0) when progress starts (FUN_180252c20) and destroys it via `CWnd::DestroyWindow`
when progress ends (FUN_180251370). Status text (`SetPaneText` -> FUN_180253450) goes to that edit when its
`m_hWnd` is non-zero: `CWnd::SetWindowText`, whose `ENSURE(::IsWindow(m_hWnd))` throws CInvalidArgException.

Wine's server wraps a USER handle's generation at 0xffff; Inventor churns windows and the free list is LIFO,
so after enough windows in a session the edit gets an HWND with bit 31 set. Wine then hands out two 64-bit forms
of that handle:
- CreateWindowExW returns it sign-extended (0xffffffff8cea0474), which MFC puts in its permanent handle map;
- WM_DESTROY / WM_NCDESTROY (also WM_SHOWWINDOW, WM_MOVE, GetWindow(GW_CHILD)) carry it zero-extended
  (0x000000008cea0474, win32u `USER_HANDLE_FROM_INDEX` uses UlongToHandle).

AfxWndProc can't find the CEdit for the zero-extended hwnd, so `OnNcDestroy` never clears `m_hWnd`. The next
progress start sees `m_hWnd != 0` and doesn't create a new edit; the next status text throws
CInvalidArgException (mfc140u) through rse.dll -> AmSrv -> FwSrv, which RxAssembly's SaveAs/Open turns into
E_FAIL. Before the throw, the dependents are saved in place and the copy is written only partially (it lacks
the CacheGraphics/LWUFRx and Meta streams). Whether a save fails depends on the edit's generation when it is
created, so the failure is intermittent and shows up only after long sessions.

Evidence (inst/invscen/inv3/059/, not in git):
- `relay-edit-8cea0474.txt`: +relay of CreateWindowExW/DestroyWindow plus window procs. The edit comes back as
  ffffffff8cea0474 and gets WM_NCDESTROY as 000000008CEA0474. On the next save, the throw has
  r10=ffffffff8cea0474.
- `inv-s3.log` (+seh,+unwind): the throw's stack is kernelbase RaiseException < vcruntime <
  mfc140u+2301b1 < mfc140u+2b5051 < FwUI+253618 (the `CWnd::SetWindowTextW` call in FUN_180253450, called from
  FUN_180252470 / SetPaneText) < rse+d28b1 … < AmSrv < FwSrv. It is caught in RxAssembly+7bce6 (catch
  CException*). This is the only extra C++ throw compared with a passing save.
- Reproduced with only Buffer Prep Skid (`INVSCEN_ONLY="Buffer Prep"`) run repeatedly in one Inventor
  session. It failed on the 3rd or 4th save in each session. Session 1 (full sequence) failed on the 2nd run.

Ruled out (sampled every 5 s over the runs; resprobe = GdiSharedHandleTable + SystemExtendedHandleInformation):
kernel handles ~3k, GDI ~1.7k, windows ~120, RSS ~7 GB of a 75–81 GB VM size, 700 threads, 3.5k fds,
peak VMA count 30k (max_map_count 65530), disk space. warn+file shows no failing file operation.
rse.dll takes ~11k write-AVs per save, all handled by its vectored handler (dirty tracking); they occur in
passing saves too.

With build 061fa687382 (includes the 047 fix), session 1's failed save was still followed by the 058 Fan Cover
Rebuild2 crash (0x800706BE). 058's MFC CException from FwSrv is most likely this same stale status-bar edit
(not verified in that crash).

## Windows ground truth (`tests/hwnd_signext.c`, Win11 VM vs Wine)
The probe creates and destroys a child window in a loop, and compares every HWND the app sees for one window.
- Windows: over 2.5M windows the uniq (HIWORD) wraps from 0x7ffe to 1 (76 wraps). No HWND ever has bit 31
  set, and all mismatch counts are 0.
- Wine: after ~32.7k reuses, HWNDs have bit 31 set (0xffffffff800004a2 from CreateWindow). The window proc
  gets 0x00000000800004a2 for WM_DESTROY, WM_MOVE, WM_SHOWWINDOW (x2) and WM_NCDESTROY, and GetWindow(GW_CHILD)
  returns the zero-extended form.

## Task
Keep HWND generations below 0x8000 like Windows: in `server/user.c:alloc_user_entry`, wrap after 0x7ffe
(check other USER object types on the VM, e.g. menus, before applying it to all of them). Consider also making
win32u's `USER_HANDLE_FROM_INDEX` sign-extend, for consistency with `wine_server_ptr_handle`; once
generations stay below 0x8000 that is moot. Add a user32 test: create/destroy windows past 0x8000 reuses and
check that HIWORD <= 0x7ffe and the hwnd passed to the proc matches the one returned. Then rerun the full
samples sequence twice in one Inventor session. Expected: Buffer Prep Skid saves, and the Fan Cover crash (058)
doesn't happen.
