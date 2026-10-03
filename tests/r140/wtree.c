/* Focus window of every GUI thread of a process and its parent chain, or the subtree of a window:
 * class, style, exstyle, id, text (140: what IsDialogMessage walks in Inventor).
 *   wtree.exe IMAGE-SUBSTRING     focus/active/capture per thread + the focus window's parents
 *   wtree.exe 0xHWND              that window's subtree (hidden windows included)
 *   wtree.exe text:TEXT           subtrees of all windows with that text, with their parent chains
 * Never sends messages (works on a hung process). Build: x86_64-w64-mingw32-gcc -O2 -o wtree.exe wtree.c */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void line( HWND h, int depth )
{
    char cls[80] = "?";
    WCHAR text[64] = L"";
    GetClassNameA( h, cls, sizeof(cls) );
    InternalGetWindowText( h, text, 64 );
    printf( "%*s%p %s style=%08lx ex=%08lx id=%ld '%ls'%s%s\n", depth * 2, "", h, cls, GetWindowLongA( h, GWL_STYLE ),
            GetWindowLongA( h, GWL_EXSTYLE ), (long)GetWindowLongPtrA( h, GWLP_ID ), text,
            (GetWindowLongA( h, GWL_STYLE ) & WS_VISIBLE) ? "" : " HIDDEN", (GetWindowLongA( h, GWL_STYLE ) & WS_DISABLED) ? " DISABLED" : "" );
}

static void tree( HWND h, int depth )
{
    HWND c;
    line( h, depth );
    for (c = GetWindow( h, GW_CHILD ); c; c = GetWindow( c, GW_HWNDNEXT )) tree( c, depth + 1 );
}

static const char *find_text;

static BOOL CALLBACK find_proc( HWND h, LPARAM top )
{
    WCHAR text[64] = L"";
    char buf[64];
    HWND p;
    InternalGetWindowText( h, text, 64 );
    WideCharToMultiByte( CP_ACP, 0, text, -1, buf, sizeof(buf), NULL, NULL );
    if (!strcmp( buf, find_text ))
    {
        printf( "parents:\n" );
        for (p = GetAncestor( h, GA_PARENT ); p && p != GetDesktopWindow(); p = GetAncestor( p, GA_PARENT )) line( p, 1 );
        printf( "subtree:\n" );
        tree( h, 1 );
    }
    if (top) EnumChildWindows( h, find_proc, 0 );
    return TRUE;
}

int main( int argc, char **argv )
{
    PROCESSENTRY32 pe = { sizeof(pe) };
    THREADENTRY32 te = { sizeof(te) };
    HANDLE snap;

    if (argc < 2) return 1;
    if (!strncmp( argv[1], "text:", 5 )) { find_text = argv[1] + 5; EnumWindows( find_proc, 1 ); return 0; }
    if (!strncmp( argv[1], "0x", 2 )) { tree( (HWND)(ULONG_PTR)strtoull( argv[1], NULL, 16 ), 0 ); return 0; }

    snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0 );
    if (Process32First( snap, &pe )) do
    {
        if (!strstr( pe.szExeFile, argv[1] )) continue;
        te.dwSize = sizeof(te);
        if (Thread32First( snap, &te )) do
        {
            GUITHREADINFO gi = { sizeof(gi) };
            HWND h;
            if (te.th32OwnerProcessID != pe.th32ProcessID || !GetGUIThreadInfo( te.th32ThreadID, &gi )) continue;
            if (!gi.hwndFocus && !gi.hwndActive) continue;
            printf( "%s pid %04lx tid %04lx: active %p focus %p capture %p flags %lx\n", pe.szExeFile, pe.th32ProcessID,
                    te.th32ThreadID, gi.hwndActive, gi.hwndFocus, gi.hwndCapture, gi.flags );
            for (h = gi.hwndFocus; h && h != GetDesktopWindow(); h = GetAncestor( h, GA_PARENT )) line( h, 1 );
        } while (Thread32Next( snap, &te ));
    } while (Process32Next( snap, &pe ));
    return 0;
}
