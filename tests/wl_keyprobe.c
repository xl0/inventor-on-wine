/* Wayland driver probe: logs key/char/focus messages of a window for SECS (default 8) seconds,
 * once for real input injected by the compositor (x/wshot.sh key/type) and once for SendInput
 * ('n' down/up at t=2s after SetForegroundWindow). Look for VK_PROCESSKEY (0xe5). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LRESULT CALLBACK proc( HWND h, UINT m, WPARAM w, LPARAM l )
{
    switch (m)
    {
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_ACTIVATE: case WM_INPUTLANGCHANGE:
        printf( "msg %04x wp %llx lp %llx\n", m, (unsigned long long)w, (unsigned long long)l ); fflush( stdout );
        break;
    case WM_DESTROY: PostQuitMessage( 0 ); break;
    }
    return DefWindowProcW( h, m, w, l );
}

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 8;
    WNDCLASSW wc = { .lpfnWndProc = proc, .hInstance = GetModuleHandleW( NULL ), .lpszClassName = L"wlkey",
                     .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1) };
    DWORD end = GetTickCount() + secs * 1000, sent = 0;
    MSG msg; HWND hwnd;
    RegisterClassW( &wc );
    hwnd = CreateWindowW( L"wlkey", L"keyprobe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, 0, 0, wc.hInstance, 0 );
    printf( "layout %p\n", GetKeyboardLayout( 0 ) );
    while (GetTickCount() < end)
    {
        while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
        if (!sent && GetTickCount() > end - (secs - 2) * 1000)
        {
            INPUT in[2] = { { .type = INPUT_KEYBOARD, .ki = { 'N', 0, 0 } }, { .type = INPUT_KEYBOARD, .ki = { 'N', 0, KEYEVENTF_KEYUP } } };
            sent = 1; SetForegroundWindow( hwnd ); printf( "SendInput N\n" ); SendInput( 2, in, sizeof(INPUT) );
        }
        Sleep( 10 );
    }
    return 0;
}
