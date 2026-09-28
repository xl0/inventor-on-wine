/* DateTimePicker (DTS_SHORTDATEFORMAT, with/without DTS_SHOWNONE) showing 9/28/2026;
 * exits after 15 s (screenshot it). Wine pads the month field: "9 /28/2026" (issue 044).
 * Build: x86_64-w64-mingw32-gcc -O2 -mwindows -o dtp_short.exe dtp_short.c -lcomctl32 */
#include <windows.h>
#include <commctrl.h>
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER || m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE p, LPSTR c, int n)
{
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_DATE_CLASSES};
    WNDCLASSW wc = {0, proc, 0, 0, hi, 0, LoadCursor(0, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), 0, L"dtp"};
    SYSTEMTIME st = {2026, 9, 1, 28};
    HWND h, d1, d2; MSG msg;
    InitCommonControlsEx(&icc); RegisterClassW(&wc);
    h = CreateWindowW(L"dtp", L"dtp", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 200, 0, 0, hi, 0);
    d1 = CreateWindowW(DATETIMEPICK_CLASSW, 0, WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT | DTS_SHOWNONE, 10, 10, 250, 24, h, 0, hi, 0);
    d2 = CreateWindowW(DATETIMEPICK_CLASSW, 0, WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT, 10, 50, 250, 24, h, 0, hi, 0);
    SendMessageW(d1, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st); SendMessageW(d2, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
    SetTimer(h, 1, 15000, 0);
    while (GetMessageW(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
