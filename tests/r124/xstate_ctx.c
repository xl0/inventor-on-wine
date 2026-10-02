/* 124: what extended context do exception handlers / APCs get, and how do the xstate helpers
 * validate their arguments.
 * Build: x86_64-w64-mingw32-gcc -O1 -o xstate_ctx.exe xstate_ctx.c -lntdll
 * Modes: (none) = all safe ones; locbad = RtlLocateExtendedFeature* on broken CONTEXT_EX (may crash).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>

#ifndef CONTEXT_XSTATE
#define CONTEXT_XSTATE (CONTEXT_AMD64 | 0x40)
#endif

typedef struct { ULONG Offset; ULONG Size; } XFEAT;
typedef struct
{
    ULONG64 EnabledFeatures;
    ULONG64 EnabledVolatileFeatures;
    ULONG Size;
    ULONG ControlFlags;
    XFEAT Features[64];
    ULONG64 EnabledSupervisorFeatures;
    ULONG64 AlignedFeatures;
    ULONG AllFeatureSize;
    ULONG AllFeatures[64];
    ULONG64 EnabledUserVisibleSupervisorFeatures;
    ULONG64 ExtendedFeatureDisableFeatures;
    ULONG AllNonLargeFeatureSize;
    ULONG Spare;
} XCFG;
#define XCFGP ((XCFG *)(0x7ffe0000 + 0x3d8))

typedef struct { LONG Offset; ULONG Length; } CHUNK;
typedef struct { CHUNK All, Legacy, XState; } CTXEX;
typedef struct { ULONG64 Mask, CompactionMask, Reserved2[6]; } XHDR;

extern void *WINAPI RtlLocateExtendedFeature( CTXEX *ex, ULONG id, ULONG *len );
extern void *WINAPI RtlLocateExtendedFeature2( CTXEX *ex, ULONG id, XCFG *cfg, ULONG *len );
extern ULONG64 WINAPI RtlGetEnabledExtendedFeatures( ULONG64 mask );
extern NTSTATUS WINAPI RtlGetExtendedContextLength2( ULONG flags, ULONG *len, ULONG64 mask );
extern NTSTATUS WINAPI RtlInitializeExtendedContext2( void *ctx, ULONG flags, CTXEX **ex, ULONG64 mask );
extern ULONG64 WINAPI RtlGetExtendedFeaturesMask( CTXEX *ex );
static BOOL (WINAPI *pQueueUserAPC2)( PAPCFUNC, HANDLE, ULONG_PTR, ULONG );
static BOOL (WINAPI *pInitializeContext2)( void *, DWORD, CONTEXT **, DWORD *, ULONG64 );
extern NTSTATUS WINAPI NtRaiseException( EXCEPTION_RECORD *, CONTEXT *, BOOL );
extern NTSTATUS WINAPI NtContinue( CONTEXT *, BOOLEAN );

static const char *tag = "";

static int readable( const void *p, SIZE_T len )
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery( p, &mbi, sizeof(mbi) )) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (char *)p + len <= (char *)mbi.BaseAddress + mbi.RegionSize;
}

static void dump_context( const char *what, CONTEXT *ctx )
{
    CTXEX *ex = (CTXEX *)(ctx + 1);
    ULONG_PTR sp = (ULONG_PTR)&sp;
    unsigned int i;

    printf( "%s%s: ctx %%64=%u flags %#lx (xstate %d) ctx-sp %#lx\n", tag, what,
            (unsigned)((ULONG_PTR)ctx & 63), ctx->ContextFlags,
            (ctx->ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE, (long)((ULONG_PTR)ctx - sp) );
    if (!readable( ex, sizeof(*ex) )) { printf( "  context_ex unreadable\n" ); return; }
    printf( "  ex: All %ld/%#lx Legacy %ld/%#lx XState %ld/%#lx\n", ex->All.Offset, ex->All.Length,
            ex->Legacy.Offset, ex->Legacy.Length, ex->XState.Offset, ex->XState.Length );
    if ((ctx->ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE)
    {
        XHDR *xs = (XHDR *)((char *)ex + ex->XState.Offset);
        if (!readable( xs, sizeof(*xs) )) { printf( "  XSTATE HEADER UNREADABLE %p\n", xs ); return; }
        printf( "  xs %%64=%u Mask %#I64x Compaction %#I64x\n", (unsigned)((ULONG_PTR)xs & 63), xs->Mask, xs->CompactionMask );
        for (i = 0; i < 20; i++)
        {
            DWORD len = 0xdead;
            void *p = LocateXStateFeature( ctx, i, &len );
            if (p || (i >= 2 && (XCFGP->EnabledFeatures >> i & 1)) || i == 11)
                printf( "  feature %u: %s off(xs) %ld len %#lx\n", i, p ? "ok" : "NULL",
                        p ? (long)((char *)p - (char *)xs) : 0, len );
        }
        {
            DWORD64 m = 0xdeadbeef;
            BOOL r = GetXStateFeaturesMask( ctx, &m );
            printf( "  GetXStateFeaturesMask %d %#I64x\n", r, m );
        }
    }
    else
    {
        DWORD len = 0xdead;
        void *p = LocateXStateFeature( ctx, 2, &len );
        void *q = LocateXStateFeature( ctx, 11, &len );
        printf( "  no xstate flag: Locate(2) %p Locate(11) %p\n", p, q );
    }
}

/* ---- exceptions ---- */
static int veh_calls, veh_nested;
static void (*barrier)( void *dst, void *val );

static LONG CALLBACK veh( EXCEPTION_POINTERS *ep )
{
    EXCEPTION_RECORD *rec = ep->ExceptionRecord;
    CONTEXT *ctx = ep->ContextRecord;
    char buf[64];

    if (rec->ExceptionCode == 0x40010006 /* DBG_PRINTEXCEPTION_C */) return EXCEPTION_CONTINUE_SEARCH;
    veh_calls++;
    sprintf( buf, "veh code %#lx rec %%16=%u", rec->ExceptionCode, (unsigned)((ULONG_PTR)rec & 15) );
    dump_context( buf, ctx );
    printf( "  rec-ctx %ld\n", (long)((ULONG_PTR)rec - (ULONG_PTR)ctx) );

    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        if (veh_nested == 1)
        {
            /* fault inside the handler: nested dispatch */
            veh_nested = 2;
            tag = "  nested ";
            barrier( (void *)8, 0 );
            tag = "";
        }
        /* like CoreCLR's VirtualUnwindLeafCallFrame: return to the caller */
        ctx->Rip = *(ULONG64 *)ctx->Rsp;
        ctx->Rsp += 8;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (rec->ExceptionCode == EXCEPTION_BREAKPOINT) { ctx->Rip++; return EXCEPTION_CONTINUE_EXECUTION; }
    if (rec->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION) { ctx->Rip += 2; return EXCEPTION_CONTINUE_EXECUTION; }
    if (rec->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO)
    {
        ctx->Rip = *(ULONG64 *)ctx->Rsp;
        ctx->Rsp += 8;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (rec->ExceptionCode == 0xe0000124 || rec->ExceptionCode == 0xe0000125) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static void dirty_avx(void)
{
    ULONG64 f = GetEnabledXStateFeatures();
    if (f & 4) __asm__ volatile ( "vpcmpeqd %ymm1,%ymm1,%ymm1" );
    if (f & 0xe0) __asm__ volatile ( ".byte 0x62,0xf1,0x75,0x48,0x76,0xc9" /* vpcmpeqd %zmm1,%zmm1,%k1 */ );
}
static void clean_avx(void)
{
    if (GetEnabledXStateFeatures() & 4) __asm__ volatile ( "vzeroall" );
}

static void *make_code( const unsigned char *code, unsigned int len )
{
    void *p = VirtualAlloc( NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
    memcpy( (char *)p + 0x10, code, len );
    return (char *)p + 0x10;
}

static void test_exceptions(void)
{
    static const unsigned char barrier_code[] = { 0x48, 0x89, 0x11, 0xc3 }; /* mov %rdx,(%rcx); ret */
    static const unsigned char div_code[] = { 0x31, 0xc9, 0xf7, 0xf1, 0xc3 }; /* xor %ecx,%ecx; div %ecx; ret */
    static const unsigned char bp_code[] = { 0xcc, 0xc3 };
    static const unsigned char ud_code[] = { 0x0f, 0x0b, 0xc3 };
    void (*f)(void);
    void *h = AddVectoredExceptionHandler( 1, veh );

    barrier = make_code( barrier_code, sizeof(barrier_code) );

    printf( "--- AV, clean avx\n" );
    clean_avx();
    barrier( (void *)8, 0 );
    printf( "--- AV, dirty avx\n" );
    dirty_avx();
    barrier( (void *)8, 0 );
    printf( "--- AV, nested\n" );
    veh_nested = 1;
    barrier( (void *)8, 0 );
    veh_nested = 0;
    printf( "--- div0\n" );
    f = make_code( div_code, sizeof(div_code) ); f();
    printf( "--- breakpoint\n" );
    f = make_code( bp_code, sizeof(bp_code) ); f();
    printf( "--- illegal\n" );
    f = make_code( ud_code, sizeof(ud_code) ); f();
    printf( "--- RaiseException\n" );
    RaiseException( 0xe0000124, 0, 0, NULL );
    printf( "--- NtRaiseException CONTEXT_FULL\n" );
    {
        static volatile int pass;
        EXCEPTION_RECORD rec = { 0xe0000125 };
        struct { CONTEXT c; char pad[4096]; } x;
        memset( &x, 0xcc, sizeof(x) );
        pass = 0;
        RtlCaptureContext( &x.c );
        if (!pass++)
        {
            printf( "RtlCaptureContext flags %#lx\n", x.c.ContextFlags );
            rec.ExceptionAddress = (void *)x.c.Rip;
            printf( "NtRaiseException -> %#lx\n", (long)NtRaiseException( &rec, &x.c, TRUE ) );
        }
    }
    printf( "--- NtRaiseException CONTEXT_FULL|XSTATE with garbage CONTEXT_EX\n" );
    {
        static volatile int pass;
        EXCEPTION_RECORD rec = { 0xe0000125 };
        struct { CONTEXT c; char pad[4096]; } x;
        memset( &x, 0x11, sizeof(x) );
        pass = 0;
        RtlCaptureContext( &x.c );
        if (!pass++)
        {
            x.c.ContextFlags |= CONTEXT_XSTATE;
            rec.ExceptionAddress = (void *)x.c.Rip;
            printf( "NtRaiseException -> %#lx\n", (long)NtRaiseException( &rec, &x.c, TRUE ) );
        }
    }
    RemoveVectoredExceptionHandler( h );
}

/* ---- special user APC ---- */
static HANDLE apc_event;
static volatile LONG apc_stop, apc_seen;

static void CALLBACK apc_cb( ULONG_PTR arg )
{
    struct { ULONG_PTR Parameter; CONTEXT *ContextRecord; ULONG_PTR r0, r1; } *data = (void *)arg;
    printf( "apc: param %#Ix\n", data->Parameter );
    dump_context( "apc context", data->ContextRecord );
    apc_seen++;
}

static DWORD WINAPI apc_thread( void *arg )
{
    volatile double x = 1.0;
    SetEvent( apc_event );
    if (arg) { while (!apc_stop) SleepEx( 10, TRUE ); return 0; }
    dirty_avx();
    while (!apc_stop) x = x * 1.0000001 + 0.1;
    return 0;
}

static void test_apc(void)
{
    HANDLE thread;
    NTSTATUS status;
    int i, mode;

    for (mode = 0; mode < 2; mode++)
    {
        apc_stop = apc_seen = 0;
        apc_event = CreateEventW( NULL, FALSE, FALSE, NULL );
        thread = CreateThread( NULL, 0, apc_thread, (void *)(ULONG_PTR)mode, 0, NULL );
        WaitForSingleObject( apc_event, INFINITE );
        Sleep( 50 );
        printf( "--- special user APC with context, target %s\n", mode ? "in alertable wait" : "spinning" );
        status = pQueueUserAPC2( apc_cb, thread, 0x1234, 1 /* SPECIAL */ | 0x10000 /* CALLBACK_DATA_CONTEXT */ );
        printf( "QueueUserAPC2 -> %ld err %lu\n", (long)status, GetLastError() );
        for (i = 0; i < 20 && !apc_seen; i++) Sleep( 50 );
        printf( "apc delivered: %ld\n", apc_seen );
        apc_stop = 1;
        WaitForSingleObject( thread, INFINITE );
        if (!apc_seen) printf( "(apc after stop: %ld)\n", apc_seen );
        CloseHandle( thread );
    }
}

/* ---- helpers ---- */
static void test_config(void)
{
    XCFG *c = XCFGP;
    unsigned int i;
    int regs[4];

    printf( "GetEnabledXStateFeatures %#I64x RtlGetEnabledExtendedFeatures(~0) %#I64x\n",
            GetEnabledXStateFeatures(), RtlGetEnabledExtendedFeatures( ~(ULONG64)0 ) );
    printf( "cfg: Enabled %#I64x Volatile %#I64x Size %#lx Control %#lx Supervisor %#I64x Aligned %#I64x AllSize %#lx UserVisSup %#I64x\n",
            c->EnabledFeatures, c->EnabledVolatileFeatures, c->Size, c->ControlFlags, c->EnabledSupervisorFeatures,
            c->AlignedFeatures, c->AllFeatureSize, c->EnabledUserVisibleSupervisorFeatures );
    for (i = 0; i < 64; i++)
        if (c->Features[i].Size || c->AllFeatures[i])
            printf( "  feature %u: offset %#lx size %#lx all %#lx\n", i, c->Features[i].Offset, c->Features[i].Size, c->AllFeatures[i] );
    __asm__( "cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3]) : "a" (0xd), "c" (0) );
    printf( "cpuid d.0: %#x %#x %#x %#x\n", regs[0], regs[1], regs[2], regs[3] );
    __asm__( "cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3]) : "a" (0xd), "c" (1) );
    printf( "cpuid d.1: %#x %#x %#x %#x\n", regs[0], regs[1], regs[2], regs[3] );
    __asm__( "cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3]) : "a" (7), "c" (0) );
    printf( "cpuid 7.0: ebx %#x ecx %#x edx %#x (cet_ss %d)\n", regs[1], regs[2], regs[3], regs[2] >> 7 & 1 );
}

static void test_init_context(void)
{
    static const ULONG64 masks[] = { 0, 4, 0xe4, 0xe7, 0x800, ~(ULONG64)0 };
    static const ULONG flags[] = { CONTEXT_FULL, CONTEXT_FULL | CONTEXT_XSTATE, CONTEXT_XSTATE };
    unsigned int i, j, k;

    for (j = 0; j < sizeof(flags) / sizeof(flags[0]); j++)
    for (i = 0; i < sizeof(masks) / sizeof(masks[0]); i++)
    {
        static char buf[0x8000];
        CONTEXT *ctx = NULL;
        DWORD len = 0, len2;
        BOOL ret;

        memset( buf, 0xcc, sizeof(buf) );
        SetLastError( 0xdeadbeef );
        ret = pInitializeContext2( NULL, flags[j], NULL, &len, masks[i] );
        printf( "InitializeContext2(%#lx, mask %#I64x): %d err %lu len %#lx", flags[j], masks[i], ret, GetLastError(), len );
        len2 = len;
        ret = pInitializeContext2( buf, flags[j], &ctx, &len2, masks[i] );
        if (!ret) { printf( " -> fail %lu\n", GetLastError() ); continue; }
        {
            CTXEX *ex = (CTXEX *)(ctx + 1);
            printf( " -> flags %#lx ex All %ld/%#lx Legacy %ld/%#lx XState %ld/%#lx", ctx->ContextFlags,
                    ex->All.Offset, ex->All.Length, ex->Legacy.Offset, ex->Legacy.Length, ex->XState.Offset, ex->XState.Length );
            if (flags[j] & 0x40)
            {
                XHDR *xs = (XHDR *)((char *)ex + ex->XState.Offset);
                DWORD64 m = 0;
                printf( " Mask %#I64x Compaction %#I64x;", xs->Mask, xs->CompactionMask );
                for (k = 2; k < 20; k++)
                {
                    DWORD l = 0;
                    void *p = LocateXStateFeature( ctx, k, &l );
                    if (p) printf( " %u@%ld/%#lx", k, (long)((char *)p - (char *)xs), l );
                }
                ret = SetXStateFeaturesMask( ctx, 0xe4 );
                GetXStateFeaturesMask( ctx, &m );
                printf( " Set(e4) %d -> %#I64x", ret, m );
            }
            printf( "\n" );
        }
    }
}

/* ---- argument validation of Locate ---- */
static jmp_buf jb;
static volatile LONG faults;
static LONG CALLBACK veh_jmp( EXCEPTION_POINTERS *ep )
{
    _JUMP_BUFFER *b = (_JUMP_BUFFER *)jb;
    CONTEXT *c = ep->ContextRecord;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    faults++;
    c->Rbx = b->Rbx; c->Rsp = b->Rsp; c->Rbp = b->Rbp; c->Rsi = b->Rsi; c->Rdi = b->Rdi;
    c->R12 = b->R12; c->R13 = b->R13; c->R14 = b->R14; c->R15 = b->R15; c->Rip = b->Rip;
    c->Rax = 1;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void test_locate_bad(void)
{
    char *mem = VirtualAlloc( NULL, 0x20000, MEM_RESERVE, PAGE_NOACCESS );
    struct { CONTEXT c; CTXEX ex; } *x;
    void *h = AddVectoredExceptionHandler( 1, veh_jmp );
    volatile unsigned int i, variant;

    VirtualAlloc( mem, 0x10000, MEM_COMMIT, PAGE_READWRITE );
    x = (void *)(mem + 0x8000);
    for (variant = 0; variant < 5; variant++)
    {
        static const char *names[] = { "xstate header unreadable", "zeroed header, Length 0x40", "zeroed header, Length 0",
                                       "header Mask=Compaction=all, Length 0x40", "no CONTEXT_XSTATE flag, header unreadable" };
        XHDR *xs = (XHDR *)(mem + 0x9000);
        memset( mem, 0, 0x10000 );
        x->c.ContextFlags = CONTEXT_FULL | CONTEXT_XSTATE;
        x->ex.All.Offset = x->ex.Legacy.Offset = -(LONG)sizeof(CONTEXT);
        x->ex.Legacy.Length = sizeof(CONTEXT);
        x->ex.XState.Offset = (char *)xs - (char *)&x->ex;
        x->ex.XState.Length = 0x40;
        switch (variant)
        {
        case 0: x->ex.XState.Offset = mem + 0x10000 - (char *)&x->ex; x->ex.XState.Length = 0x2000; break;
        case 2: x->ex.XState.Length = 0; break;
        case 3: xs->Mask = ~(ULONG64)0; xs->CompactionMask = ~(ULONG64)0; break;
        case 4: x->c.ContextFlags = CONTEXT_FULL; x->ex.XState.Offset = mem + 0x10000 - (char *)&x->ex; break;
        }
        printf( "--- %s\n", names[variant] );
        for (i = 0; i < 66; i++)
        {
            ULONG len = 0xdead;
            void *p = (void *)0xdead;
            volatile int which;

            if (i > 20 && i < 62) continue;
            for (which = 0; which < 3; which++)
            {
                if (which == 2 && i < 2) continue; /* legacy features come from the CONTEXT */
                len = 0xdead; p = (void *)0xdead;
                if (!_setjmp( jb, NULL ))
                {
                    if (which == 0) p = RtlLocateExtendedFeature( &x->ex, i, &len );
                    else if (which == 1) p = RtlLocateExtendedFeature2( &x->ex, i, XCFGP, &len );
                    else p = LocateXStateFeature( &x->c, i, &len );
                    if (p) printf( "  %s(%u): xs+%ld len %#lx\n", which == 0 ? "Rtl" : which == 1 ? "Rtl2" : "Locate", (unsigned)i,
                                   (long)((char *)p - (char *)&x->ex - x->ex.XState.Offset), len );
                    else printf( "  %s(%u): NULL len %#lx\n", which == 0 ? "Rtl" : which == 1 ? "Rtl2" : "Locate", (unsigned)i, len );
                }
                else printf( "  %s(%u): FAULT\n", which == 0 ? "Rtl" : which == 1 ? "Rtl2" : "Locate", (unsigned)i );
            }
        }
        if (!_setjmp( jb, NULL ))
        {
            DWORD64 m = 0xdead;
            BOOL r = GetXStateFeaturesMask( &x->c, &m );
            printf( "  GetXStateFeaturesMask: %d %#I64x\n", r, m );
        }
        else printf( "  GetXStateFeaturesMask: FAULT\n" );
    }
    RemoveVectoredExceptionHandler( h );
}

/* ---- what do the Locate functions check: one change at a time on a valid context ---- */
static void locate_row( const char *name, CONTEXT *ctx )
{
    static const unsigned int ids[] = { 2, 5, 6, 7, 9, 11, 19 };
    CTXEX *ex = (CTXEX *)(ctx + 1);
    volatile unsigned int i, which;

    printf( "%-44s", name );
    for (which = 0; which < 2; which++)
    {
        printf( which ? " | Locate:" : " Rtl:" );
        for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        {
            if (!_setjmp( jb, NULL ))
            {
                ULONG len = 0xdead;
                char *p = which ? LocateXStateFeature( ctx, ids[i], &len ) : RtlLocateExtendedFeature( ex, ids[i], &len );
                if (p) printf( " %u@%ld/%lx", ids[i], (long)(p - (char *)ex - ex->XState.Offset), len );
                else if (len != 0xdead) printf( " %u:0/%lx", ids[i], len );
                else printf( " %u:0", ids[i] );
            }
            else printf( " %u:FAULT", ids[i] );
        }
    }
    if (!_setjmp( jb, NULL ))
    {
        DWORD64 m = 0xdead;
        BOOL r = GetXStateFeaturesMask( ctx, &m );
        printf( " | Get %d %I64x", r, m );
    }
    else printf( " | Get FAULT" );
    if (!_setjmp( jb, NULL )) printf( " RtlGet %I64x", RtlGetExtendedFeaturesMask( ex ) );
    else printf( " RtlGet FAULT" );
    printf( "\n" );
}

static void test_locate_matrix(void)
{
    static const ULONG64 init_masks[] = { 0xe4, 0xe0, 0xa0, 0x80, 0x4, 0 };
    char *mem = VirtualAlloc( NULL, 0x20000, MEM_RESERVE, PAGE_NOACCESS );
    void *h = AddVectoredExceptionHandler( 1, veh_jmp );
    CONTEXT *ctx;
    CTXEX *ex, saved_ex;
    XHDR *xs, saved_xs;
    DWORD len;
    unsigned int i;
    char name[64];

    VirtualAlloc( mem, 0x10000, MEM_COMMIT, PAGE_READWRITE );

    for (i = 0; i < sizeof(init_masks) / sizeof(init_masks[0]); i++)
    {
        memset( mem, 0, 0x10000 );
        len = 0x8000;
        if (!pInitializeContext2( mem + 0x8000, CONTEXT_FULL | CONTEXT_XSTATE, &ctx, &len, init_masks[i] )) { printf( "init failed\n" ); return; }
        ex = (CTXEX *)(ctx + 1);
        xs = (XHDR *)((char *)ex + ex->XState.Offset);
        sprintf( name, "init mask %I64x (Compaction %I64x Len %lx)", init_masks[i], xs->CompactionMask, ex->XState.Length );
        locate_row( name, ctx );
        xs->Mask = 0xe4;
        locate_row( "  + header Mask e4", ctx );
    }
    memset( mem, 0, 0x10000 );
    len = 0x8000;
    pInitializeContext2( mem + 0x8000, CONTEXT_FULL | CONTEXT_XSTATE, &ctx, &len, 0xe4 );
    ex = (CTXEX *)(ctx + 1);
    xs = (XHDR *)((char *)ex + ex->XState.Offset);
    saved_ex = *ex; saved_xs = *xs;
#define ROW(n, change) do { *ex = saved_ex; *xs = saved_xs; ctx->ContextFlags = CONTEXT_FULL | CONTEXT_XSTATE; change; locate_row( n, ctx ); } while (0)
    ROW( "base e4", (void)0 );
    ROW( "header Compaction 0", xs->CompactionMask = 0 );
    ROW( "header Compaction 0, Mask e4", (xs->CompactionMask = 0, xs->Mask = 0xe4) );
    ROW( "header Compaction bit63|4", xs->CompactionMask = ((ULONG64)1 << 63) | 4 );
    ROW( "header Compaction bit63|e0", xs->CompactionMask = ((ULONG64)1 << 63) | 0xe0 );
    ROW( "header Compaction bit63|80", xs->CompactionMask = ((ULONG64)1 << 63) | 0x80 );
    ROW( "header Compaction e4 (no bit63)", xs->CompactionMask = 0xe4 );
    ROW( "header Compaction bit63|ae4", xs->CompactionMask = ((ULONG64)1 << 63) | 0xae4 );
    ROW( "header Compaction ~0", xs->CompactionMask = ~(ULONG64)0 );
    ROW( "XState.Length 0", ex->XState.Length = 0 );
    ROW( "XState.Length 0x3f", ex->XState.Length = 0x3f );
    ROW( "XState.Length 0x40", ex->XState.Length = 0x40 );
    ROW( "XState.Length 0x13f", ex->XState.Length = 0x13f );
    ROW( "XState.Length 0x140", ex->XState.Length = 0x140 );
    ROW( "XState.Length 0x77f", ex->XState.Length = 0x77f );
    ROW( "XState.Length 0x2000", ex->XState.Length = 0x2000 );
    ROW( "XState.Length ~0", ex->XState.Length = ~0u );
    ROW( "XState.Offset +1 (unaligned)", ex->XState.Offset += 1 );
    ROW( "XState.Offset unreadable", ex->XState.Offset = mem + 0x10000 - (char *)ex );
    ROW( "XState.Offset unreadable, Length 0x2000", (ex->XState.Offset = mem + 0x10000 - (char *)ex, ex->XState.Length = 0x2000) );
    ROW( "XState.Offset unreadable, Length 0", (ex->XState.Offset = mem + 0x10000 - (char *)ex, ex->XState.Length = 0) );
    ROW( "XState.Offset negative -> unreadable", ex->XState.Offset = mem - 0x10000 - (char *)ex );
    ROW( "All.Length 0", ex->All.Length = 0 );
    ROW( "All 0/0", (ex->All.Length = 0, ex->All.Offset = 0) );
    ROW( "All.Length ~0", ex->All.Length = ~0u );
    ROW( "Legacy 0/0", (ex->Legacy.Length = 0, ex->Legacy.Offset = 0) );
    ROW( "ex all 0xcc", memset( ex, 0xcc, sizeof(*ex) ) );
    ROW( "ex all 0", memset( ex, 0, sizeof(*ex) ) );
    ROW( "Offset-64", ex->XState.Offset -= 64 );
    ROW( "Offset+1, Length-1", (ex->XState.Offset += 1, ex->XState.Length -= 1) );
    ROW( "Offset+64, Length-64", (ex->XState.Offset += 64, ex->XState.Length -= 64) );
    ROW( "Offset 0 Length 0x40", (ex->XState.Offset = 0, ex->XState.Length = 0x40) );
    ROW( "Offset 24 Length 0x40", (ex->XState.Offset = 24, ex->XState.Length = 0x40) );
    ROW( "Offset 32 Length 0x40", (ex->XState.Offset = 32, ex->XState.Length = 0x40) );
    ROW( "Offset -8 Length 0x40", (ex->XState.Offset = -8, ex->XState.Length = 0x40) );
    ROW( "Offset -1232 Length 0x40", (ex->XState.Offset = -1232, ex->XState.Length = 0x40) );
    ROW( "Offset -1233 Length 0x40", (ex->XState.Offset = -1233, ex->XState.Length = 0x40) );
    ROW( "Offset -1296 Length 0x40", (ex->XState.Offset = -1296, ex->XState.Length = 0x40) );
    ROW( "All.Length-1", ex->All.Length -= 1 );
    ROW( "All.Length+1", ex->All.Length += 1 );
    ROW( "All.Length+0x1000", ex->All.Length += 0x1000 );
    ROW( "All.Offset+64", ex->All.Offset += 64 );
    ROW( "All.Offset-64", ex->All.Offset -= 64 );
    ROW( "All.Offset+64 Length-64", (ex->All.Offset += 64, ex->All.Length -= 64) );
    ROW( "All.Offset=XState.Offset, same end", (ex->All.Length -= ex->XState.Offset - ex->All.Offset, ex->All.Offset = ex->XState.Offset) );
    ROW( "All.Offset=XState.Offset+64, same end", (ex->All.Length -= ex->XState.Offset + 64 - ex->All.Offset, ex->All.Offset = ex->XState.Offset + 64) );
    ROW( "XState.Length 0x80000000", ex->XState.Length = 0x80000000 );
    ROW( "XState.Length 0xfffffff0", ex->XState.Length = 0xfffffff0 );
    ROW( "XState.Length 0x781", ex->XState.Length = 0x781 );
    ROW( "All.Length+0x1000, XState.Length 0x1780", (ex->All.Length += 0x1000, ex->XState.Length = 0x1780) );
    ROW( "All=XState=0xcccccccc/0xcccccccc hdr ok", (memset( ex, 0xcc, sizeof(*ex) ), (void)0) );
    ROW( "no CONTEXT_XSTATE flag", ctx->ContextFlags = CONTEXT_FULL );
    ROW( "only 0x40 flag (no arch)", ctx->ContextFlags = 0x40 );
    ROW( "flags CONTEXT_XSTATE only", ctx->ContextFlags = CONTEXT_XSTATE );
    ROW( "flags i386 xstate", ctx->ContextFlags = 0x10040 );
#undef ROW
    RemoveVectoredExceptionHandler( h );
}

int main( int argc, char **argv )
{
    const char *mode = argc > 1 ? argv[1] : "all";
    setvbuf( stdout, NULL, _IONBF, 0 );
    pQueueUserAPC2 = (void *)GetProcAddress( GetModuleHandleA( "kernel32" ), "QueueUserAPC2" );
    pInitializeContext2 = (void *)GetProcAddress( GetModuleHandleA( "kernel32" ), "InitializeContext2" );
    if (!strcmp( mode, "all" ) || !strcmp( mode, "cfg" )) test_config();
    if (!strcmp( mode, "all" ) || !strcmp( mode, "init" )) test_init_context();
    if (!strcmp( mode, "all" ) || !strcmp( mode, "loc" )) test_locate_bad();
    if (!strcmp( mode, "all" ) || !strcmp( mode, "mat" )) test_locate_matrix();
    if (!strcmp( mode, "all" ) || !strcmp( mode, "apc" )) test_apc();
    if (!strcmp( mode, "all" ) || !strcmp( mode, "exc" )) test_exceptions();
    return 0;
}
