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

## To check
- Windows: how long the popup stays blank there (VM; needs a free licence seat,
  see CODE.md licensing notes).
- Wine: what AdskAccessUIHost's GPU process does while blank (GPU vs software
  compositing, DirectComposition path, 017/023–027), and whether the first
  frame is lost (compare 078: first present of a new offscreen surface lost).
