/* rawrpc.exe MODE ...: raw rpcrt4 server/client in one process (no MIDL).
 * restart ROUNDS EPS THREADS MS: EPS ncalrpc endpoints; ROUNDS x (RpcServerListen, THREADS clients call random
 *   endpoints with fresh bindings for MS ms while main adds an endpoint, StopServerListening, WaitServerListen).
 * burst THREADS ITERS: THREADS threads released at once, each binds fresh + calls endpoint 0, ITERS times. */
#include <windows.h>
#include <rpc.h>
#include <rpcdcep.h>
#include <stdio.h>
#include <stdlib.h>

static const RPC_SYNTAX_IDENTIFIER ndr = {{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}};
static const GUID ifid = {0x11400114,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
static LONG served, ok_calls, bad_calls, stopping;
static int neps;

static void __RPC_STUB f0(PRPC_MESSAGE msg)
{
    int v = *(int *)msg->Buffer;
    InterlockedIncrement(&served);
    msg->BufferLength = 4;
    if (I_RpcGetBuffer(msg)) return;
    *(int *)msg->Buffer = v + 1;
}
static RPC_DISPATCH_FUNCTION funcs[] = { f0 };
static RPC_DISPATCH_TABLE dtab = { 1, funcs, 0 };
static RPC_SERVER_INTERFACE sif = { sizeof(RPC_SERVER_INTERFACE), {ifid,{1,0}}, {{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}}, &dtab };
static RPC_CLIENT_INTERFACE cif = { sizeof(RPC_CLIENT_INTERFACE), {ifid,{1,0}}, {{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}} };

static RPC_STATUS call_ep(int ep)
{
    char sb[128]; RPC_BINDING_HANDLE b; RPC_MESSAGE msg = {0}; RPC_STATUS st; unsigned char *s;
    sprintf(sb, "ncalrpc:[r114rev_%lx_%d]", GetCurrentProcessId(), ep);
    if ((st = RpcBindingFromStringBindingA((unsigned char *)sb, &b))) return st;
    msg.Handle = b; msg.RpcInterfaceInformation = &cif; msg.ProcNum = 0; msg.BufferLength = 4;
    msg.DataRepresentation = 0x10; msg.RpcFlags = 0;
    if (!(st = I_RpcGetBuffer(&msg)))
    {
        *(int *)msg.Buffer = ep;
        st = I_RpcSendReceive(&msg);
        if (!st && *(int *)msg.Buffer != ep + 1) st = -2;
        I_RpcFreeBuffer(&msg);
    }
    RpcBindingFree(&b);
    (void)s;
    return st;
}

static int add_ep(int i)
{
    char ep[64]; RPC_STATUS st;
    sprintf(ep, "r114rev_%lx_%d", GetCurrentProcessId(), i);
    st = RpcServerUseProtseqEpA((unsigned char *)"ncalrpc", 0, (unsigned char *)ep, NULL);
    if (st) printf("UseProtseqEp %d: %ld\n", i, st);
    return st;
}

static volatile LONG run_ms;
static DWORD CALLBACK client(void *arg)
{
    DWORD end = GetTickCount() + run_ms; unsigned seed = (unsigned)(ULONG_PTR)arg;
    while (GetTickCount() < end)
    {
        RPC_STATUS st = call_ep((seed = seed * 1103515245 + 12345) % neps);
        if (!st) InterlockedIncrement(&ok_calls); else { InterlockedIncrement(&bad_calls); Sleep(1); }
    }
    return 0;
}

static HANDLE go;
static int iters;
static DWORD CALLBACK burst(void *arg)
{
    int i; RPC_STATUS st;
    WaitForSingleObject(go, INFINITE);
    for (i = 0; i < iters; i++)
    {
        if (!(st = call_ep(0))) InterlockedIncrement(&ok_calls);
        else { InterlockedIncrement(&bad_calls); if (bad_calls < 5) printf("burst call: %ld\n", st); }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, r;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "restart"))
    {
        int rounds = atoi(argv[2]), nthreads = atoi(argv[4]);
        neps = atoi(argv[3]); run_ms = atoi(argv[5]);
        for (i = 0; i < neps; i++) add_ep(i);
        if ((r = RpcServerRegisterIf(&sif, NULL, NULL))) printf("RegisterIf %d\n", r);
        for (r = 0; r < rounds; r++)
        {
            HANDLE th[64]; LONG s0 = served; RPC_STATUS st; DWORD t;
            ok_calls = bad_calls = 0;
            if ((st = RpcServerListen(1, 100, TRUE))) printf("round %d listen %ld\n", r, st);
            for (i = 0; i < nthreads; i++) th[i] = CreateThread(NULL, 0, client, (void *)(ULONG_PTR)(r * 64 + i + 1), 0, NULL);
            Sleep(run_ms / 2);
            if (!add_ep(neps)) neps++; /* new endpoint while listening (sync with the server thread) */
            if (!getenv("HOT")) WaitForMultipleObjects(nthreads, th, TRUE, INFINITE);
            /* last endpoint added this round must work */
            st = call_ep(neps - 1);
            t = GetTickCount();
            if ((st = RpcMgmtStopServerListening(NULL))) printf("stop %ld\n", st);
            if ((st = RpcMgmtWaitServerListen())) printf("wait %ld\n", st);
            if (getenv("HOT")) WaitForMultipleObjects(nthreads, th, TRUE, INFINITE);
            for (i = 0; i < nthreads; i++) CloseHandle(th[i]);
            printf("round %d: eps %d ok %ld bad %ld served %ld stop %lu ms\n", r, neps, ok_calls, bad_calls, served - s0, GetTickCount() - t);
            /* calls while stopped must fail, not hang */
            st = call_ep(0);
            if (!st) printf("round %d: call while stopped succeeded\n", r);
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "burst"))
    {
        int nthreads = atoi(argv[2]); HANDLE th[256]; DWORD t;
        iters = atoi(argv[3]); neps = 1;
        add_ep(0);
        RpcServerRegisterIf(&sif, NULL, NULL);
        RpcServerListen(1, 1000, TRUE);
        for (r = 0; r < 20; r++)
        {
            go = CreateEventA(NULL, TRUE, FALSE, NULL);
            for (i = 0; i < nthreads; i++) th[i] = CreateThread(NULL, 0, burst, NULL, 0, NULL);
            Sleep(50); t = GetTickCount(); SetEvent(go);
            if (WaitForMultipleObjects(nthreads, th, TRUE, 30000)) { printf("burst %d hangs ok %ld\n", r, ok_calls); return 1; }
            printf("burst %d: ok %ld bad %ld %lu ms\n", r, ok_calls, bad_calls, GetTickCount() - t);
            for (i = 0; i < nthreads; i++) CloseHandle(th[i]);
            CloseHandle(go);
        }
        return 0;
    }
    printf("usage\n");
    return 1;
}
