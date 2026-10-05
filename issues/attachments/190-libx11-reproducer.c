/* ximopen.c: XOpenIM / XCloseIM from several threads, each on its own Display (issue 190).
 *
 * XInitThreads(); every thread opens its own display connection and then input methods on it, all threads
 * at the same moment, the way a toolkit with one connection per UI thread does when its threads start
 * together. No Display is shared, but libX11 keeps process-wide state for input methods without any lock:
 *   modules/im/ximcp/imInt.c   _XimCurrentIMlist / _XimCurrentIMcount: _XimOpenIM adds the new XIM with
 *                              Xrealloc( list, (count + 1) * sizeof(Xim) ); two threads realloc the same block
 *   modules/im/ximcp/imLcIm.c  the cached compose tree of the local input method and its reference count
 *
 *   gcc -O2 -o ximopen ximopen.c -lX11 -lpthread
 *   XMODIFIERS=@im=none ./ximopen [THREADS] [OPENS] [lock]         (defaults 16 threads, 64 opens each)
 *   for i in $(seq 100); do XMODIFIERS=@im=none ./ximopen; done | sort | uniq -c
 *
 * The list only grows while a process has more input methods open than ever before, so one process is one
 * attempt; XCOMPOSEFILE=/dev/null makes XOpenIM itself short (no compose table to parse) and the overlap likelier.
 * lock: all XOpenIM / XCloseIM calls under one mutex (the workaround).
 * Output: "ok" or "CRASH: abort" (glibc: "double free or corruption", "realloc(): invalid pointer", ...),
 * "CRASH: SIGSEGV", "HANG" (threads waiting for the allocator lock of a thread that died in it).
 */
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>

static pthread_mutex_t im_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t barrier;
static int do_lock, opens = 64;

static void *thread( void *arg )
{
    Display *dpy = XOpenDisplay( NULL );
    XIM *im = calloc( opens, sizeof(*im) );
    int i;

    if (!dpy) { printf( "no display\n" ); exit( 2 ); }
    pthread_barrier_wait( &barrier );
    for (i = 0; i < opens; i++)
    {
        if (do_lock) pthread_mutex_lock( &im_mutex );
        im[i] = XOpenIM( dpy, NULL, NULL, NULL );
        if (do_lock) pthread_mutex_unlock( &im_mutex );
        if (!im[i]) { printf( "no input method\n" ); exit( 2 ); }
    }
    for (i = 0; i < opens; i++)
    {
        if (do_lock) pthread_mutex_lock( &im_mutex );
        XCloseIM( im[i] );
        if (do_lock) pthread_mutex_unlock( &im_mutex );
    }
    XCloseDisplay( dpy );
    return NULL;
}

static void crash( int sig )
{
    static const char segv[] = "CRASH: SIGSEGV\n", abrt[] = "CRASH: abort\n", hang[] = "HANG\n";
    const char *msg = sig == SIGSEGV ? segv : sig == SIGABRT ? abrt : hang;
    if (write( 1, msg, strlen( msg ) )) {}
    _exit( 1 );
}

int main( int argc, char **argv )
{
    int i, n = argc > 1 ? atoi( argv[1] ) : 16;
    pthread_t threads[256];

    if (argc > 2) opens = atoi( argv[2] );
    if (argc > 3 && !strcmp( argv[3], "lock" )) do_lock = 1;
    if (n > 256) n = 256;
    setlocale( LC_ALL, "" );
    XSetLocaleModifiers( "" );
    XInitThreads();
    signal( SIGSEGV, crash );
    signal( SIGABRT, crash );
    signal( SIGALRM, crash );
    alarm( 60 );

    pthread_barrier_init( &barrier, NULL, n );
    for (i = 0; i < n; i++) pthread_create( &threads[i], NULL, thread, NULL );
    for (i = 0; i < n; i++) pthread_join( threads[i], NULL );
    printf( "ok\n" );
    return 0;
}
