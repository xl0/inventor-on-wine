# 027 win32u/server: WebView2 inside a color-keyed layered window renders black
Status: fixed (awaiting review) · Owner: 017 worker · Branch: fix/027-layered-client-surface · Found in: inv-vm, AdskIdentityManager sign-in dialog

## Symptom
AdskIdentityManager's dialog is a WS_EX_LAYERED toplevel with LWA_COLORKEY (key red),
filled with the key colour, and hosts WebView2. Chromium's GPU process presents on its
own WS_EX_TRANSPARENT child window inside it. The dialog showed black on :107. Clearing
WS_EX_LAYERED made the page appear.

## Windows ground truth
tests/layered_child_gpu.c: host process with a colour-keyed layered toplevel filled with
the key colour; a child process presents green via D3D11 on its own child window of it.
Win11: the child area shows green; key-coloured host areas are transparent. Wine: black.

## Cause and fix (3 commits)
1. server clip_pixel_format_children() skipped WS_EX_TRANSPARENT children, even those
   with a pixel format / client surface.
2. The owner's surface region wasn't updated when a foreign-process child got a
   client surface (see window-surfaces.md): apply_window_pos() now posts
   WM_WINE_UPDATEWINDOWSTATE to the surface window if it belongs to another process.
3. set_surface_shape() derived the colour-key shape from pixels under client surfaces
   too. Areas outside the surface clip region are now kept opaque.
No Wine conformance test: this needs two processes, D3D and screen readback.
Checked with the repro (Xvfb + lavapipe) and in inv-vm (dialog renders).
