# 179 openbox: switching desktops minimizes Wine windows; owned windows come back incompletely
Status: draft (found by worker-175, not investigated) · Owner: - · Branch: - · Found in: 175's probe matrix (integ b5d75449ffe, openbox 3.6)

## Symptom
openbox sets WM_STATE Iconic on windows that are on a hidden desktop (ICCCM reading: not viewable = iconic).
winex11 treats WM_STATE Iconic as "the user minimized the window": the Win32 window gets SC_MINIMIZE
(WM_SIZE SIZE_MINIMIZED, rect -32000,-32000, owned popups hidden by win32u) when the user switches to another
desktop or moves the window to one, and is restored when it becomes visible again. Seen with tests/r175/owned.exe:
`xdotool set_desktop 1` -> owner iconic, bar / dlg / tool hidden; `set_desktop 0` -> restored.
Consequences seen in tests/r175/scen.sh on openbox (same on integ and with 175's fix):
- a window that can't be minimized (the dialog owned by a dialog) is re-mapped by winex11 and so lands on the
  current desktop: it stays behind alone when its owners move to another desktop;
- under openbox + picom, after the first cycle the owned dialogs / tool window of the probe were no longer on
  screen on either desktop (only the owner and the override-redirect popups), until the owner was moved again;
- a dialog owned from another process was not shown again after such a cycle.
An application also sees a minimize on every desktop switch (Inventor: not checked what it does with it).
awesome doesn't do this (it unmaps its frame and leaves WM_STATE Normal); mutter / KWin keep windows mapped.

## Question
Whether winex11 should tell "iconified by the user" from "on another desktop" (`_NET_WM_DESKTOP` vs
`_NET_CURRENT_DESKTOP`, `_NET_WM_STATE_HIDDEN`: on the hidden desktop the probe's owner had WM_STATE Iconic and an empty `_NET_WM_STATE`) and
not minimize in the second case. Upstream behaviour, low priority unless openbox matters to the user.
