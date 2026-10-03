# fix/134 (wt/134-build) vs unfixed (wt/wayland-build) on current compositors, 2026-10-03
`vmwl/wl_xowner.sh COMPOSITOR BUILD` (VM copy of tests/wl_xowner.sh: same cases and colour checks; screenshots/clicks via QEMU,
WAYLAND_DEBUG=1 logs). 1280x800 screen. Raw output `results/<compositor>.txt`, logs/screenshots in `results/<compositor>-<build>/` (not in git).
Start the VM with `VMWL_BIND="wt/134-build wt/134/nls wt/134/fonts" vmwl/run.sh` (the build's symlinks point into the worktree).

| compositor | wt/134-build | wt/wayland-build (unfixed) |
|---|---|---|
| GNOME (mutter 50.1) | no FAIL | 17 FAIL: B/C hidden behind owner after clicking the owner in every case (self, chain, late, hide, reowner, both cross-process) |
| KDE (KWin 6) | no FAIL | 16 FAIL: same pattern as GNOME |
| sway 1.11 | only FAIL: chain-3 (C, owned by B, disappears after clicking B twice; B stays above A) | same single FAIL chain-3; everything else passes (sway floats Wine's popups above the tiled owner anyway, so the old build is not visibly broken there) |

Protocol (fixed build, identical on all three; `grep` of WAYLAND_DEBUG logs):
- Protocol errors: 0 in every process, no client death, on all three compositors.
- Same-process owned windows: `xdg_toplevel.set_parent(owner)` (self 1, chain 2, late 1, hide 4, reowner 4-5 incl. `set_parent(nil)` when unowned and the stale-parent attempt).
- Cross-process (xa/xb, xha/xhb): owner process exports with `zxdg_exporter_v2.export_toplevel`, the owned window's process calls
  `zxdg_importer_v2.import_toplevel` + `zxdg_imported_v2.set_parent_of` (1 per show; 2 in the hide/re-show case). `zxdg_importer_v1` was never used: all
  three compositors offer xdg-foreign v2 (mutter 50, KWin, sway), so the v1 fallback is not exercised here. Unfixed build: no set_parent/import/export at all.
- sway/wlroots strictness cases: "B shown before its owner" (late), "owner hidden / re-shown / destroyed" (hide, xh), and the parent-loop attempt
  (reowner: `set_parent(A2)`, then `set_parent(nil)` and `A2.set_parent(B)`) all PASS with zero protocol errors; B disappears with A in hide-5 as expected.
- Placement of owned windows: GNOME and KDE centre B on its owner horizontally (B x 443..837 on the 1052 px wide A at 114); vertically KDE centres exactly
  (B y 259..527, A 100..686), mutter is ~54 px above centre (B 241..509, A 136..722). sway floats B centred on the screen (443,290), A being tiled full-screen.
  Chain: C is centred on B the same way. A "late" B (owner not yet shown) is centred on the screen (GNOME 443,295; KDE 443,259; sway 443,290).

Not conclusive: sway chain-3 (probe ends with a click inside B, B raised over its child C; wlroots does not keep a floating child of a floating child above
its parent). Probe note: the first check of the very first run after a fresh prefix can fail on timing (prefix boot); rerun.
