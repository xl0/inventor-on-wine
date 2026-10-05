/* win32u set_window_text use-after-free (issue 183): two threads set the text of one window with
 * DefWindowProc(WM_SETTEXT); each call frees the previous string, which the other thread may still be
 * handing to the driver (title conversion in winex11's sync_window_text).
 * Build: x86_64-w64-mingw32-gcc -O1 -o settext_race.exe settext_race.c -luser32
 *   settext_race [SECS]   prints DONE; a crash or heap corruption report is the result. */
#include <windows.h>
#include <stdio.h>

static HWND W;
static volatile LONG stop, sets;

static DWORD WINAPI text_proc(void *arg)
{
    static char text[2][4096];
    int i, k = (int)(INT_PTR)arg;
    memset(text[k], 'a' + k, sizeof(text[k]) - 1);
    for (i = 0; !stop; i++)
    {
        text[k][16 + (i * 37) % 4000] = 0;  /* another length every time: the old block is freed and reused */
        DefWindowProcA(W, WM_SETTEXT, 0, (LPARAM)text[k]);
        text[k][16 + (i * 37) % 4000] = 'a' + k;
        InterlockedIncrement(&sets);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    HANDLE th[2];
    W = CreateWindowExA(0, "static", "settext", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 200, NULL, NULL, NULL, NULL);
    th[0] = CreateThread(NULL, 0, text_proc, (void *)0, 0, NULL);
    th[1] = CreateThread(NULL, 0, text_proc, (void *)1, 0, NULL);
    Sleep(secs * 1000);
    stop = 1;
    WaitForMultipleObjects(2, th, TRUE, 5000);
    printf("DONE %ld sets\n", sets); fflush(stdout);
    return 0;
}
