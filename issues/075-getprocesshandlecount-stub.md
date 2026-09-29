# 075 GetProcessHandleCount / ProcessHandleCount always returns 0 (low)
Status: open (draft, low) · Owner: - · Branch: - · Found in: 067

NtQueryInformationProcess(ProcessHandleCount) and GetProcessHandleCount are
stubs returning 0 in Wine; Windows returns the real count. Apps and diagnostics
that watch their handle count (and our own leak tests) get nothing. Workaround
in tests: NtQuerySystemInformation(SystemExtendedHandleInformation) filtered by
PID. Fix: a server request (the server knows the process's handle table size).
