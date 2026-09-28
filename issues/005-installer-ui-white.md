# 005 Installer UI window stays blank white (possible regression from integ)
Status: wip · Owner: worker · Branch: fix/005-x11-empty-surface-clip (wt/005, from master) · Found in: Inventor web installer, build/ at `integ`

## Observed
- `build/` rebuilt at wine-src `integ` (master 4e819f054dd + fix/001 + fix/002).
- Launched the original web installer with no workaround:
  `inst/webinstall/Setup.exe` (DLLs in ODIS\ subdirs, found via Setup.exe.config),
  prefix `prefixes/inv`, detached, stderr `inst/wine-setup.log`.
- Setup.exe, Installer.exe and AdskAccessUIHost.exe (Electron UI) start; no
  import errors. But the 700x522 UI window at (610,279) stays plain white for
  4+ minutes (x/shot.sh; VNC shows the same).
- Earlier, with build/ at master and the staged copy
  (`prefixes/inv/drive_c/users/xl0/Downloads/invsetup/Setup.exe`, assemblies
  copied beside it), the same UI rendered ("This installation includes the
  following update" → Next). The user also once saw a white window during the
  003 worker's session (old build), so it may be intermittent, not a regression.

## Suspected (unverified)
Regression from 001 (app configs now read for every activation context — e.g.
an Electron exe with a `.config`?) or 002 (query fallback). Or intermittent.

## Task
Determine cause. Cheap discriminators: rerun with `wt/001-build/wine`,
`wt/002-build/wine` (single-fix builds, still present) and a master build;
rerun the staged copy vs the original layout; check ODIS/Electron logs
(DDA-UI.log, ELECTRON_ENABLE_LOGGING, see issue 003 notes).

## Findings (worker)
- NOT a 001/002 regression: integ build renders 8/8 fresh launches. Trigger is the
  X screensaver: :98 had default 600 s blanking; the white launch at 21:01 started
  while blanked, x/vnc.sh (`xset s off`) unblanked at 21:02. Repro on any build:
  `xset s on; xset s activate`, wait, `xset s off; xset s reset` -> window turns
  (and stays) white, also when already rendered. Any full obscure/expose does it.
- DDA-UI.log is identical for white/rendered runs (page reached); GPU process not hung.
- Chromium (DComp fails) presents from the GPU process straight onto the browser's
  toplevel HWND (no child HWND): win32u offscreen client surface, blitted by
  X11DRV_client_surface_present onto the toplevel X window.
- On Expose, win32u expose_window_surface() only re-flushes the toplevel's window
  surface (since 3437ba2dea1 it no longer redraws regions the surface doesn't
  cover). Nobody re-presents the GL content. RedrawWindow(RDW_INVALIDATE) on the
  toplevel (scratch redraw.exe) restores the UI -> Chromium repaints on WM_PAINT.
- White (not black) suggests the surface flush also paints over the pixel-format
  client area: server surface region excludes the client (PAINT_HAS_PIXEL_FORMAT,
  set via WM_WINE_SETPIXELFORMAT), i.e. the clip region is empty, and
  x11drv_surface_set_clip() treats count 0 as "no clip" (XSetClipMask None). Verifying.
