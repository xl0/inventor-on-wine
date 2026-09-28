# 018 Session-0 services: follow-ups (low)
Status: open (draft, low) · Owner: - · Branch: - · Found in: 014 + its review

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
