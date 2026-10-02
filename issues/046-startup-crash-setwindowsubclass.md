# 046 Intermittent Inventor startup crash in comctl32 SetWindowSubclass
Status: fixed (pending confirmation in Inventor) · Owner: 046 worker · Branch: fix/046-subclass-thread · Found in: 040/041/043 verification

## Symptom (integ 13704a2e74b, prefixes/inv, :98)
Cold start via `tools/invscen/run.sh hello`: Inventor died ~21 s in (CER dialog,
connect timed out); the next start worked. CER counts 11 crashes in this prefix,
so it recurs (unknown how many are this one vs 034).
CER: ACCESS_VIOLATION read at -1 in winsxs comctl32 v6. Minidump
(`winedbg <dmp>` in the prefix) backtrace, main thread:

    0 SetWindowSubclass(hWnd=0x300ca, pfnSubclass=fwui+0xd2de0, id=0, ref=0x627495d0)
      commctrl.c:1071 `if ((proc->id == uIDSubclass) &&` -- proc = 0x525f59545245504f ("OPERTY_R")
    1 fwui+0xd3867
    2 DefSubclassProc(hWnd=0x100e2, msg=0xc0c3 (registered), wp=1, lp=0)
    3 COMCTL32_SubclassProc ... DispatchMessageW <- fwui <- mfc140u <- Inventor

The subclass list of hwnd 0x300ca holds a pointer overwritten with string data:
a freed SUBCLASSPROCS node (use-after-free) or a stale/reused "CC32SubclassInfo"
prop. Dump kept at prefixes/inv/drive_c/users/xl0/AppData/Local/Temp/Inventor260928134655.dmp.

## Findings (046 worker)
Caller (FwUI.dll FUN_1800d37e0, a subclass proc on 0x100e2 reacting to a registered message,
wp=1): `hwnd = GetWindow(main_frame, GW_ENABLEDPOPUP); SetWindowSubclass(hwnd, ...)`, and on
wp=2 `RemoveWindowSubclass` of the stored hwnd. That popup may belong to another thread or
process. Wine's subclass functions had no thread/process check: GetPropW on another process's
window returns a pointer into that process, and another thread's list can change under us.
The small dump has no heap, so the owner of 0x300ca can't be proven; this is the only path
in the code that produces "a list we don't own". No `C:\winedbg-crash.log` exists in inv.

Windows ground truth (Win11, `tests/subclass_probe.c`, x64):
- v5 uses prop `CC32SubclassInfo`, v6 `UxSubclassInfo`: separate lists and window procs.
- SetWindowSubclass on another thread's window fails (last error untouched). Get/Remove work
  from another thread of the same process; Remove then leaves the prop and window proc until
  the window's thread handles its next message. All four fail on another process's window.
- Removing a subclass from a nested call, before the outer call reaches it: the outer
  DefSubclassProc skips it (Wine: used the freed entry -> execute fault).
- Removing the last subclass while another wndproc sits on top keeps the prop and chain
  (Wine: freed it, then every message through our proc was dropped with an ERR).
- Subclass procs see WM_NCDESTROY; GetWindowSubclass still succeeds after DefSubclassProc there.

Fix (4 commits, subclass tests extended; pass on VM + Wine, x86_64 + i386):
thread/process checks; per-call position frames (nested-removal UAF); keep data while
another wndproc is on top; v6 uses `UxSubclassInfo`.
regress (comctl32|user32|shell32|comdlg32) vs integ 42f83791: 0 REAL, 3 FLAKY (user32 input/winstation).
Not fixed: subclass data leaks when a subclassed window is destroyed (no WM_NCDESTROY
cleanup); cross-thread DefSubclassProc still reads the other thread's data.
If it recurs: winedbg log (AeDebug, notes/wine/debugging.md) plus `GetWindowThreadProcessId`
of the SetWindowSubclass hwnd would confirm which owner it was.

## Local workstation reproduction
Old build `47e296ffde4d` reproduced this on 2026-09-28 at 18:56 and 19:01.
Both CER dumps fault at comctl32 v6 +0x722d9: `SetWindowSubclass`, old
`commctrl.c:1071`, reading `proc->id`. Latest dump has
`rax=0x0303030303030303`, target hwnd `0x7010c`, and exception parameters
`[0, 0xffffffffffffffff]`. Private local dump:
`prefixes/inv/drive_c/users/xl0/AppData/Local/Temp/Inventor260928190131.dmp`.
The cursor trace routes that same hwnd's `WM_WINE_SETCURSOR` to process 0294,
while crashing Inventor is process 0604: consistent with foreign-window access.
Rebuilt to `38e4c1c00c` and resumed interactive use. Later captured local
crashes have different signatures: the WPF font-fallback FailFast
([083](083-wpf-keytip-font-fallback.md)) and the CoreCLR crash
([124](124-open-dialog-resize-coreclr-crash.md)). No formal replay of the
original failing interaction was performed.
