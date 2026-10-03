/* IsDialogMessage(WM_CHAR) with msg->hwnd inside a container that lacks WS_VISIBLE (a hidden dock pane
 * whose child still has WS_VISIBLE). Wine's DIALOG_IsAccelerator only descends into visible
 * controls, never gets back to msg->hwnd and loops forever (ui2 issue 140).
 * Exit 0 = returned in time. Build: x86_64-w64-mingw32-gcc -O2 -o isdialogmsg_hidden.exe isdialogmsg_hidden.c -luser32 */
#include <windows.h>
#include <stdio.h>

static HWND frame, pane, edit;
static volatile LONG done;

static DWORD WINAPI call(void *arg)
{
    MSG msg = { edit, WM_CHAR, 0x1a, 1 };
    BOOL r = IsDialogMessageW(frame, &msg);
    printf("IsDialogMessageW returned %d\n", r);
    InterlockedExchange(&done, 1);
    return 0;
}

int main(void)
{
    HANDLE th;
    frame = CreateWindowA("STATIC", "frame", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 300, 0, 0, 0, 0);
    /* hidden container (no WS_VISIBLE), its child has WS_VISIBLE like the hidden iLogic ControlBar */
    pane = CreateWindowA("STATIC", "pane", WS_CHILD, 0, 0, 200, 200, frame, (HMENU)1, 0, 0);
    edit = CreateWindowA("EDIT", "e", WS_CHILD | WS_VISIBLE, 0, 0, 100, 20, pane, (HMENU)2, 0, 0);
    CreateWindowA("EDIT", "other", WS_CHILD | WS_VISIBLE, 0, 220, 100, 20, frame, (HMENU)3, 0, 0);
    printf("pane visible=%d edit visible=%d (style bit set)\n", IsWindowVisible(pane), !!(GetWindowLongA(edit, GWL_STYLE) & WS_VISIBLE));
    th = CreateThread(NULL, 0, call, NULL, 0, NULL);
    WaitForSingleObject(th, 3000);
    if (!done) { printf("HANG: IsDialogMessageW did not return in 3 s\n"); return 1; }
    printf("OK\n");
    return 0;
}
