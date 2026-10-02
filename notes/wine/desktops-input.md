# Desktops, input desktop, cursor — checked at wine-11.18-453-g4f92c92ace1 + fix/101

- Every desktop's window belongs to an `explorer.exe /desktop` process, started by the first
  thread that needs it (`win32u/winstation.c:get_desktop_window`; inherits that process' stderr).
  Its parent is that app, so a DEBUG_PROCESS debugger of the app used to get explorer as a
  debuggee too (and killed the desktop on exit); fix/105 gives it a private debug object, detached before it runs.
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
  pointer shows up only after 100 ms. fix/101: ClipCursor warps the host pointer when it moves
  the cursor, and the driver position is clamped to the shm clip rect.
- All desktops share the X pointer: SetCursorPos on a non-input desktop warps it (Windows: fails).
  A new desktop's shm clip is (0,0,0,0) until its desktop window is sized, so the server clamps
  cursor positions there to (0,0). A thread's desktop handle is closed when the thread exits
  (`release_thread_desktop`), unlike Windows.
- winex11 confines the pointer for ClipCursor only when the X input focus is on a window of the
  calling process (`grab_clipping_window`); otherwise clipping is server-side only.
- Fake mouse moves (server `update_cursor_pos` -> 16 ms per-desktop timeout -> `set_cursor_pos`): Windows
  posts WM_MOUSEMOVE (extra info 0, no pointer messages) to the window under a still cursor ~16 ms after
  any window is shown, hidden, moved or resized (not after a no-op SetWindowPos), one per burst. fix/120
  matches that (before: immediate, moves/resizes of visible windows only, pen signature 0xff515700).
  Consumers must ignore unchanged positions like Windows' do: the menu loop (fix/120), Chromium's
  tooltip controller. A window sliding under a still cursor changes the client position, so Chromium
  shows HTML title tooltips then, on Windows too. The moves land in the GetMouseMovePointsEx history.
  Probes: tests/hover_tooltip.c, tests/fake_mousemove.c, tests/r120/ (120, 122).
