/* guardmalloc.c: LD_PRELOAD allocator that turns heap overflows and uses after free in the Unix libraries of a
 * Wine process into an immediate fault with a backtrace (glibc only reports the damage later, somewhere else).
 * Every block gets its own pages from one big reserved range: [header page][data, right-aligned][guard page];
 * free() makes the data inaccessible and nothing is ever reused. A SIGSEGV inside the range prints the fault
 * address, the block (live / freed, size), a backtrace of the faulting thread and where the block was allocated
 * and freed, then stops the process (SIGSTOP: attach gdb, or kill it). Other faults go on to the handler the
 * program installed (Wine's).
 *
 *   gcc -O2 -shared -fPIC -o guardmalloc.so guardmalloc.c -ldl
 *   LD_PRELOAD=$PWD/guardmalloc.so [GUARDMALLOC_ONLY=app.exe] wine app.exe     (ONLY: not in the other processes
 *   of the prefix; explorer with its GL libraries takes minutes under it)
 * Costs 2-3 mappings per block (vm.max_map_count around a million) and a slower start.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE 4096UL
#define ARENA_SIZE (256UL << 30)
#define MAGIC 0x6d61726475616721ULL
#define FREED 0x6465657266212121ULL
#define NBT 14

struct header
{
    uint64_t magic;
    size_t   size;      /* requested size */
    size_t   pages;     /* header + data + guard */
    void    *data;
    void    *alloc_bt[NBT];
    void    *free_bt[NBT];
};

static char *arena;
static uint32_t *shadow;  /* per arena page: pages back to the block's header page, + 1 */
static volatile size_t arena_used;
static struct sigaction next_segv;
static int (*real_sigaction)( int, const struct sigaction *, struct sigaction * );
static __thread int nested;

extern void *__libc_malloc( size_t );
extern void *__libc_calloc( size_t, size_t );
extern void __libc_free( void * );
extern void *__libc_realloc( void *, size_t );
extern void *__libc_memalign( size_t, size_t );

static int is_ours( void *ptr ) { return arena && (char *)ptr >= arena && (char *)ptr < arena + ARENA_SIZE; }
static struct header *get_header( void *ptr ) { return (struct header *)(((uintptr_t)ptr & ~(PAGE - 1)) - PAGE); }

__attribute__((constructor)) static void init(void)
{
    void *bt[4];
    const char *only = getenv( "GUARDMALLOC_ONLY" );
    static int done;
    if (arena || done) return;
    done = 1;
    nested++;
    real_sigaction = dlsym( RTLD_NEXT, "sigaction" );
    if (only)  /* only for processes with this string in their command line */
    {
        char cmd[1024] = {0};
        int i, n = 0, fd = open( "/proc/self/cmdline", O_RDONLY );
        if (fd >= 0) { n = read( fd, cmd, sizeof(cmd) - 1 ); close( fd ); }
        for (i = 0; i < n; i++) if (!cmd[i]) cmd[i] = ' ';
        if (!strstr( cmd, only )) { nested--; return; }
    }
    backtrace( bt, 4 );  /* loads libgcc now, not inside an allocation */
    shadow = mmap( NULL, ARENA_SIZE / PAGE * sizeof(*shadow), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 );
    arena = mmap( NULL, ARENA_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 );
    if (arena == MAP_FAILED || shadow == MAP_FAILED) arena = NULL;
    nested--;
}

static void *guard_alloc( size_t size )
{
    size_t data_pages = size ? (size + PAGE - 1) / PAGE : 1, pages = data_pages + 2;
    size_t pos = __sync_fetch_and_add( &arena_used, pages * PAGE );
    struct header *h;
    size_t i;

    if (pos + pages * PAGE > ARENA_SIZE) return NULL;
    h = (struct header *)(arena + pos);
    for (i = 0; i < pages; i++) shadow[pos / PAGE + i] = i + 1;
    if (mprotect( h, (pages - 1) * PAGE, PROT_READ | PROT_WRITE )) return NULL;
    h->magic = MAGIC;
    h->size = size;
    h->pages = pages;
    h->data = (void *)(((uintptr_t)h + (pages - 1) * PAGE - (size ? size : 1)) & ~15UL);
    if (!nested) { nested++; backtrace( h->alloc_bt, NBT ); nested--; }
    return h->data;
}

static void report_bt( const char *what, void **bt );

static void bad_free( void *ptr )
{
    uint32_t i = shadow[((uintptr_t)ptr - (uintptr_t)arena) / PAGE];
    struct header *h = i ? (struct header *)(((uintptr_t)ptr & ~(PAGE - 1)) - (i - 1) * PAGE) : NULL;
    char buf[256];
    void *bt[40];

    nested++;
    if (write( 2, buf, snprintf( buf, sizeof(buf), "GUARDMALLOC: free(%p): %s block %p size %zu, pid %d tid %d\n", ptr,
                                 !h ? "no" : h->magic == FREED ? "already FREED" : "not the start of live", h ? h->data : NULL,
                                 h ? h->size : 0, getpid(), gettid() ) )) {}
    if (write( 2, "GUARDMALLOC:   freeing thread:\n", 31 )) {}
    backtrace_symbols_fd( bt, backtrace( bt, 40 ), 2 );
    if (h) report_bt( "allocated at", h->alloc_bt );
    if (h && h->magic == FREED) report_bt( "freed at", h->free_bt );
    kill( getpid(), SIGSTOP );
    for (;;) pause();
}

void *malloc( size_t size )
{
    if (!arena || nested) return __libc_malloc( size );
    return guard_alloc( size );
}

void free( void *ptr )
{
    struct header *h;
    if (!ptr) return;
    if (!is_ours( ptr )) { __libc_free( ptr ); return; }
    h = get_header( ptr );
    if (h->magic != MAGIC || h->data != ptr) bad_free( ptr );
    h->magic = FREED;
    if (!nested) { nested++; backtrace( h->free_bt, NBT ); nested--; }
    mprotect( (char *)h + PAGE, (h->pages - 2) * PAGE, PROT_NONE );
}

void *calloc( size_t n, size_t size )
{
    if (!arena || nested) return __libc_calloc( n, size );
    return guard_alloc( n * size );  /* fresh pages are zero */
}

void *realloc( void *ptr, size_t size )
{
    void *ret;
    size_t old;
    if (!ptr) return malloc( size );
    if (!is_ours( ptr )) return __libc_realloc( ptr, size );
    old = get_header( ptr )->size;
    if ((ret = guard_alloc( size ))) { memcpy( ret, ptr, old < size ? old : size ); free( ptr ); }
    return ret;
}

void *memalign( size_t align, size_t size )
{
    if (!arena || nested || align > 16) return __libc_memalign( align, size );
    return guard_alloc( size );
}
void *aligned_alloc( size_t align, size_t size ) { return memalign( align, size ); }
int posix_memalign( void **ret, size_t align, size_t size ) { *ret = memalign( align, size ); return *ret ? 0 : ENOMEM; }
size_t malloc_usable_size( void *ptr )
{
    static size_t (*real)( void * );
    if (!ptr) return 0;
    if (is_ours( ptr )) return get_header( ptr )->size;
    if (!real) real = dlsym( RTLD_NEXT, "malloc_usable_size" );
    return real( ptr );
}

static void report_bt( const char *what, void **bt )
{
    int n = 0;
    char buf[64];
    while (n < NBT && bt[n]) n++;
    if (write( 2, buf, snprintf( buf, sizeof(buf), "GUARDMALLOC:   %s:\n", what ) )) {}
    backtrace_symbols_fd( bt, n, 2 );
}

static void segv_handler( int sig, siginfo_t *info, void *ctx )
{
    uintptr_t addr = (uintptr_t)info->si_addr, page = addr & ~(PAGE - 1);
    struct header *h = NULL;
    char buf[256];
    void *bt[40];
    uint32_t i;

    if (is_ours( (void *)addr ) && (i = shadow[(page - (uintptr_t)arena) / PAGE]))
        h = (struct header *)(page - (i - 1) * PAGE);
    if (!h)
    {
        if (next_segv.sa_flags & SA_SIGINFO) next_segv.sa_sigaction( sig, info, ctx );
        else if (next_segv.sa_handler != SIG_DFL && next_segv.sa_handler != SIG_IGN) next_segv.sa_handler( sig );
        else { signal( SIGSEGV, SIG_DFL ); raise( SIGSEGV ); }
        return;
    }
    nested++;
    if (write( 2, buf, snprintf( buf, sizeof(buf), "GUARDMALLOC: fault at %p: %s block %p size %zu, %s, pid %d tid %d\n",
                                 (void *)addr, h->magic == FREED ? "FREED" : "live", h->data, h->size,
                                 addr >= (uintptr_t)h->data + h->size ? "past its end" :
                                 addr < (uintptr_t)h->data ? "before its start" : "inside", getpid(), gettid() ) )) {}
    if (write( 2, "GUARDMALLOC:   faulting thread:\n", 32 )) {}
    backtrace_symbols_fd( bt, backtrace( bt, 40 ), 2 );
    report_bt( "allocated at", h->alloc_bt );
    if (h->magic == FREED) report_bt( "freed at", h->free_bt );
    kill( getpid(), SIGSTOP );
    for (;;) pause();
}

int sigaction( int sig, const struct sigaction *act, struct sigaction *old )
{
    init();
    if (sig == SIGSEGV && arena)
    {
        struct sigaction mine;
        if (old) *old = next_segv;
        if (!act) return 0;
        next_segv = *act;
        mine = *act;
        mine.sa_sigaction = segv_handler;
        mine.sa_flags |= SA_SIGINFO;
        return real_sigaction( sig, &mine, NULL );
    }
    return real_sigaction( sig, act, old );
}
