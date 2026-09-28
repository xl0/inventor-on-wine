# Sessions — checked at wine-11.18-218-g4e819f054dd + fix/014

- Session id is a token property: `server/process.c:create_process` sets
  `process->session_id` from the (inherited or passed) token. First process
  gets `default_session_id` (1). PEB->SessionId (init_first_thread reply),
  ProcessSessionInformation, SystemProcessInformation, TokenSessionId all
  read it.
- With fix/014, wineboot `start_services_process` creates services.exe with
  a session-0 token (SeTcb enabled, `NtSetInformationToken(TokenSessionId)`),
  so services.exe and everything it spawns is session 0, like Windows.
- Namespaces follow PEB->SessionId (`kernelbase/sync.c:
  BaseGetNamedObjectDirectory`, user32/win32u winstations dir):
  session 0 -> `\BaseNamedObjects`, `\Windows\WindowStations`;
  session N -> `\Sessions\N\...` (`server/directory.c:create_session`).
  Anything shared between services and user processes must use `Global\`
  (e.g. SVCCTL_STARTED_EVENT; with fix/018 also Wine's HKLM/GAC/winsxs locks).
- `WTSGetActiveConsoleSessionId` = KUSER_SHARED_DATA.ActiveConsoleId,
  written by wineboot `create_user_shared_data` (its own session, 1).
- Service processes become Wine "system" processes in
  StartServiceCtrlDispatcher (sechost), so they don't keep wineserver alive.
- Go/.NET IsWindowsService(): parent (InheritedFromUniqueProcessId in
  SystemProcessInformation) named services.exe with SessionId 0.
- BroadcastSystemMessage (BSM_ALLDESKTOPS/ALLCOMPONENTS) enumerates only the
  caller's session; with fix/014 a session-0 caller also reaches the console
  session's WinSta0 (device-change broadcasts from plugplay/mountmgr).
- Services' window station: `Service-0x0-3e7$` with fix/018
  (`__wineservice_winstation` before). win32u `is_service_process()` keys
  off that name (no explorer desktop, virtual monitor only); keep
  programs/services and win32u in sync.
- WTSEnumerateSessions with fix/018: 0 "Services" (disconnected) + console.
