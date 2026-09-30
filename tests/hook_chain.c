/* Hook chain behaviour when hooks are added/removed while the chain runs, and the cost of
 * a hooked call (081). Thread WH_MSGFILTER hooks A, B, C (A runs first), CallMsgFilter
 * calls the chain synchronously. Prints the call order per case; exit code = failures.
 * Usage: hook_chain.exe [ITERS]   (benchmark iterations, default 100000)
 * Build: x86_64-w64-mingw32-gcc -O2 -o hook_chain.exe hook_chain.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HHOOK hooks[4];   /* A, B, C, D */
static char order[64];
static int failures;
static enum { PLAIN, UNHOOK_SELF, UNHOOK_NEXT, UNHOOK_SELF_NEXT, ADD_HOOK, OTHER_THREAD, OTHER_THREAD_ADD } mode;
static HANDLE go, done;

static LRESULT CALLBACK hook_proc( int idx, int code, WPARAM wp, LPARAM lp )
{
    size_t len = strlen( order );
    order[len] = 'A' + idx;
    order[len + 1] = 0;
    if (idx == 0)
    {
        switch (mode)
        {
        case UNHOOK_SELF: UnhookWindowsHookEx( hooks[0] ); break;
        case UNHOOK_NEXT: UnhookWindowsHookEx( hooks[1] ); break;
        case UNHOOK_SELF_NEXT: UnhookWindowsHookEx( hooks[0] ); UnhookWindowsHookEx( hooks[1] ); break;
        case ADD_HOOK: break;  /* handled by the caller, see below */
        case OTHER_THREAD:
        case OTHER_THREAD_ADD:
            SetEvent( go );
            WaitForSingleObject( done, INFINITE );
            break;
        default: break;
        }
    }
    return CallNextHookEx( 0, code, wp, lp );
}

static LRESULT CALLBACK hook_a( int code, WPARAM wp, LPARAM lp ) { return hook_proc( 0, code, wp, lp ); }
static LRESULT CALLBACK hook_b( int code, WPARAM wp, LPARAM lp ) { return hook_proc( 1, code, wp, lp ); }
static LRESULT CALLBACK hook_c( int code, WPARAM wp, LPARAM lp ) { return hook_proc( 2, code, wp, lp ); }
static LRESULT CALLBACK hook_d( int code, WPARAM wp, LPARAM lp ) { return hook_proc( 3, code, wp, lp ); }
static LRESULT CALLBACK hook_a_add( int code, WPARAM wp, LPARAM lp )
{
    strcat( order, "A" );
    if (!hooks[3]) hooks[3] = SetWindowsHookExA( WH_MSGFILTER, hook_d, 0, GetCurrentThreadId() );
    return CallNextHookEx( 0, code, wp, lp );
}

static DWORD main_tid;
static DWORD WINAPI other_thread( void *arg )
{
    WaitForSingleObject( go, INFINITE );
    if (mode == OTHER_THREAD)
    {
        BOOL ret = UnhookWindowsHookEx( hooks[1] );
        printf( "  other thread UnhookWindowsHookEx(B): %d err %lu\n", ret, ret ? 0 : GetLastError() );
    }
    else
    {
        /* a thread hook for the main thread installed from this thread: goes first, not after A */
        hooks[3] = SetWindowsHookExA( WH_MSGFILTER, hook_d, GetModuleHandleA( 0 ), main_tid );
        printf( "  other thread SetWindowsHookEx(D for main thread): %p err %lu\n", hooks[3], hooks[3] ? 0 : GetLastError() );
    }
    SetEvent( done );
    return 0;
}

static void setup(void)
{
    int i;
    for (i = 0; i < 4; i++) if (hooks[i]) UnhookWindowsHookEx( hooks[i] );
    memset( hooks, 0, sizeof(hooks) );
    hooks[2] = SetWindowsHookExA( WH_MSGFILTER, hook_c, 0, GetCurrentThreadId() );
    hooks[1] = SetWindowsHookExA( WH_MSGFILTER, hook_b, 0, GetCurrentThreadId() );
    hooks[0] = SetWindowsHookExA( WH_MSGFILTER, hook_a, 0, GetCurrentThreadId() );
}

static void run( const char *name, const char *expect1, const char *expect2 )
{
    MSG msg = {0};
    char first[64];
    int ok;

    order[0] = 0;
    CallMsgFilterA( &msg, 0x1234 );
    strcpy( first, order );
    order[0] = 0;
    mode = PLAIN;
    CallMsgFilterA( &msg, 0x1234 );
    ok = !strcmp( first, expect1 ) && !strcmp( order, expect2 );
    if (!ok) failures++;
    printf( "%-40s %-6s then %-6s %s (expected %s, %s)\n", name, first, order, ok ? "ok" : "FAIL", expect1, expect2 );
}

int main( int argc, char **argv )
{
    int iters = argc > 1 ? atoi( argv[1] ) : 100000, i;
    LARGE_INTEGER f, t0, t1;
    MSG msg = {0};
    HANDLE thread;

    main_tid = GetCurrentThreadId();
    go = CreateEventA( NULL, FALSE, FALSE, NULL );
    done = CreateEventA( NULL, FALSE, FALSE, NULL );

    setup(); mode = PLAIN; run( "plain", "ABC", "ABC" );
    setup(); mode = UNHOOK_SELF; run( "A unhooks itself", "ABC", "BC" );
    setup(); mode = UNHOOK_NEXT; run( "A unhooks B", "AC", "AC" );
    setup(); mode = UNHOOK_SELF_NEXT; run( "A unhooks itself and B", "AC", "C" );
    setup();
    UnhookWindowsHookEx( hooks[0] );
    hooks[0] = SetWindowsHookExA( WH_MSGFILTER, hook_a_add, 0, GetCurrentThreadId() );
    mode = PLAIN; run( "A adds D", "ABC", "DABC" );
    setup(); mode = OTHER_THREAD;
    thread = CreateThread( NULL, 0, other_thread, NULL, 0, NULL );
    run( "other thread unhooks B while A runs", "AC", "AC" );
    WaitForSingleObject( thread, INFINITE );
    setup(); mode = OTHER_THREAD_ADD;
    thread = CreateThread( NULL, 0, other_thread, NULL, 0, NULL );
    run( "other thread adds D while A runs", "ABC", "DABC" );
    WaitForSingleObject( thread, INFINITE );

    setup(); mode = PLAIN;
    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &t0 );
    for (i = 0; i < iters; i++) { order[0] = 0; CallMsgFilterA( &msg, 0x1234 ); }
    QueryPerformanceCounter( &t1 );
    printf( "CallMsgFilter, 3 hooks: %.0f ns/call\n", (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / iters );

    PostThreadMessageA( GetCurrentThreadId(), WM_USER, 0, 0 );
    for (i = 0; i < 4; i++) if (hooks[i]) UnhookWindowsHookEx( hooks[i] );
    hooks[0] = SetWindowsHookExA( WH_GETMESSAGE, hook_a, 0, GetCurrentThreadId() );
    hooks[1] = SetWindowsHookExA( WH_GETMESSAGE, hook_b, 0, GetCurrentThreadId() );
    hooks[2] = SetWindowsHookExA( WH_GETMESSAGE, hook_c, 0, GetCurrentThreadId() );
    hooks[3] = 0;
    QueryPerformanceCounter( &t0 );
    for (i = 0; i < iters; i++) { order[0] = 0; PeekMessageA( &msg, 0, 0, 0, PM_NOREMOVE ); }
    QueryPerformanceCounter( &t1 );
    printf( "PeekMessage(PM_NOREMOVE) with a message queued, 3 WH_GETMESSAGE hooks: %.0f ns/call (order %s)\n",
            (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / iters, order );
    for (i = 0; i < 3; i++) UnhookWindowsHookEx( hooks[i] );
    QueryPerformanceCounter( &t0 );
    for (i = 0; i < iters; i++) PeekMessageA( &msg, 0, 0, 0, PM_NOREMOVE );
    QueryPerformanceCounter( &t1 );
    printf( "PeekMessage(PM_NOREMOVE) with a message queued, no hooks: %.0f ns/call\n",
            (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / iters );
    printf( "%d failures\n", failures );
    return failures;
}
