/* Custom caption: WS_OVERLAPPEDWINDOW whose WM_NCCALCSIZE keeps only the
 * side/bottom frame, so the client covers the caption (Chromium/Office style).
 * Paints its own blue caption strip. Prints window/client rects.
 * custom_caption.exe [max|plain|plainmax] [seconds]   (plain: default WM_NCCALCSIZE)   (issue 040, screenshot it) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static BOOL plain;

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (wp && !plain)
        {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
            LONG top = p->rgrc[0].top, left = p->rgrc[0].left;
            DefWindowProcW( hwnd, msg, wp, lp );
            /* maximized windows hang off-screen by the frame width */
            p->rgrc[0].top = IsZoomed( hwnd ) ? top + p->rgrc[0].left - left : top;
            return 0;
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT rc, cap;
        HDC hdc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        FillRect( hdc, &rc, GetStockObject( WHITE_BRUSH ) );
        cap = rc;
        cap.bottom = 30;
        SetBkColor( hdc, RGB( 0, 90, 200 ) );
        ExtTextOutW( hdc, 0, 0, ETO_OPAQUE, &cap, NULL, 0, NULL );
        SetTextColor( hdc, RGB( 255, 255, 255 ) );
        DrawTextW( hdc, L"  CUSTOM CAPTION (client area)", -1, &cap, DT_SINGLELINE | DT_VCENTER );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_NCHITTEST:
    {
        LRESULT ret = DefWindowProcW( hwnd, msg, wp, lp );
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (ret == HTCLIENT && ScreenToClient( hwnd, &pt ) && pt.y < 30) return HTCAPTION;
        return ret;
    }
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static void print_rects( HWND hwnd )
{
    RECT wr, cr;
    POINT pt = { 0, 0 };

    GetWindowRect( hwnd, &wr );
    GetClientRect( hwnd, &cr );
    ClientToScreen( hwnd, &pt );
    printf( "style %08lx window %ld,%ld-%ld,%ld client %ldx%ld at %ld,%ld\n", GetWindowLongW( hwnd, GWL_STYLE ),
            wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom, pt.x, pt.y );
    fflush( stdout );
}

int main( int argc, char **argv )
{
    WNDCLASSW cls = { 0, wndproc, 0, 0, GetModuleHandleW( NULL ), NULL, LoadCursorW( NULL, (LPCWSTR)IDC_ARROW ), NULL, NULL, L"custom_caption" };
    BOOL max = argc > 1 && strstr( argv[1], "max" ) != NULL;
    int secs = argc > 2 ? atoi( argv[2] ) : 30;
    HWND hwnd;
    MSG msg;

    plain = argc > 1 && strstr( argv[1], "plain" ) != NULL;
    RegisterClassW( &cls );
    hwnd = CreateWindowExW( WS_EX_TOPMOST, L"custom_caption", L"custom_caption", WS_OVERLAPPEDWINDOW,
                            100, 100, 500, 300, NULL, NULL, NULL, NULL );
    SetWindowPos( hwnd, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER );
    ShowWindow( hwnd, SW_SHOW ); /* the first call may take the (hidden) STARTUPINFO show state */
    ShowWindow( hwnd, SW_SHOW );
    UpdateWindow( hwnd );
    print_rects( hwnd );

    /* maximize after the window is mapped, like a user would */
    if (max) SetTimer( hwnd, 2, 1000, NULL );
    SetTimer( hwnd, 1, secs * 1000, NULL );
    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        if (msg.message == WM_TIMER && msg.hwnd == hwnd && msg.wParam == 1) break;
        if (msg.message == WM_TIMER && msg.hwnd == hwnd && msg.wParam == 2)
        {
            KillTimer( hwnd, 2 );
            ShowWindow( hwnd, SW_MAXIMIZE );
            print_rects( hwnd );
            continue;
        }
        DispatchMessageW( &msg );
    }
    print_rects( hwnd );
    return 0;
}
