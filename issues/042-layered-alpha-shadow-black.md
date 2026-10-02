# 042 Per-pixel-alpha popups: translucent shadow drawn opaque black (low)
Status: fixed by 062 (fix/062): pixels below alpha 128 are no longer drawn without a compositor · Owner: worker-062 · Branch: fix/062 · Found in: UI test campaign (tooltips, menus)

## Symptom (integ d53133a66a1, :98 openbox, no compositor)
Inventor's WPF popups (ribbon tooltips, File-menu flyouts, floating
Properties panel) have a soft drop shadow on Windows. On Wine the shadow area
is a solid black frame (~4 px, rounded) around the popup. Cosmetic; content
and input are fine.
- VM: ![vm](attachments/042-tooltip-vm.png)
- Wine: ![wine](attachments/042-tooltip-wine.png),
  submenu ![submenu](attachments/042-submenu-wine.png)

## Notes
WPF AllowsTransparency popups are WS_EX_LAYERED + UpdateLayeredWindow with
per-pixel alpha. winex11 maps them with the default (non-ARGB) visual, so
partially transparent pixels can only be blended against black / cut by the
window shape. Check whether a compositor on the display plus an ARGB visual for
per-pixel-alpha layered windows fixes it, and whether upstream has work on
this; likely a long-standing Wine limitation rather than a quick fix.
062 (worker-061): winex11 does give these windows an ARGB visual (depth 32, xwininfo); only
alpha-0 pixels are cut from the shape, the rest is drawn opaque because nothing blends without
a compositing manager. See 062 "Why no fix" for the X11 shape/input trade-off.
fix/062: without a compositing manager the shadow pixels (alpha < 128) are left out of the X shape, so the
popup has no shadow instead of a black frame; they are click-through then (Windows hit-tests them).
tests/layered_splitter.exe's tooltip: shadow pixel 000000 -> the pane colour. Not yet looked at in Inventor.
