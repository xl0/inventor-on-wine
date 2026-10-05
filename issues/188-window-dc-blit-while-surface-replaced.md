# 188 win32u: BitBlt from a window DC faults when another thread replaces the window's surface; the fault is swallowed and the surface stays locked
Status: draft (found while writing 182's GDI stress, not worked on) · upstream code (win32u) · same family as 171 / 176:
two threads on one window

## Symptom
A process where one thread reads from a window DC (BitBlt window -> memory DC) while another thread moves / resizes that
window or changes its layered state stops painting: every thread ends up in `flush_window_surfaces` waiting for dce.c's
`surfaces_lock` or for a window surface mutex that no live thread is holding. Nothing is printed.
`tests/r182/gdistress.exe 20 6 SEED x` (flag `x` = workers also draw on and re-layer each other's windows), Xvfb without a
WM: fix/173 (wt/173-build, no 171) 10 of 10 runs "threads did not stop", integ + fix/182 (with 171) 5 of 10. Without `x`
(every window DC used by its owner only, X windows still recreated cross-thread): fix build 0 of 20, fix/173 1 hang of 10
(not looked at; 171 is not in that build).

## Where (gdb, breakpoint on ntdll's handle_syscall_fault; `inst/182/out/gs-gdb-bt.txt`)
```
segv in __memcpy_avx512_unaligned_erms <- copy_rect_32 (dibdrv/primitives.c:140) <- copy_rect <- mask_rect
  <- dibdrv_PutImage (dibdrv/bitblt.c:1033) <- nulldrv_StretchBlt (bitblt.c:294) <- NtGdiStretchBlt <- NtGdiBitBlt
```
`WINEDEBUG=+seh`: `handle_syscall_fault code=c0000005 ... info[0]=0 info[1]=<source address>` (read fault on the source
bits). After that (`inst/182/out/gs-hang1-hang.txt`): one thread in `flush_window_surfaces -> window_surface_flush ->
window_surface_lock`, all others waiting for `surfaces_lock`.

## Reading (not verified beyond the stack)
`nulldrv_StretchBlt` gets the source bits from the source DC's `pGetImage`. For a window DC that is `windrv_GetImage`
(dibdrv/dc.c): it locks the DC's window surface and returns a pointer into the surface's pixels, the unlock happens in
`bits->free` after the copy. The pixels were unmapped during the copy although the surface was locked: the surface was
destroyed, i.e. its last reference went away, which the lock does not prevent. The DC's reference is dropped when
another thread updates that DC (window moved / resized / re-layered -> `invalidate_dce` -> `update_visible_region` ->
`set_visible_region` with the new surface) while the first thread is inside the blit. The fault is turned into a
return status by ntdll (172), so the surface mutex taken in `windrv_GetImage` is never released, and the next flush pass
blocks on it with `surfaces_lock` held.
What to check: whether win32u changes the surface of a DC that another thread is using (DC objects are not locked
against that), and what Windows does for a DC in use (its GDI objects are not thread-safe either, but there the second
user is the window manager, not the application).

## Repro
```
inst/182/x.sh start
inst/182/run.sh fix 1485 gs-hang 80 tests/r182/gdistress.exe 20 6 1 x      # "threads did not stop", stacks in out/gs-hang-hang.txt
```
