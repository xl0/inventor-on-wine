/* wl_winctl.exe HWND_HEX ACTION: top | show | hide | max | restore | close | foreground | topmost | move X Y W H  on any top-level window.
 * Build: x86_64-w64-mingw32-gcc -o tests/wl_winctl.exe tests/wl_winctl.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    HWND h = (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16); const char *a = argv[2];
    if (!strcmp(a, "top")) BringWindowToTop(h);
    else if (!strcmp(a, "show")) ShowWindow(h, SW_SHOW);
    else if (!strcmp(a, "hide")) ShowWindow(h, SW_HIDE);
    else if (!strcmp(a, "max")) ShowWindow(h, SW_MAXIMIZE);
    else if (!strcmp(a, "restore")) ShowWindow(h, SW_RESTORE);
    else if (!strcmp(a, "close")) PostMessageW(h, WM_CLOSE, 0, 0);
    else if (!strcmp(a, "foreground")) printf("SetForegroundWindow %d\n", SetForegroundWindow(h));
    else if (!strcmp(a, "topmost")) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    else if (!strcmp(a, "move")) SetWindowPos(h, 0, atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), SWP_NOZORDER);
    printf("fg=%p\n", GetForegroundWindow());
    return 0;
}
