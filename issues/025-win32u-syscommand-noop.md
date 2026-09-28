# 025 win32u: SC_MAXIMIZE/SC_MINIMIZE show hidden windows already in that state (Edge crash)
Status: fixed (awaiting review) · Owner: 017 worker · Branch: fix/025-win32u-syscommand-noop · Found in: Edge 154 in inv-vm

## Symptom
The msedge.exe browser process crashed (AV in msedge.dll) at every start. It creates
its frame hidden with WS_MAXIMIZE and sends WM_SYSCOMMAND SC_MAXIMIZE to it. Wine's
DefWindowProc called ShowWindow(SW_MAXIMIZE), so the window was shown and activated
(WM_ACTIVATE/WM_SETFOCUS) before Edge had set it up.

## Windows ground truth
tests/syscommand_hidden.c: SC_MAXIMIZE on an already maximized window and SC_MINIMIZE on
an already minimized window do nothing, even for hidden windows. ShowWindow() itself
does show them.

## Fix
Return early in the SC_MAXIMIZE/SC_MINIMIZE handling. user32:win test
test_syscommand_hidden: VM 0 failures; Wine only the 2 known flaky failures.
