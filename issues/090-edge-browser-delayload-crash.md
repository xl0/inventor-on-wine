# 090 Edge browser process dies a few minutes after start (delay-load procedure not found)
Status: open (draft) · Owner: - · Branch: - · Found in: 088 (inv3, integ 9c1eea5beac + fix/088)

## Symptom
`msedge.exe --user-data-dir=C:\t\edge088 --no-first-run file:///C:/t/static.html` on inv3
(:100): the browser disappears after ~3-5 min, idle; next start shows "Restore pages".
Crashpad dumps in C:\t\edge088\Crashpad\reports (ptype browser), 2 of 3 not caused by kills:
exception 0xc06d007f (delay-load ERROR_PROC_NOT_FOUND) raised in kernelbase+0x4f72e
(RaiseException), i.e. Edge's delay-load helper found a missing export. The dump doesn't
contain the DelayLoadInfo memory, so the DLL/function is unknown.

## Next step
Run with WINEDEBUG=+seh,+module (or +relay filtered to GetProcAddress) around the crash
and find the missing export. Log shows the usual WinRT activation failures
(IUserStatics, IAppCapability 0x80040154) shortly before; possibly a background task.
Not seen with WebView2 in Inventor so far.
