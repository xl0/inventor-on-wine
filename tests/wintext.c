/* Dump visible top-level windows whose title contains argv[1] (default: all
 * visible) with their child tree: class, text, rect. For reading dialogs whose
 * content doesn't render. Build: x86_64-w64-mingw32-gcc -O2 -o wintext.exe wintext.c */
#include <windows.h>
#include <stdio.h>

static const char *filter;

static BOOL CALLBACK child(HWND h, LPARAM depth)
{
    char cls[128], text[512];
    DWORD pid = 0;
    RECT r;
    GetClassNameA(h, cls, sizeof(cls));
    GetWindowTextA(h, text, sizeof(text));
    GetWindowRect(h, &r);
    GetWindowThreadProcessId(h, &pid);
    printf("%*s%p pid %04lx %s '%s' %ldx%ld+%ld+%ld style %08lx ex %08lx%s\n", (int)depth * 2, "", h, pid, cls, text,
           r.right - r.left, r.bottom - r.top, r.left, r.top, GetWindowLongA(h, GWL_STYLE), GetWindowLongA(h, GWL_EXSTYLE),
           IsWindowVisible(h) ? "" : " hidden");
    return TRUE;
}

static BOOL CALLBACK top(HWND h, LPARAM unused)
{
    char text[512];
    if (!IsWindowVisible(h)) return TRUE;
    GetWindowTextA(h, text, sizeof(text));
    if (filter && !strstr(text, filter)) return TRUE;
    child(h, 0);
    EnumChildWindows(h, child, 1);
    return TRUE;
}

int main(int argc, char **argv)
{
    filter = argc > 1 ? argv[1] : NULL;
    EnumWindows(top, 0);
    return 0;
}
