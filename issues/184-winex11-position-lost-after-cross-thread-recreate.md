# 184 winex11: window jumps (position doubled) after its X window was recreated by another thread
Status: draft, root-caused, one-line experiment fixes it, not in any series · Found in: review of 173
(`inst/173-review/vstate.exe loop 60`, kept as tests/r184/vstate.c) · upstream code · needs a reparenting WM (openbox)

## Symptom
A top-level window at 260,190 whose X window is recreated from a thread that doesn't own it (first
UpdateLayeredWindow: ARGB visual; SetLayeredWindowAttributes / WS_EX_LAYERED reset: default visual) sometimes ends up at
520,380 (or 524,410): Win32 position = twice the real one, the X window follows.
`vstate.exe loop 60` under openbox (120 recreations per run): integ 1 of 120 (a second run died of 173's X_GetProperty
BadWindow), fix/173 4 and 3 of 120; the reviewer had 3-4 of 240 on both builds. Without a WM, or when the owner thread
recreates the window itself, it doesn't happen.

## Cause (trace `inst/173/out/vs-trace.out`, WINEDEBUG=+x11drv,+event on integ)
```
00dc X11DRV_WindowPosChanged win 0x10052/e00005 ...            <- other thread: new X window e00005, config 260,190
0024 host window .../200175 ConfigureNotify (260,190)-(680,500) <- owner: the OLD frame is still data->parent
0024 208 ConfigureNotify for hwnd/window 0x10052/e00005         <- real (not synthetic) event of the new window, root-relative
0024 handle_state_change ... mismatch config (520,380)-(584,444)/208, expected (260,190)-(324,254)/200
0024 208 ReparentNotify ... set_window_parent window e00005, parent 2001d8   <- only now the parent is replaced
0024 window_update_client_config config changed (260,190) -> (520,380)       <- Win32 window moved there
```
`destroy_whole_window` called from another thread cannot release the host window parent (it belongs to the owner's
display) and sets `data->parent_invalid`. `X11DRV_ConfigureNotify` (event.c ~985-993) honours that flag for
`host_window_configure_child` but then maps the event through the stale parent anyway:
`if (!event->send_event) pos = host_window_map_point( data->parent, event->x, event->y );`
so the new window's first ConfigureNotify (relative to the root, it is not reparented yet) gets the old frame's
origin added. `X11DRV_GravityNotify` has the same two uses of `data->parent`.

## Experiment
`tests/r184/parent-invalid-try.patch` (ConfigureNotify only: `data->parent_invalid ? NULL : data->parent`) on fix/173:
3 runs of `vstate.exe loop 60` under openbox, 0 of 360 recreations moved. Not reviewed, GravityNotify not covered,
embedded windows not looked at.

## Repro
```
inst/173/x.sh start      # :1411 and :1415 have openbox
inst/173/run.sh integ 1415 vs 300 inst/173-review/vstate.exe loop 60        # "DONE loop 60 other: moved N"
```
