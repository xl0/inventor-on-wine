# 175 Moving Inventor to another virtual desktop leaves the splitter bar behind
Status: open, user-reported on the laptop (2026-10-04); not reproduced on the server yet · Owner: - · Branch: -

## Symptom
Laptop: awesome WM on X11 (NVIDIA), picom (`--backend glx --vsync --no-use-damage`), 144 DPI, Wine
`wine-11.18-537-gb5d75449ffe`. Moving Inventor's main window to another virtual desktop (awesome tag)
leaves a window on the old desktop: "No window title, just the split bar outline" — the outline of the
splitter between the browser pane and the viewport.

## What it probably is (not verified)
Inventor's pane splitter is a WPF popup: a separate top-level, layered (per-pixel alpha 3-6), owned by the
main window (issues 062, 042, 077). The window manager moved the main client to the other tag but not this
one. On Windows an owned window always stays with its owner (same virtual desktop, hidden with it).
Questions for the fix worker: is the popup a managed X window; does it carry WM_TRANSIENT_FOR for its
owner (winex11 sets it for owned windows that are managed — is it set here, and at the time awesome
needs it); what does awesome do with transients when the main client changes tag (stock awesome 4.3:
transient windows don't follow unless a rule/signal handler moves them — check, and compare other WMs:
openbox, mutter, KWin all keep transients with their parent's desktop via `_NET_WM_DESKTOP` handling);
should winex11 follow the owner's `_NET_WM_DESKTOP` itself for owned windows (as it follows
minimize/restore), or keep such popups unmanaged (override-redirect) so that they can't be left behind
— and then hide them when the owner is not on the current desktop (the owner's unmapped/iconic state on
a tag switch is WM-specific: awesome unmaps clients of hidden tags). "Just the outline" suggests the
popup itself is invisible under picom (062 sets `_NET_WM_WINDOW_OPACITY` 0 for all-faint windows) and
what stays visible is awesome's border around the managed frame.
