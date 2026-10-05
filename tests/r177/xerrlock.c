/* 177: Xlib deadlock between a thread that reads an X error and a thread that holds XLockDisplay and waits for a reply.
 * Plain Xlib, no Wine.  Build: gcc -O2 -o xerrlock xerrlock.c -lX11 -lpthread
 *
 *   xerrlock hang SECS [plain|ext|temp|tnofl|tlate|perm]
 *       reader thread: a request that fails (BadDrawable, no reply) + XSync, in a loop;
 *       holder thread: XLockDisplay + XSync + XUnlockDisplay, in a loop.
 *       plain: nothing else (deadlocks within milliseconds: the reader's _XError waits for the user lock,
 *              the holder waits for the reader to leave _XReply);
 *       ext:   an extension error hook (XAddExtension + XESetError) consumes the errors that _XReply reads, before
 *              _XError is called (what winex11 does since 177; no cost per request);
 *       temp:  the holder puts an async handler that consumes errors on the display before it takes the lock
 *              (tried first for 177); tnofl: the same without the flush before;
 *       tlate: the handler is added after XLockDisplay (still deadlocks: the reader can read an error in between);
 *       perm:  the same handler stays on the display all the time.
 *       Prints HANG when no thread makes progress for 3 s.
 *   xerrlock bench SECS [plain|ext|lock|temp|tnofl|perm] [SYNC_EVERY]
 *       one thread, XChangeGC + XDrawPoint pairs on a pixmap, XSync every SYNC_EVERY pairs (default 16000):
 *       ns per request; lock: XSync inside XLockDisplay; temp: and with the async handler around the XSync
 *       (tnofl: the requests buffered before are sent with the handler in place, a record each);
 *       perm: with a permanent async handler (Xlib then keeps a record per request).
 */
#define _GNU_SOURCE
#include <X11/Xlibint.h>
#include <X11/Xlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static Display *dpy;
static volatile int stop;
static volatile long nread, nheld, nerr, nasync, nhook;
static int temp, lock;

static double now(void)
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

static int on_error( Display *d, XErrorEvent *e ) { __sync_fetch_and_add( &nerr, 1 ); return 0; }

/* called by Xlib in the thread that reads the error, with the display locked, before it takes the user lock */
static Bool async_error( Display *d, xReply *rep, char *buf, int len, XPointer data )
{
    if (rep->generic.type != X_Error) return False;
    __sync_fetch_and_add( &nasync, 1 );
    return True;
}

static _XAsyncHandler async = { NULL, async_error, NULL };

/* extension error hook (XESetError): called by _XReply in the thread that reads the error, with the display
 * locked, before _XError takes the user lock */
static int ext_error( Display *d, xError *err, XExtCodes *codes, int *ret_code )
{
    __sync_fetch_and_add( &nhook, 1 );
    *ret_code = 0;
    return 1;
}

static void *reader_thread( void *arg )
{
    GC gc = XCreateGC( dpy, DefaultRootWindow( dpy ), 0, NULL );
    while (!stop)
    {
        XDrawPoint( dpy, 0x7f000001, gc, 0, 0 );  /* BadDrawable, read together with the reply below */
        XSync( dpy, False );
        __sync_fetch_and_add( &nread, 1 );
    }
    return NULL;
}

static void *holder_thread( void *arg )
{
    while (!stop)
    {
        if (temp == 3) XLockDisplay( dpy );  /* too early: an error read before the handler is in place waits for the lock */
        if (temp)
        {
            LockDisplay( dpy );
            if (temp > 1) _XFlush( dpy );
            async.next = dpy->async_handlers;
            dpy->async_handlers = &async;
            UnlockDisplay( dpy );
        }
        if (temp != 3) XLockDisplay( dpy );
        XSync( dpy, False );
        if (temp)
        {
            LockDisplay( dpy );
            DeqAsyncHandler( dpy, &async );
            UnlockDisplay( dpy );
        }
        XUnlockDisplay( dpy );
        __sync_fetch_and_add( &nheld, 1 );
        usleep( 50 );
    }
    return NULL;
}

int main( int argc, char **argv )
{
    const char *mode = argc > 3 ? argv[3] : "plain";
    int i, secs, perm;
    pthread_t t[2];
    long last = -1, idle = 0;

    if (argc < 3) return 2;
    secs = atoi( argv[2] );
    perm = !strcmp( mode, "perm" );
    temp = !strcmp( mode, "temp" ) ? 2 : !strcmp( mode, "tlate" ) ? 3 : !strcmp( mode, "tnofl" );
    lock = !strcmp( mode, "lock" );

    XInitThreads();
    if (!(dpy = XOpenDisplay( NULL ))) return 2;
    XSetErrorHandler( on_error );
    if (perm) dpy->async_handlers = &async;
    if (!strcmp( mode, "ext" )) XESetError( dpy, XAddExtension( dpy )->extension, ext_error );

    if (!strcmp( argv[1], "bench" ))
    {
        int every = argc > 4 ? atoi( argv[4] ) : 16000;
        Pixmap pix = XCreatePixmap( dpy, DefaultRootWindow( dpy ), 8, 8, DefaultDepth( dpy, DefaultScreen( dpy ) ) );
        GC gc = XCreateGC( dpy, pix, 0, NULL );
        double start = now(), end = start + secs * 1e9, t, worst = 0;
        long n = 0;
        while ((t = now()) < end)
        {
            for (i = 0; i < every; i++)
            {
                XSetForeground( dpy, gc, i );
                XDrawPoint( dpy, pix, gc, 1, 1 );
            }
            t = now();
            if (temp)
            {
                LockDisplay( dpy );
                if (temp > 1) _XFlush( dpy );
                async.next = dpy->async_handlers;
                dpy->async_handlers = &async;
                UnlockDisplay( dpy );
                XLockDisplay( dpy );
            }
            XSync( dpy, False );
            if (temp)
            {
                LockDisplay( dpy );
                DeqAsyncHandler( dpy, &async );
                UnlockDisplay( dpy );
                XUnlockDisplay( dpy );
            }
            else if (lock) { XLockDisplay( dpy ); XUnlockDisplay( dpy ); }
            t = now() - t;
            if (t > worst) worst = t;
            n += 2 * every + 1;
        }
        printf( "bench %-5s sync every %d pairs: %.1f ns per request, %.2f us per round, slowest XSync %.0f us\n", mode, every,
                (now() - start) / n, (now() - start) / n * (2 * every + 1) / 1e3, worst / 1e3 );
        return 0;
    }

    pthread_create( &t[0], NULL, reader_thread, NULL );
    pthread_create( &t[1], NULL, holder_thread, NULL );
    for (i = 0; i < secs * 2 || idle; i++)
    {
        long cur;
        usleep( 500000 );
        cur = nread + nheld;
        if (cur == last)
        {
            if (++idle >= 6)
            {
                printf( "HANG after %.1f s: reader %ld syncs, holder %ld locked syncs, %ld errors in the handler, %ld in the async handler, %ld in the extension hook, mode %s\n",
                        i / 2.0 - 3, nread, nheld, nerr, nasync, nhook, mode );
                fflush( stdout );
                if (getenv( "XERRLOCK_PAUSE" )) pause();
                _exit( 3 );
            }
        }
        else idle = 0;
        last = cur;
    }
    stop = 1;
    pthread_join( t[0], NULL );
    pthread_join( t[1], NULL );
    printf( "DONE reader %ld syncs, holder %ld locked syncs, %ld errors in the handler, %ld in the async handler, %ld in the extension hook, mode %s\n",
            nread, nheld, nerr, nasync, nhook, mode );
    return 0;
}
