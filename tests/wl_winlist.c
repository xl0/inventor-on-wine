/* Lists visible top-level windows: hwnd pid class title rect style exstyle owner; with arg "loop" repeats every 1s.
 * Build: x86_64-w64-mingw32-gcc -o tests/wl_winlist.exe tests/wl_winlist.c -luser32 */
#include <windows.h>
#include <stdio.h>
static BOOL CALLBACK cb(HWND h, LPARAM l)
{
    char cls[128], title[128]; RECT r; DWORD pid;
    if (!IsWindowVisible(h)) return TRUE;
    GetClassNameA(h, cls, sizeof(cls)); GetWindowTextA(h, title, sizeof(title));
    GetWindowRect(h, &r); GetWindowThreadProcessId(h, &pid);
    printf("%p pid=%04lx %-28.28s '%.40s' (%ld,%ld)-(%ld,%ld) %ldx%ld st=%08lx ex=%08lx own=%p\n", h, pid, cls, title,
           r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top,
           (DWORD)GetWindowLongA(h, GWL_STYLE), (DWORD)GetWindowLongA(h, GWL_EXSTYLE), GetWindow(h, GW_OWNER));
    return TRUE;
}
int main(int argc, char **argv)
{
    do { EnumWindows(cb, 0); fflush(stdout); if (argc > 1) { puts("--"); Sleep(1000); } } while (argc > 1);
    return 0;
}
