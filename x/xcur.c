/* x/xcur.c: print the X server's current cursor (XFixes): size, hotspot, opaque pixels, name.
 * 1x1 with 0 opaque = invisible pointer (085). Build: gcc -O2 -o xcur xcur.c -lX11 -lXfixes; run DISPLAY=:N ./xcur */
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
int main(void)
{
    Display *d = XOpenDisplay(NULL);
    XFixesCursorImage *c;
    int i, n = 0;
    if (!d || !(c = XFixesGetCursorImage(d))) return 1;
    for (i = 0; i < c->width * c->height; i++) if (c->pixels[i] >> 24) n++;
    printf("pos %d,%d size %ux%u hot %u,%u opaque %d serial %lu name '%s'\n", c->x, c->y,
           c->width, c->height, c->xhot, c->yhot, n, c->cursor_serial, c->name ? c->name : "");
    return 0;
}
