# 128 winewayland: every key press/release is preceded by an extra VK_PROCESSKEY (0xE5) message
Status: draft · Found in: wayland test pass (notes/wine/wayland.md)

## Symptom
With winewayland.drv a plain key press gives the window WM_KEYDOWN wParam=0xE5 (VK_PROCESSKEY) followed by
the real WM_KEYDOWN 'N', then WM_CHAR; likewise two WM_KEYUPs. Both for compositor-injected keys and for
SendInput. tests/wl_keyprobe.c output (one key):
`msg 0100 wp e5` `msg 0100 wp 4e` `msg 0102 wp 6e` `msg 0101 wp e5` `msg 0101 wp 4e`.
GetKeyboardLayout is 0xe0010409 (the Wayland driver's IME-flavoured HKL, wayland_keyboard.c
`get_locale_hkl`/0xe001) after a WM_INPUTLANGCHANGE; the process starts as 04090409.
user32:msg (14605/14619/14734/19753) and user32:input ("Spurious keyboard layout changed ...
E0010409") fail on it; winex11 (Xvfb) passes the same tests.
An app handling WM_KEYDOWN (Inventor shortcuts, Ctrl+key accelerators) sees an extra 0xE5 keydown per key.

## Open
Is the 0xE5 sent by win32u's IME path because the layout is an IME layout (ImmProcessKey -> the
default IME says "processed" but then the key is still posted)? Compare with Windows with an IME
layout active and a non-composing IME (no VK_PROCESSKEY unless the IME eats the key). Probably a bug in
how winewayland marks itself as an IME layout whenever text-input-v3 exists (mutter has it), not only
while a text field is focused.
