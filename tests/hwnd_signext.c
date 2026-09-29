/* HWNDs with bit 31 set (059): create/destroy child windows until handles
 * with bit 31 show up, then check that every HWND the app sees for one
 * window has the same 64-bit value: CreateWindowEx result, hwnd passed to
 * the window proc (per message), GetWindow(GW_CHILD), EnumChildWindows.
 * Build: x86_64-w64-mingw32-gcc -O2 -o hwnd_signext.exe hwnd_signext.c */
#include <windows.h>
#include <stdio.h>

static HWND expect;              /* HWND the proc should see (0 while creating) */
static HWND created;             /* hwnd seen in WM_NCCREATE */
static int bad[WM_USER];         /* per message: proc got a different value */
static HWND bad_example[WM_USER];

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) created = hwnd;
    else if (msg < WM_USER && (expect ? hwnd != expect : hwnd != created))
    {
        if (!bad[msg]++) bad_example[msg] = hwnd;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND enum_found;
static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM lp) { enum_found = hwnd; return FALSE; }

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 300000, high = 0, signext = 0, zeroext = 0;
    int create_mismatch = 0, getwindow_mismatch = 0, enum_mismatch = 0, iswindow_other = 0;
    HWND parent, h, first_high = 0;
    WORD hi, maxhi = 0, lasthi = 0; int wraps = 0;
    WNDCLASSA wc = {0};

    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "signext_test";
    RegisterClassA(&wc);
    parent = CreateWindowA("static", "parent", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, 0, 0);

    for (i = 0; i < n; i++)
    {
        expect = created = 0;
        h = CreateWindowA("signext_test", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, (HMENU)0x25af, 0, 0);
        if (!h) { printf("CreateWindow failed at %d: %lu\n", i, GetLastError()); break; }
        if (created != h) create_mismatch++;
        hi = HIWORD(h);
        if (hi > maxhi) maxhi = hi;
        if (hi + 0x4000 < lasthi) { if (!wraps++) printf("uniq wrapped at %d: %p after %04x\n", i, h, lasthi); }
        lasthi = hi;
        if ((ULONG_PTR)h & 0x80000000)
        {
            ULONG_PTR other = (LONG_PTR)h < 0 ? (ULONG_PTR)h & 0xffffffff : (ULONG_PTR)(LONG_PTR)(LONG)(ULONG_PTR)h;
            if (!high++) first_high = h;
            if ((LONG_PTR)h < 0) signext++; else zeroext++;
            if (IsWindow((HWND)other)) iswindow_other++;
            if (GetWindow(parent, GW_CHILD) != h) getwindow_mismatch++;
            enum_found = 0;
            EnumChildWindows(parent, enum_proc, 0);
            if (enum_found != h) enum_mismatch++;
        }
        expect = h;
        SetWindowPos(h, 0, 1, 1, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        DestroyWindow(h);
        if (high >= 1000) break;
    }
    printf("iterations %d, last hwnd %p, bit-31 handles %d (sign-extended %d, zero-extended %d), first %p\n",
           i, h, high, signext, zeroext, first_high);
    printf("max HIWORD %04x, wraps %d\n", maxhi, wraps);
    printf("mismatches: create %d, GetWindow %d, EnumChildWindows %d; IsWindow(other extension) TRUE %d\n",
           create_mismatch, getwindow_mismatch, enum_mismatch, iswindow_other);
    for (i = 0; i < WM_USER; i++)
        if (bad[i]) printf("proc got a different hwnd: msg %04x x%d, e.g. %p\n", i, bad[i], bad_example[i]);
    DestroyWindow(parent);
    return 0;
}
