/* xthreads.c: what each UI thread of Wine's X11 driver does with Xlib's input method / locale code, from N
 * threads at once, each on its own Display (issues 190, 191). For ThreadSanitizer runs against a libX11 built
 * with -fsanitize=thread (inst/191/x11-build-tsan): every report is a data race inside libX11 between threads
 * that share no Display.
 *   gcc -fsanitize=thread -g -O1 -o xthreads xthreads.c -lX11 -lpthread
 *   XMODIFIERS=@im=none [XTHREADS_PHASES=1] ./xthreads [THREADS] [ROUNDS] [lock]
 * lock: XCreateFontSet / XOpenIM / XCloseIM / XFreeFontSet under one mutex (what winex11 does since 190).
 */
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

static pthread_mutex_t im_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t barrier, barrier2;
static int do_lock, phases;

static void *thread( void *arg )
{
    Display *dpy;
    XFontSet font_set;
    XIM im;
    XIC ic;
    Window win;
    XTextProperty prop;
    XClassHint *hint;
    XEvent ev;
    KeySym keysym;
    Status status;
    char buf[32], *title = (char *)"window title", **list;
    int i, count;

    pthread_barrier_wait( &barrier );
    if (!(dpy = XOpenDisplay( NULL ))) { printf( "no display\n" ); exit( 2 ); }
    XkbUseExtension( dpy, NULL, NULL );

    if (do_lock) pthread_mutex_lock( &im_mutex );
    font_set = XCreateFontSet( dpy, "fixed", &list, &count, NULL );
    if (list) XFreeStringList( list );
    im = XOpenIM( dpy, NULL, NULL, NULL );
    if (do_lock) pthread_mutex_unlock( &im_mutex );
    if (!im) { printf( "no input method\n" ); exit( 2 ); }
    /* XTHREADS_PHASES: wait until all input methods are open, the rest then runs in all threads at once */
    if (phases) pthread_barrier_wait( &barrier2 );

    win = XCreateSimpleWindow( dpy, DefaultRootWindow( dpy ), 0, 0, 10, 10, 0, 0, 0 );
    if ((hint = XAllocClassHint()))
    {
        hint->res_name = hint->res_class = (char *)"xthreads";
        XSetClassHint( dpy, win, hint );
        XFree( hint );
    }
    XSetWMProperties( dpy, win, NULL, NULL, NULL, 0, NULL, NULL, NULL );
    if (XmbTextListToTextProperty( dpy, &title, 1, XStdICCTextStyle, &prop ) == Success)
    {
        XSetWMName( dpy, win, &prop );
        XFree( prop.value );
    }

    ic = XCreateIC( im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, win, XNFocusWindow, win, NULL );
    if (ic)
    {
        XSetICFocus( ic );
        memset( &ev, 0, sizeof(ev) );
        ev.xkey.type = KeyPress;
        ev.xkey.display = dpy;
        ev.xkey.window = win;
        for (i = 0; i < 20; i++)
        {
            ev.xkey.keycode = 24 + i;
            if (!XFilterEvent( &ev, None )) XmbLookupString( ic, &ev.xkey, buf, sizeof(buf), &keysym, &status );
        }
        XUnsetICFocus( ic );
        XDestroyIC( ic );
    }
    XDestroyWindow( dpy, win );
    XSync( dpy, False );

    if (do_lock) pthread_mutex_lock( &im_mutex );
    XCloseIM( im );
    if (font_set) XFreeFontSet( dpy, font_set );
    if (do_lock) pthread_mutex_unlock( &im_mutex );
    XCloseDisplay( dpy );
    return NULL;
}

int main( int argc, char **argv )
{
    int i, r, n = argc > 1 ? atoi( argv[1] ) : 8, rounds = argc > 2 ? atoi( argv[2] ) : 2;
    pthread_t threads[64];

    if (argc > 3 && !strcmp( argv[3], "lock" )) do_lock = 1;
    phases = getenv( "XTHREADS_PHASES" ) != NULL;
    if (n > 64) n = 64;
    setlocale( LC_ALL, "" );
    XSetLocaleModifiers( "" );
    XInitThreads();
    for (r = 0; r < rounds; r++)
    {
        pthread_barrier_init( &barrier, NULL, n );
        pthread_barrier_init( &barrier2, NULL, n );
        for (i = 0; i < n; i++) pthread_create( &threads[i], NULL, thread, NULL );
        for (i = 0; i < n; i++) pthread_join( threads[i], NULL );
        pthread_barrier_destroy( &barrier );
        pthread_barrier_destroy( &barrier2 );
    }
    printf( "ok\n" );
    return 0;
}
