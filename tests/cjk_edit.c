/* 119: CJK text in EDIT controls and DrawText/ExtTextOut per font, left on screen for a screenshot.
 * cjk_edit.exe [SECS] (default 30)
 * x86_64-w64-mingw32-gcc -O2 -o cjk_edit.exe cjk_edit.c -lgdi32 -luser32 */
#include <windows.h>
#include <stdlib.h>

static const WCHAR *faces[] = { L"Tahoma", L"Segoe UI", L"MS Shell Dlg", L"Arial", L"MS UI Gothic" };
static const WCHAR text[] = L"Gr\x00fc\x00df\x0065 \x65e5\x672c\x8a9e \x6e2c\x8a66 \x041f\x0440\x0438\x0432\x0435\x0442";
static HFONT fonts[ARRAYSIZE(faces)];

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        unsigned int i;
        for (i = 0; i < ARRAYSIZE(faces); i++)
        {
            RECT rc = { 250, 10 + 30 * i, 600, 40 + 30 * i };
            SelectObject( hdc, fonts[i] );
            DrawTextW( hdc, text, -1, &rc, DT_SINGLELINE );
            ExtTextOutW( hdc, 600, 10 + 30 * i, 0, NULL, text, lstrlenW( text ), NULL );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    if (msg == WM_DESTROY) PostQuitMessage( 0 );
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    WNDCLASSW wc = { 0, wndproc, 0, 0, GetModuleHandleW( NULL ), 0, LoadCursorW( 0, (LPCWSTR)IDC_ARROW ),
                     (HBRUSH)(COLOR_BTNFACE + 1), NULL, L"cjk_edit" };
    HWND hwnd;
    MSG msg;
    unsigned int i;

    RegisterClassW( &wc );
    hwnd = CreateWindowW( L"cjk_edit", L"cjk_edit", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 900, 200,
                          0, 0, wc.hInstance, NULL );
    for (i = 0; i < ARRAYSIZE(faces); i++)
    {
        HWND edit = CreateWindowW( L"Edit", text, WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10 + 30 * i,
                                   230, 24, hwnd, 0, wc.hInstance, NULL );
        fonts[i] = CreateFontW( -11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, faces[i] );
        SendMessageW( edit, WM_SETFONT, (WPARAM)fonts[i], TRUE );
    }
    SetTimer( hwnd, 1, (argc > 1 ? atoi( argv[1] ) : 30) * 1000, NULL );
    while (GetMessageW( &msg, 0, 0, 0 ) && msg.message != WM_TIMER) DispatchMessageW( &msg );
    return 0;
}
