Text for a new issue at https://gitlab.freedesktop.org/xorg/lib/libx11/-/issues. Not filed.
Reproducer: [190-libx11-reproducer.c](190-libx11-reproducer.c) (= `tests/r191/ximopen.c`).
Patch: [190-libx11-im-list-lock.patch](190-libx11-im-list-lock.patch) (against 1.8.13; applies to git master 6d4432b).
Existing reports: not searched for (do it before filing: "_XimOpenIM", "_XimCurrentIMlist", "XOpenIM thread").

---

**`XOpenIM()` / `XCloseIM()` from several threads corrupt the heap, also when every thread has its own `Display`: the process-wide list of input methods has no lock**

**Version:** libX11 1.8.13 (Ubuntu 26.04, 2:1.8.13-1, and an own build of the same source), glibc 2.43, x86_64. The
code is the same in git master (6d4432b): `modules/im/ximcp/imInt.c` and `src/xlibi18n/lcConv.c` are unchanged.

**Summary**

A program calls `XInitThreads()` and gives every UI thread its own display connection and its own input method.
No Xlib object is shared between the threads. When several threads call `XOpenIM()` at the same time, glibc aborts
the process:

```
double free or corruption (!prev)          (also seen: "double free or corruption (out)", "realloc(): invalid old size",
                                            SIGSEGV)
#7  malloc_printerr
#8  _int_free_merge_chunk
#10 _int_realloc
    realloc <- _XimSetIMStructureList <- _XimOpenIM <- XOpenIM
```

`_XimOpenIM()` (modules/im/ximcp/imInt.c) registers every new input method in a list that is global to the process,
without a lock:

```c
static Xim 		*_XimCurrentIMlist  = (Xim *)NULL;
static int		 _XimCurrentIMcount = 0;
...
	if(i >= _XimCurrentIMcount) {
	    if(!(xim = Xrealloc(_XimCurrentIMlist,
					 ((i + 1) * sizeof(Xim)))))
```

Two threads realloc the same block. `_XimDestroyIMStructureList()` (`XCloseIM()`) and `_XimServerDestroy()` (the
input method server went away, called from `XFilterEvent()`) read and write the same list unlocked, so they can
write into the block another thread has just freed.

The same concurrency has a second effect: `XOpenIM()` returns NULL in one of the threads (2 to 4 of 100 processes
with 8 threads opening one input method each, the compose table of en_US.UTF-8). `_XimLocalOpenIM()` fails at its
first `_XlcOpenConverter()`. `open_indirect_converter()` (src/xlibi18n/lcConv.c) initializes three static quarks
lazily and tests the one it sets first:

```c
    static XrmQuark QChar, QCharSet, QCTCharSet = (XrmQuark) 0;

    if (QCTCharSet == (XrmQuark) 0) {
	QCTCharSet = XrmStringToQuark(XlcNCTCharSet);
	QCharSet = XrmStringToQuark(XlcNCharSet);
	QChar = XrmStringToQuark(XlcNChar);
    }
```

A second thread that comes in between looks up converters with a zero quark and fails.

**Reproducer**

```
gcc -O2 -o ximopen 190-libx11-reproducer.c -lX11 -lpthread
for i in $(seq 30); do XMODIFIERS=@im=none XCOMPOSEFILE=/dev/null ./ximopen; done 2>&1 | sort | uniq -c
```

16 threads, each with its own display, open 64 input methods each at the same moment, then close them
(`XCOMPOSEFILE=/dev/null` only makes `XOpenIM()` short, so that the calls overlap more often; `./ximopen 8 1` with
the default compose file is the pattern of a program whose threads start together).

| libX11 | `./ximopen` (16 x 64, empty compose file), 30 processes | `./ximopen 8 1` (default compose file), 200 processes |
|---|---|---|
| 1.8.13, distribution package | 27 abort, 3 "no input method" | 2 "no input method" of 60 |
| 1.8.13, own build | 28 abort, 1 SIGSEGV, 1 "no input method" | 4 "no input method", 1 SIGSEGV |
| own build + patch | 30 ok | 200 ok |
| distribution package, all `XOpenIM()` / `XCloseIM()` under a mutex of the program (`./ximopen 16 64 lock`) | 30 ok | |

**Proposed fix**

The three functions that touch the list take `_Xi18n_lock`; `_XimServerDestroy()` takes the input method out of
the list under the lock and runs the callbacks without it. `open_indirect_converter()` sets the quark it tests
last.

**Not covered by the patch** (ThreadSanitizer, libX11 built with `-fsanitize=thread`, threads that share no
`Display`; `tests/r191/xthreads.c` in the Wine project):

- modules/im/ximcp/imLcIm.c: the cached compose table (`_XimCache_mmap`, `_XimCachedDefaultTreeBase`,
  `_XimCachedDefaultTreeRefcount`) is filled and reference-counted without a lock;
- modules/im/ximcp/imInsClbk.c: the list of instantiate callbacks (`callback_list`, `lock`) of
  `XRegisterIMInstantiateCallback()`;
- modules/im/ximcp/imRm.c `_XimInitialResourceInfo()`, src/xlibi18n/lcWrap.c `_XlcCompileResourceList()` (from
  `XCreateFontSet()`): lazy initializations of static tables (they write the same values, so probably harmless);
- modules/im/ximcp/imLcFlt.c `_XimLocalFilter()`: scratch buffer and previous key code in function statics.

Where this was found: Wine's X11 driver opens one display and one input method per thread that creates windows.
Wine now serializes its `XOpenIM()` / `XCloseIM()` / `XCreateFontSet()` calls.
