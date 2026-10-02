# 122 Fake WM_MOUSEMOVEs carry the pen signature 0xff515700 in GetMessageExtraInfo
Status: draft · Owner: - · Found in: 120 (integ 43790927731)

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
