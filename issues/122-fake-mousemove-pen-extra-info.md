# 122 Fake WM_MOUSEMOVEs carry the pen signature 0xff515700 in GetMessageExtraInfo
Status: fixed (awaiting review) · Owner: 120 worker · Branch: fix/120 (last commit of the series) · Found in: 120 (integ 43790927731)

## Symptom
The WM_MOUSEMOVE that wineserver queues when the window layout changes under a still cursor
(`update_cursor_pos` -> `set_cursor_pos`, server/queue.c) and after SetCursorPos has
GetMessageExtraInfo() == 0xff515700 (upstream 094b9f7f109 "server: Mask pointer inputs for
SetCursorPos requests", so mouse-in-pointer windows get no WM_POINTERUPDATE for it).
0xff515700 is MI_WP_SIGNATURE: Chromium's `ui::GetMousePointerDetailsFromMSG` (events_win_utils.cc)
reports such mouse messages as `EventPointerType::kPen`, so WebView2/Edge pages see pen-type
pointer events (pointerType "pen") for these moves and the tooltip controller runs its pen-hover
dedup path.

## Windows
Win11 VM (tests/hover_tooltip.c `hook` + tests/hover_tooltip_hook/hook.dll, a WH_GETMESSAGE hook
logging Edge's mouse messages): the system's fake moves after a window change have extra info 0.

## Task
Keep the moves out of the pointer conversion without the app-visible signature (e.g. a server-side
flag in hardware_msg_data instead of extra_info), check what Windows does for SetCursorPos with
EnableMouseInPointer, add a user32 test (GetMessageExtraInfo of the fake move).

## Fix
`server: Don't mark SetCursorPos mouse moves as pointer input.` on fix/120: set_cursor_pos passes extra
info 0; win32u process_mouse_message skips the mouse-in-pointer conversion when the message source origin
is IMO_SYSTEM. Windows (tests/r120/mip.c, VM): fake moves and SetCursorPos give WM_MOUSEMOVE with extra
info 0 and no WM_POINTERUPDATE under EnableMouseInPointer; Wine now the same. Test in user32:input.
