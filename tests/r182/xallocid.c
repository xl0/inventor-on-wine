/* libX11 XID allocation race without Wine (issue 182): N threads create and free GCs on one Display.
 * Build: gcc -O2 -o xallocid xallocid.c -lX11 -lpthread
 * Run:   DISPLAY=:N ./xallocid [THREADS] [SECS] [MODE] [NOISE]
 *   MODE  none     plain XCreateGC in every thread (default)
 *         lock     XLockDisplay / XUnlockDisplay around XCreateGC in every thread
 *         mutex    a pthread mutex around XCreateGC in every thread
 *         mixlock  thread 0 plain, the others as "lock"
 *         mix2lock threads 0 and 1 plain, the others as "lock"
 *         mixmutex thread 0 plain, the others as "mutex"
 *   NOISE threads that send XNoOperation requests and an XSync every NOISE_PERIOD (environment, default 1000)
 *         requests (they allocate nothing)
 * libX11 1.8.13 aborts with "_XAllocID: Assertion `ret != inval_id' failed"; prints DONE otherwise. */
#include <X11/Xlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;
static volatile int stop;
static volatile long total;
static const char *mode = "none";
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *thread( void *arg )
{
    int plain = !strcmp( mode, "none" ) || (!strncmp( mode, "mix", 3 ) && (long)arg < (mode[3] == '2' ? 2 : 1));
    int use_lock = !plain && strstr( mode, "lock" ), use_mutex = !plain && strstr( mode, "mutex" );

    while (!stop)
    {
        GC gc;
        if (use_lock) XLockDisplay( dpy );
        if (use_mutex) pthread_mutex_lock( &mutex );
        gc = XCreateGC( dpy, DefaultRootWindow( dpy ), 0, NULL );
        if (use_mutex) pthread_mutex_unlock( &mutex );
        if (use_lock) XUnlockDisplay( dpy );
        XFreeGC( dpy, gc );
        __sync_fetch_and_add( &total, 1 );
    }
    return NULL;
}

static void *noise( void *arg )
{
    int i, period = getenv( "NOISE_PERIOD" ) ? atoi( getenv( "NOISE_PERIOD" ) ) : 1000;
    while (!stop)
    {
        for (i = 0; i < period && !stop; i++) XNoOp( dpy );
        XSync( dpy, False );
    }
    return NULL;
}

int main( int argc, char **argv )
{
    int i, n = argc > 1 ? atoi( argv[1] ) : 4, secs = argc > 2 ? atoi( argv[2] ) : 10, nn = argc > 4 ? atoi( argv[4] ) : 0;
    pthread_t t[128];

    if (argc > 3) mode = argv[3];
    XInitThreads();
    if (!(dpy = XOpenDisplay( NULL ))) return 2;
    for (i = 0; i < n; i++) pthread_create( &t[i], NULL, thread, (void *)(long)i );
    for (i = 0; i < nn; i++) pthread_create( &t[n + i], NULL, noise, NULL );
    sleep( secs );
    stop = 1;
    for (i = 0; i < n + nn; i++) pthread_join( t[i], NULL );
    printf( "DONE %ld GCs, %d threads, mode %s, noise %d\n", total, n, mode, nn );
    return 0;
}
