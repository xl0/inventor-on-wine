/* unregif_race.exe [N]: RpcServerUnregisterIf(wait) racing the end of a call (109).
 * Raw rpcrt4 server interface (no MIDL) on ncalrpc, in-process client thread.
 * Each round: register the interface, a client thread makes one call, the server
 * routine signals the main thread just before returning, the main thread unregisters
 * with WaitForCallsToComplete = TRUE 0-63 us later (the call is still in progress),
 * which must return once the call completes (a watchdog thread checks). Wine lost the wakeup when the call finished
 * while the unregister was marking the interface for deletion: hang (exit 1).
 * Build: x86_64-w64-mingw32-gcc -O2 -o unregif_race.exe unregif_race.c -lrpcrt4 */
#include <windows.h>
#include <rpc.h>
#include <rpcndr.h>
#include <stdio.h>
#include <stdlib.h>

static const RPC_SYNTAX_IDENTIFIER ndr_syntax =
    {{0x8a885d04, 0x1ceb, 0x11c9, {0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60}}, {2, 0}};
static const GUID if_guid = {0x1fe1a1d6, 0x0109, 0x4b4e, {0x9a, 0x31, 0x7a, 0x55, 0x01, 0x09, 0x00, 0x01}};
static HANDLE called;

static void __RPC_STUB server_routine(RPC_MESSAGE *msg)
{
    SetEvent(called);
    msg->BufferLength = 4;
    I_RpcGetBuffer(msg);
    *(DWORD *)msg->Buffer = 0x109;
}

static RPC_DISPATCH_FUNCTION dispatch_funcs[] = { server_routine };
static RPC_DISPATCH_TABLE dispatch = { 1, dispatch_funcs };
static RPC_SERVER_INTERFACE server_if;
static RPC_CLIENT_INTERFACE client_if;

static DWORD WINAPI client_thread(void *arg)
{
    RPC_BINDING_HANDLE binding;
    RPC_MESSAGE msg = {0};
    RPC_STATUS status;

    RpcBindingFromStringBindingA((RPC_CSTR)"ncalrpc:[unregif_race]", &binding);
    msg.Handle = binding;
    msg.RpcInterfaceInformation = &client_if;
    msg.ProcNum = 0 | RPC_FLAGS_VALID_BIT;
    msg.BufferLength = 4;
    status = I_RpcGetBuffer(&msg);
    if (!status) status = I_RpcSendReceive(&msg);
    if (!status && (msg.BufferLength != 4 || *(DWORD *)msg.Buffer != 0x109)) status = -1;
    if (!status) I_RpcFreeBuffer(&msg);
    RpcBindingFree(&binding);
    return status;
}

static volatile LONG round_no = -1;

/* exits if a round's RpcServerUnregisterIf doesn't return within 5 s */
static DWORD WINAPI watchdog_thread(void *arg)
{
    LONG last = -2;

    for (;;)
    {
        Sleep(5000);
        if (round_no >= 0 && round_no == last)
        {
            printf("round %ld: RpcServerUnregisterIf(wait) hangs\n", last);
            fflush(stdout);
            ExitProcess(1);
        }
        last = round_no;
    }
}

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 1000;
    RPC_STATUS status;
    LARGE_INTEGER freq, start, now;
    DWORD ret;

    server_if.Length = sizeof(server_if);
    server_if.InterfaceId.SyntaxGUID = if_guid;
    server_if.TransferSyntax = ndr_syntax;
    server_if.DispatchTable = &dispatch;
    client_if.Length = sizeof(client_if);
    client_if.InterfaceId = server_if.InterfaceId;
    client_if.TransferSyntax = ndr_syntax;
    called = CreateEventW(NULL, FALSE, FALSE, NULL);
    QueryPerformanceFrequency(&freq);

    CloseHandle(CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL));
    status = RpcServerUseProtseqEpA((RPC_CSTR)"ncalrpc", 0, (RPC_CSTR)"unregif_race", NULL);
    if (status) { printf("RpcServerUseProtseqEp %ld\n", status); return 2; }

    for (i = 0; i < n; i++)
    {
        HANDLE client;

        status = RpcServerRegisterIfEx(&server_if, NULL, NULL, RPC_IF_AUTOLISTEN,
                                       RPC_C_LISTEN_MAX_CALLS_DEFAULT, NULL);
        if (status) { printf("RpcServerRegisterIfEx %ld\n", status); return 2; }
        client = CreateThread(NULL, 0, client_thread, NULL, 0, NULL);
        if (WaitForSingleObject(called, 5000)) { printf("round %d: call not received\n", i); return 2; }
        /* sweep the end of the call across the unregistration: 0-63 us */
        QueryPerformanceCounter(&start);
        do QueryPerformanceCounter(&now); while ((now.QuadPart - start.QuadPart) * 1000000 < (i % 64) * freq.QuadPart);
        round_no = i;
        status = RpcServerUnregisterIf(&server_if, NULL, TRUE);
        round_no = -1;
        if (status) { printf("round %d: RpcServerUnregisterIf %ld\n", i, status); return 2; }
        if (WaitForSingleObject(client, 5000)) { printf("round %d: call hangs\n", i); return 2; }
        GetExitCodeThread(client, &ret);
        if (ret) { printf("round %d: call failed %lu\n", i, ret); return 2; }
        CloseHandle(client);
    }
    printf("%d rounds ok\n", n);
    return 0;
}
