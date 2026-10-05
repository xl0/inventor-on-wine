# 170 winewayland: protocol error "wl_surface already has a buffer committed" when a toplevel with a GL child is hidden and shown again quickly (mutter 50 + NVIDIA EGL)
Status: partly fixed on fix/157 (e243e7d08bb: windows hidden for 5 ms or longer); open for a hide and show within ~2 ms · Found in: 157 (stress on the host Wayland session) · reproduces on integ b5d75449ffe without the 157 commits

## Symptom
The process loses its Wayland connection and exits:
```
xdg_wm_base#11: error 4: wl_surface@26 already has a buffer committed
err:waylanddrv:wayland_read_events_thread Failed to read events from the compositor, terminating process
```

## Repro
Host session (`x/wayland.sh start`: gnome-shell 50 headless on NVIDIA 595 EGL), `renderer=gl` not needed (plain WGL):
`build/wine inst/157/lockstress.exe glhide N [HIDE_MS]` (source tests/r157/lockstress.c): one UI thread does
ShowWindow(SW_HIDE), pumps HIDE_MS (default 0, 1 or 2 ms), ShowWindow(SW_SHOWNA) on a top-level; a second thread swaps
(interval 0) on a WS_CHILD of it. No second UI thread, no cross-process surfaces involved.
| build | `glhide 3000` (hidden 0-2 ms) | `glhide 2000/3000 5` (hidden 5 ms) |
|---|---|---|
| integ `build/` | fatal 6/6 (reviewer: 14/14) | fatal 3/3 |
| fix/157 with e243e7d08bb | fatal 2/2 | DONE 14/14, 0 protocol errors |
| + blocking wl_display roundtrip before get_xdg_surface (experiment) | fatal 5/5 | |
| fix/157-surface-per-show (7ef19cf2795) | DONE 10/10 | |
Never seen in the vmwl guest (llvmpipe: GNOME, KDE, sway; ~150 lockstress runs with the same GL child hidden and shown).

## What the wire shows (WAYLAND_DEBUG=1, `inst/157/evidence/170-trace-wl_surface-222.txt`)
```
-> wl_subsurface#183.place_above(wl_surface#222)        GL client subsurface (EGL window, NVIDIA dma-bufs)
-> wl_surface#222.commit()
-> xdg_toplevel#235.destroy()  -> xdg_surface#261.destroy()        window hidden: wayland_surface_clear_role
-> wl_surface#222.attach(nil, 0, 0)  -> wl_surface#222.commit()
-> wl_subsurface#183.destroy()
-> wl_surface#222.attach(nil, 0, 0)  -> wl_surface#222.commit()    (twice more, 0.7 ms apart)
-> xdg_wm_base#11.get_xdg_surface(new id xdg_surface#187, wl_surface#222)      shown again, 1.5 ms after the first NULL commit
wl_display#1.error(xdg_wm_base#11, 4, "wl_surface@222 already has a buffer committed")
```
The driver did what `wayland_surface_clear_role` relies on (NULL buffer attached and committed before the role is given
again), but the compositor still had the old buffer when `get_xdg_surface` arrived.

## Cause (the client side is from the traces; the compositor side is inferred from them, mutter was not read or instrumented)
mutter applies commits as transactions. A commit of the toplevel that carries the position of a subsurface
(`wl_subsurface.place_above` / `set_position` are applied with the parent; the driver sends them with every update of a
client surface) is ordered after the pending commits of that subsurface, and those wait for the GPU with NVIDIA EGL
(dma-bufs with explicit sync). Every later commit of the toplevel queues behind it, including the one that removes its
buffer. `xdg_wm_base.get_xdg_surface` is not part of a transaction: it is checked against the surface state as applied so
far ("wl_surface@N already has a buffer committed"). With SHM / llvmpipe buffers nothing waits (vmwl guest: never seen).
Two ways for the buffer to be there when the window is shown again:
1. The driver itself committed one while the window was hidden: a window surface flush on a surface whose role was
   cleared (`wayland_surface_reconfigure` returned TRUE for "no role"); found by the 157 reviewer
   (inst/157-review/gh-trace-integ.out). The NULL attach + commit that `wayland_surface_make_toplevel` sends right before
   get_xdg_surface is not applied in time. Fixed by e243e7d08bb "winewayland: Don't commit buffers to surfaces without a
   role." (nothing is committed to a hidden surface; the flush is reported as done and the
   window surface is exposed when the surface gets a role: first configure of a toplevel, WindowPosChanged for a subsurface).
2. The hide's own NULL commit has not been applied yet when the show follows within a millisecond or two
   (inst/157/r3/glhide-trace.out: hide at 507.574, get_xdg_surface at 508.099, no buffer attached in between). Open.

## What does not work for case 2
- Waiting for the compositor with wl_display.sync before get_xdg_surface (tried as a blocking roundtrip on a private
  queue, glhide 3000: fatal 5/5): the sync is answered when the requests have been read, not when the transaction has
  been applied. A deferred show on the sync's done event would be the same. There is no event for "this commit is applied"
  on an unmapped surface (no frame callbacks; SHM buffers are released at upload, not at replacement).
- A new wl_surface whenever a window becomes a toplevel (branch `fix/157-surface-per-show`, 7ef19cf2795, on top of the
  surface serial of 157): glhide passes, client surfaces and 132's sinks follow by the serial, cursor / icons / opacity are
  carried over. But windows with the subsurface role (unmanaged popups) stay children of the destroyed surface of their
  owner: `wl_subsurface::place_above: wl_surface@N is not a valid parent or sibling` (sink stress 6/6 fatal), and without
  that error they would stay unmapped until their next WindowPosChanged. It needs: the owner serial in
  `wayland_surface_make_subsurface`, re-creating the wl_subsurface of every popup of the window when its surface is
  replaced, and a repaint of those popups. Not small; not merged.

## Open
Component: winewayland.drv. Options for case 2: finish the surface-per-show branch (above), or keep the xdg_surface /
xdg_toplevel of a hidden window and unmap it the xdg-shell way (NULL buffer; map again with a commit and a configure),
which never calls get_xdg_surface on a used surface but changes what "hidden" means everywhere in the driver.
Impact of what is left: a top-level with a GL/Vulkan child that is hidden and shown again within ~2 ms on a
GPU-composited mutter; kills the process. Check KWin and sway on a GPU compositor (not available: the vmwl guest is llvmpipe).
