# Window surfaces, client surfaces, Expose (X11) — checked at wine-11.18-218-g4e819f054dd

- Toplevel GDI content lives in a window surface (win32u/dce.c, winex11
  bitblt.c x11drv_surface_*), owned by the window's process. On X Expose,
  winex11 event.c X11DRV_Expose -> win32u window.c expose_window_surface():
  with a surface it only re-flushes the surface (no WM_PAINT); without one it
  RedrawWindow()s. fix/005 also redraws the exposed part outside the surface
  clip region (client-surface areas) so the app re-presents.
- GL/Vulkan/DXGI content is a client surface (win32u window.c client_surface_*,
  winex11 init.c). "Offscreen" ones (child windows, DPI scaling, foreign-process
  windows, e.g. Chromium's GPU process drawing on the browser HWND) render to an
  X window under the dummy parent and are StretchBlt'ed onto the toplevel X
  window on each present (X11DRV_client_surface_present). Nothing keeps that
  image: an Expose loses it until the app presents again.
- Cross-process pixel format: set_window_pixel_format() on a foreign HWND posts
  WM_WINE_SETPIXELFORMAT; the owner sets clip_clients -> server
  PAINT_HAS_PIXEL_FORMAT -> surface region excludes the client rect
  (server/window.c get_surface_region), i.e. surface clip_region.
- An empty surface clip reaches x11drv_surface_set_clip as count 0 (rects
  non-NULL); clearing it passes rects NULL. fix/006 clips everything on count 0.
- Client surfaces of a *foreign* toplevel are only visible when offscreen
  (needs_offscreen_rendering: child window / DPI scaling); otherwise the client
  X window stays under the dummy parent. A foreign child's pixel format now makes
  the child's process post WM_WINE_UPDATEWINDOWSTATE to the surface owner
  (fix/027), so the owner's clip excludes it.
- Colour-keyed / alpha layered surfaces: shape bits outside the clip region
  (client surfaces) are forced opaque (fix/027). Otherwise key-coloured pixels
  under GPU children cut holes.
- Cross-process GL (wglSetPixelFormat on a foreign HWND) fails; Vulkan/DXGI works.
- GDI on an own child window of a foreign top-level draws nothing: children have no
  surface of their own and the parent's lives in the other process (dcomp.md).
- Test harness for expose bugs on :98: `xset s on; xset s activate` then
  `xset s off; xset s reset` covers and re-exposes every window. On Xvfb the
  screensaver sends no Expose; map+kill a window instead (`xlogo -geometry
  400x400+0+0 & sleep 1; kill $!`). Xvfb + lavapipe
  (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`) presents fine.
- winex11 state tracking: a managed (activated / captioned) toplevel's map request sets
  `wm_state_serial`, cleared only by the WM writing WM_STATE. On a WM-less X server (our :98)
  it never clears: later hides/moves of that window are deferred forever (029).
- Visible rect / WM decorations (win32u get_visible_rect): window rect minus the
  style NC that the host decorations replace (driver GetWindowStyleMasks); winex11
  asks for MWM title/border only when window != visible. window == visible when
  window == client, shaped, Decorated=N, or (fix/040) the client rect sticks out of
  the would-be visible rect (custom caption via WM_NCCALCSIZE).
- Apps that present on their own schedule (Inventor/OGS) don't always repaint on the fix/005
  redraw (not inside Wine's move loops). fix/061: expose_window_surface() also re-runs the driver
  present of the toplevel's offscreen client surfaces (same process only): the redirected
  offscreen X window keeps the last presented image.
- The offscreen copy runs right after the driver's present returns; NVIDIA may not have written
  the image into the redirected window yet, so the copy shows the previous frame and the last
  frame of a burst never shows (078). GLX waits (glXWaitForSbcOML / glFinish); fix/078 makes
  Vulkan wait for the queue and EGL swaps glFinish. tools/pixgrab.py WINID reads a redirected
  window's pixmap (what the app last presented), xwd -id of it returns screen garbage.
- Per-pixel-alpha layered windows get an ARGB visual; alpha-0 pixels are cut from the X shape,
  every other pixel is drawn opaque (premultiplied colour) unless a compositing manager blends.
  The X shape is also the input region; ShapeInput and InputOnly children can't reach outside it.
  A manually Composite-redirected window (no compositing manager) isn't drawn, doesn't clip what
  is below and keeps its input, but then a compositing manager can't start (one manual
  redirection per window): tried and dropped in 062.
- fix/062: a per-pixel-alpha surface whose pixels are all below alpha 16 is "hidden": while a
  compositing manager owns _NET_WM_CM_Sn the window gets _NET_WM_WINDOW_OPACITY 0 (WMs copy it to
  the frame, which hides awesome's border under picom). XFixes selection events track the owner.
  XFixesQueryExtension on gdi_display changed X timing (stale pixels in awesome's frames): the
  query uses the thread display.
- Window surfaces are rounded up to 128 px; the padding starts as opaque white. fix/062 clears it
  on UpdateLayeredWindow and adds it to the clip region (so it isn't taken for a client surface).
- winex11's surface flush can run with win_data_mutex held by the same thread (WindowPosChanged ->
  window_surface_set_shape): don't take the window data there, post a driver message.
- winex11 lock order (173; the opposite of winewayland's, 157): win32u client `surfaces_lock` (window.c) ->
  `win_data_mutex` -> win32u user lock -> window surface mutex -> leaves (gdi, font, display lock, xrender_mutex).
  The driver reads window styles / owner / text / DCs with the window data locked everywhere, so: win32u must not
  call a driver entry that locks the window data, nor draw on a window DC (a dirty DC calls pGetDC -> get_win_data),
  while it holds the user lock (a window / icon / menu pointer). What win32u does call with the user lock or a surface
  locked (surface flush / set_shape / set_clip, pReleaseDC) must never wait for the window data: trylock + posted
  retry as in try_set_window_hidden, or do it from the window's thread. In the driver, sends / SetWindowPos come after
  release_win_data. Open violations in win32u: 176 (NtUserUpdateLayeredWindow blends from a caller DC, and
  move_window_bits_surface draws on a window DC, with a surface locked: surface mutex -> user lock and -> window data).
  dce.c's `surfaces_lock` is taken around flushes (flush_window_surfaces); 171's rework keeps it out of the user lock.
- Icon bitmaps (win32u cursoricon.c) are only protected by the user lock, which also serializes selecting them into a
  DC (a bitmap goes into one DC at a time): code that must not hold the user lock draws from copies (copy_bitmap), as
  NtUserDrawIconEx does for non-memory DCs since 173.
- An X window can be destroyed and recreated by a thread that doesn't own it (set_window_visual: layered attributes,
  UpdateLayeredWindow, WS_EX_LAYERED): X requests on it are only safe with the window data locked, on data->display,
  after comparing the window if the id came from an event (173: property handlers, SetWindowText; 184: ConfigureNotify,
  GravityNotify, ReparentNotify, `get_event_win_data`). The host window parent (`data->parent`: WM frame / embedder,
  owner-thread data) cannot be released by the other thread, which only sets `parent_invalid`; the new window is a
  child of the root until its ReparentNotify, so the owner drops the stale parent at the first position event (184).
  Still open: the surface flush draws into the old window (X error -> 177), the window's XIC is destroyed from the
  wrong thread (191).
- Lock-order debug build: tests/r173/lockorder-debug.patch (+ lockorder.py, cycles.py) reports every win32u / winex11
  mutex taken while another is held, with the call chain, and X errors with a backtrace; repros tests/r173/iconlock.c,
  visual_race.c, flushpost.c. Never relink a .so of a build while a test process runs on it (the process dies silently).
- Several threads allocating X resources on one Display (gdi_display: a GC per DC, pixmaps, pictures, cursors) abort
  the process in libX11 1.8.13 (`_XAllocID: Assertion ret != inval_id`, 182): `LockDisplay` fetches the next id and
  then may wait for the sequence sync reply with the display unlocked. winex11 rule since 182: a request that
  allocates an id on gdi_display, or on a thread's display when another thread may use it, is made inside
  `lock_xid_alloc( display )` / `unlock_xid_alloc( display )` (= XLockDisplay; `create_gc()`, `create_pixmap()`,
  xrender's `create_picture()` do it; not in synchronous mode). Only requests without a reply belong inside: a thread
  that waits for a reply with the display's user lock held is half of 177 (as X11DRV_expect_error regions are, also
  without synchronous mode: `X11DRV_GetImage`). XUnlockDisplay itself calls LockDisplay, where Xlib's sequence sync
  would wait for a reply with the user lock held, so lock_xid_alloc syncs first when `NextRequest -
  LastKnownRequestProcessed > 0x8000`. Xlib's locks are not fair: while one thread allocates at a high rate, other
  threads' Xlib calls on that display can wait milliseconds on a busy host. The user lock makes a locked request safe from unlocked allocators
  too (NVIDIA's EGL allocates ids on the Display inside eglCreatePbufferSurface / eglMakeCurrent, called by win32u);
  a mutex of our own would not. Find allocators with tests/r182/debug-build.patch (hooks the Display's
  `resource_alloc`) + xidsites.py.
- Threads that create their first window at the same time race in libX11's XOpenIM (global IM list, 190).
- Owned windows and the window manager (175): winex11 manages a top-level when it is activated on show, has a caption
  or thick frame, is `WS_POPUP|WS_SYSMENU`, a fullscreen popup, `WS_EX_APPWINDOW`, or owns a managed window
  (`is_window_managed`; a managed window's owner is made managed too); tool-window / layered styles don't matter. Every
  WPF `Window` has WS_SYSMENU, so borderless WPF helper windows (Inventor's pane splitter `PaneBorder`, the four
  `MiniFrameBorder`s of a floating pane) are WM clients: `_NET_WM_WINDOW_TYPE_DIALOG`, WM_TRANSIENT_FOR the owner's
  whole window (set before the map), group leader = owner. Unmanaged (override-redirect) owned popups are on every
  desktop and above everything.
  What WMs do with transients: awesome 4.3 gives one its parent's tags only when it is mapped and never moves it
  later (each client has its own tags); it hides clients of unselected tags by unmapping its frame (client
  IsUnviewable, WM_STATE stays Normal, only `_NET_WM_DESKTOP` changes). openbox moves a transient tree as a whole and
  sets WM_STATE Iconic on hidden desktops, which winex11 takes for a minimize (179; mutter in the GNOME guest
  likewise). fix/175: winex11 remembers each window's last `_NET_WM_DESKTOP` (`net_wm_desktop` in the window data);
  when a PropertyNotify brings another value, it sends the EWMH `_NET_WM_DESKTOP` request for the managed windows
  owned by that window (through other owned windows and child-window owners too) that were on the previous
  desktop, and records the new one for them at once (their own PropertyNotify is then no move, and a second
  quick owner move still finds them; Windows: an owned window is always on its owner's desktop). Windows of other
  processes are requested unconditionally (their desktop isn't known). Only a changed value counts: awesome
  rewrites the property with the same value on every tag toggle of the client, and its desktop numbers run over
  all screens (tag 1 of screen 2 = 9), so a window parked on another tag or screen has another number and stays.
  awesome removes the property when a window is withdrawn; sticky clients keep their number (0xFFFFFFFF requests
  are ignored), openbox uses 0xFFFFFFFF.
- awesome copies `_NET_WM_WINDOW_OPACITY` to its frame when it changes on a managed client, and at manage time
  unless the value is 0 (and 0 set again later is no change for it); picom only looks at the frame
  (`detect-client-opacity` off). fix/175 sets 1 instead of 0, or alpha-0 / "hidden" layered windows (062) are drawn
  as if opaque, with awesome's border. awesome's default rule
  also moves every new floating client to free screen space, and its `_NET_CLIENT_LIST_STACKING` is not the X
  stacking order (transients are kept above their parents): read `xwininfo -root -children`.
- Harness (175): picom on the headless NVIDIA Xorg shows X state changes up to ~2 s late in screenshots; wait before
  judging pixels. `tests/r175/wm.sh :N awesome|openbox [picom]` swaps the WM (awesome on a private D-Bus session,
  `ac.sh :N LUA` = awesome-client; `ac.sh :N clients` lists clients with tags / transient_for).
- One wineserver must not serve two X displays one after the other (explorer's windows live on the first): the app dies
  of BadWindow on X_UnmapWindow. `wineserver -k` between displays (077, 173).
- Moves: with _NET_WM_MOVERESIZE (openbox) the WM moves the frame, Wine waits in
  move_resize_window() (sends WM_ENTER/EXITSIZEMOVE). Without it (awesome 4.3) win32u's
  sys_command_size_move() loop does SetWindowPos per mouse move. WM-initiated moves (Mod4+drag,
  Alt+drag, keyboard moves): fix/077 posts ENTER/EXITSIZEMOVE when the WM changes the config
  while it grabs the keyboard or a button is down; the end is seen via XI2 raw button release
  (delivered despite grabs) or FocusIn(NotifyUngrab). openbox sends no client ConfigureNotify
  during a move, only the frame's (host window -> GravityNotify), the synthetic one at the end.
- A window that the WM resizes while its thread doesn't pump (hang, debugger stop, crash in progress)
  shows black padding when grown and cropped old content when shrunk (X keeps the old pixels by bit
  gravity). Tell it from stale Win32 geometry with tests/wintext.exe ("NOT RESPONDING", rect) (124).
- DCE visible regions are cached per process and invalidated only by that process' own
  window changes. NtUserGetDCEx always refreshes DCs of foreign windows and (fix/065) of
  own windows whose toplevel is foreign (GPU-process children in another app's tree);
  before that, a GPU process kept presenting through a stale region after the host hid
  the panel around it (065). `static` has CS_PARENTDC, which masks this in repros.
- Debug channels of a running process can be flipped with gdb: `nm` the .so for
  `__wine_dbch_<chan>` (one static per source file), add the mapping base from
  /proc/PID/maps, `set {unsigned char}ADDR = 8` (TRACE) / 0 (off).
- Cursors: WM_WINE_SETCURSOR goes to the thread of the toplevel under the pointer (it owns
  the X window), with the cursor of the (possibly attached, cross-process child) input.
  Cursor bits live in the creating process only (get_icon_ptr: "icon handle from other
  process"), so a foreign cursor couldn't be created there: the pointer stayed empty over
  WebView2/Chromium children (085). fix/085 posts it to the cursor's owner thread, whose
  winex11 XDefineCursor()s the foreign whole window. Check the screen cursor with x/xcur.c.
- Surface list (171): `win->surface` changes with the window pointer (= user lock) held, but the process' list of
  window surfaces (dce.c `window_surfaces`, what `flush_window_surfaces` walks) is updated after the release, as
  upstream does: `register_window_surface` in `apply_window_pos`, `destroy_window`, `free_window_handle`,
  `destroy_thread_windows`. With two threads on one window the calls for a surface come in any order, so the
  surface counts them (`registered`, under dce.c `surfaces_lock`): listed while registrations > unregistrations.
  The list holds no reference; a listed surface is alive because whoever still owes the unregistration holds one.
  Don't move the registration under the user lock (171's first fix did): `flush_window_surfaces` holds
  `surfaces_lock` while it waits for every surface mutex, and holders of a surface mutex take the user lock
  (176: `UpdateLayeredWindow` from a window DC, `move_window_bits_surface`; in winex11 also win_data), so user ->
  list lock closes deadlocks (10 of 10 in the review's probe) and makes every USER call in the process wait for
  whole flush passes (GetWindowLongW up to 1.3 s). dce.c `surfaces_lock` is taken with no user lock held.
- `UpdateLayeredWindow` is the one caller of `apply_window_pos` that runs on a thread that doesn't own the
  window (Windows too: direct, no messages to the owner, works while the owner doesn't pump; `SetWindowPos`
  goes through the owner's message loop on both; Wine's `SetLayeredWindowAttributes` surface update is posted
  to the owner). So a window's surface can change under its owner thread at any time: take it from the
  window pointer with a reference, never keep it across a release. Probe: `tests/r171/ulwrace.c`.
- A fault in Unix-side code inside a syscall is not a crash: ntdll returns the exception code to the PE caller
  and mutexes held stay locked (172). A hang on a Unix mutex whose owner isn't in the locked region: rerun
  with `WINEDEBUG=+seh` and grep `handle_syscall_fault`.
- Offscreen client surfaces (181, probes tests/r174/ and tests/r181/gpuchild.c): the toplevel X window only holds a
  copy of the offscreen X window, made at each full driver present. What refreshes it otherwise (fix/181):
  an Expose presents the exposed region again (fix/061; only that region since 181), a child that moved without
  resizing is presented again at its new place and invalidated (`update_client_surfaces()`), and `move_window_bits()`
  also copies on the host window what the window surface doesn't paint (`get_dc_ex()` with the internal
  `DCX_CLIENTSURFACES`: no surface, visible region minus the surface clip region; not reachable through
  NtUserGetDCEx), which is all that moves a GPU child of *another* process when its container moves. Both re-present
  paths need `client_surface.presented`: set by a real present, cleared when the surface grows or changes between
  onscreen and offscreen. Before the first present the offscreen X window holds what the server copied from the
  screen or, on NVIDIA, old VRAM (fragments of other windows). Windows: a plain move keeps the bits, a child that a resized sibling covered first gets
  WM_PAINT instead (186).
- GDI on a window with a client surface (client DC, not DCX_WINDOW) has no window surface and draws on the toplevel
  X window directly (`update_visible_region()` / `X11DRV_GetDC`), so the offscreen X window never gets it. wined3d
  presents partial rectangles of COPY-effect swapchains that way (`swapchain_blit_gdi()`, WPF's dirty rectangles),
  from its command stream thread, on the DC it got when the swapchain was created. Three traps (181): a DC that was
  valid before the window got its pixel format flag kept the window surface (drawing lands under the client surface,
  never visible) until something invalidated it - `apply_window_pos()` now does when the flag is first sent;
  gdi_display is only flushed by `X11DRV_ProcessEvents` when the thread has X events (upstream d3cb94b543e), a surface
  flush or a full present, so such drawing stayed in Xlib's buffer - the image operations (PutImage, StretchBlt,
  XRender blits) now flush, fills / lines / text still don't (189; a flush per primitive costs 8x on PatBlt); and an Expose (no compositing manager)
  over such drawing brings back the last *full* frame there (open, needs partial presents to reach the offscreen
  window: wined3d keeping the front buffer, or DCs of these windows drawing into it).
- A window DPI-scaled by Wine (unaware application, scaled monitor) has a scaled window surface and a host window
  with other coordinates: direct GDI on its client surfaces is misplaced upstream, and 181's bits move is skipped.
- Hardware WPF = one wined3d swapchain (Vulkan surface, offscreen X window) per HwndSource, a new one per size; software
  WPF (`RenderOptions.ProcessRenderMode`, per-target `RenderMode`) BitBlts from the render thread into the window
  surface and behaves like any GDI child. Client surfaces of a process: X windows below its 1x1 unnamed dummy parent in
  `xwininfo -root -tree`.
- win32u answers vkAcquireNextImageKHR / vkQueuePresentKHR on a swapchain whose extent no longer matches the window
  with VK_SUBOPTIMAL_KHR; the present still shows the old-size image, unscaled, the rest of the window undefined.
  wined3d (fix/181) recreates the swapchain when the acquire says so and only then presents; it used to present, then
  recreate, and show nothing new until the next Present. GL has no such status: the GLX drawable follows the X window,
  whose resize request (`client_surface_update_geometry()`, gdi_display) must have reached the server before the frame
  is rendered (XSync there since 181).
- A blt-model present from WM_SIZE of a *top-level* window that just grew can miss the new strip on Windows too (1 of
  6 on Win11): not a reference for anything.
- `X11DRV_client_surface_present()` gets no region for an exclusive fullscreen window; it must still select it (a null
  region removes the clip), or the frame stays clipped to the former window rectangle (181, `gpuchild fsclip`).
- A WM resize is X first: ConfigureNotify (awesome: a real one mapped one pixel off, then the synthetic one) and the
  Expose events of the new area are handled before Win32 knows the size; under a compositing manager these are the only
  Expose events a mapped window gets. awesome's frame ConfigureNotify also yields a GravityNotify whose posted
  WM_WINE_WINDOW_STATE_CHANGED applies the new size before fix/077's WM_ENTERSIZEMOVE (35-60 % of resizes, 174).
