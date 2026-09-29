# 085 Trial welcome popup stays white long after Inventor has loaded
Status: open (draft, Windows unchecked) · Owner: - · Branch: - · Found in: user's laptop + inv4 (integ 3951ce31e31)

## Symptom
On start, the licensing/trial popup ("Dig into your trial", Autodesk Access UI:
AdskAccessUIHost.exe, Chromium-based) appears as a plain white window next to the
Inventor splash plaque and stays white for a while after Inventor's main window
is up (user report, laptop). The same white 860x500 rectangle is on inv4/:101
over the Home area at 530,290 (same geometry as the popup) well after load:
![white popup](attachments/085-trial-popup-white.png)
It eventually renders (on inv2 after "Check again" it showed its content).

Also (user, laptop): the mouse pointer is invisible over the popup, yet clicks
work (the X closes it). Possibly separate from the blank rendering: Chromium sets
the cursor from the browser process (SetCursor / cursor created from the
renderer's bitmap). Check what cursor the window gets (+cursor, XDefineCursor)
vs Windows.

## To check
- Windows: how long the popup stays blank there (VM; needs a free licence seat,
  see CODE.md licensing notes).
- Wine: what AdskAccessUIHost's GPU process does while blank (GPU vs software
  compositing, DirectComposition path, 017/023–027), and whether the first
  frame is lost (compare 078: first present of a new offscreen surface lost).

## Laptop: rendered "having trouble" dialog and full licensing log

User-provided screenshot from the 2026-09-29 14:29:53 -0300 launch,
`prefixes/inv`, `wine-11.18-398-g77b5f2b672`. The previous instance exited
cleanly; the prefix's wineserver was stopped before this launch.
Vulkan and `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--disable-gpu` were retained
(the latter has not been shown to actually disable WebView2 GPU use).

Exact dialog text:

> We're having trouble.
> Please try again later.

The bottom link reads **Change license type**. Screenshot cropped to the dialog;
no email or underlying recent-document names are included:

![Rendered licensing error dialog](attachments/085-licensing-having-trouble.png)

This captures the rendered error, not the earlier white-window state.
Whether those two symptoms share a cause remains unverified; also see
[048](048-cold-start-first-doc-hang-trial-dialog.md).

[Full non-ENCODED licensing section](attachments/085-licensing-20260929-142953.txt):
all 86 lines, including 79 `[I]`, one `[E]`, two `[W]` and four header/blank
lines. Selected by date and time from 14:29:50 through capture at 14:43:08
local time; no severity filter or head/tail truncation. Only ENCODED lines
were omitted; account/device/session identifiers are redacted.
The original full section remains private under
`inst/local/debug-20260929-142953/licensing-evidence/`.

Notable sequence:
- 14:30:04: service starts, HTTP server on `127.0.0.1:40345`.
- 14:30:09: `authorize_succ:true`, `cls_check_succ:true`; then
  `[E] File does not exist:` with no filename.
- 14:30:12: cached license is valid for auth avoidance.
- 14:30:13: `subscription overuse enabled = true for status ALLOW`,
  followed by `CoFlowReturnNode`.
- 14:30:15: PubNub monitor connection succeeds.
- 14:30:50 and 14:31:20: UI analytics report `source:"error"`,
  `page:"main"`, message ID `1359509856`.

This excerpt does **not** show an explicit device-limit denial. The rendered
dialog alone therefore does not establish licensing refusal or its cause.
