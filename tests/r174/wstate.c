/* wstate.c: state of another process' top-level window after a resize (174).
 * Usage: wstate.exe TITLE-PREFIX [tree] [redraw]
 * Prints the window / client rect, the window region, and with `tree` every visible descendant
 * (class, rect in the top-level's client coordinates, update rect if any). `redraw` then invalidates
 * the whole tree (RedrawWindow with RDW_ALLCHILDREN), to tell content the application never painted
 * (a screenshot changes after it) from content that didn't reach the screen.
 * Build: x86_64-w64-mingw32-gcc -O2 -o wstate.exe wstate.c -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *prefix;
static HWND found, top;

static BOOL CALLBACK find_proc( HWND hwnd, LPARAM lp )
{
    char title[256];
    if (!IsWindowVisible( hwnd ) || !GetWindowTextA( hwnd, title, sizeof(title) )) return TRUE;
    if (strncmp( title, prefix, strlen( prefix ) )) return TRUE;
    found = hwnd;
    return FALSE;
}

static BOOL CALLBACK child_proc( HWND hwnd, LPARAM lp )
{
    char class[80];
    RECT rc, upd;
    int depth = 0;
    HWND parent;

    if (!IsWindowVisible( hwnd )) return TRUE;
    for (parent = GetParent( hwnd ); parent && parent != top; parent = GetParent( parent )) depth++;
    GetClassNameA( hwnd, class, sizeof(class) );
    GetWindowRect( hwnd, &rc );
    MapWindowPoints( 0, top, (POINT *)&rc, 2 );
    printf( "%*s%p %-40.40s %ld,%ld-%ld,%ld", depth * 2 + 2, "", hwnd, class, rc.left, rc.top, rc.right, rc.bottom );
    if (GetUpdateRect( hwnd, &upd, FALSE )) printf( " UPDATE %ld,%ld-%ld,%ld", upd.left, upd.top, upd.right, upd.bottom );
    printf( "\n" );
    return TRUE;
}

int main( int argc, char **argv )
{
    RECT win, client, box, upd;
    HRGN rgn = CreateRectRgn( 0, 0, 0, 0 );
    int i, type;

    if (argc < 2) return 2;
    prefix = argv[1];
    EnumWindows( find_proc, 0 );
    if (!(top = found)) { printf( "no window '%s'\n", prefix ); return 1; }
    GetWindowRect( top, &win );
    GetClientRect( top, &client );
    type = GetWindowRgn( top, rgn );
    GetRgnBox( rgn, &box );
    printf( "hwnd %p win %ld,%ld-%ld,%ld client %ldx%ld style %08lx", top, win.left, win.top, win.right, win.bottom,
            client.right, client.bottom, GetWindowLongA( top, GWL_STYLE ) );
    if (type) printf( " region type %d box %ld,%ld-%ld,%ld", type, box.left, box.top, box.right, box.bottom );
    if (GetUpdateRect( top, &upd, FALSE )) printf( " UPDATE %ld,%ld-%ld,%ld", upd.left, upd.top, upd.right, upd.bottom );
    printf( "\n" );
    for (i = 2; i < argc; i++)
    {
        if (!strcmp( argv[i], "tree" )) EnumChildWindows( top, child_proc, 0 );
        if (!strcmp( argv[i], "redraw" ))
            RedrawWindow( top, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN );
    }
    return 0;
}
