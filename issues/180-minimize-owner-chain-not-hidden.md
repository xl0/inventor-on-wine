# 180 Minimizing an owner: ShowWindow(SW_MINIMIZE) doesn't hide owned windows, SC_MINIMIZE not the whole owner chain
Status: draft (found by worker-175, not investigated) · Owner: - · Branch: - · Found in: 175's probe (integ b5d75449ffe; independent of the window manager)

## Symptom / ground truth
`tests/r175/owned.exe auto` (owner; owned layered popup, dialog, tool window, WS_EX_NOACTIVATE and plain popups, a
popup shown before the owner, and `sub`, a dialog owned by the dialog): after each step IsWindowVisible of every
owned window, exit 0 = as on Windows.
| step | Windows 11 (VM, 0 failures) | Wine (8 failures) |
|---|---|---|
| ShowWindow(owner, SW_MINIMIZE) | all 7 owned windows hidden (WM_SHOWWINDOW 0 / SW_PARENTCLOSING each, `sub` too) | all 7 stay visible |
| ShowWindow(owner, SW_RESTORE) | all shown (SW_PARENTOPENING) | - |
| WM_SYSCOMMAND SC_MINIMIZE | all 7 hidden | 6 hidden, `sub` (owned by an owned window) stays visible |
| SC_RESTORE | all shown | all shown (openbox; awesome: draft 178) |
| ShowWindow(owner, SW_HIDE) | owned windows stay visible | same |
Outputs: inst/175/vm-owned-win11.txt, inst/175/wine-owned-auto.txt.

## Where
win32u only calls `NtUserShowOwnedPopups` from DefWindowProc's SC_MINIMIZE / SC_RESTORE handling (defwnd.c), and
that walks direct owners only. Windows does it for every minimize / restore of a window and for the whole chain.
user32:win has ShowOwnedPopups tests to extend.

## Why it matters here
On X a window minimized by the app (or by winex11 following the WM: WM_STATE Iconic, e.g. every desktop switch
under openbox, 179) leaves its dialogs' dialogs — and for SW_MINIMIZE all owned windows — on screen: Inventor's
floating panes carry four owned border popups each (175), i.e. a second level under the main window.
