# 185 winewayland on mutter 50: GL child shown on the owner window after a role change, Clutter criticals, one shell hang
Status: draft · Found in: re-review of fix/157 (inst/157-rr) · not caused by fix/157 (same with integ's request order);
integ itself deadlocks in the scenario (157) · compositor side: mutter 50.1

## Symptom
A popup with a GL child that is being presented to changes between managed and unmanaged (toplevel <-> subsurface of its
owner: the driver destroys its wl_surface and makes a new one). On the host session (gnome-shell / mutter 50.1, NVIDIA):
- gnome-shell logs thousands of `Clutter-CRITICAL: clutter_actor_insert_child_at_index: assertion 'child->priv->parent ==
  NULL' failed` per second of flipping, sometimes followed by `clutter_actor_is_mapped: assertion 'CLUTTER_IS_ACTOR (self)'
  failed` / `instance with invalid (NULL) class pointer` (use after free in the shell);
- after the last flip the GL contents can stay on the wrong window: drawn in the owner at the child's offset, while the
  child's place in the popup shows the GDI background ([screenshot](attachments/185-gl-child-on-owner.png): magenta and
  cyan belong to the two grey rectangles in the blue popup);
- once the shell stopped for good (100 % CPU in `clutter_actor_destroy` -> dispose -> `g_ptr_array_foreach` ->
  `g_signal_handlers_disconnect_matched` in libmutter; SIGTERM ignored): every Wayland client of the session is frozen.
No protocol error, nothing in Wine's log; only the shell's log shows it.

## Repro
`inst/157-rr/run.sh 157 TAG 60 -- flip 40 30 gl=2 hideshow` (probe `inst/157-rr/rr.c`: A main window, F popup owned by A
with two GL children swapped by their own threads, 40 flips 30 ms apart, the first child hidden before and shown after each
flip; screenshots 1.5 s after the last flips; the runner counts the CRITICAL lines of /tmp/wl-xdg/shell.log).
| driver | runs | criticals per run | GL child left on the owner window |
|---|---|---|---|
| fix/157 tip 750bb757812 | 13 | 6 500 - 207 000 (0 in the one run under WAYLAND_DEBUG) | 4 |
| same + client subsurfaces destroyed before their parent (integ's request order, experiment B below) | 11 | 7 300 - 116 000 | 1 |
| integ b5d75449ffe | - | 2 in its only flip | deadlocks at the first flip (157, row 5 of its table) |
- No criticals and no misplaced child when the children do not present during the flips (`idle`: 20 flips, same pictures on
  integ and fix/157), without GL children (100 and 1500 flips), or with 1.6 s between the flips (`each=30`).
- Also with flips only (`flip 200 25 gl=4`: 46 000 criticals), children created during the flips (`mk`), a D3D11 child with
  the GL and the Vulkan renderer.
- The hang: once, in `flip 40 25 gl=2 mk` with pointer jitter after two earlier flip runs in the same session
  (inst/157-rr/evidence/shell-hang-1*.txt: log summary, gdb backtrace).

## Cause (mutter 50.1 source, src/compositor/meta-window-actor-wayland.c, src/wayland/meta-wayland-subsurface.c)
Every window actor keeps the surface actors of its subsurface tree as direct children of one container.
`meta_window_actor_wayland_rebuild_surface_tree()` of a window removes the container children that are no longer in its
tree and inserts the ones that are new. A surface that leaves the tree of window X (wl_subsurface destroyed, or its parent
destroyed: both end in `permanently_unmap_subsurface()`, a transaction on the old parent that can wait behind the
subsurface's own pending buffers) and joins the tree of window Y is inserted into Y's container while its actor is still a
child of X's: the insert fails with the assertion, the actor stays where it was (drawn in X at its own offset), and it is
added to Y's list of surface actors anyway (`meta_window_actor_add_surface_actor`). When X's container goes away the actor
is destroyed with it and Y keeps a dangling pointer: the later criticals and the hang in Y's dispose.
The driver's requests are legal (`inst/157-rr/wlcheck.py` on a WAYLAND_DEBUG trace: every place_above names the parent or a
sibling, nothing is sent to a subsurface whose parent is gone).

## What the driver could do
- Order of the requests does not avoid it: experiment B (inst/157-rr/evidence/experiment-B.diff: in `wayland_win_data_create_wayland_surface`, before
  `wayland_surface_destroy`: `wayland_client_surface_attach(client, NULL, NULL)` for every win data whose client surface is
  attached to the window; no win32u call, so fine under win_data) still gets the criticals and 1 misplaced child in 11 runs.
- A client wl_surface cannot be replaced (the EGL / Vulkan surface is bound to it), and there is no event for "the old
  tree has been rebuilt".
- So: report to mutter (not done; check newer 50.x first, tags up to 50.5 exist), or avoid role changes of a window with
  client surfaces (keep the role, e.g. by not letting `managed` flip while client surfaces are attached). Not evaluated.

## Impact
A window that changes its managed state while a GL / Vulkan child presents: probably rare in applications (not looked for in Inventor), but
the result can be a frozen desktop session. The stress probes of 157 (`lockstress`, `roleflip ... gl`) trigger it on the
host session; their results do not show it because only the shell's log does (lockstress with jitter: ~200 criticals of
other kinds per run, 14 of this kind in two sink stress runs).
