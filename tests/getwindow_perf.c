/* GetWindow() cost (057): MFC's idle-time command UI update walks the whole
 * window tree with GetWindow(GW_CHILD / GW_HWNDNEXT) after every message, and
 * Inventor has thousands of windows. Creates a hidden parent with N children
 * (each with a few grandchildren), walks the tree ITERS times and prints the
 * cost per call of GetWindow and a few other per-window calls MFC makes (GetProp: 081).
 * Usage: getwindow_perf.exe [N] [ITERS]
 * Build: x86_64-w64-mingw32-gcc -O2 -o getwindow_perf.exe getwindow_perf.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LARGE_INTEGER freq;
static double now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart / freq.QuadPart; }

static unsigned walk(HWND parent)
{
    unsigned n = 0;
    HWND child;
    for (child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        n++;
        n += walk(child);
    }
    return n + 1;  /* + the GW_CHILD / last GW_HWNDNEXT call returning NULL */
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1000, iters = argc > 2 ? atoi(argv[2]) : 20, i, j;
    HWND top, c, *all;
    unsigned calls = 0, count = 0;
    double t;

    QueryPerformanceFrequency(&freq);
    top = CreateWindowA("static", "top", WS_POPUP, 0, 0, 100, 100, 0, 0, 0, 0);
    all = malloc(n * sizeof(*all));
    for (i = 0; i < n; i++)
    {
        all[i] = c = CreateWindowA("static", "c", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, top, 0, 0, 0);
        if (i % 4 == 0) for (j = 0; j < 3; j++) CreateWindowA("static", "g", WS_CHILD, 0, 0, 5, 5, c, 0, 0, 0);
    }

    t = now();
    for (i = 0; i < iters; i++) calls += walk(top);
    t = now() - t;
    printf("GetWindow walk: %u windows, %.0f ns/call (%u calls)\n", calls / iters, t * 1e9 / calls, calls);

    t = now();
    for (i = 0; i < iters; i++) for (j = 0; j < n; j++) count += GetParent(all[j]) == top;
    printf("GetParent: %.0f ns/call\n", (now() - t) * 1e9 / (iters * n));
    t = now();
    for (i = 0; i < iters; i++) for (j = 0; j < n; j++) count += !!GetPropA(all[j], "x");
    printf("GetProp: %.0f ns/call\n", (now() - t) * 1e9 / (iters * n));
    for (j = 0; j < n; j++) SetPropA(all[j], "AfxOldWndProc423", all[j]);
    t = now();
    for (i = 0; i < iters; i++) for (j = 0; j < n; j++) count += GetPropA(all[j], "AfxOldWndProc423") == all[j];
    printf("GetProp (set, by name): %.0f ns/call\n", (now() - t) * 1e9 / (iters * n));
    t = now();
    for (i = 0; i < iters; i++) for (j = 0; j < n; j++) count += !!GetPropA(all[j], (const char *)(ULONG_PTR)0xc02c);
    printf("GetProp (unset, by atom): %.0f ns/call\n", (now() - t) * 1e9 / (iters * n));
    t = now();
    for (i = 0; i < iters; i++) for (j = 0; j < n; j++) count += IsWindowVisible(all[j]);
    printf("IsWindowVisible: %.0f ns/call\n", (now() - t) * 1e9 / (iters * n));
    t = now();
    for (i = 0; i < iters; i++) for (j = 0; j < n; j++) count += GetWindowLongW(all[j], GWL_STYLE) != 0;
    printf("GetWindowLong: %.0f ns/call\n", (now() - t) * 1e9 / (iters * n));
    DestroyWindow(top);
    return count == 0;
}
