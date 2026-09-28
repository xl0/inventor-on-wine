/* Draw "Pan\r" (trailing CR, as in Inventor's combo box strings) with Tahoma via
 * ExtTextOutW, DrawTextW and a user32 combobox/listbox; exits after 15 s (screenshot
 * it). Windows draws nothing for the CR, Wine a .notdef box (issue 039).
 * Build: x86_64-w64-mingw32-gcc -O2 -mwindows -o ctrlchar_text.exe ctrlchar_text.c */
#include <windows.h>
static const WCHAR s[] = L"Pan\r|";
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r = {10, 40, 300, 60};
        HFONT f = CreateFontW(-13, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Tahoma");
        SelectObject(dc, f);
        TextOutW(dc, 10, 10, L"ExtTextOutW:", 12); ExtTextOutW(dc, 120, 10, 0, NULL, s, 5, NULL);
        TextOutW(dc, 10, 40, L"DrawTextW SL:", 13); r.left = 120; DrawTextW(dc, s, 5, &r, DT_SINGLELINE);
        r.top = 70; r.bottom = 90; TextOutW(dc, 10, 70, L"DrawTextW:", 10); DrawTextW(dc, s, 5, &r, 0);
        EndPaint(h, &ps); DeleteObject(f); return 0;
    }
    if (m == WM_TIMER) PostQuitMessage(0);
    if (m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE p, LPSTR c, int n)
{
    WNDCLASSW wc = {0, proc, 0, 0, hi, 0, LoadCursor(0, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), 0, L"crtext"};
    HWND h, cb, lb; MSG msg;
    HFONT f = CreateFontW(-13, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Tahoma");
    RegisterClassW(&wc);
    h = CreateWindowW(L"crtext", L"crtext", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 260, 0, 0, hi, 0);
    cb = CreateWindowW(L"ComboBox", 0, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 10, 100, 150, 200, h, 0, hi, 0);
    lb = CreateWindowW(L"ListBox", 0, WS_CHILD | WS_VISIBLE | WS_BORDER, 180, 100, 150, 60, h, 0, hi, 0);
    SendMessageW(cb, WM_SETFONT, (WPARAM)f, 0); SendMessageW(lb, WM_SETFONT, (WPARAM)f, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Pan\r"); SendMessageW(cb, CB_SETCURSEL, 0, 0);
    SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"Pan\r"); SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"Orbit\r");
    SetTimer(h, 1, 15000, 0);
    while (GetMessageW(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
