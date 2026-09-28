# 046 Intermittent Inventor startup crash in comctl32 SetWindowSubclass
Status: open (draft) · Owner: - · Branch: - · Found in: 040/041/043 verification

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

## Next
Check comctl32 subclass bookkeeping when a subclass is removed from inside its
own proc / during WM_NCDESTROY, and when the window is destroyed while nested
DefSubclassProc calls run (Windows keeps the node alive until the stack unwinds).
