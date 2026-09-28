# 030 COM activation ignores per-user class registrations
Status: open (draft, low) · Owner: - · Branch: - · Found in: 028 (merged HKCR)

combase/ole32 keep their own HKLM-only classes root, so COM servers registered
only under HKCU\Software\Classes (per-user installs, e.g. many modern apps /
ClickOnce / per-user Office add-ins) are invisible to CoCreateInstance, even
with 028's merged HKCR view in kernelbase. Windows resolves per-user
registrations first. Check combase's open_classes_key / create_classes_root_hkey
and whether they can reuse the merged view.
