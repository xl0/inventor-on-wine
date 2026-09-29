/* USER handle generation (uniq, HIWORD) per object type (059): create/destroy
 * one object of each type in a loop and report the max HIWORD, where it wraps
 * and how many handles had bit 31 set.
 * Usage: user_handle_uniq.exe [iterations]
 * Build: x86_64-w64-mingw32-gcc -O2 -o user_handle_uniq.exe user_handle_uniq.c -limm32 */
#include <windows.h>
#include <imm.h>
#include <stdio.h>

static LRESULT CALLBACK hook_proc(int code, WPARAM wp, LPARAM lp) { return CallNextHookEx(0, code, wp, lp); }

static HANDLE create(int type, HWND parent)
{
    static BYTE bits[32 * 4];
    ACCEL accel = { FVIRTKEY, 'A', 1 };
    switch (type)
    {
    case 0: return CreateWindowA("static", "", WS_CHILD, 0, 0, 10, 10, parent, 0, 0, 0);
    case 1: return CreateMenu();
    case 2: return CreateIcon(0, 32, 32, 1, 1, bits, bits);
    case 3: return CreateAcceleratorTableA(&accel, 1);
    case 4: return SetWindowsHookExA(WH_CBT, hook_proc, 0, GetCurrentThreadId());
    case 5: return BeginDeferWindowPos(1);
    case 6: return ImmCreateContext();
    }
    return 0;
}

static void destroy(int type, HANDLE h)
{
    switch (type)
    {
    case 0: DestroyWindow(h); break;
    case 1: DestroyMenu(h); break;
    case 2: DestroyIcon(h); break;
    case 3: DestroyAcceleratorTable(h); break;
    case 4: UnhookWindowsHookEx(h); break;
    case 5: EndDeferWindowPos(h); break;
    case 6: ImmDestroyContext(h); break;
    }
}

int main(int argc, char **argv)
{
    static const char *names[] = { "window", "menu", "icon", "accel", "hook", "dwp", "himc" };
    int n = argc > 1 ? atoi(argv[1]) : 200000, type, i;
    HWND parent = CreateWindowA("static", "parent", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, 0, 0);

    for (type = 0; type < 7; type++)
    {
        WORD hi, maxhi = 0, lasthi = 0, wrap_from = 0, wrap_to = 0;
        int wraps = 0, high = 0, lows = 0;
        HANDLE h = 0, first_high = 0, lastlow = 0;
        for (i = 0; i < n; i++)
        {
            if (!(h = create(type, parent))) { printf("%s: create failed at %d: %lu\n", names[type], i, GetLastError()); break; }
            hi = HIWORD(h);
            if (LOWORD(h) != LOWORD(lastlow)) lows++;
            lastlow = h;
            if (hi > maxhi) maxhi = hi;
            if (hi + 0x4000 < lasthi && !wraps++) { wrap_from = lasthi; wrap_to = hi; }
            lasthi = hi;
            if ((ULONG_PTR)h & 0x80000000 && !high++) first_high = h;
            destroy(type, h);
        }
        printf("%-6s: %d iterations, last %p, LOWORD changes %d, max HIWORD %04x, wraps %d (first %04x -> %04x), "
               "bit-31 %d (first %p)\n", names[type], i, h, lows, maxhi, wraps, wrap_from, wrap_to, high, first_high);
    }
    DestroyWindow(parent);
    return 0;
}
