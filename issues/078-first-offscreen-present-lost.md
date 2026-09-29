# 078 First present of a new offscreen client surface doesn't reach the screen
Status: open (draft) · Owner: - · Branch: - · Found in: 061 test (tests/expose_present.c)

## Symptom
D3D11 child window (offscreen client surface), FLIP_DISCARD swapchain, present once, wait 1 s,
read the screen: black on Wine (NVIDIA :101 and Xvfb+lavapipe); the second and later presents
show at once (colour per present: Wine 000000, 00ff00, ff0000, 0000ff; VM 0000ff, 00ff00,
ff0000, 0000ff). Harmless for apps presenting continuously; an app that presents one frame and
waits shows black until the next one.

## Repro
tests/expose_present.c with its first present removed (or the colour-per-present variant in
the 061 worker's history): `WINE_D3D_CONFIG=renderer=vulkan wine expose_present.exe`.
Suspect: the client surface is created/redirected (offscreen decision, XComposite redirect,
hdc_src/hdc_dst) during the first present, so the first StretchBlt copies from a window that
doesn't hold the frame yet.
