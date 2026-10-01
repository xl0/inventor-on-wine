# Desktops, input desktop, cursor — checked at wine-11.18-453-g4f92c92ace1 + fix/101

- Every desktop's window belongs to an `explorer.exe /desktop` process, started by the first
  thread that needs it (`win32u/winstation.c:get_desktop_window`; inherits that process' stderr).
  The server closes the desktop when only the owner's threads remain: `server/winstation.c:
  remove_desktop_user` arms a 1 s timeout (`desktop->users == owner->running_threads`), then
  unlinks the name and posts WM_CLOSE; any new user cancels it. An exiting thread is still in
  `running_threads` during `cleanup_thread` (fix/101 accounts for that; before, an explorer thread
  exiting with one foreign user attached killed explorer under a running app).
- winex11 cursor clip window: an InputOnly X window created by the desktop thread
  (`X11DRV_CreateWindow` for the desktop hwnd), published as a desktop window prop; every thread
  caches it once (`init_clip_window`) and XUnmapWindow's it on each WM_WINE_CLIPCURSOR (sent to
  the foreground thread on every foreground change). A dead owner = BadWindow, process killed.
  The cache is never reset on SetThreadDesktop (latent).
- Without a virtual desktop all desktops' windows are on the one X screen. A host event (motion,
  crossing, button) on a window makes that window's desktop the input desktop
  (`server/queue.c:get_hardware_input_desktop` + `set_input_desktop` in send_hardware_message).
  Injected input (SendInput/mouse_event/keybd_event) from a thread not on the input desktop fails
  with ERROR_ACCESS_DENIED. So a window of a hidden desktop under the pointer can steal the input
  desktop (user32:input rawinput test 16, fix/101 test change).
- `NtUserGetCursorPos` returns the server position only if it changed in the last 100 ms, else
  it asks the driver (X pointer). Anything that moves the server cursor without moving the host
  pointer shows up only after 100 ms (ClipCursor before fix/101; mouse moves clamped by a clip).
- winex11 confines the pointer for ClipCursor only when the X input focus is on a window of the
  calling process (`grab_clipping_window`); otherwise clipping is server-side only.
