# 170 winewayland: protocol error "wl_surface already has a buffer committed" when a toplevel with a GL child is hidden and shown again quickly (mutter 50 + NVIDIA EGL)
Status: draft · Owner: - · Branch: - · Found in: 157 (stress on the host Wayland session) · reproduces on integ b5d75449ffe without the 157 commits

## Symptom
The process loses its Wayland connection and exits:
```
xdg_wm_base#11: error 4: wl_surface@26 already has a buffer committed
err:waylanddrv:wayland_read_events_thread Failed to read events from the compositor, terminating process
```

## Repro
Host session (`x/wayland.sh start`: gnome-shell 50 headless on NVIDIA 595 EGL), `renderer=gl` not needed (plain WGL):
`build/wine inst/157/lockstress.exe glhide 3000` (source tests/r157/lockstress.c): one UI thread does
ShowWindow(SW_HIDE) / ShowWindow(SW_SHOWNA) on a top-level, a second thread swaps (interval 0) on a WS_CHILD of it.
integ `build/` 6 of 6 runs, fix/157 build 6 of 6. No second UI thread, no cross-process surfaces involved.
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

## Suspected cause (not verified)
mutter applies commits as transactions; a commit of the parent that carries a subsurface position (`place_above` /
`set_position` are applied with the parent) is ordered after the pending commits of that subsurface, and those wait for
the fences of the NVIDIA dma-buf buffers. The NULL-buffer commits of the toplevel then queue behind it, and
`get_xdg_surface` (not part of a transaction) is checked against the surface state that has not been updated yet.
With SHM / llvmpipe buffers nothing waits, which would explain the VM.

## Open
Component: winewayland.drv (wayland_surface.c). Options to look at: destroy and recreate the wl_surface instead of reusing
it when a role is given again after `clear_role` (as the role change path already does), or wait for the NULL commit
(wl_display.sync / frame) before `get_xdg_surface`. Check KWin and sway on a GPU compositor.
Impact: any GL/Vulkan child window in a top-level that is hidden and shown again within a few ms on a GPU-composited
mutter; kills the process.
