# 061 GPU child content lost on Expose (black trail after window drag)
Status: open (draft) · Owner: - · Branch: - · Found in: UI latency pass (tools/uilat wmdrag)

## Symptom (integ 061fa687382, :98 openbox, wined3d-vk)
Inventor restored (not maximized), drag the main window by its caption strip 200 px to
the right: the left 200 px of the 3D viewport stay black until Inventor presents again
(hover over geometry, orbit, ...). Repeated drags leave black vertical stripes.
![trail](attachments/061-drag-black-trail.png)
The window itself follows the pointer in ~1 ms at 60 fps (openbox moves the frame,
server-side copy): the lag is not in the move, the damage is.

## Cause
- The trail is the path of a screen-fixed window over the viewport: issue 062's 5 px WPF
  splitter popup stays at its old screen position during the move (Inventor repositions it
  only after the move). Each step exposes the strip it uncovered.
- RECORD of Inventor's requests during the drag: one ShmPutImage per step of the window
  surface for the exposed 8-16x682 strip (window-relative x decreasing as the window moves
  right). In the viewport (offscreen client surface, window-surfaces.md) the window surface
  holds black; fix/005 then RedrawWindow()s the exposed client-surface part, but OGS does not
  present on WM_PAINT, so the black stays. On Windows (DWM) the content is never lost.

## Ideas
On Expose of an area covered by an offscreen client surface, re-blit that surface's last
image (the offscreen X window still holds it: `X11DRV_client_surface_present` source,
composite-redirected) instead of, or in addition to, asking the app to repaint.
Any screen-fixed window over a GPU child (tooltips, menus, other apps) should show the same.

## Repro
tools/invscen/run.sh uilat; restore Inventor (xdotool click the restore button) and
`tools/uilat/uilat.py wmdrag --hz 60`, then screenshot (x/shot.sh).
