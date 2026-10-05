/* gdistress.c: several threads use GDI and USER objects that winex11 backs with X resources on its shared GDI display
 * (issue 182: libX11 XID allocation race). Each thread owns a top-level window and loops over random operations:
 * memory DC churn, pens / solid / hatched / pattern brushes on the screen DC (drawn by the X11 driver itself) and on
 * window DCs, BitBlt with ROPs, StretchBlt, AlphaBlend, GradientFill, GetPixel, text in several fonts with and without
 * antialiasing and complex clip regions, icons, cursors (colour, mono, system) set on the own window, window regions,
 * and UpdateLayeredWindow / SetLayeredWindowAttributes on a window of the main thread, which only pumps messages (X window
 * recreated from another thread, one thread at a time).
 * Usage: gdistress.exe SECS [THREADS] [SEED] [FLAGS]   prints "DONE gdistress ..." ; a hang, an abort or an X error is the result.
 *   FLAGS x: also draw on, and change the layered state of, the other workers' windows. That runs into win32u races between
 *            a thread that uses a window DC and another one that changes the window (issue 188), on any build.
 *         n: no X window recreation at all. With it the stress also runs into issue 177 (an X error from the flush into
 *            the replaced X window vs. a thread that reads the screen: always in synchronous mode, a few percent of the
 *            runs under a window manager otherwise) and issue 191 (the window's XIC destroyed under the owner thread).
 * Build: x86_64-w64-mingw32-gcc -O2 -o gdistress.exe gdistress.c -lgdi32 -luser32 -lmsimg32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 32
static volatile LONG stop, total, failures;
static HWND wins[MAXT], victim;
static LONG victim_busy;
static int nthreads, seed, cross, nolayer;

struct rnd { unsigned int s; };
static unsigned int rnd( struct rnd *r, unsigned int n ) { r->s = r->s * 1664525 + 1013904223; return (r->s >> 8) % n; }

static const char * const fonts[] = { "Tahoma", "Arial", "Courier New", "Times New Roman", "Symbol", "Marlett", "MS Sans Serif", "System" };
static const DWORD rops[] = { SRCCOPY, SRCINVERT, SRCAND, SRCPAINT, MERGECOPY, PATINVERT, 0x00B8074A, 0x00E20746, NOTSRCCOPY, DSTINVERT, PATCOPY };

static HBITMAP make_dib( int w, int h, int bpp, DWORD fill, HDC *mem )
{
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -h, 1, bpp, BI_RGB}};
    void *bits;
    HBITMAP dib = CreateDIBSection( 0, &bi, DIB_RGB_COLORS, &bits, 0, 0 );
    int i;
    if (bpp == 32) for (i = 0; i < w * h; i++) ((DWORD *)bits)[i] = fill ^ (i * 0x010203);
    else memset( bits, fill, ((w * bpp + 31) / 32) * 4 * h );
    *mem = CreateCompatibleDC( 0 );
    SelectObject( *mem, dib );
    return dib;
}

static HRGN make_rgn( struct rnd *r, int x, int y )
{
    HRGN rgn = CreateRectRgn( x, y, x + 60 + rnd( r, 100 ), y + 40 + rnd( r, 100 ) ), tmp;
    int i, n = rnd( r, 5 );
    for (i = 0; i < n; i++)
    {
        int cx = x + rnd( r, 150 ), cy = y + rnd( r, 120 );
        tmp = rnd( r, 2 ) ? CreateEllipticRgn( cx, cy, cx + 50, cy + 40 ) : CreateRectRgn( cx, cy, cx + 30, cy + 30 );
        CombineRgn( rgn, rgn, tmp, rnd( r, 2 ) ? RGN_OR : RGN_XOR );
        DeleteObject( tmp );
    }
    return rgn;
}

static HCURSOR make_cursor( struct rnd *r )
{
    ICONINFO ii = { FALSE, rnd( r, 16 ), rnd( r, 16 ) };
    BYTE and_bits[32 * 4], xor_bits[32 * 4];
    HDC mem;
    HCURSOR cur;
    int i;

    for (i = 0; i < sizeof(and_bits); i++) { and_bits[i] = rnd( r, 256 ); xor_bits[i] = rnd( r, 256 ); }
    if (!rnd( r, 3 )) return CreateCursor( 0, ii.xHotspot, ii.yHotspot, 32, 32, and_bits, xor_bits );
    ii.hbmMask = CreateBitmap( 32, 32, 1, 1, and_bits );
    ii.hbmColor = make_dib( 32, 32, 32, rnd( r, 2 ) ? 0xff000000 | rnd( r, 0xffffff ) : rnd( r, 0xffffff ), &mem );
    DeleteDC( mem );
    cur = CreateIconIndirect( &ii );
    DeleteObject( ii.hbmMask ); DeleteObject( ii.hbmColor );
    return cur;
}

static void draw( struct rnd *r, HDC dc, int ox, int oy )
{
    int x = ox + rnd( r, 300 ), y = oy + rnd( r, 200 ), op = rnd( r, 12 );
    HDC mem;
    HBITMAP bmp;
    HGDIOBJ obj, old, old2;

    switch (op)
    {
    case 0: /* pen + brush kinds */
        obj = rnd( r, 3 ) ? CreatePen( rnd( r, 5 ), rnd( r, 4 ), rnd( r, 0xffffff ) ) : GetStockObject( NULL_PEN );
        old = SelectObject( dc, obj );
        switch (rnd( r, 4 ))
        {
        case 0: old2 = SelectObject( dc, CreateSolidBrush( rnd( r, 0xffffff ) ) ); break;
        case 1: old2 = SelectObject( dc, CreateHatchBrush( rnd( r, 6 ), rnd( r, 0xffffff ) ) ); break;
        case 2: bmp = CreateBitmap( 8, 8, 1, 1, "\x55\0\xaa\0\x55\0\xaa\0\x55\0\xaa\0\x55\0\xaa\0" );
                old2 = SelectObject( dc, CreatePatternBrush( bmp ) ); DeleteObject( bmp ); break;
        default: bmp = make_dib( 8, 8, 32, rnd( r, 0xffffff ), &mem ); DeleteDC( mem );
                old2 = SelectObject( dc, CreatePatternBrush( bmp ) ); DeleteObject( bmp ); break;
        }
        SetBkMode( dc, rnd( r, 2 ) ? OPAQUE : TRANSPARENT );
        if (rnd( r, 2 )) Rectangle( dc, x, y, x + 80, y + 60 ); else Ellipse( dc, x, y, x + 80, y + 60 );
        if (rnd( r, 2 )) { MoveToEx( dc, x, y, NULL ); LineTo( dc, x + 90, y + 70 ); }
        DeleteObject( SelectObject( dc, old2 ) );
        DeleteObject( SelectObject( dc, old ) );
        break;
    case 1: /* BitBlt from a DIB with a ROP and a brush */
        bmp = make_dib( 64, 48, rnd( r, 3 ) ? 32 : 1, rnd( r, 0xffffff ), &mem );
        old = SelectObject( dc, CreateSolidBrush( rnd( r, 0xffffff ) ) );
        BitBlt( dc, x, y, 64, 48, mem, 0, 0, rops[rnd( r, ARRAYSIZE(rops) )] );
        DeleteObject( SelectObject( dc, old ) );
        DeleteDC( mem ); DeleteObject( bmp );
        break;
    case 2: /* the DC to itself, stretched */
        StretchBlt( dc, x, y, 40 + rnd( r, 60 ), 30 + rnd( r, 60 ), dc, ox + rnd( r, 200 ), oy + rnd( r, 100 ), 50, 40, rnd( r, 2 ) ? SRCCOPY : SRCINVERT );
        break;
    case 3: /* DC to memory */
        bmp = make_dib( 64, 48, 32, 0, &mem );
        BitBlt( mem, 0, 0, 64, 48, dc, x, y, SRCCOPY );
        DeleteDC( mem ); DeleteObject( bmp );
        GetPixel( dc, x, y );
        break;
    case 4: /* alpha blending, with and without source alpha, stretched or not */
    {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 64 + rnd( r, 192 ), rnd( r, 2 ) ? AC_SRC_ALPHA : 0 };
        bmp = make_dib( 64, 48, 32, 0x80404040, &mem );
        GdiAlphaBlend( dc, x, y, rnd( r, 2 ) ? 64 : 100, 48, mem, 0, 0, 64, 48, bf );
        DeleteDC( mem ); DeleteObject( bmp );
        break;
    }
    case 5: /* gradient */
    {
        TRIVERTEX v[2] = { { x, y, rnd( r, 0xff00 ), 0, 0, 0xff00 }, { x + 90, y + 50, 0, rnd( r, 0xff00 ), 0xff00, 0xff00 } };
        GRADIENT_RECT gr = { 0, 1 };
        GdiGradientFill( dc, v, 2, &gr, 1, rnd( r, 2 ) ? GRADIENT_FILL_RECT_H : GRADIENT_FILL_RECT_V );
        break;
    }
    case 6: case 7: case 8: /* text: fonts, sizes, antialiasing, clipping */
    {
        static const BYTE quality[] = { DEFAULT_QUALITY, NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY };
        HRGN rgn = rnd( r, 2 ) ? make_rgn( r, x, y ) : 0;
        obj = CreateFontA( -(8 + (int)rnd( r, 30 )), 0, rnd( r, 4 ) ? 0 : rnd( r, 3600 ), 0, rnd( r, 2 ) ? FW_BOLD : FW_NORMAL, rnd( r, 4 ) == 0, 0, 0,
                           DEFAULT_CHARSET, 0, 0, quality[rnd( r, 4 )], 0, fonts[rnd( r, ARRAYSIZE(fonts) )] );
        old = SelectObject( dc, obj );
        if (rgn) SelectClipRgn( dc, rgn );
        SetTextColor( dc, rnd( r, 0xffffff ) ); SetBkColor( dc, rnd( r, 0xffffff ) );
        SetBkMode( dc, rnd( r, 2 ) ? OPAQUE : TRANSPARENT );
        ExtTextOutA( dc, x, y, rnd( r, 2 ) ? ETO_OPAQUE : 0, NULL, "The quick brown fox 0123456789", 10 + rnd( r, 20 ), NULL );
        if (rnd( r, 3 ) == 0) { RECT rc = { x, y + 40, x + 150, y + 90 }; DrawTextA( dc, "jumps over\nthe lazy dog", -1, &rc, DT_CENTER | DT_WORDBREAK ); }
        SelectClipRgn( dc, 0 );
        if (rgn) DeleteObject( rgn );
        DeleteObject( SelectObject( dc, old ) );
        break;
    }
    case 9: /* icons */
        DrawIconEx( dc, x, y, LoadIconA( 0, (char *)(ULONG_PTR)(32512 + rnd( r, 6 )) ), 16 + rnd( r, 48 ), 16 + rnd( r, 48 ), 0, 0,
                    rnd( r, 3 ) ? DI_NORMAL : DI_MASK );
        break;
    case 10: /* region fill and frame */
    {
        HRGN rgn = make_rgn( r, x, y );
        obj = CreateSolidBrush( rnd( r, 0xffffff ) );
        if (rnd( r, 2 )) FillRgn( dc, rgn, obj ); else FrameRgn( dc, rgn, obj, 2, 2 );
        InvertRgn( dc, rgn );
        DeleteObject( obj ); DeleteObject( rgn );
        break;
    }
    default: /* compatible bitmap round trip */
        mem = CreateCompatibleDC( dc );
        bmp = CreateCompatibleBitmap( dc, 50, 40 );
        old = SelectObject( mem, bmp );
        BitBlt( mem, 0, 0, 50, 40, dc, x, y, SRCCOPY );
        PatBlt( mem, 5, 5, 20, 20, DSTINVERT );
        BitBlt( dc, x + 10, y + 10, 50, 40, mem, 0, 0, SRCCOPY );
        SelectObject( mem, old );
        DeleteObject( bmp ); DeleteDC( mem );
        break;
    }
}

static void layered( struct rnd *r, HWND hwnd )
{
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT pt = { 0, 0 };
    SIZE size = { 64, 64 };
    HDC mem;
    HBITMAP bmp;

    if (!(GetWindowLongA( hwnd, GWL_EXSTYLE ) & WS_EX_LAYERED)) return;
    if (rnd( r, 2 )) SetLayeredWindowAttributes( hwnd, 0, 128 + rnd( r, 128 ), LWA_ALPHA );
    else
    {
        /* reset the attributes so that UpdateLayeredWindow is valid again: the X window gets an ARGB visual */
        SetWindowLongA( hwnd, GWL_EXSTYLE, GetWindowLongA( hwnd, GWL_EXSTYLE ) & ~WS_EX_LAYERED );
        SetWindowLongA( hwnd, GWL_EXSTYLE, GetWindowLongA( hwnd, GWL_EXSTYLE ) | WS_EX_LAYERED );
        bmp = make_dib( 64, 64, 32, 0xff206080, &mem );
        UpdateLayeredWindow( hwnd, NULL, NULL, &size, mem, &pt, 0, &bf, ULW_ALPHA );
        DeleteDC( mem ); DeleteObject( bmp );
    }
}

static LRESULT WINAPI wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_SETCURSOR) return TRUE; /* keep the cursors set by the thread */
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static DWORD WINAPI worker( void *arg )
{
    int idx = (INT_PTR)arg, i;
    struct rnd r = { 12345 + idx * 7919 + seed * 104729 };
    HWND hwnd;
    HCURSOR cursor = 0;
    MSG msg;

    hwnd = CreateWindowExA( (idx & 1) ? WS_EX_LAYERED : 0, "gdistress", "gdistress", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            40 + (idx % 4) * 300, 40 + (idx / 4 % 3) * 260, 280, 240, 0, 0, 0, 0 );
    if (idx & 1) SetLayeredWindowAttributes( hwnd, 0, 255, LWA_ALPHA );
    wins[idx] = hwnd;

    while (!stop)
    {
        HDC dc;
        unsigned int op = rnd( &r, 100 );

        if (op < 20) { DeleteDC( CreateCompatibleDC( 0 ) ); }
        else if (op < 45) /* the screen: drawn by the X11 driver */
        {
            dc = GetDC( 0 );
            for (i = rnd( &r, 4 ); i >= 0; i--) draw( &r, dc, 100 + (idx % 4) * 250, 600 );
            ReleaseDC( 0, dc );
        }
        else if (op < 70) /* own window, client or window DC, with or without a clip region */
        {
            HRGN rgn = rnd( &r, 3 ) ? 0 : make_rgn( &r, 0, 0 );
            dc = rgn ? GetDCEx( hwnd, rgn, DCX_CACHE | DCX_INTERSECTRGN | (rnd( &r, 2 ) ? DCX_WINDOW : 0) ) : rnd( &r, 4 ) ? GetDC( hwnd ) : GetWindowDC( hwnd );
            for (i = rnd( &r, 4 ); i >= 0; i--) draw( &r, dc, 0, 0 );
            ReleaseDC( hwnd, dc );
        }
        else if (op < 78) /* another thread's window */
        {
            HWND other = wins[rnd( &r, nthreads )];
            if (!cross) DeleteObject( CreateCompatibleBitmap( dc = GetDC( 0 ), 16, 16 ) ), ReleaseDC( 0, dc );
            else if (other && (dc = GetDC( other ))) { draw( &r, dc, 0, 0 ); ReleaseDC( other, dc ); }
        }
        else if (op < 86) /* cursors */
        {
            HCURSOR prev = cursor;
            cursor = rnd( &r, 3 ) ? make_cursor( &r ) : 0;
            SetCursor( cursor ? cursor : LoadCursorA( 0, (char *)(ULONG_PTR)(32512 + rnd( &r, 4 )) ) );
            if (prev) DestroyCursor( prev );
        }
        else if (op < 90) SetWindowRgn( hwnd, rnd( &r, 3 ) ? make_rgn( &r, 0, 0 ) : 0, TRUE );
        else if (op < 94)
        {
            HWND other = wins[rnd( &r, nthreads )];
            if (nolayer) DeleteDC( CreateCompatibleDC( 0 ) );
            else if (cross && other) layered( &r, other );
            else if (!InterlockedCompareExchange( &victim_busy, 1, 0 )) { layered( &r, victim ); InterlockedExchange( &victim_busy, 0 ); }
        }
        else if (op < 97) SetWindowPos( hwnd, 0, 40 + rnd( &r, 900 ), 40 + rnd( &r, 500 ), 200 + rnd( &r, 200 ), 160 + rnd( &r, 200 ), SWP_NOZORDER | SWP_NOACTIVATE );
        else RedrawWindow( hwnd, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW );

        InterlockedIncrement( &total );
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
    }
    wins[idx] = 0;
    DestroyWindow( hwnd );
    return 0;
}

int main( int argc, char **argv )
{
    WNDCLASSA wc = { 0, wndproc, 0, 0, GetModuleHandleA( 0 ), 0, 0, (HBRUSH)(COLOR_WINDOW + 1), 0, "gdistress" };
    int i, secs = argc > 1 ? atoi( argv[1] ) : 10;
    HANDLE h[MAXT];
    DWORD end;
    MSG msg;

    nthreads = argc > 2 ? min( atoi( argv[2] ), MAXT ) : 4;
    if (argc > 3) seed = atoi( argv[3] );
    cross = argc > 4 && strchr( argv[4], 'x' );
    nolayer = argc > 4 && strchr( argv[4], 'n' );
    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    victim = CreateWindowExA( WS_EX_LAYERED, "gdistress", "gdistress victim", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 900, 620, 200, 160, 0, 0, 0, 0 );
    SetLayeredWindowAttributes( victim, 0, 255, LWA_ALPHA );
    for (i = 0; i < nthreads; i++)
    {
        h[i] = CreateThread( 0, 0, worker, (void *)(INT_PTR)i, 0, 0 );
        /* one at a time: threads that create their first window together run into issue 190 (XOpenIM) */
        while (!wins[i]) { while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg ); Sleep( 1 ); }
    }
    for (end = GetTickCount() + secs * 1000; (int)(end - GetTickCount()) > 0;)
    {
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 20, QS_ALLINPUT );
    }
    stop = 1;
    for (end = GetTickCount() + 30000; (int)(end - GetTickCount()) > 0;)
    {
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        if (WaitForMultipleObjects( nthreads, h, TRUE, 50 ) != WAIT_TIMEOUT) break;
    }
    if (WaitForMultipleObjects( nthreads, h, TRUE, 0 ) == WAIT_TIMEOUT)
    {
        printf( "threads did not stop\n" );
        Sleep( INFINITE ); /* for the watchdog's backtraces */
    }
    printf( "DONE gdistress threads %d: %ld operations\n", nthreads, total );
    return 0;
}
