/* xtransient.c [SECS]: plain X clients for WM policy checks without Wine (175): a parent window "xt parent", a
 * transient "xt child" (WM_TRANSIENT_FOR + group leader = parent, dialog type) and a transient of that, "xt grandchild".
 * Move the parent with `xdotool set_desktop_for_window` / the WM and read the others' _NET_WM_DESKTOP with xprop.
 * Build: gcc -o xtransient xtransient.c -lX11 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <unistd.h>

static Window create(Display *d, const char *name, int x, unsigned long color, Window parent)
{
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), x, 600, 150, 100, 0, 0, color);
    Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), dialog = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XWMHints hints = {.flags = WindowGroupHint, .window_group = parent ? parent : w};
    XStoreName(d, w, name);
    XSetWMHints(d, w, &hints);
    if (parent)
    {
        XSetTransientForHint(d, w, parent);
        XChangeProperty(d, w, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dialog, 1);
    }
    XMapWindow(d, w);
    XSync(d, False);
    usleep(300000);
    return w;
}

int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    Window parent = create(d, "xt parent", 100, 0x008080, 0), child = create(d, "xt child", 300, 0x800080, parent);
    create(d, "xt grandchild", 500, 0x808000, child);
    sleep(argc > 1 ? atoi(argv[1]) : 30);
    return 0;
}
