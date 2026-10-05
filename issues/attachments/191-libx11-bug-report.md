Text for a new issue at https://gitlab.freedesktop.org/xorg/lib/libx11/-/issues. Not filed.
Reproducer: [191-libx11-reproducer.c](191-libx11-reproducer.c) (= `tests/r191/xfilter.c`).
Patch: [191-libx11-filter-list-lock.patch](191-libx11-filter-list-lock.patch) (against 1.8.13; applies to git master 6d4432b).
Existing reports: not searched for (do it before filing: "XFilterEvent", "_XUnregisterFilter", "im_filters").

---

**`_XUnregisterFilter()` changes `Display->im_filters` without the display lock: `XFilterEvent()` in another thread walks freed memory**

**Version:** libX11 1.8.13 (Ubuntu 26.04, 2:1.8.13-1, and an own build of the same source), x86_64. The code is the
same in git master (6d4432b): `src/RegstFlt.c` and `src/FilterEv.c` are unchanged.

**Summary**

With `XInitThreads()`, two threads on one `Display`:

- thread A runs the event loop and calls `XFilterEvent()` for every event, as the Xlib manual asks for;
- thread B calls `XUnsetICFocus()`, `XSetICFocus()` on another context, `XDestroyIC()`, `XCloseIM()` or
  `XUnregisterIMInstantiateCallback()`: anything that ends in `_XUnregisterFilter()`.

`_XRegisterFilterByMask()` / `_XRegisterFilterByType()` insert into `display->im_filters` inside
`LockDisplay()` / `UnlockDisplay()`, and `XFilterEvent()` walks the list inside the same lock. `_XUnregisterFilter()`
(src/RegstFlt.c) takes no lock at all:

```c
    for (prev = &display->im_filters; (fl = *prev); ) {
	if (fl->window == window &&
	    fl->filter == filter && fl->client_data == client_data) {
	    *prev = fl->next;
	    Xfree(fl);
	} else
	    prev = &fl->next;
    }
```

Thread A loads `p`, thread B unlinks and frees that node, thread A reads `p->next` from freed memory (with glibc the
first word of a freed chunk is the mangled tcache link) and faults on the next iteration:

```
Thread 1 "xfilter" received signal SIGSEGV, Segmentation fault.
#0  XFilterEvent () from libX11.so.6      <XFilterEvent+136>: mov 0x8(%rax),%rsi      ; p->window
#1  main () at xfilter.c:116
```

The same unlocked walk also races with a locked insertion (the new head can end up pointing at the node that is
being freed, which then stays linked for good) and with a second `_XUnregisterFilter()` (double free).

A second, smaller hole is in `XFilterEvent()` itself: it unlocks the display and then reads the node again to call
the filter,

```c
		UnlockDisplay(ev->xany.display);
		ret = (*(p->filter))(ev->xany.display, p->window, ev,
				      p->client_data);
```

so even with a locked `_XUnregisterFilter()` the node can be gone by then.

**Reproducer**

```
gcc -O2 -o xfilter 191-libx11-reproducer.c -lX11 -lpthread
XMODIFIERS=@im=none ./xfilter            # Xlib's built-in input method, no IM server needed
```

The main thread calls `XFilterEvent()` on a KeyPress event of a window without filters; a second thread moves the
focus between eight input contexts (eight `XOpenIM()` on the display, so that the list has several nodes). No
context is destroyed, nothing but the list is shared.

| libX11 | default | `match` (the filters get called) | `destroy` |
|---|---|---|---|
| 1.8.13, distribution package | SIGSEGV 3 of 3, within the first second | 3 of 3 | 3 of 3 |
| 1.8.13, own build | SIGSEGV 2 of 2 | 2 of 2 | 2 of 2 |
| own build + patch | ok 2 of 2 (30 million `XFilterEvent()` against 20 million focus changes in 5 s) | ok 2 of 2 | ok 2 of 2 |

**Proposed fix**

`_XUnregisterFilter()` takes the display lock like its two counterparts; `XFilterEvent()` copies the filter and its
client data before it unlocks. All callers of `_XUnregisterFilter()` (modules/im/ximcp: imLcIc.c, imDefFlt.c, imRm.c,
imThaiIc.c, imTrans.c, imTrX.c, imInsClbk.c) are the unregistering halves of code that calls `_XRegisterFilterBy*()`
in the same context, so none of them holds the display lock.

**What the patch does not make safe**

The filter is called with the display unlocked and gets the input context as client data. A thread that destroys an
input context while another thread may be filtering an event for it still frees memory under the filter
(`./xfilter 5 destroy match`: SIGSEGV in `_XimLocalFilter()` reading `ic->private.local`, also with the patch).
That is the caller's bug: an input context has to be destroyed by the thread that filters its events, or with those
threads kept out. The local input method's filter also keeps its scratch buffer and the previous key in function
statics (`_XimLocalFilter()`: `buf`, `prevcode`, `prevstate`), shared by all displays of the process.

Where this was found: Wine's X11 driver recreated a window, and destroyed its input context, in a thread other than
the one that reads the window's events. Wine no longer does that.
