/* Dumps all top-level windows (visible or not) of processes whose name contains argv[1] (default: all), with children.
 * Build: x86_64-w64-mingw32-gcc -o tests/wl_wintree.exe tests/wl_wintree.c -luser32 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
static const char *filter;
static void dump(HWND h, int depth);

static void dump(HWND h, int depth)
{
    char cls[96], title[48], pname[64] = "?"; RECT r; DWORD pid; LONG st = GetWindowLongA(h, GWL_STYLE);
    GetClassNameA(h, cls, sizeof(cls)); GetWindowTextA(h, title, sizeof(title)); GetWindowRect(h, &r);
    printf("%*s%p %-34.34s '%.24s' (%ld,%ld)-(%ld,%ld) %s%s ex=%08lx\n", depth * 2, "", h, cls, title, r.left, r.top, r.right, r.bottom,
           IsWindowVisible(h) ? "VIS" : "hid", (st & WS_CHILD) ? " child" : "", (DWORD)GetWindowLongA(h, GWL_EXSTYLE));
    { HWND c = GetWindow(h, GW_CHILD); for (; c; c = GetWindow(c, GW_HWNDNEXT)) dump(c, depth + 1); }
}
static BOOL CALLBACK top_cb(HWND h, LPARAM l)
{
    DWORD pid; HANDLE s, p; PROCESSENTRY32 pe = {sizeof(pe)}; BOOL ok = !filter;
    GetWindowThreadProcessId(h, &pid);
    if (filter && (s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) != INVALID_HANDLE_VALUE)
    {
        for (p = s; Process32First(s, &pe) ? 1 : 0; ) { do { if (pe.th32ProcessID == pid && strstr(pe.szExeFile, filter)) ok = TRUE; } while (Process32Next(s, &pe)); break; }
        CloseHandle(s);
    }
    if (ok) { printf("pid=%04lx ", pid); dump(h, 0); }
    return TRUE;
}
int main(int argc, char **argv) { filter = argc > 1 ? argv[1] : NULL; EnumWindows(top_cb, 0); return 0; }
