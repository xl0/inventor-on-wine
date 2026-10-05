/* xopacity.c: a plain X client that sets _NET_WM_WINDOW_OPACITY before (argv[1] = "before") or 1 s after mapping (argv[2]: the value, default 0x7fffffff)
 * its window, then prints whether the WM frame (the window's parent) carries the property (175: awesome 4.3 only
 * copies changes made after it manages the window; picom only looks at the frame by default).
 * Build: gcc -o xopacity xopacity.c -lX11 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 100, 100, 200, 100, 0, 0, 0xff0000), root, parent, *kids;
    Atom opacity = XInternAtom(d, "_NET_WM_WINDOW_OPACITY", False), type;
    unsigned long value = 0x7fffffff, count = 0, rest;
    unsigned char *data = NULL;
    unsigned int n;
    int before = argc > 1 && !strcmp(argv[1], "before"), format;

    if (argc > 2) value = strtoul(argv[2], NULL, 0);

    XStoreName(d, w, "xopacity");
    if (before) XChangeProperty(d, w, opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
    XMapWindow(d, w);
    XSync(d, False);
    sleep(1);
    if (!before) XChangeProperty(d, w, opacity, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
    XSync(d, False);
    sleep(1);
    XQueryTree(d, w, &root, &parent, &kids, &n);
    if (parent == root) printf("set %s map: not reparented\n", before ? "before" : "after");
    else
    {
        XGetWindowProperty(d, parent, opacity, 0, 1, False, XA_CARDINAL, &type, &format, &count, &rest, &data);
        printf("set %s map: frame %lx %s\n", before ? "before" : "after", parent, count ? "has the opacity" : "has NO opacity property");
    }
    return 0;
}
