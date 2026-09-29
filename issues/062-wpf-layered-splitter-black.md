# 062 WPF per-pixel-alpha popup (browser pane splitter) drawn as a black bar
Status: open (draft) · Owner: - · Branch: - · Found in: UI latency pass (tools/uilat wmdrag)

## Symptom (integ 061fa687382, :98 openbox, no compositing manager)
In a part document a solid black 5 px vertical bar sits between the Model browser and the
viewport (maximized: x 240-245, y 145-1055). When the main window is restored or moved,
the bar stays at its old screen position until the move ends (and punches the black trail
of issue 061 into the viewport while the window slides under it).
Visible in attachments/061-drag-black-trail.png (left edge of the black area) and in
any sketch screenshot.

## Window
`tests/wintext.exe`: `HwndWrapper[DefaultDomain;;...]`, 5x682 (restored), style 96080000
(WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS|WS_SYSMENU), ex 00080080 (WS_EX_LAYERED|WS_EX_TOOLWINDOW),
Inventor.exe. X: managed (openbox frame, not override-redirect), _NET_WM_WINDOW_TYPE_NORMAL.
A WPF window with AllowsTransparency (UpdateLayeredWindow, per-pixel alpha): presumably a
transparent splitter hot zone / drag adorner, mostly or fully transparent on Windows.

## To check
- Windows: what the splitter looks like (VM, part open) and whether it follows the window
  during a drag (owned popup moved by WPF on LocationChanged?).
- Wine: how winex11 shows ULW_ALPHA windows without a compositor (alpha -> shape only for
  alpha 0? black where alpha is low?), and why it is managed (activated at some point?).
