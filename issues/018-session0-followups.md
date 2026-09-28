# 018 Session-0 services: follow-ups (low)
Status: fixed · Owner: worker-018 · Branch: fix/018-{global-locks,wineboot-setcb,wts-sessions,service-winsta} (all on integ df07207e58d; combined: wip/018-all) · Found in: 014 + its review

Leftovers from moving services into session 0 (014, merged in integ):
- WTSEnumerateSessions / WTSQuerySessionInformation list only the caller's
  session; Windows lists session 0 and the console session, so a service can
  find the active user session.
- Service window station is `__wineservice_winstation`; Windows uses
  `Service-0x0-3e7$`.
- wineboot enables SeTcb in its own token and leaves it enabled, so its
  children (RunOnce, rundll32 installs) inherit it; disable after starting
  services.exe.
- Wine-internal locks around shared registry/file state use unqualified names
  and are now per session (crypt32_root_semaphore, __WINE_WINSPOOL_MUTEX__,
  __WINE_FUSION_CACHE_MUTEX__, sxs cache mutex, PowerProfileRegistrySemaphore,
  display_device_init, __wine_clipboard_<winsta>): a service and a user app
  can race. Consider Global\ names where the protected state is global.
- Device broadcasts: 014 review added a user32 cross-session broadcast; on
  Windows the PnP manager delivers per session — upstream may want it in
  plugplay/mountmgr instead.
- Minor server nits: set_token_session_id checks privilege before handle,
  doesn't require TOKEN_ADJUST_DEFAULT, allows changing an in-use token's
  session.

## Outcome
- **Named locks** (fix/018-global-locks, one commit per module): `Global\`
  for crypt32_root_semaphore (HKLM root import), __WINE_WINSPOOL_MUTEX__
  (HKLM printers init), __WINE_FUSION_CACHE_MUTEX__ (GAC),
  __WINE_SXS_CACHE_MUTEX__ (winsxs); win32u display_device_init back to
  `\BaseNamedObjects\` (HKLM display devices; no other module opens it any
  more, which was why it moved to the session dir in 94ece19f8ec).
  Left alone: PowerProfileRegistrySemaphore (only guards two HKLM reads;
  Windows 11 doesn't create the name at all, VM probe: neither
  `\BaseNamedObjects\` nor `\Sessions\1\...` after GetActivePwrScheme);
  `__wine_clipboard_<winsta>` (clipboard is per window station, so per
  session is right); HKCU-state locks (__wine_dinput_reg_mutex,
  winemenubuilder_semaphore) are per user on Windows. Note Wine services
  share the user's HKCU, unlike LocalSystem.
- **wineboot SeTcb** (fix/018-wineboot-setcb): restores the previous state
  right after setting the services token session. RunOnce child: SeTcb
  enabled 1 -> 0; services keep it (WTSQueryUserToken still ok there).
- **WTS** (fix/018-wts-sessions): WTSEnumerateSessions lists 0 "Services"
  (WTSDisconnected) + console "Console" (WTSActive);
  WTSQuerySessionInformation answers for session 0 (disconnected, empty
  user/domain, "Services") and the console, other ids fail
  ERROR_FILE_NOT_FOUND. VM ground truth in tests/svc_session.c output
  (service and user process see the same; LogonTime set only for console).
  Not done: WTSSessionId / WTSWinStationName classes (still unimplemented).
  wtsapi32 test: todo_wine removed, new test_services_session; VM + Wine
  x86_64/i386 pass.
- **Window station** (fix/018-service-winsta): `Service-0x0-3e7$`. win32u
  keyed "is service" (no explorer desktop, virtual monitor only) off the old
  name in two places; both now use is_service_process() with the new name.
  Without that, wineboot -u left an explorer /desktop behind (wineserver -w
  never returned).
- Server nits and the device-broadcast placement: not touched.
- regress (crypt32 winspool.drv fusion sxs win32u wtsapi32 advapi32 user32,
  both arches) vs integ df07207e58d: 0 worse of 124 units.
