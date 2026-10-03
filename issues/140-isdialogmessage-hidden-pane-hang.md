# 140 Inventor freezes (100 % CPU) on the first typed character after a docked pane is closed
Status: draft · Owner: - · Branch: - · Found in: UI pass 2 (inst/ui2, inv :98, build/ 04293594c50) · Windows reference: unknown

## Symptom
Inventor's main window stops answering (wintext: NOT RESPONDING on the AfxMDIFrame140u frame), main thread at 100 % CPU,
forever. User impact: any key press that produces a character after closing a docked browser pane that had the focus
(here the iLogic browser) loses the whole session.

## Steps (3 of 3 in Inventor; first time by accident, then on purpose twice)
1. New part (Standard.ipt). Manage tab > iLogic Browser (opens a second pane "iLogic" next to Model).
2. Click the "Part1" node in the iLogic pane (focus goes into the WinForms tree).
3. Close the pane with its X.
4. Press Ctrl+Z (or any character key; the first run was Ctrl+Z after the pane had been used for Edit Rule).
Result: freeze within a second. `inst/ui2/hang1/gdb1.txt`, `inst/ui2/hang2/main-thread.txt`.

## Evidence
`tools/gdb/sehbt.py` of the main thread (both runs):
```
#0 win32u NtUserCallHwndParam / NtUserMessageCall   (alternating)
#1 user32 GetWindow / SendMessageW
#2 user32 IsDialogMessageW.part.0 + 588 / 547
#3 mfc140u.dll+0x2b4ec7   (CWnd::PreTranslateInput / IsDialogMessage in MFC's PreTranslateMessage)
...  FwUI.dll+0x459fe5 .. Inventor.exe+0x265a
```
A gdb breakpoint on NtUserCallHwndParam shows the loop walking GW_CHILD (5) / GW_HWNDNEXT (2) over exactly the six windows
of the closed pane: ControlBar 'iLogic' 0x3058a, WindowsForms 0xb06ce / 0x40ac4 / 0x40a34 / 0x230a32 and the Static 0x30a2c
(`wintext`: all "hidden"; the outer Afx:ControlBar 0x10592 has no WS_VISIBLE, its child 0x3058a still has the WS_VISIBLE bit).

Third run: the sampled main-thread stack was NtUserMessageCall <- CallWindowProcW <- JIT code (a WinForms window proc),
i.e. the loop spans user32 and managed code; the 2 other samples were in IsDialogMessageW. The standalone probe below
proves that IsDialogMessageW itself can spin; whether it is the only spinner here is not proven.

## Cause (analysis, medium confidence)
`dlls/user32/dialog.c DIALOG_IsAccelerator()` (reached from IsDialogMessageW for WM_CHAR/WM_SYSCHAR) walks the tree from
msg->hwnd and stops when it gets back to msg->hwnd. It only descends into a control whose own style has WS_VISIBLE and not
WS_DISABLED. If msg->hwnd lies below a container without WS_VISIBLE (a hidden pane whose children keep their own WS_VISIBLE
bit), the walk (climb to hwndDlg, restart at its first child) never reaches msg->hwnd again: endless loop.
The keyboard focus is still inside the hidden pane (Wine does not move focus away when an ancestor of the focus window
is hidden; unverified whether Windows does, but Windows' IsDialogMessage does not hang either way).

## Probe
`tests/isdialogmsg_hidden.c`: IsDialogMessageW(frame, {hwnd=visible-bit child of a hidden child, WM_CHAR}) in a thread, 3 s
timeout. Wine (build/ 04293594c50, Xvfb :1210): `HANG: IsDialogMessageW did not return in 3 s`, exit 1. Windows: unknown.

## Guess at the component (guess)
user32 dialog.c (DIALOG_IsAccelerator: bound the walk, e.g. count visited windows or break when hwnd is not reachable;
and/or win32u focus handling when the focus window's ancestor is hidden).
