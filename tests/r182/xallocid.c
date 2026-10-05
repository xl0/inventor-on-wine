/* libX11 XID allocation race without Wine (issue 182): N threads create and free GCs on one Display.
 * Build: gcc -O2 -o xallocid xallocid.c -lX11 -lpthread     Run: DISPLAY=:N ./xallocid [THREADS] [SECS] [lock]   (lock: XLockDisplay around XCreateGC)
 * libX11 1.8.13 aborts with "_XAllocID: Assertion `ret != inval_id' failed"; prints DONE otherwise. */
#include <X11/Xlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static Display *dpy;
static volatile int stop, use_lock;
static volatile long total;

static void *thread( void *arg )
{
    while (!stop)
    {
        GC gc;
        if (use_lock) XLockDisplay( dpy );
        gc = XCreateGC( dpy, DefaultRootWindow( dpy ), 0, NULL );
        if (use_lock) XUnlockDisplay( dpy );
        XFreeGC( dpy, gc );
        __sync_fetch_and_add( &total, 1 );
    }
    return NULL;
}

int main( int argc, char **argv )
{
    int i, n = argc > 1 ? atoi( argv[1] ) : 4, secs = argc > 2 ? atoi( argv[2] ) : 10;
    pthread_t t[64];

    use_lock = argc > 3;
    XInitThreads();
    if (!(dpy = XOpenDisplay( NULL ))) return 2;
    for (i = 0; i < n; i++) pthread_create( &t[i], NULL, thread, NULL );
    sleep( secs );
    stop = 1;
    for (i = 0; i < n; i++) pthread_join( t[i], NULL );
    printf( "DONE %ld GCs, %d threads\n", total, n );
    return 0;
}
