# gdb python: for every thread blocked in pthread_mutex_lock print the mutex address and its owner TID;
# with a libX11 that has debug info also the Xlib lock state of the display in _XLockDisplay frames
import gdb
for t in gdb.selected_inferior().threads():
    t.switch()
    f = gdb.newest_frame()
    n = 0
    while f is not None and n < 12:
        name = f.name() or ''
        if 'pthread_mutex_lock' in name:
            try:
                m = f.read_var('mutex')
                d = m.dereference()['__data']
                print("MUTEXWAIT thread LWP %d waits for mutex %s owner LWP %d count %d kind %d" % (t.ptid[1], m, int(d['__owner']), int(d['__count']), int(d['__kind'])))
            except Exception as e:
                print("MUTEXWAIT thread LWP %d: %s" % (t.ptid[1], e))
        if name in ('_XLockDisplay', '_XInternalLockDisplay', '_XDisplayLockWait', '_XUserLockDisplay', 'XCheckIfEvent', 'XFlush', '_XError', 'XLockDisplay', 'XUnlockDisplay'):
            try:
                dpy = f.read_var('dpy')
                lk = dpy['lock']
                print("XLIBSTATE thread LWP %d in %s: dpy %s fd %d in_ifevent %d ifevent_thread %s locking_level %d locking_thread %s mutex %s" % (
                    t.ptid[1], name, dpy, int(dpy['fd']), int(dpy['in_ifevent']), dpy['ifevent_thread'], int(lk['locking_level']), lk['locking_thread'], lk['mutex']))
                print("XLIBSTATE   mutex owner LWP %d; pthread_self of this thread %s" % (int(lk['mutex'].dereference()['__data']['__owner']), gdb.parse_and_eval('(unsigned long)pthread_self()')))
            except Exception as e:
                print("XLIBSTATE thread LWP %d in %s: %s" % (t.ptid[1], name, e))
            break
        f = f.older(); n += 1
