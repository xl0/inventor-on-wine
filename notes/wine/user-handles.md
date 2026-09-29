# USER handles (HWND, HMENU, ...) — checked at wine integ 061fa687382

- Handle = `(index << 1) + FIRST_USER_HANDLE + (generation << 16)`
  (`server/user.c:entry_to_handle`). A freed index goes onto a LIFO free list
  (`free_user_entry`), so an app that churns windows reuses the same few
  indices and bumps their generation by one per allocation.
- Upstream Wine wraps the generation at 0xffff (`alloc_user_entry`: 0 and 0xffff
  skipped), so after ~32k reuses of an index HWNDs get bit 31 set (0x8000xxxx+).
  Windows 11 never does: its uniq wraps 0x7ffe -> 1, HIWORD stays <= 0x7ffe
  (`tests/hwnd_signext.c`, 2.5M windows, 76 wraps, VM). Same for every USER
  type, which share one table and slot (menu, icon, accel, hook, HDWP, HIMC;
  `tests/user_handle_uniq.c`, 64- and 32-bit). Fixed by 059 (wrap at 0x7ffe).
- Without 059, such handles come out in two forms on 64-bit: `wine_server_ptr_handle`
  sign-extends (CreateWindowEx returns 0xffffffff8xxxxxxx), while win32u's
  `USER_HANDLE_FROM_INDEX` (`get_user_entry` -> "full" handle) zero-extends
  (0x000000008xxxxxxx). The zero-extended form reaches window procs for
  WM_MOVE, WM_SHOWWINDOW, WM_DESTROY, WM_NCDESTROY and GetWindow(GW_CHILD);
  WM_NCCREATE/WM_CREATE/WM_SIZE/WM_WINDOWPOSCHANGED get the sign-extended one.
  Apps keying maps by HWND (MFC's permanent handle map) miss the window.
- `is_valid_entry_uniq` accepts HIWORD 0 and 0xffff as wildcards, and HIWORD()
  ignores bits 32-63, so IsWindow accepts both forms.
