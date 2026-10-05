/* xgeclose.c: libXext frees its global XGE (Generic Event extension) record when the last Display that uses it is
 * closed, without a lock, while another thread may be registering its own Display (issue 193).
 * Each thread: XOpenDisplay, XIQueryVersion (libXi registers the display with libXext's XGE code), XCloseDisplay,
 * in a loop. No Display is shared. src/Xge.c: _xgeFindDisplay() reads the static xge_info and passes it to
 * XextFindDisplay() / XextAddDisplay(); _xgeDpyClose() does XextDestroyExtension( xge_info ); xge_info = NULL
 * when the display count reaches 0.
 *   gcc -O2 -o xgeclose xgeclose.c -lX11 -lXi -lpthread
 *   ./xgeclose [THREADS] [SECONDS] [keep]
 * keep: the main thread keeps one registered display open the whole time (the record is then never freed).
 * Output: "ok: N cycles" or "CRASH: SIGSEGV" / "CRASH: abort" (glibc heap check) / "HANG" (Xlib's global lock
 * left locked is not possible here; a hang is threads waiting for the allocator lock of a thread that died).
 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

static volatile int stop;
static unsigned long cycles[64];

static Display *open_xi( void )
{
    int major = 2, minor = 2;
    Display *dpy = XOpenDisplay( NULL );
    if (!dpy) { printf( "no display\n" ); exit( 2 ); }
    XIQueryVersion( dpy, &major, &minor );
    return dpy;
}

static void *thread( void *arg )
{
    unsigned long *count = arg;
    while (!stop)
    {
        XCloseDisplay( open_xi() );
        (*count)++;
    }
    return NULL;
}

static void crash( int sig )
{
    const char *msg = sig == SIGSEGV ? "CRASH: SIGSEGV\n" : sig == SIGABRT ? "CRASH: abort\n" : "HANG\n";
    if (write( 1, msg, strlen( msg ) )) {}
    _exit( 1 );
}

int main( int argc, char **argv )
{
    int i, n = argc > 1 ? atoi( argv[1] ) : 4, secs = argc > 2 ? atoi( argv[2] ) : 5;
    unsigned long total = 0;
    pthread_t threads[64];

    if (n > 64) n = 64;
    XInitThreads();
    signal( SIGSEGV, crash );
    signal( SIGABRT, crash );
    signal( SIGALRM, crash );
    alarm( secs + 20 );
    if (argc > 3 && !strcmp( argv[3], "keep" )) open_xi();
    for (i = 0; i < n; i++) pthread_create( &threads[i], NULL, thread, &cycles[i] );
    sleep( secs );
    stop = 1;
    for (i = 0; i < n; i++) pthread_join( threads[i], NULL );
    for (i = 0; i < n; i++) total += cycles[i];
    printf( "ok: %lu cycles\n", total );
    return 0;
}
