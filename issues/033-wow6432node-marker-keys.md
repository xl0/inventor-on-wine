# 033 32-bit per-user class redirection depends on deletable marker keys (low)
Status: open (draft, low) · Owner: - · Branch: - · Found in: 030 (028+030 series)

Wine's 32-bit view of per-user classes needs empty marker keys
HKCU\Software\Classes\Wow6432Node\{CLSID,Interface,DirectShow,Media Type,
MediaFoundation} (created by wine.inf, shared by the server like HKLM's).
Anything that deletes empty HKCR keys (e.g. MSI removing empty parents on
uninstall) removes them, and 32-bit per-user classes then silently fall back
to the 64-bit view. Windows has no such fragility. Fix direction: make the
server recreate/treat the redirected subkeys as always present, or redirect
by name without requiring the key. See notes/wine/registry.md.
