/* WaitForInputIdle ground truth: which thread / which call makes a process "input idle" (issue 133).
 *
 *   idle.exe [-v] [-x EXE] [NAME...]             run the built-in scenarios (all, or those whose name starts with NAME)
 *   idle.exe [-v] [-x EXE] run G|C PARENT CHILD  one ad-hoc scenario
 * -v: print the child's timeline; -x EXE: children run (a copy of) EXE from our directory, e.g. idle32.exe.
 *
 * The child is a copy of this exe with the PE subsystem patched (G = GUI, C = console; idle_gui.exe /
 * idle_con.exe next to the exe). Child script: threads separated by '/', the first one is the process'
 * main thread, the others are started by it before it does anything else. Tokens, comma separated:
 *   sN  Sleep N ms                          k  PeekMessage(PM_NOREMOVE) once (creates the queue)
 *   K   PeekMessage(PM_REMOVE) once
 *   w   create a visible top-level window   m  create a message-only window
 *   g   GetMessage loop forever             gN GetMessage loop, left after N ms (posted thread message)
 *   pN  PeekMessage(PM_REMOVE)+Sleep(1) loop for N ms
 *   MN  drain, then MsgWaitForMultipleObjects(0, NULL, FALSE, N, QS_ALLINPUT)
 *   WN  drain, then WaitMessage (ended by a thread message posted after N ms)
 *   SN  SendMessage to the parent's window, which sleeps N ms in its window proc
 *   tN  SetTimer(hwnd, 1, N, NULL) on the last window         q  GetQueueStatus(QS_ALLINPUT)
 *   IN  start a grandchild that never idles and WaitForInputIdle(grandchild, N)
 *   c   create a window like a Wine driver's clipboard manager (Wine only)
 *   r   set the "ready" event               x  ExitThread      e  ExitProcess
 * Parent script: sN sleep, iN WaitForInputIdle(child, N), r wait for the child's ready event.
 * Output per call: i@T=RET(+DT): called T ms after CreateProcess was called, took DT ms (-v: child
 * timeline on the same clock).
 * Then the child is terminated and WaitForInputIdle is called once more ("dead=").
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o idle.exe idle.c -luser32   (i686-w64-mingw32-gcc for idle32.exe)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static LARGE_INTEGER freq, base;
static HANDLE ready;
static FILE *clog_file;
static int verbose;

static int now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (int)((t.QuadPart - base.QuadPart) * 1000 / freq.QuadPart);
}

struct poster { DWORD tid, delay; };
static DWORD WINAPI poster_proc(void *arg)
{
    struct poster *p = arg;
    Sleep(p->delay);
    PostThreadMessageA(p->tid, WM_APP, 0, 0);
    free(p);
    return 0;
}
static void post_later(DWORD delay)
{
    struct poster *p = malloc(sizeof(*p));
    p->tid = GetCurrentThreadId(); p->delay = delay;
    CloseHandle(CreateThread(NULL, 0, poster_proc, p, 0, NULL));
}

static void drain(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

static void chlog(int thread, const char *tok)
{
    if (!clog_file) return;
    fprintf(clog_file, "  child %d: thread %d (%04lx) %s\n", now(), thread, GetCurrentThreadId(), tok);
    fflush(clog_file);
}

/* like winewayland's per-process clipboard manager window: Wine handles its messages in win32u, and
 * WM_NCCREATE marks the thread as internal. On winex11 this makes the child a second clipboard manager
 * (import window, format listener) for its few seconds of life: scratch prefixes only. */
static LRESULT CALLBACK clipboard_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static LRESULT (WINAPI *pNtUserMessageCall)(HWND, UINT, WPARAM, LPARAM, void *, DWORD, BOOL);
    if (!pNtUserMessageCall) pNtUserMessageCall = (void *)GetProcAddress(LoadLibraryA("win32u.dll"), "NtUserMessageCall");
    if (msg == WM_NCCREATE) pNtUserMessageCall(hwnd, msg, wp, lp, 0, 0x0300 /* NtUserClipboardWindowProc */, FALSE);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

struct tscript { int index; char *script; };

static DWORD WINAPI run_script(void *arg)
{
    struct tscript *ts = arg;
    char *tok, *next;
    HWND hwnd = NULL;
    MSG msg;

    for (tok = ts->script; tok && *tok; tok = next)
    {
        int n = atoi(tok + 1), end;
        if ((next = strchr(tok, ','))) *next++ = 0;
        chlog(ts->index, tok);
        switch (*tok)
        {
        case 's': Sleep(n); break;
        case 'k': PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE); break;
        case 'K': PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE); break;
        case 'q': GetQueueStatus(QS_ALLINPUT); break;
        case 'w': hwnd = CreateWindowA("static", "idle child", WS_POPUP | WS_VISIBLE, 0, 0, 100, 50, NULL, NULL, NULL, NULL); break;
        case 'm': hwnd = CreateWindowA("static", "idle child", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, NULL, NULL); break;
        case 'c':
        {
            WNDCLASSA cls = {0};
            cls.lpfnWndProc = clipboard_proc;
            cls.lpszClassName = "r133_clipboard";
            RegisterClassA(&cls);
            hwnd = CreateWindowA("r133_clipboard", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
            break;
        }
        case 't': SetTimer(hwnd, 1, n, NULL); break;
        case 'g':
            if (tok[1]) post_later(n);
            while (GetMessageA(&msg, NULL, 0, 0))
            {
                if (!msg.hwnd && msg.message == WM_APP) break;
                DispatchMessageA(&msg);
            }
            break;
        case 'p':
            end = now() + n;
            while (now() < end) { drain(); Sleep(1); }
            break;
        case 'M':
            drain();
            MsgWaitForMultipleObjects(0, NULL, FALSE, n, QS_ALLINPUT);
            break;
        case 'W':
            drain();
            post_later(n);
            WaitMessage();
            drain();
            break;
        case 'S':
            SendMessageA(FindWindowExA(HWND_MESSAGE, NULL, "r133_parent", NULL), WM_APP, n, 0);
            break;
        case 'I':
        {
            char cmd[MAX_PATH + 64]; STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi;
            cmd[0] = '"'; GetModuleFileNameA(NULL, cmd + 1, MAX_PATH); strcat(cmd, "\" child s4000 0");
            CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            WaitForInputIdle(pi.hProcess, n);
            TerminateProcess(pi.hProcess, 0);
            break;
        }
        case 'r': SetEvent(ready); break;
        case 'x': ExitThread(0);
        case 'e': ExitProcess(0);
        }
    }
    chlog(ts->index, "end");
    return 0;
}

static int child_main(char *script, const char *base_str)
{
    struct tscript *ts, *hs[16];
    char *p, *next, path[MAX_PATH];
    int i = 0, n_helpers = 0;

    base.QuadPart = _strtoi64(base_str, NULL, 10);
    ready = OpenEventA(EVENT_ALL_ACCESS, FALSE, "r133_ready");
    GetModuleFileNameA(NULL, path, MAX_PATH);
    strcpy(strrchr(path, '\\') + 1, "idle_child.log");
    clog_file = fopen(path, "w");
    chlog(0, "start");

    /* start the helper threads first, then run the main script on this thread */
    for (p = strchr(script, '/'); p; p = next)
    {
        struct tscript *h = malloc(sizeof(*h));
        *p++ = 0;
        next = strchr(p, '/');
        h->index = ++i; h->script = p;
        hs[i] = h;
    }
    for (; i > 0; i--) CloseHandle(CreateThread(NULL, 0, run_script, hs[n_helpers++ + 1], 0, NULL));
    ts = malloc(sizeof(*ts));
    ts->index = 0; ts->script = script;
    run_script(ts);
    return 0;
}

/* ---- parent ---- */

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP) { Sleep(wp); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI parent_window_thread(void *arg)
{
    MSG msg;
    CreateWindowA("r133_parent", "r133 parent", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, NULL, NULL);
    SetEvent(arg);
    while (GetMessageA(&msg, NULL, 0, 0)) DispatchMessageA(&msg);
    return 0;
}

static const char *child_src; /* -x EXE: use (a patched copy of) EXE for the children, e.g. the other bitness */

static void make_child_exe(char *path, WORD subsystem)
{
    char self[MAX_PATH];
    IMAGE_DOS_HEADER dos;
    DWORD n, off;
    HANDLE f;

    GetModuleFileNameA(NULL, self, MAX_PATH);
    strcpy(path, self);
    if (child_src) strcpy(strrchr(self, '\\') + 1, child_src);
    strcpy(strrchr(path, '.'), subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI ? "_gui.exe" : "_con.exe");
    if (!CopyFileA(self, path, FALSE)) { printf("CopyFile failed %lu\n", GetLastError()); exit(1); }
    f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ReadFile(f, &dos, sizeof(dos), &n, NULL);
    /* Subsystem is at the same offset in the 32- and 64-bit optional headers */
    off = dos.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader.Subsystem);
    SetFilePointer(f, off, NULL, FILE_BEGIN);
    WriteFile(f, &subsystem, sizeof(subsystem), &n, NULL);
    CloseHandle(f);
}

static void dump_child_log(void)
{
    char path[MAX_PATH], line[256];
    FILE *f;
    GetModuleFileNameA(NULL, path, MAX_PATH);
    strcpy(strrchr(path, '\\') + 1, "idle_child.log");
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), f)) fputs(line, stdout);
    fclose(f);
}

static void run(const char *name, char subsys, const char *parent_script, const char *child_script)
{
    char path[MAX_PATH], cmd[2048], script[256], basestr[32], *tok, *next;
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD ret;

    make_child_exe(path, subsys == 'G' ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI);
    ResetEvent(ready);
    QueryPerformanceCounter(&base);
    _i64toa(base.QuadPart, basestr, 10);
    sprintf(cmd, "\"%s\" child %s %s", path, child_script, basestr);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("%s: CreateProcess failed %lu\n", name, GetLastError());
        return;
    }
    printf("%-22s %c %-14s %-34s:", name, subsys, parent_script, child_script);
    strcpy(script, parent_script);
    for (tok = script; tok && *tok; tok = next)
    {
        int n = atoi(tok + 1), t;
        if ((next = strchr(tok, ','))) *next++ = 0;
        switch (*tok)
        {
        case 's': Sleep(n); break;
        case 'r': WaitForSingleObject(ready, 10000); break;
        case 'i':
            t = now();
            ret = WaitForInputIdle(pi.hProcess, n);
            printf(" i@%d=%lx(+%d)", t, ret, now() - t);
            break;
        }
    }
    printf(" alive=%d", WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT);
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 10000);
    printf(" dead=%lx\n", WaitForInputIdle(pi.hProcess, 100));
    fflush(stdout);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (verbose) dump_child_log();
}

static const struct { const char *name; char subsys; const char *parent, *child; } scenarios[] =
{
    /* baseline: main thread busy 1.5 s, then waits */
    { "main_getmsg",        'G', "i5000",          "w,s1500,g" },
    { "main_peekloop_exit", 'G', "i9000",          "w,s1500,p1500" },          /* tests/wl_idle.c */
    { "exit_before_idle",   'G', "i5000",          "w,s1000,e" },
    { "main_exitthread",    'G', "i3000",          "w,s1000,x/s3000" },
    /* which wait makes a thread idle (process exits at ~3.5 s otherwise) */
    { "kind_getmsg",        'G', "i9000",          "w,s1000,g500,s2000" },
    { "kind_peekloop",      'G', "i9000",          "w,s1000,p500,s2000" },
    { "kind_msgwait",       'G', "i9000",          "w,s1000,M500,s2000" },
    { "kind_waitmessage",   'G', "i9000",          "w,s1000,W500,s2000" },
    { "kind_getmsg_nowin",  'G', "i9000",          "s1000,g500,s2000" },
    { "kind_msgwait_nowin", 'G', "i9000",          "s1000,M500,s2000" },
    { "kind_msgwait_queue", 'G', "i9000",          "k,s1000,M500,s2000" },
    { "kind_waitmsg_nowin", 'G', "i9000",          "s1000,W500,s2000" },
    { "kind_peek_nowin",    'G', "i9000",          "s1000,p500,s2000" },
    { "kind_timer_peek",    'G', "i9000",          "w,t10,s1000,p500,s2000" },
    { "kind_sendmsg",       'G', "i9000",          "w,s1000,S500,s2000" },
    /* helper threads */
    { "helper_win_getmsg",  'G', "i5000",          "w,s1500,g/m,g" },          /* helper pumps from the start */
    { "helper_vis_getmsg",  'G', "i5000",          "w,s1500,g/w,g" },
    { "helper_nowin_getmsg",'G', "i5000",          "w,s1500,g/g" },
    { "helper_msgwait",     'G', "i5000",          "w,s1500,g/M4000" },
    { "helper_late",        'G', "i5000",          "w,s1500,g/s500,m,g" },     /* helper idle after main is GUI */
    { "helper_first_gui",   'G', "i5000",          "s1000,w,s1000,g/w,g" },    /* helper GUI + idle before main is GUI */
    { "helper_first_late",  'G', "s300,i5000",     "s1000,w,s1000,g/w,g" },    /* same, called when the helper already idles */
    { "helper_first_late2", 'G', "s1300,i5000",    "s1000,w,s1000,g/w,g" },    /* same, called when main is GUI and busy */
    { "helper_first_busy",  'G', "i5000",          "s500,w,g/w,s2500,g" },     /* helper is the first GUI thread, busy; main idles first */
    { "helper_queue_busy",  'G', "i5000",          "s500,w,g/k,s2500,g" },
    { "main_never_gui",     'G', "i3000",          "s4000/s500,w,g" },         /* only the helper is a GUI thread */
    { "main_never_gui_late",'G', "s1000,i3000",    "s4000/s500,w,g" },
    { "main_exits_helper",  'G', "i5000",          "s500,x/s1500,w,g" },       /* main thread gone, helper idles later */
    { "main_gui_exits",     'G', "i5000",          "w,s500,x/w,s1500,g" },
    { "main_exits_nogui",   'G', "i3000",          "s500,x/s4000" },
    { "two_helpers",        'G', "i5000",          "s4000/s500,w,s1500,g/s1000,w,g" }, /* 1st GUI helper busy, 2nd idles at 1 s */
    /* no GUI thread at all */
    { "gui_no_queue",       'G', "i1500,i5000",    "s3000" },
    { "gui_queue_only",     'G', "i1500,i5000",    "k,s3000" },
    /* once only */
    { "once",               'G', "i5000,s500,i5000,s700,i5000", "w,s500,g300,s2000,g" },
    { "once_called_late",   'G', "s1500,i5000",    "w,s500,g300,s3000,g" },    /* first call when it was idle and is busy again */
    { "called_while_idle",  'G', "s1500,i5000",    "w,s500,g" },
    /* round 2: single peeks */
    { "peek_remove_x3",     'G', "i9000",          "s1000,K,s500,K,s500,K,s2000" },
    { "peek_noremove_x3",   'G', "i9000",          "s1000,k,s500,k,s500,k,s2000" },
    { "peek_win_x3",        'G', "i9000",          "w,s1000,K,s500,K,s500,K,s2000" },
    { "peek_drain_x2",      'G', "i9000",          "w,p1,s1000,K,s500,K,s2000" },
    { "helper_peek",        'G', "i3000",          "s4000/k,s4000" },
    { "helper_peek_late",   'G', "i3000",          "s4000/s500,k,s4000" },
    { "helper_none",        'G', "i3000",          "s4000/s4000" },
    { "helper_qstatus",     'G', "i3000",          "s4000/s500,q,s4000" },
    { "helper_peek_mainq",  'G', "i3000",          "k,s4000/s500,k,s4000" },
    { "helper_peek_mainw",  'G', "i3000",          "w,s4000/s500,k,s4000" },
    { "helper_window_only", 'G', "i3000",          "s4000/s500,w,s4000" },
    { "main_window_only",   'G', "i3000",          "s500,w,s4000" },
    /* round 2: is the idle state reset */
    { "state_msgwait_tmo",  'G', "i5000,s500,i5000", "w,s500,M300,s2000,g" },  /* wait ends without a message */
    { "state_peekloop",     'G', "i5000,s500,i5000", "w,s500,p300,s2000,g" },
    { "state_other_thread", 'G', "s1500,i5000",    "w,s500,g300,s2500,g/s200,m,g" }, /* helper stays idle, main woke up */
    { "state_other_woke",   'G', "s1500,i5000",    "w,s500,g/s200,m,g300,s2500,g" }, /* main stays idle, helper woke up */
    { "timeouts",           'G', "i0,i100,i1000,i2000", "s6000" },
    /* round 3: single peeks on the main thread, sleeps and order vs the WaitForInputIdle call */
    { "pk_K",               'G', "i3000",          "K,s4000" },
    { "pk_s200_K",          'G', "i3000",          "s200,K,s4000" },
    { "pk_k_s200_K",        'G', "i3000",          "k,s200,K,s4000" },
    { "pk_k_r_s200_K",      'G', "r,i3000",        "k,r,s200,K,s4000" },       /* user32:msg wait_idle case 1 */
    { "pk_k_r_s1000_K",     'G', "r,i3000",        "k,r,s1000,K,s4000" },
    { "pk_k_s1000_K",       'G', "i3000",          "k,s1000,K,s4000" },
    { "pk_K_s200_K",        'G', "i3000",          "K,s200,K,s4000" },
    { "pk_k_s200_k",        'G', "i3000",          "k,s200,k,s4000" },
    { "pk_k_r_s200_k_k",    'G', "r,i3000",        "k,r,s200,k,s200,k,s4000" },
    { "pk_k_r_K_x3",        'G', "r,i3000",        "k,r,s200,K,s200,K,s200,K,s4000" },
    { "pk_r_K",             'G', "r,i3000",        "r,s200,K,s4000" },
    { "pk_r_k_k",           'G', "r,i3000",        "r,s200,k,s200,k,s4000" },
    { "pk_helper_r_k",      'G', "r,i3000",        "s4000/k,r,s200,k,s4000" },
    { "pl_start",           'G', "i5000",          "p6000" },                  /* polling loop from the start */
    { "pl_start_win",       'G', "i5000",          "w,p6000" },
    { "pl_start_helper",    'G', "i5000",          "s6000/p6000" },
    /* round 3: which thread's state counts after the first idle */
    { "state_main_first",   'G', "s1500,i5000",    "w,g/s500,m,g300,s2500,g" }, /* main idles first and stays; helper woke up */
    { "state_main_first2",  'G', "s1500,i5000",    "w,g300,s2500,g/s500,m,g" }, /* main idles first, woke up; helper stays idle */
    { "state_3threads",     'G', "s1500,i5000",    "s5000/s100,m,g/s300,m,g300,s2500,g" },
    { "state_first_exits",  'G', "s1500,i5000",    "s5000/s100,m,g300,x/s800,m,g200,s2000,g" }, /* first idle thread is gone */
    /* console subsystem */
    { "con_nogui",          'C', "i3000",          "s1000" },
    { "con_window",         'C', "i3000,s1500,i3000", "w,s1000,g" },
    { "con_helper",         'C', "i3000,s1500,i3000", "s2000/w,g" },
    /* is WaitForInputIdle itself an idle wait (last: on Wine the grandchild outlives the scenario) */
    { "kind_wfii",          'G', "i9000",          "s1000,I1000,s2000" },
    /* Wine only: a driver's internal clipboard manager thread (winewayland has one per process) must not count */
    { "wine_clip_thread",   'G', "i5000",          "w,s1500,g/c,g" },
    { "wine_clip_only",     'G', "i2000",          "s4000/c,g" },
    { "wine_clip_peek",     'G', "i5000",          "w,s1500,g/c,M10,t10,p4000" },
};

int main(int argc, char **argv)
{
    WNDCLASSA cls = {0};
    HANDLE thread;
    int i, j;

    QueryPerformanceFrequency(&freq);
    if (argc >= 4 && !strcmp(argv[1], "child")) return child_main(argv[2], argv[3]);

    setvbuf(stdout, NULL, _IONBF, 0);
    ready = CreateEventA(NULL, TRUE, FALSE, "r133_ready");
    cls.lpfnWndProc = parent_proc;
    cls.lpszClassName = "r133_parent";
    RegisterClassA(&cls);
    thread = CreateEventA(NULL, TRUE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, parent_window_thread, thread, 0, NULL));
    WaitForSingleObject(thread, 5000);
    if (argc > 1 && !strcmp(argv[1], "-v")) { verbose = 1; argv++; argc--; }
    if (argc > 2 && !strcmp(argv[1], "-x")) { child_src = argv[2]; argv += 2; argc -= 2; }
    if (argc >= 5 && !strcmp(argv[1], "run"))
    {
        run("adhoc", argv[2][0], argv[3], argv[4]);
        return 0;
    }
    for (i = 0; i < (int)(sizeof(scenarios) / sizeof(scenarios[0])); i++)
    {
        for (j = 1; j < argc; j++) if (!strncmp(scenarios[i].name, argv[j], strlen(argv[j]))) break;
        if (argc > 1 && j == argc) continue;
        if (!strncmp(scenarios[i].name, "wine_", 5) && !GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) continue;
        run(scenarios[i].name, scenarios[i].subsys, scenarios[i].parent, scenarios[i].child);
    }
    return 0;
}
