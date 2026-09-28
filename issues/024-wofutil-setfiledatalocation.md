# 024 wofutil: WofSetFileDataLocation unimplemented (Edge setup crashes)
Status: fixed (awaiting review) · Owner: 017 worker · Branch: fix/024-wofutil-setfiledatalocation · Found in: Edge 154 MSI install into inv-vm

## Symptom
Microsoft Edge's setup.exe (run by MicrosoftEdgeUpdate in session 0) aborted on the
wofutil.WofSetFileDataLocation stub. winedbg --auto then hung, and so did msiexec.

## Windows ground truth
tests/wofset.c: on NTFS it returns S_OK. On a filesystem without WOF (virtio-fs Z:) it
returns HRESULT_FROM_WIN32 of the FSCTL_SET_EXTERNAL_BACKING error.

## Fix
Implement it for WOF_PROVIDER_FILE as FSCTL_SET_EXTERNAL_BACKING. Wine returns
ERROR_NOT_SUPPORTED (0x80070032); Edge's setup logs that and continues. Headers
added to wofapi.h. No test: it's a thin wrapper.
