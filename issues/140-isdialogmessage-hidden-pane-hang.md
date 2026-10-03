# 140 Inventor freezes (100 % CPU) on the first typed character after a docked pane is closed
Status: fixed (not merged) · Owner: issue-140 worker · Branch: fix/140 (134e08b4733, 7071ce82478; base d18a5dcd1ef) · Found in: UI pass 2 (inst/ui2, inv :98, build/ 04293594c50) · Windows reference: probe on the VM (tests/r140), Inventor itself not run there

## Symptom
Inventor's main window stops answering (wintext: NOT RESPONDING on the AfxMDIFrame140u frame), main thread at 100 % CPU,
forever. User impact: any key press that produces a character after closing a docked browser pane that had the focus
(here the iLogic browser) loses the whole session.

## Steps (3 of 3 for the reporter, 1 of 1 for the fix worker on build/ 04293594c50)
1. New part. Manage tab > iLogic Browser (opens a second pane "iLogic" next to Model).
   Scripted: `INV=inv3 tools/invscen/run.sh r140` (new part + box + command `iLogic.RuleBrowser`).
2. Click the "Part1" node in the iLogic pane (focus goes into the WinForms tree).
3. Close the pane with its X.
4. Press Ctrl+Z (or any character key).
Result: freeze within a second.

## Cause (confirmed in Inventor with gdb, `inst/140/gdb-loop.txt`)
`dlls/user32/dialog.c:DIALOG_IsAccelerator` (IsDialogMessageW, WM_CHAR / WM_SYSCHAR) walks the controls from msg.hwnd
until it gets back to msg.hwnd, descending only into windows that are visible and enabled. If msg.hwnd has a hidden or
disabled ancestor below hwndDlg, the walk (climb to hwndDlg, restart at its first child) never meets msg.hwnd again.

In Inventor (window dump `inst/140/tree-closed.txt`, `tests/r140/wtree.exe`):
```
Afx:ControlBar 'iLogic'            HIDDEN (the closed pane)
  Afx:TabWnd
    Afx:ControlBar 'iLogic'        hwndDlg (MFC CControlBar::PreTranslateMessage -> IsDialogMessage)
      WinForms 'iLogic'            WS_TABSTOP, WS_EX_CONTROLPARENT
        WinForms                   HIDDEN, WS_EX_CONTROLPARENT   <- hidden by WinForms when the pane closes
          WinForms                 WS_TABSTOP                    <- focus = msg.hwnd (WinForms moved it here from the tree)
            WinForms 'Rules' (CP) > WinForms tree (focus while the pane was open)
        WinForms (CP) > WinForms, WinForms Static 'iLogic'
```
MSG = { hwnd = that focus window, WM_CHAR, 0x1a }, hwndDlg = the inner control bar. Breakpoint on the walk's back-edge
(`hwndNext = GetWindow( hwndDlg, GW_CHILD )`): 299 hits in 22 s (gdb-slowed), every round the same four windows
(WinForms 'iLogic', the visible container, its child, the Static). Their answers to WM_GETDLGCODE(0,0): 0, 0, 0,
DLGC_STATIC: nothing in the app feeds the loop, the "managed frame" sample of the third hang was a WinForms window proc
answering WM_GETDLGCODE. No second spinner: the other loops of this path terminate (below).

Focus: when the pane closes, WinForms hides the container that holds the focus after moving the focus to that
container's own first tab stop (managed `SelectNextIfFocused`, runs before the hide); MFC hides the outer control bar.
Neither Windows nor Wine moves the focus when an ancestor of the focus window gets hidden (probe: `H` / `P` trees), so
the same state is expected on Windows; not verified in Inventor itself.

## Windows ground truth (Win11 VM, `tests/r140/idm.exe`, 119 trees x 15 operations = 1785 cases)
Outputs: `inst/140/idm-vm.txt` (raw), `.brief.txt` (one line per case), Wine: `idm-wine-fix.txt`, `idm-wine-base.txt`.
Tree notation in `tests/r140/idm.c`. `x` = msg.hwnd, CP = WS_EX_CONTROLPARENT.

General (all cases):
- IsDialogMessageW returns TRUE whenever msg.hwnd is hwndDlg or a descendant: hidden/disabled dialog, hidden/disabled
  msg.hwnd, hidden/disabled containers, no control at all: 1289 of 1289 IsDialogMessage cases that return (20 don't, below).
- First message: WM_GETDLGCODE(wParam = key/char, lParam = &msg) to msg.hwnd, also when it is hidden or disabled
  (not for chars when msg.hwnd is the dialog: nothing is sent at all then).
- WM_CHAR / WM_SYSCHAR (unless DLGC_WANTCHARS/WANTMESSAGE for WM_CHAR): mnemonic search in two passes. Start control =
  msg.hwnd, or its ancestor whose parent is the dialog or a control parent. Pass 1: the start control's group in
  GetNextDlgGroupItem order. Pass 2: every control after the start control, wrapping, until the start control; it
  descends only into visible control parents (disabled ones too), and it does look at hidden/disabled controls that
  are not control parents. Each visited control gets WM_GETDLGCODE(0,0), then WM_GETTEXT unless it wants chars.
- Found push button: WM_GETDLGCODE(0,0) again, then WM_COMMAND(id, BN_CLICKED) straight to its parent; no BM_CLICK,
  focus stays. Found static / group box: focus to the next tab stop (EM_SETSEL for edits). Disabled match: no click.
- Nothing found: WM_CHAR is eaten (never dispatched, TRUE); WM_SYSCHAR ends in WM_SYSCOMMAND(SC_KEYMENU) at the
  top-level window (msg.hwnd's window proc does not see the WM_SYSCHAR).
- Tab / arrows: WM_GETDLGCODE(0,0) (arrows: also (vk, &msg)) to the next tab / group item, focus set directly
  (no WM_NEXTDLGCTL, also in non-dialog parents), then default-button bookkeeping (WM_GETDLGCODE to every control,
  BM_SETSTYLE). Enter: WM_GETDLGCODE(0,0) to the focus window, DM_GETDEFID to the dialog, WM_COMMAND(default id or
  IDOK, BN_CLICKED, GetDlgItem) to the dialog. Escape: WM_COMMAND(IDCANCEL) to the dialog.
  "The dialog" = hwndDlg, or for a WS_CHILD dialog with DS_CONTROL or CP inside a dialog the outer dialog.
- Hiding (ShowWindow(SW_HIDE), SetWindowPos(SWP_HIDEWINDOW)) or disabling an ancestor leaves the focus where it is.

Unreachable msg.hwnd (what 140 is about). "cap" = Windows gives up a pass after 1024 controls:

| tree (dialog f, pane p, x and button `&A` in p, button `&Z` in f) | key | Windows | Wine before | Wine fixed |
|---|---|---|---|---|
| p hidden or disabled, not CP (12, 18, 24, 30, 36) | z | WM_COMMAND from Z, TRUE | same (BM_CLICK) | same (BM_CLICK) |
| | a | pass 1 runs into the cap (341 rounds over f's 3 controls), not found, TRUE | clicks A (inside the hidden pane) | not found, TRUE |
| | q | as `a` | **hang** | not found, TRUE |
| p hidden or disabled, CP (15, 21, 27, 33, 39) | a | WM_COMMAND from A (inside the pane), TRUE | clicks A | not found, TRUE |
| | z, q | **hang** (silent, inside the group search) | z: clicks Z; q: **hang** | z: clicks Z; q: TRUE |
| only p (hidden, not CP) and x (60) | any | 1025 x WM_GETDLGCODE to p, TRUE | **hang** | TRUE |
| dialog under a hidden parent, hidden container inside, no CP (65) | any | 1025 x WM_GETDLGCODE to the top container, TRUE | **hang** | TRUE |
| Inventor's pane, CPs as above (117; 66) | char | 1024 rounds over the two visible leaf controls, TRUE, not dispatched | **hang** | one round, TRUE, dispatched |
| | Tab | **hang** (also GetNextDlgTabItem) | focus to the WinForms root | same |
| x itself hidden / disabled (42, 43) | q | 513 rounds over the 2 other controls, TRUE | TRUE | TRUE |
| dialog hidden / disabled, or under a hidden parent (46, 47, 63, 64) | any | no effect on the search | same | same |
| no visible or enabled control (50-53) | any | hidden controls are still looked at in pass 2, TRUE | TRUE | TRUE |
| WS_CHILD dialog d in dialog f, x in d: IsDialogMessage(f) with d plain (70) | q | d is one control: cap (257 rounds), TRUE | TRUE (searches d too) | same |
| d with DS_CONTROL or CP (73-81) | q | f's and d's controls searched once; Enter/Esc WM_COMMAND to f | similar, Enter picks d's button | same |

Watchdog hits (3 s): Windows 25 of 1785 (15 chars with a hidden/disabled control parent around msg.hwnd, trees 15 21
27 33 39; 10 Tab / GetNextDlgTabItem in trees 66 117 118), Wine before the fix 91 (all WM_CHAR / WM_SYSCHAR, 33
trees), Wine fixed 0.

So Windows does not hang in the Inventor shape on a character: it sends about 2000 WM_GETDLGCODE and returns TRUE.
It would hang there on Tab; whether the same focus state arises in Inventor on Windows is not verified.

## Other loops of the path (audit, Wine)
- `GetNextDlgGroupItem`: wraps at most twice (`fLooped`), plus one more round after jumping to the group start; ends
  when it gets back to hwndCtrl or, if hwndCtrl is unreachable, at the second wrap. Its inner `while (!hwndNext)`
  would spin if `GetParent()` returned 0 before reaching hwndDlg; `IsChild( hwndDlg, hwndCtrl )` rules that out
  for WS_CHILD chains (a WS_POPUP|WS_CHILD window in the chain would break it: GetParent returns its owner; not seen).
- `DIALOG_GetNextTabItem`: recursion only goes down into control parents or up one ancestor at a time; exponential in
  depth in the worst case, finite.
- `DIALOG_FindMsgDestination`, `DIALOG_IdToHwnd`, `DIALOG_FixChildrenOnChangeFocus`: parent climb / window list.
- Probe: no watchdog hit on Wine in any Tab / Shift+Tab / arrow / Enter / Escape / GetNextDlg*Item case (1190 cases),
  before or after the fix.

## Fix (fix/140)
`134e08b4733 user32: Start the accelerator search from the outermost hidden or disabled parent.`
DIALOG_IsAccelerator first climbs from msg.hwnd to hwndDlg and takes the outermost window that is not visible+enabled
as the start (and end) of the walk. That window is always reachable (everything above it is visible and enabled), so
the walk ends after one round; controls inside the hidden/disabled window are no longer searched (Windows: same for a
container that is not a control parent). Nothing changes when msg.hwnd is reachable.
`7071ce82478 user32/tests: Test IsDialogMessage() with the message window inside of a hidden or disabled window.`
(dialog > pane hidden | disabled > hidden container > msg.hwnd; a button outside is found, one inside is not, an
unmatched char returns TRUE.) Unfixed Wine: the test never returns.

Not changed (differences from Windows that predate this, see 148): unmatched WM_CHAR is dispatched, buttons are
clicked with BM_CLICK, the search order, hidden control parents, Tab/arrow handling in non-dialogs.

## Verification
- `user32_test dialog` on the VM: x86_64 490 tests 0 failures, i386 490 / 0 (478 before the new test).
  Wine fix build: 1018 tests, 0 failures, both arches. Unfixed build/ with the new test exe: timeout.
- Probe: fixed Wine 0 watchdog hits of 1785 (before 91, Windows 25). Result differences to Windows that remain
  (`tests/r140/cmp2.py`): return value 86 (74 GetNextDlg*Item, 12 Shift+Tab/Tab with no tab stop), WM_COMMAND 82,
  final focus 309, dispatched chars 405; unfixed Wine: 86 / 67 / 341 / 277 over the cases that returned. Issue 148.
- Inventor (inv3, wt/140-build): repro steps 6 of 6 without a hang (main thread idle, COM calls answered); also `q`
  and Tab in that state. Ctrl+Z with the focus in the viewport undoes (Extrusion1 gone); with the focus in the iLogic
  pane it does not undo, open or closed (the key goes to the WinForms control; same with the pane visible).
  Application Options: Tab / Shift+Tab move the focus, Down/Up switch the Spell Check radio pair, Alt+X opens Export
  ("Save Copy As"), Escape cancels it and the dialog, Enter presses the default button (Apply). Open dialog (WPF,
  not IsDialogMessage): Tab / Shift+Tab move the focus cue, Escape closes. Alt shows the key tips.
- `tools/regress.sh run wt/140-build -m '^(user32|comctl32|comdlg32)$'` (116 units, both arches) vs
  deps/regress/04293594c50: 0 worse (110 pass; user32:msg, sysparams, win fail as in the baseline).

## Probes
- `tests/r140/idm.c` (table-driven, child process per run, 3 s watchdog), `fold.py`, `brief.py`, `cmp2.py`, `cmp.py`.
- `tests/r140/wtree.c`: focus chain / subtree with styles of another process, no messages sent (works on a hung app).
- `tests/isdialogmsg_hidden.c`: the reporter's single case (tree 60).
- `inst/140/loop.py`: gdb script for the back-edge count.
