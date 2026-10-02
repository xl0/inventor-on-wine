/* listen_restart.exe [auto|slow|autoslow]: RpcServerListen / RpcServerRegisterIf2(AUTOLISTEN) right after
 * RpcMgmtStopServerListening while a client connection is still open (116).
 * Each step runs in a thread with a 5 s timeout; prints status codes.
 * Build: x86_64-w64-mingw32-gcc -O2 -o listen_restart.exe listen_restart.c -lrpcrt4 */
#include <windows.h>
#include <rpc.h>
#include <rpcdcep.h>
#include <stdio.h>
#include <string.h>

static const GUID ifid = {0x11600116,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
static const GUID ifid2 = {0x11600117,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
#define NDR {{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}}

static void __RPC_STUB f0(PRPC_MESSAGE msg)
{
    int v = *(int *)msg->Buffer;
    if (v == 1000) Sleep(2000);
    msg->BufferLength = 4;
    if (I_RpcGetBuffer(msg)) return;
    *(int *)msg->Buffer = v + 1;
}
static RPC_DISPATCH_FUNCTION funcs[] = { f0 };
static RPC_DISPATCH_TABLE dtab = { 1, funcs, 0 };
static RPC_SERVER_INTERFACE sif = { sizeof(RPC_SERVER_INTERFACE), {ifid,{1,0}}, NDR, &dtab };
static RPC_SERVER_INTERFACE sif2 = { sizeof(RPC_SERVER_INTERFACE), {ifid2,{1,0}}, NDR, &dtab };
static RPC_CLIENT_INTERFACE cif = { sizeof(RPC_CLIENT_INTERFACE), {ifid,{1,0}}, NDR };
static char ep[64];
static RPC_BINDING_HANDLE held;

static RPC_CLIENT_INTERFACE cif2 = { sizeof(RPC_CLIENT_INTERFACE), {ifid2,{1,0}}, NDR };

static RPC_STATUS call_if(RPC_BINDING_HANDLE b, RPC_CLIENT_INTERFACE *ci, int v)
{
    RPC_MESSAGE msg = {0}; RPC_STATUS st;
    msg.Handle = b; msg.RpcInterfaceInformation = ci; msg.ProcNum = 0; msg.BufferLength = 4;
    msg.DataRepresentation = 0x10;
    if (!(st = I_RpcGetBuffer(&msg)))
    {
        *(int *)msg.Buffer = v;
        st = I_RpcSendReceive(&msg);
        if (!st && *(int *)msg.Buffer != v + 1) st = -2;
        if (!st) I_RpcFreeBuffer(&msg);
    }
    return st;
}

static RPC_STATUS call(RPC_BINDING_HANDLE b) { return call_if(b, &cif, 41); }

/* a 2 s call on its own binding, in progress while main stops listening */
static DWORD CALLBACK slow_call(void *arg)
{
    RPC_BINDING_HANDLE b;
    char sb[128];
    RPC_STATUS st;
    DWORD t = GetTickCount();
    sprintf(sb, "ncalrpc:[%s]", ep);
    RpcBindingFromStringBindingA((unsigned char *)sb, &b);
    st = call_if(b, &cif, 1000);
    printf("  (slow call returned %ld after %lu ms)\n", st, GetTickCount() - t);
    fflush(stdout);
    RpcBindingFree(&b);
    return 0;
}

static RPC_BINDING_HANDLE bind_new(void)
{
    char sb[128]; RPC_BINDING_HANDLE b = NULL;
    sprintf(sb, "ncalrpc:[%s]", ep);
    RpcBindingFromStringBindingA((unsigned char *)sb, &b);
    return b;
}

enum { LISTEN, STOP, WAIT, CALL_NEW, REG_AUTO, ISLISTENING, CALL_HELD, CALL_AUTO };
static const char *names[] = { "RpcServerListen", "RpcMgmtStopServerListening", "RpcMgmtWaitServerListen",
                               "call (new binding)", "RpcServerRegisterIf2(AUTOLISTEN)", "RpcMgmtIsServerListening", "call (held binding)", "call autolisten if (new binding)" };
static DWORD CALLBACK step_thread(void *arg)
{
    RPC_BINDING_HANDLE b;
    RPC_STATUS st = -1;
    switch ((int)(ULONG_PTR)arg)
    {
    case LISTEN: st = RpcServerListen(1, 100, TRUE); break;
    case STOP: st = RpcMgmtStopServerListening(NULL); break;
    case WAIT: st = RpcMgmtWaitServerListen(); break;
    case CALL_NEW: b = bind_new(); st = call(b); RpcBindingFree(&b); break;
    case REG_AUTO: st = RpcServerRegisterIf2(&sif2, NULL, NULL, RPC_IF_AUTOLISTEN, 100, -1, NULL); break;
    case CALL_AUTO: b = bind_new(); st = call_if(b, &cif2, 41); RpcBindingFree(&b); break;
    case CALL_HELD: st = call(held); break;
    case ISLISTENING: st = RpcMgmtIsServerListening(NULL); break;
    }
    return st;
}

/* RPC exceptions in a step end its thread with the exception code */
static LONG WINAPI filter(EXCEPTION_POINTERS *ep)
{
    printf("  exception %#lx\n", ep->ExceptionRecord->ExceptionCode);
    ExitThread(ep->ExceptionRecord->ExceptionCode);
}

static void step(int s)
{
    DWORD t = GetTickCount(), code;
    HANDLE th = CreateThread(NULL, 0, step_thread, (void *)(ULONG_PTR)s, 0, NULL);
    if (WaitForSingleObject(th, 5000)) { printf("%-34s HANG (>5 s)\n", names[s]); fflush(stdout); return; }
    GetExitCodeThread(th, &code);
    CloseHandle(th);
    printf("%-34s %lu (%lu ms)\n", names[s], code, GetTickCount() - t);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int autolisten = argc > 1 && strstr(argv[1], "auto") != NULL;
    int with_slow = argc > 1 && strstr(argv[1], "slow") != NULL;
    RPC_STATUS st;

    SetUnhandledExceptionFilter(filter);
    sprintf(ep, "r116_%lx", GetCurrentProcessId());
    if ((st = RpcServerUseProtseqEpA((unsigned char *)"ncalrpc", 0, (unsigned char *)ep, NULL))) printf("UseProtseqEp %ld\n", st);
    if ((st = RpcServerRegisterIf(&sif, NULL, NULL))) printf("RegisterIf %ld\n", st);

    printf("-- listen, held connection\n");
    step(LISTEN);
    held = bind_new();
    step(CALL_HELD);
    if (with_slow)
    {
        CloseHandle(CreateThread(NULL, 0, slow_call, NULL, 0, NULL));
        Sleep(300);
        printf("-- slow call in progress\n");
    }
    printf("-- stop, then at once %s\n", autolisten ? "register an AUTOLISTEN interface" : "listen again");
    step(STOP);
    step(ISLISTENING);
    step(autolisten ? REG_AUTO : LISTEN);
    step(ISLISTENING);
    step(CALL_NEW);
    if (autolisten) step(CALL_AUTO);
    step(CALL_HELD);
    if (with_slow) Sleep(2500);
    printf("-- free held binding, stop + wait, listen\n");
    RpcBindingFree(&held);
    step(STOP);
    step(WAIT);
    step(ISLISTENING);
    step(LISTEN);
    step(CALL_NEW);
    if (autolisten) step(CALL_AUTO);
    step(STOP);
    step(WAIT);
    printf("done\n");
    fflush(stdout);
    ExitProcess(0);
}
