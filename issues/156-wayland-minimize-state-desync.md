# 156 winewayland: after ShowWindow(SW_MINIMIZE) Win32 restores the window by itself while the compositor keeps it minimized
Status: draft · Found in: review of fix/134 (inst/134-review, `rv min`) · predates fix/134

## Symptom
`rv.exe min` (inst/134-review/rv.c): A shown, `ShowWindow(A, SW_MINIMIZE)` -> `IsIconic(A)=1`; the driver sends
`xdg_toplevel.set_minimized`. With nothing else happening, 3 s later `IsIconic(A)=0`, `IsWindowVisible(A)=1`
(inst/134-review/out/f_min.log: "before D: A iconic=0 visible=1"), while A is not on screen (0 red px): mutter still has it
minimized. `+waylanddrv`: `wayland_configure_window hwnd=... restoring from minimize`.
Consequence with fix/134: a window owned by A that is shown now gets A as parent and mutter hides it with its minimized
parent (0 green px; on integ the unparented D is visible, 105592 px). Whether a later `ShowWindow(A, SW_RESTORE)` brings A
back was not checked.

## Cause (read from the code, not verified further)
window.c `wayland_configure_window`: "restoring from minimize" is taken whenever a configure arrives while the last window
config was minimized at the -32000 sentinel position. mutter sends a configure in response to `set_minimized` itself, so the
driver answers its own minimize with `SC_RESTORE`. xdg-shell has no "minimized" state and no un-minimize request: the client
cannot know whether the compositor still has the toplevel minimized, nor ask to show it again (short of an activation token).

## Open
Component: winewayland.drv. Needs a way to tell the configure caused by set_minimized from a user un-minimize (e.g. only
treat a configure as restore when the surface also got the `activated` state or keyboard focus).
