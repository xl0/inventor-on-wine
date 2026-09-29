# 065 Closing the Autodesk Assistant panel leaves its image over the viewport; viewport clipped to the old size
Status: open (draft) · Owner: - · Branch: - · Found in: specialised-environments pass (inv3/:100, integ c036c687c47, openbox, wined3d-vk)

## Symptom
Part or assembly open, Autodesk Assistant docked on the right (WebView2, cross-process).
Click the panel's X: the panel's frame/title go away and Inventor grows the graphics window
to the right edge (Win32: panel ControlBar + its Chrome_WidgetWin_* children now hidden,
MDI view 1678 px wide instead of 1328), and Inventor renders for the new size (model re-centred
175 px to the right). But on screen:
- the last Assistant image stays at x >= 1574 (never repainted),
- the 3D view is cut off at the old right edge x = 1574 (zoom/orbit redraw only the left part),
- the ViewCube and navigation bar (top-right of the viewport) are not visible.
Restore/maximize of the main window does not repair it (the stale strip moves around).
![stale panel](attachments/065-assistant-closed-stale.png)
On Windows the viewport extends over the freed area (expected; not re-checked on the VM:
it needs UI clicks there).

## Notes
- X side after closing: the WebView2 host's client window (350x884) is still mapped but as a
  child of a 1x1 dummy parent (not visible); the stale pixels belong to the main window.
- Looks like the viewport's GPU (client-surface) region / window-surface clip is not
  recomputed when a sibling hides and the view grows, cf. 061 (black exposed areas under
  closed dialogs, also seen in this pass after Design Accelerator / print dialogs).

## Repro
`INV_PREFIX=prefixes/inv3 DISPLAY=:100 DRI_PRIME=pci-0000_16_00_0 tools/invscen/run.sh beam`
(opens a part visible), click the X of the "Autodesk Assistant" panel tab, scroll-zoom in the
viewport, screenshot (x/shot.sh).
