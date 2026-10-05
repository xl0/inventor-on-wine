/* Xlib deadlock: a thread that reads an X error inside _XReply() waits for the XLockDisplay() lock of a thread
 * that waits for a reply behind it.
 *
 *   gcc -O2 -o xerrlock xerrlock.c -lX11 -lpthread && ./xerrlock
 *
 * Prints "HANG" (libX11 1.8.13 and git master: within milliseconds) or "ok" after 5 seconds.
 */
#include <X11/Xlib.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static Display *dpy;
static volatile long count;

static int on_error( Display *d, XErrorEvent *e ) { return 0; }

/* a request that fails without a reply, then a round trip: XSync() reads the error together with its reply */
static void *reader( void *arg )
{
    GC gc = XCreateGC( dpy, DefaultRootWindow( dpy ), 0, NULL );
    for (;;)
    {
        XDrawPoint( dpy, 0x7f000001, gc, 0, 0 );  /* BadDrawable */
        XSync( dpy, False );
        __sync_fetch_and_add( &count, 1 );
    }
    return NULL;
}

/* a round trip with the display locked */
static void *holder( void *arg )
{
    for (;;)
    {
        XLockDisplay( dpy );
        XSync( dpy, False );
        XUnlockDisplay( dpy );
        __sync_fetch_and_add( &count, 1 );
        usleep( 50 );
    }
    return NULL;
}

int main(void)
{
    pthread_t t;
    long last = -1;
    int i;

    XInitThreads();
    if (!(dpy = XOpenDisplay( NULL ))) return 2;
    XSetErrorHandler( on_error );
    pthread_create( &t, NULL, reader, NULL );
    pthread_create( &t, NULL, holder, NULL );
    for (i = 0; i < 5; i++)
    {
        sleep( 1 );
        if (count == last)
        {
            printf( "HANG after %ld round trips\n", count );
            return 1;
        }
        last = count;
    }
    printf( "ok, %ld round trips\n", count );
    return 0;
}
