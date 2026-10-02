/* xvfb-modes WxH...: add RandR modes to the first output of $DISPLAY (an Xvfb) and stay
 * connected: the server frees client-created modes when the client exits, and `xrandr
 * --newmode` can't help, so Wine's tests (ChangeDisplaySettings etc.) would see only one mode.
 * Prints "ready" once every mode is verified on the output; exits 1 on failure, and when the X
 * server goes away. Used by regress.sh. */
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    Window root;
    XRRScreenResources *res;
    XRROutputInfo *out;
    XEvent ev;
    int i, w, h;

    if (!d) return 1;
    root = DefaultRootWindow(d);
    res = XRRGetScreenResources(d, root);
    for (i = 1; i < argc; i++)
    {
        char name[32];
        XRRModeInfo m = {0};
        if (sscanf(argv[i], "%dx%d", &w, &h) != 2) return 1;
        snprintf(name, sizeof(name), "%dx%d", w, h);
        m.width = w; m.height = h; m.name = name; m.nameLength = strlen(name);
        m.hTotal = w + 100; m.vTotal = h + 30; m.dotClock = (unsigned long)m.hTotal * m.vTotal * 60;
        m.hSyncStart = w + 20; m.hSyncEnd = w + 60; m.vSyncStart = h + 5; m.vSyncEnd = h + 10;
        XRRAddOutputMode(d, res->outputs[0], XRRCreateMode(d, root, &m));
    }
    XSync(d, False);
    XRRFreeScreenResources(res);
    res = XRRGetScreenResources(d, root);
    out = XRRGetOutputInfo(d, res, res->outputs[0]);
    for (i = 1; i < argc; i++)
    {
        int j, found = 0;
        sscanf(argv[i], "%dx%d", &w, &h);
        for (j = 0; j < out->nmode; j++)
            for (int k = 0; k < res->nmode; k++)
                if (res->modes[k].id == out->modes[j] && res->modes[k].width == w && res->modes[k].height == h) found = 1;
        if (!found) { fprintf(stderr, "xvfb-modes: %s not on output\n", argv[i]); return 1; }
    }
    puts("ready");
    fflush(stdout);
    for (;;) XNextEvent(d, &ev);
}
