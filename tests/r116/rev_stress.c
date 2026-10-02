/* rev.exe MODE ROUNDS
 * abba: listen_count 2 (RpcServerListen + AUTOLISTEN if); thread A in RpcMgmtWaitServerListen;
 *       main: Stop, Stop (second one drops the count to 0 and syncs under server_cs).
 * churn: several threads doing random Listen/Stop/Wait/RegisterIf2(AUTOLISTEN)/UseProtseqEp while clients call;
 *        watchdog reports a hang; afterwards counts wine_rpcrt4_server threads.
 * self: a call stops listening and registers an AUTOLISTEN interface; idle PROTSEQ: idle pooled client
 *       connection across Stop + AUTOLISTEN register.
 * Build: x86_64-w64-mingw32-gcc -O2 -o rev_stress.exe rev_stress.c -lrpcrt4 */
#include <windows.h>
#include <rpc.h>
#include <rpcdcep.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

#define NDR {{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}}
static const GUID ifid = {0x11600200,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
static LONG progress, ok_calls, bad_calls, slow_ms = 0;

static void __RPC_STUB f0(PRPC_MESSAGE msg)
{
    int v = *(int *)msg->Buffer;
    if (slow_ms) Sleep(rand() % slow_ms);
    if (v == 77) { RPC_STATUS reg_auto(void); printf("in call: stop %ld\n", RpcMgmtStopServerListening(NULL)); printf("in call: register %ld\n", reg_auto()); }
    msg->BufferLength = 4;
    if (I_RpcGetBuffer(msg)) return;
    *(int *)msg->Buffer = v + 1;
}
static RPC_DISPATCH_FUNCTION funcs[] = { f0 };
static RPC_DISPATCH_TABLE dtab = { 1, funcs, 0 };
static RPC_SERVER_INTERFACE sif = { sizeof(RPC_SERVER_INTERFACE), {{0x11600200,0x1234,0x5678,{1,2,3,4,5,6,7,8}},{1,0}}, NDR, &dtab };
static RPC_CLIENT_INTERFACE cif = { sizeof(RPC_CLIENT_INTERFACE), {{0x11600200,0x1234,0x5678,{1,2,3,4,5,6,7,8}},{1,0}}, NDR };
static RPC_SERVER_INTERFACE autoifs[4096];
static LONG nauto;

static RPC_STATUS call_ep(const char *prot, const char *ep)
{
    char sb[128]; RPC_BINDING_HANDLE b; RPC_MESSAGE msg = {0}; RPC_STATUS st;
    sprintf(sb, "%s:[%s]", prot, ep);
    if ((st = RpcBindingFromStringBindingA((unsigned char *)sb, &b))) return st;
    msg.Handle = b; msg.RpcInterfaceInformation = &cif; msg.ProcNum = 0; msg.BufferLength = 4;
    msg.DataRepresentation = 0x10;
    if (!(st = I_RpcGetBuffer(&msg)))
    {
        *(int *)msg.Buffer = 5;
        st = I_RpcSendReceive(&msg);
        if (!st && *(int *)msg.Buffer != 6) st = -2;
        I_RpcFreeBuffer(&msg);
    }
    RpcBindingFree(&b);
    return st;
}

RPC_STATUS reg_auto(void)
{
    LONG i = InterlockedIncrement(&nauto);
    RPC_SERVER_INTERFACE *s = &autoifs[i % 4096];
    *s = sif;
    s->InterfaceId.SyntaxGUID.Data1 = 0x22000000 + i;
    return RpcServerRegisterIf2(s, NULL, NULL, RPC_IF_AUTOLISTEN, 100, -1, NULL);
}

static int count_server_threads(void)
{
    static HRESULT (WINAPI *pGetThreadDescription)(HANDLE, WCHAR **);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    int n = 0;
    if (!pGetThreadDescription) pGetThreadDescription = (void *)GetProcAddress(GetModuleHandleA("kernelbase"), "GetThreadDescription");
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        HANDLE th; WCHAR *d;
        if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
        if (!(th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID))) continue;
        if (SUCCEEDED(pGetThreadDescription(th, &d))) { if (!wcscmp(d, L"wine_rpcrt4_server")) n++; LocalFree(d); }
        CloseHandle(th);
    }
    CloseHandle(snap);
    return n;
}

static DWORD CALLBACK waiter(void *arg) { return RpcMgmtWaitServerListen(); }

static volatile LONG stop_all;
static char ep_lrpc[64], ep_np[64], ep_tcp[16];

static DWORD CALLBACK client(void *arg)
{
    unsigned seed = (unsigned)(ULONG_PTR)arg;
    while (!stop_all)
    {
        RPC_STATUS st;
        seed = seed * 1103515245 + 12345;
        switch ((seed >> 16) % 3)
        {
        case 0: st = call_ep("ncalrpc", ep_lrpc); break;
        case 1: st = call_ep("ncacn_np", ep_np); break;
        default: st = call_ep("ncacn_ip_tcp", ep_tcp); break;
        }
        if (!st) InterlockedIncrement(&ok_calls); else InterlockedIncrement(&bad_calls);
        InterlockedIncrement(&progress);
    }
    return 0;
}

static LONG op_count[8];
static DWORD CALLBACK churner(void *arg)
{
    unsigned seed = (unsigned)(ULONG_PTR)arg;
    int rounds = (int)(ULONG_PTR)arg >> 8;
    while (!stop_all)
    {
        int op;
        seed = seed * 1103515245 + 12345;
        op = (seed >> 16) % 10; op = op <= 2 ? 0 : op == 3 ? 1 : op == 4 ? 2 : op == 5 ? (getenv("NOAUTO") ? 5 : 3) : op == 6 ? 4 : op == 7 ? 1 : 5;
        switch (op)
        {
        case 0: RpcServerListen(1, 100, TRUE); break;
        case 1: RpcMgmtStopServerListening(NULL); break;
        case 2: if (!getenv("NOWAIT") && (seed & 0x10000000)) RpcMgmtWaitServerListen(); break;
        case 3: reg_auto(); break;
        case 4: { char ep[64]; sprintf(ep, "%s_%u", ep_lrpc, (seed >> 8) % 16);
                  RpcServerUseProtseqEpA((unsigned char *)"ncalrpc", 0, (unsigned char *)ep, NULL); break; }
        case 5: RpcMgmtIsServerListening(NULL); break;
        }
        InterlockedIncrement(&op_count[op]);
        InterlockedIncrement(&progress);
        (void)rounds;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int r, rounds = argc > 2 ? atoi(argv[2]) : 100;
    RPC_STATUS st;
    setvbuf(stdout, NULL, _IONBF, 0);
    sprintf(ep_lrpc, "r116rev_%lx", GetCurrentProcessId());
    sprintf(ep_np, "\\pipe\\r116rev_%lx", GetCurrentProcessId());
    sprintf(ep_tcp, "%lu", 40000 + GetCurrentProcessId() % 20000);
    if ((st = RpcServerUseProtseqEpA((unsigned char *)"ncalrpc", 0, (unsigned char *)ep_lrpc, NULL))) printf("UseProtseqEp %ld\n", st);
    if ((st = RpcServerRegisterIf(&sif, NULL, NULL))) printf("RegisterIf %ld\n", st);

    if (!strcmp(argv[1], "abba"))
    {
        for (r = 0; r < rounds; r++)
        {
            HANDLE a;
            if ((st = RpcServerListen(1, 100, TRUE))) printf("listen %ld\n", st);
            if ((st = reg_auto())) printf("reg %ld\n", st);
            a = CreateThread(NULL, 0, waiter, NULL, 0, NULL);
            Sleep(5);
            RpcMgmtStopServerListening(NULL);
            if (argc > 3) Sleep(atoi(argv[3]));
            if (argc > 4) { RpcServerUnregisterIf(NULL, NULL, FALSE); RpcServerRegisterIf(&sif, NULL, NULL); }
            else RpcMgmtStopServerListening(NULL);
            if (WaitForSingleObject(a, 10000)) { printf("round %d: HANG\n", r); return 1; }
            CloseHandle(a);
        }
        printf("abba: %d rounds ok\n", rounds);
        return 0;
    }
    if (!strcmp(argv[1], "self"))
    {
        char sb[128]; RPC_BINDING_HANDLE b; RPC_MESSAGE msg = {0};
        RpcServerListen(1, 100, TRUE);
        sprintf(sb, "ncalrpc:[%s]", ep_lrpc);
        RpcBindingFromStringBindingA((unsigned char *)sb, &b);
        msg.Handle = b; msg.RpcInterfaceInformation = &cif; msg.BufferLength = 4; msg.DataRepresentation = 0x10;
        I_RpcGetBuffer(&msg); *(int *)msg.Buffer = 77; st = I_RpcSendReceive(&msg); printf("call %ld\n", st);
        return 0;
    }
    if (!strcmp(argv[1], "idle"))
    {
        /* argv[2] = protseq; idle pooled client connection kept by a binding, Stop, then register AUTOLISTEN */
        const char *prot = argv[3]; char sb[128], ep[64]; RPC_BINDING_HANDLE b; RPC_MESSAGE msg = {0}; HANDLE th; DWORD t;
        if (!strcmp(prot, "ncacn_ip_tcp")) strcpy(ep, ep_tcp); else if (!strcmp(prot, "ncacn_np")) strcpy(ep, ep_np); else strcpy(ep, ep_lrpc);
        if (strcmp(prot, "ncalrpc") && (st = RpcServerUseProtseqEpA((unsigned char *)prot, 0, (unsigned char *)ep, NULL))) printf("use %ld\n", st);
        RpcServerListen(1, 100, TRUE);
        sprintf(sb, "%s:[%s]", prot, ep);
        RpcBindingFromStringBindingA((unsigned char *)sb, &b);
        msg.Handle = b; msg.RpcInterfaceInformation = &cif; msg.BufferLength = 4; msg.DataRepresentation = 0x10;
        I_RpcGetBuffer(&msg); *(int *)msg.Buffer = 1; st = I_RpcSendReceive(&msg); printf("call %ld\n", st); I_RpcFreeBuffer(&msg);
        printf("stop %ld\n", RpcMgmtStopServerListening(NULL));
        th = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)reg_auto, NULL, 0, NULL);
        t = GetTickCount();
        printf("register: %s after %lu ms\n", WaitForSingleObject(th, 5000) ? "HANG" : "returned", GetTickCount() - t);
        RpcBindingFree(&b);
        t = GetTickCount();
        printf("after binding free: %s after %lu ms\n", WaitForSingleObject(th, 5000) ? "HANG" : "returned", GetTickCount() - t);
        return 0;
    }
    if (!strcmp(argv[1], "churn"))
    {
        HANDLE th[32]; int i, nc = 6, nw = 6; LONG last = -1; DWORD t0 = GetTickCount();
        slow_ms = argc > 3 ? atoi(argv[3]) : 20;
        RpcServerUseProtseqEpA((unsigned char *)"ncacn_np", 0, (unsigned char *)ep_np, NULL);
        if ((st = RpcServerUseProtseqEpA((unsigned char *)"ncacn_ip_tcp", 0, (unsigned char *)ep_tcp, NULL))) printf("tcp %ld\n", st);
        for (i = 0; i < nc; i++) th[i] = CreateThread(NULL, 0, client, (void *)(ULONG_PTR)(i * 7 + 1), 0, NULL);
        for (i = 0; i < nw; i++) th[nc + i] = CreateThread(NULL, 0, churner, (void *)(ULONG_PTR)(i * 13 + 3), 0, NULL);
        for (r = 0; r < rounds; r++)
        {
            int n;
            Sleep(1000);
            n = count_server_threads();
            printf("t=%lus progress %ld ok %ld bad %ld server threads %d auto %ld ops %ld/%ld/%ld/%ld/%ld/%ld\n",
                   (GetTickCount() - t0) / 1000, progress, ok_calls, bad_calls, n, nauto,
                   op_count[0], op_count[1], op_count[2], op_count[3], op_count[4], op_count[5]);
            if (n > 3) printf("  MORE THAN ONE SERVER THREAD PER PROTSEQ? %d\n", n);
            if (progress == last) { printf("NO PROGRESS: HANG\n"); fflush(stdout); Sleep(INFINITE); }
            last = progress;
        }
        stop_all = 1;
        /* a churner may listen again after a stop and then wait for the next one */
        for (i = 0; i < 200 && WaitForMultipleObjects(nc + nw, th, TRUE, 100); i++) RpcMgmtStopServerListening(NULL);
        if (i == 200) { printf("threads didn't finish: HANG\n"); Sleep(INFINITE); }
        printf("churn done\n");
        return 0;
    }
    return 1;
}
