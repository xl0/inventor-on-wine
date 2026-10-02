/* Global WH_GETMESSAGE hook DLL for hover_tooltip.exe hook: appends the mouse messages that Edge's
   threads retrieve to hook.txt next to the DLL (120).
   x86_64-w64-mingw32-gcc -O2 -shared -o hook.dll hook.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static char log_path[MAX_PATH];
static int is_edge = -1;

static LRESULT CALLBACK getmsg(int code, WPARAM wp, LPARAM lp)
{
    MSG *msg = (MSG *)lp;
    if (is_edge == -1)
    {
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, sizeof exe);
        is_edge = !!strstr(exe, "msedge");
    }
    if (code == HC_ACTION && is_edge && (wp & PM_REMOVE) &&
        ((msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST) || msg->message == WM_NCMOUSEMOVE ||
         msg->message == WM_MOUSELEAVE || msg->message == WM_INPUT || (msg->message >= 0x245 && msg->message <= 0x24f)))
    {
        char cls[64] = "";
        FILE *f = fopen(log_path, "a");
        GetClassNameA(msg->hwnd, cls, sizeof cls);
        if (f)
        {
            fprintf(f, "tick %lu pid %lu tid %lu hwnd %p '%s' msg %04x w %llx l %08lx pt %ld,%ld time %lu extra %llx\n",
                    GetTickCount(), GetCurrentProcessId(), GetCurrentThreadId(), msg->hwnd, cls, msg->message,
                    (unsigned long long)msg->wParam, (unsigned long)msg->lParam, msg->pt.x, msg->pt.y, msg->time,
                    (unsigned long long)GetMessageExtraInfo());
            fclose(f);
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

__declspec(dllexport) HOOKPROC get_hook(void) { return getmsg; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        char *p;
        GetModuleFileNameA(inst, log_path, sizeof log_path);
        if ((p = strrchr(log_path, '\\'))) strcpy(p + 1, "hook.txt");
    }
    return TRUE;
}
