# 140 Inventor freezes (100 % CPU) on the first typed character after a docked pane is closed
Status: fixed (not merged; reworked after review) · Owner: issue-140 worker · Branch: fix/140 (fd05562178b, 58150815568, 74165bbcf61, 8f96b954ac3; base d18a5dcd1ef) · Found in: UI pass 2 (inst/ui2, inv :98, build/ 04293594c50) · Windows reference: probe on the VM (tests/r140), Inventor itself not run there

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
- Nothing found: WM_CHAR is eaten (never dispatched, TRUE); for WM_SYSCHAR a WM_SYSCOMMAND(SC_KEYMENU) arrives at
  hwndDlg (msg.hwnd's window proc does not see the WM_SYSCHAR; hwndDlg was the top-level window or a child of a
  hidden window in all probe trees, so who else would get it is not established).
- msg.hwnd is the dialog itself (after the DS_CONTROL redirection): nothing is sent, searched or dispatched for
  WM_CHAR / WM_SYSCHAR, TRUE; also for a real dialog with a matching button, whatever has the focus. Only
  WM_SYSCHAR ' ' (Alt+Space) goes on to WM_SYSCOMMAND(SC_KEYMENU). (`tests/r140/self.c`, `inst/140/self-vm.txt`;
  review probe cases hidden/disabled/empty/real_dlg_self.) IsDialogMessage(d, {d}) for a DS_CONTROL child dialog d
  of a dialog is not that case: the outer dialog is searched.
- Tab / arrows: WM_GETDLGCODE(0,0) (arrows: also (vk, &msg)) to the next tab / group item, focus set directly
  (no WM_NEXTDLGCTL, also in non-dialog parents), then default-button bookkeeping (WM_GETDLGCODE to every control,
  BM_SETSTYLE). Enter: WM_GETDLGCODE(0,0) to the focus window, DM_GETDEFID to the dialog, WM_COMMAND(default id or
  IDOK, BN_CLICKED, GetDlgItem) to the dialog. Escape: WM_COMMAND(IDCANCEL) to the dialog.
  "The dialog" = hwndDlg, or for a WS_CHILD dialog with DS_CONTROL or CP inside a dialog the outer dialog.
- Hiding (ShowWindow(SW_HIDE), SetWindowPos(SWP_HIDEWINDOW)) or disabling an ancestor leaves the focus where it is.

Unreachable msg.hwnd (what 140 is about). "cap" = Windows gives up a pass after 1024 controls:

| tree (dialog f, pane p, x and button `&A` in p, button `&Z` in f) | key | Windows | Wine before | Wine fixed (bound) |
|---|---|---|---|---|
| p hidden or disabled, not CP (12, 18, 24, 30, 36) | z | WM_COMMAND from Z, TRUE | same (BM_CLICK) | same (BM_CLICK) |
| | a | pass 1 runs into the cap (341 rounds over f's 3 controls), not found, TRUE | clicks A (inside the hidden pane) | as before (148) |
| | q | as `a` | **hang** | not found after the second wrap, TRUE |
| p hidden or disabled, CP (15, 21, 27, 33, 39) | a | WM_COMMAND from A (inside the pane), TRUE | clicks A | clicks A |
| | z, q | **hang** (silent, inside the group search) | z: clicks Z; q: **hang** | z: clicks Z; q: TRUE |
| only p (hidden, not CP) and x (60) | any | 1025 x WM_GETDLGCODE to p, TRUE | **hang** | TRUE |
| dialog under a hidden parent, hidden container inside, no CP (65) | any | 1025 x WM_GETDLGCODE to the top container, TRUE | **hang** | TRUE |
| Inventor's pane, CPs as above (117; 66) | char | 1024 rounds over the two visible leaf controls, TRUE, not dispatched | **hang** | under two rounds, TRUE, dispatched |
| | Tab | **hang** (also GetNextDlgTabItem) | focus to the WinForms root | same |
| x itself hidden / disabled (42, 43) | q | 513 rounds over the 2 other controls, TRUE | TRUE | TRUE |
| dialog hidden / disabled, or under a hidden parent (46, 47, 63, 64) | any | no effect on the search | same | same |
| no visible or enabled control (50-53) | any | hidden controls are still looked at in pass 2, TRUE | TRUE | TRUE |
| WS_CHILD dialog d in dialog f, x in d: IsDialogMessage(f) with d plain (70) | q | d is one control: cap (257 rounds), TRUE | TRUE (searches d too) | same |
| d with DS_CONTROL or CP (73-81) | q | f's and d's controls searched once; Enter/Esc WM_COMMAND to f | similar, Enter picks d's button | same |
| msg.hwnd = the dialog (49, 54, 58, 112-114) | char | nothing sent, TRUE | searches the children; hidden/disabled/childless dialog: goes on with the dialog's siblings and clicks their buttons | no search, dispatched, TRUE (8f96b954ac3) |

Watchdog hits (3 s): Windows 25 of 1785 (15 chars with a hidden/disabled control parent around msg.hwnd, trees 15 21
27 33 39; 10 Tab / GetNextDlgTabItem in trees 66 117 118), Wine before the fix 91 (all WM_CHAR / WM_SYSCHAR, 33
trees), Wine fixed 0.

So Windows does not hang in the Inventor shape on a character: it sends about 2000 WM_GETDLGCODE and returns TRUE.
It would hang there on Tab; whether the same focus state arises in Inventor on Windows is not verified.

## Other loops of the path (audit, Wine)
- `GetNextDlgGroupItem`: wraps at most twice (`fLooped`), plus one more round after jumping to the group start; ends
  when it gets back to hwndCtrl or, if hwndCtrl is unreachable, at the second wrap. Its inner `while (!hwndNext)`
  spins when `GetParent()` does not lead to hwndDlg (found by the review, not fixed here, 148): a container with
  WS_CHILD | WS_POPUP (GetParent returns its owner; Wine's IsChild accepts the chain, Windows' doesn't), and
  `GetNextDlgGroupItem( GetDesktopWindow(), 0, FALSE )`. Not on Inventor's path.
- `DIALOG_GetNextTabItem`: recursion only goes down into control parents or up one ancestor at a time; exponential in
  depth in the worst case, finite.
- `DIALOG_FindMsgDestination`, `DIALOG_IdToHwnd`, `DIALOG_FixChildrenOnChangeFocus`: parent climb / window list.
- Probe: no watchdog hit on Wine in any Tab / Shift+Tab / arrow / Enter / Escape / GetNextDlg*Item case (1190 cases),
  before or after the fix.

## Fix (fix/140), final design after the review
1. `fd05562178b user32: End the accelerator search when it wraps around a second time.`
   A `looped` flag (the idiom of `fLooped` in GetNextDlgGroupItem): the first time the walk climbs to hwndDlg it
   restarts at the first child, the second time it ends. No start-point change, so every case that returned before
   returns the same (checked: all logged lines of the 1694 probe cases that returned on unfixed Wine are identical).
   It also ends when a WM_GETDLGCODE handler hides, disables or destroys a window on the path during the walk.
2. `58150815568 user32/tests: Test IsDialogMessage() with an unreachable message window.`
   Hidden top-level > pane (hidden | disabled | visible and hidden by a WM_GETDLGCODE handler during the search) >
   container > msg.hwnd. Unmatched char returns TRUE, a button outside the pane is found, a button inside is not
   (todo_wine: Wine clicks it, 148). A WM_GETDLGCODE counter destroys the window at 10000 (Windows needs ~1025 per
   pass), so unfixed Wine fails (15 failures) instead of blocking.
3. `74165bbcf61 user32/tests: Test IsDialogMessage() with a char message for the dialog.` (todo_wine), then
   `8f96b954ac3 user32: Don't search for accelerators if the message is for the dialog.`
   `if (hwnd == hwndDlg) return FALSE` in DIALOG_IsAccelerator. Before, the walk for a hidden, disabled or childless
   dialog went on with the dialog's next sibling and clicked matching buttons there, another top-level window
   included; for a visible dialog it searched the children, which Windows doesn't do. The message is still
   dispatched afterwards (Windows eats it, except Alt+Space; that is 148's item 1).
   Who could rely on the old behaviour: only code paths where the key message's window is the dialog, i.e. the
   focus is on the dialog itself or nowhere (keys then go to the active window). That happens when the dialog has no
   visible enabled control (nothing to find), after the focused control was hidden (focus goes to the parent), with
   an explicit SetFocus(dialog), or for an MFC bar/frame that has the focus itself. Mnemonics then worked on Wine and
   don't on Windows. comctl32's property sheet puts the focus on a control or button; no in-tree caller or test
   depends on it (user32, comctl32, comdlg32 units unchanged). Risk left: a Wine focus bug that leaves the focus on
   a dialog would now also lose its mnemonics.

First version (134e08b4733, dropped): started the walk at msg.hwnd's outermost hidden/disabled ancestor. The review
showed it stayed unbounded under tree changes during the walk and changed the WM_COMMAND result of 46 char cases
that returned before (the review's counts, not recomputed: 31 that agreed with Windows no longer did, in hidden/disabled
control parents and DS_CONTROL containers, where Windows does find a control inside; 16 newly agreed, plain containers).

Not changed (differences from Windows that predate this, see 148): unmatched WM_CHAR is dispatched, buttons are
clicked with BM_CLICK, the search order and scope, Tab/arrow handling in non-dialogs.

## Verification (final branch, 8f96b954ac3)
- `user32_test dialog` on the VM: 559 tests, 0 failures, x86_64 and i386 (478 without the new tests). Wine: 1087
  tests, 96 todo (93 old + 3), 0 failures, both arches. Unfixed build/ with the test exe of 58150815568: 15 failures,
  no hang.
- Probe `idm.exe`: 0 watchdog hits of 1785 (unfixed 91, Windows 25). Bound-only build vs unfixed Wine: identical in
  all 1694 cases that returned. Final build vs bound-only: 25 cases differ, all WM_CHAR/WM_SYSCHAR with msg.hwnd ==
  hwndDlg (trees 49 54 58 113 114). Differences to Windows (`tests/r140/cmp2.py`), bound-only / final: return value
  86 / 86, WM_COMMAND 67 / 62, final focus 341 / 336, dispatched chars 358 / 363 (unfixed, over the cases that
  returned: 86 / 67 / 341 / 277). Issue 148.
- Review probe `inst/140-review/rv.exe` (outputs `inst/140/rv-*.txt`): the mutation cases (mut_destroy_x_hidden,
  mut_destroy_p_hidden, mut_destroy_x_visible, mut_hide_p, mut_disable_p, mut_show_p, mut_reparent_x, mut_hide_outer)
  all return; the *_dlg_self cases no longer click anything. Still not returning on Wine: popup_child_group (hangs
  on Windows too), popup_child_arrow, desktop_group: GetNextDlgGroupItem, 148.
- Inventor (inv3, wt/140-build 8f96b954ac3): repro steps 3 of 3 without a hang (main thread idle, COM calls
  answered), also `q` and Tab in that state. Application Options: Tab / Shift+Tab move the focus, Down/Up switch the
  Spell Check radio pair, Alt+X opens Export ("Save Copy As"), Escape cancels it, Enter presses the default button
  (Apply), Escape closes the dialog. Earlier build of the first version: 6 of 6, Ctrl+Z in the viewport undoes,
  Open dialog (WPF) and key tips fine.
  One Inventor start of four on that prefix died during startup (".NET: Cannot print exception string", before
  "Logger initialized"), the one launched right after switching the prefix to the new build; the next three
  started normally. Cause not looked at.
- `tools/regress.sh run wt/140-build -m '^(user32|comctl32|comdlg32)$'` (116 units, both arches) vs
  deps/regress/04293594c50: 0 worse (110 pass; user32:msg, sysparams, win fail as in the baseline).

## Probes
- `tests/r140/idm.c` (table-driven, child process per run, 3 s watchdog), `fold.py`, `brief.py`, `cmp2.py`, `cmp.py`.
- `tests/r140/wtree.c`: focus chain / subtree with styles of another process, no messages sent (works on a hung app).
- `tests/r140/self.c`: msg.hwnd = the dialog (plain top-level, real dialog, plain child, DS_CONTROL child dialog).
- `tests/isdialogmsg_hidden.c`: the reporter's single case (tree 60).
- `inst/140-review/rv.c`: the reviewer's probe (exotic trees, trees changed during the walk).
- `inst/140/loop.py`: gdb script for the back-edge count.
