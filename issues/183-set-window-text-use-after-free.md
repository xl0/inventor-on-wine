# 183 win32u: set_window_text hands the driver a string that another thread may already have freed
Status: draft (by reading, not reproduced) · Found in: review of 173 · upstream code (win32u defwnd.c)

## What
`set_window_text` (dlls/win32u/defwnd.c ~300-330):
```
win->text = str;                      /* under the window pointer (user lock) */
... set_window_text server request ...
release_win_ptr( win );
user_driver->pSetWindowText( hwnd, str ? str : emptyW );   /* str is win->text, no lock any more */
```
A second WM_SETTEXT for the same window, handled in another thread, runs `free( win->text )` between the release and
the driver call (or while the driver reads the string): winex11's `sync_window_text` then does `lstrlenW`, two
conversions and `XChangeProperty` on freed memory. Two setters in different threads need DefWindowProc(WM_SETTEXT)
called directly for a window of another thread (the message itself is always delivered in the owner thread) or
win32u's own direct callers; lockstress does it (`poke` thread, "direct" case).

## Tried
`tests/r183/settext_race.c` (two threads, DefWindowProcA(WM_SETTEXT) with strings of changing length on one window):
integ 3 x 10 s, ~190000 sets each, no crash; with MALLOC_PERTURB_=90 and 400 samples of the X window's _NET_WM_NAME
no garbage seen (a wrong title lives only until the next set, so that says little). A read of freed heap memory is
silent most of the time; it needs an address sanitizer build or a canary in the driver entry to show.

## Direction
Copy the string for the driver before releasing the window (or call the driver with a private duplicate and free it
afterwards). With 173's commit 5 the winex11 entry runs under its window data lock, which does not help: the free
happens in win32u.
