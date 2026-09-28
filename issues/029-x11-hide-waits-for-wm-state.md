# 029 winex11: hidden managed windows stay mapped on an X server without a window manager
Status: wontfix (test-environment gap: no WM on :98; coordinator will run openbox there) · Owner: – · Branch: – · Found in: prefixes/inv on :98 (integ 6c63dc3c499), Inventor trial welcome dialog

## Symptom
Inventor's "Welcome to your trial" dialog (AdskLicensingAgent, WebView2 in a `webview`
class WS_POPUP, ex WS_EX_TOOLWINDOW, 860x500) was closed with its X button. Win32 side: the
window is hidden (no WS_VISIBLE, IsWindowVisible 0). X side: its window stays mapped
(`xwininfo` Map State IsViewable), so a white 860x500 rectangle covers Inventor Home and
eats input there. `xdotool windowunmap` removes it (local workaround).
Screenshot: [029-hidden-popup-stays-mapped.png](attachments/029-hidden-popup-stays-mapped.png).

## Repro (cheap, any prefix, WM-less X display like :98)
Toplevel WS_POPUP window, ShowWindow(SW_SHOW), pump messages 50 ms or 1 s,
ShowWindow(SW_HIDE), keep pumping. X window stays IsViewable in both cases.
(Test source was a 30-line scratch program: CreateWindowEx(WS_EX_TOOLWINDOW, WS_POPUP) +
ShowWindow; add it to tests/ if the fix needs one.)

## Cause (winex11 window.c, window_set_wm_state)
The window is activated on show, so is_window_managed() says managed. Mapping it requests
WM_STATE 0 -> NormalState and sets `wm_state_serial` (trace: "requesting WM_STATE 0 -> 0x1
serial 114", then MapNotify). Only a window manager writes the WM_STATE property; with no WM
the PropertyNotify never comes, `wm_state_serial` never clears, and every later map/unmap
(and config/_NET_WM_STATE, see the `wm_state_serial` checks in window.c) is deferred forever.
Unmanaged windows clear the serial right away ("override redirect windows won't receive
WM_STATE property changes").

## Notes
- Our :98 (x/start.sh) runs no window manager and none is installed on the host. Real
  desktops have one, so this may be "WM required" for upstream. Options: run a WM on :98
  (infra; needs a package), Wine virtual desktop mode, `Managed`=N in
  HKCU\Software\Wine\X11 Driver (all windows unmanaged: no WM_STATE wait, Wine draws the
  non-client area), or make winex11 not wait for WM_STATE when no WM is present
  (_NET_SUPPORTING_WM_CHECK). Coordinator to decide.
- Related: Inventor's main frame (WS_CAPTION, managed) is mapped without its caption (X window
  at y=26, nothing drawn above it: Wine leaves decorations to the WM, and there is none).
