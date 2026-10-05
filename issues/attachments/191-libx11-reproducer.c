/* xfilter.c: libX11 event filter list vs. threads (issue 191).
 *
 * One Display, XInitThreads(). The main thread calls XFilterEvent() the way an event loop does for every
 * event it reads; a second thread moves the input focus between input contexts of the same display
 * (XSetICFocus / XUnsetICFocus), which registers / unregisters a key filter in Display->im_filters.
 * _XUnregisterFilter() (src/RegstFlt.c) unlinks and frees list nodes without LockDisplay(), XFilterEvent()
 * walks the list inside LockDisplay(): it reads a freed node.
 *
 *   gcc -O2 -o xfilter xfilter.c -lX11 -lpthread
 *   XMODIFIERS=@im=none ./xfilter [SECONDS] [destroy|match]
 *
 * (XMODIFIERS=@im=none: Xlib's built-in input method, no IM server needed.)
 * default: the contexts stay alive, only their focus changes: nothing but the list is shared.
 * destroy: the second thread also destroys and recreates the contexts (what Wine's X11 driver did).
 * match:   the filtered events are KeyPress events of the focus windows, so the filters get called
 *          (XFilterEvent() reads the node again after UnlockDisplay()).
 * Output: "ok" or a crash (SIGSEGV in XFilterEvent / "free(): ..." abort from glibc).
 */
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>

#define NIM 8

static Display *dpy;
static XIM im[NIM];
static XIC ic[NIM];
static Window win[NIM];
static volatile int stop;
static volatile unsigned long filtered, toggles;
static int do_destroy, do_match;

static XIC create_ic( int i )
{
    return XCreateIC( im[i], XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                      XNClientWindow, win[i], XNFocusWindow, win[i], NULL );
}

static void *focus_thread( void *arg )
{
    int i;

    while (!stop)
    {
        for (i = 0; i < NIM; i++)
        {
            XSetICFocus( ic[i] );    /* _XRegisterFilterByType: takes the display lock */
            toggles++;
        }
        for (i = 0; i < NIM; i++)
        {
            XUnsetICFocus( ic[i] );  /* _XUnregisterFilter: no lock */
            if (do_destroy)
            {
                XDestroyIC( ic[i] );
                ic[i] = create_ic( i );
            }
        }
    }
    return NULL;
}

static void segv( int sig )
{
    static const char msg[] = "CRASH: SIGSEGV\n";
    if (write( 1, msg, sizeof(msg) - 1 )) {}
    _exit( 1 );
}

int main( int argc, char **argv )
{
    int i, secs = argc > 1 ? atoi( argv[1] ) : 10;
    pthread_t thread;
    time_t end;
    XEvent ev;

    for (i = 2; i < argc; i++)
    {
        if (!strcmp( argv[i], "destroy" )) do_destroy = 1;
        if (!strcmp( argv[i], "match" )) do_match = 1;
    }
    setlocale( LC_ALL, "" );
    XSetLocaleModifiers( "" );
    XInitThreads();
    if (!(dpy = XOpenDisplay( NULL ))) { printf( "no display\n" ); return 2; }

    /* Xlib's local input method has one focused context per XIM: several XIMs give a longer filter list */
    for (i = 0; i < NIM; i++)
    {
        win[i] = XCreateSimpleWindow( dpy, DefaultRootWindow( dpy ), 0, 0, 10, 10, 0, 0, 0 );
        if (!(im[i] = XOpenIM( dpy, NULL, NULL, NULL ))) { printf( "no input method\n" ); return 2; }
        if (!(ic[i] = create_ic( i ))) { printf( "no input context\n" ); return 2; }
    }
    XSync( dpy, False );
    signal( SIGSEGV, segv );

    memset( &ev, 0, sizeof(ev) );
    ev.xkey.type = KeyPress;
    ev.xkey.display = dpy;
    ev.xkey.window = do_match ? win[0] : DefaultRootWindow( dpy );
    ev.xkey.keycode = 38;

    pthread_create( &thread, NULL, focus_thread, NULL );
    end = time( NULL ) + secs;
    while (time( NULL ) < end)
    {
        for (i = 0; i < 100000; i++)
        {
            if (do_match) ev.xkey.window = win[i % NIM];
            XFilterEvent( &ev, None );
            filtered++;
        }
    }
    stop = 1;
    pthread_join( thread, NULL );
    printf( "ok: %lu XFilterEvent calls, %lu focus changes\n", filtered, toggles );
    return 0;
}
