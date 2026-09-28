# 014 Go services never start: service processes run in session 1, not 0
Status: open (draft) · Owner: - · Branch: - · Found in: inv-vm transplant (build/ at integ) — verify on real install path before fixing

## Observed
- `AdskLicensingService` and `Autodesk CER Service` (auto-start, OWN_PROCESS)
  fail: `err:service:process_send_start_message service L"AdskLicensingService"
  failed to start` (1053), same for `net start`. With `ServicesPipeTimeout`
  raised to 120 s they still never call StartServiceCtrlDispatcher.
- The processes do run: AdskLicensingService.log shows the HTTP server up and
  AdskLicensingAgent registered; winedbg shows the main thread in the app's own
  IOCP loop. Inventor then says "Unable to connect with the Autodesk Desktop
  Licensing Service"; SCM kills the service process after the timeout.
- Both exes are Go (go1.26) and contain
  `golang.org/x/sys/windows/svc.IsWindowsService`. That function (public Go
  source) returns true only if the parent process (NtQuerySystemInformation
  SystemProcessInformation, matched by InheritedFromUniqueProcessId) is named
  `services.exe` **and has SessionId 0**. Under Wine every process, including
  services.exe, is in session 1 (`tasklist`; `server/process.c` takes the
  session from the token, `default_session_id`), so Go services think they run
  interactively and never connect to the SCM.
- Workaround for testing: start AdskLicensingService.exe by hand as a console
  process; Inventor then gets past the licensing check.

## Windows ground truth
VM (`Get-Process ... SessionId`): services.exe, AdskLicensingService.exe and
cer_service.exe are in session 0, explorer.exe (autologon desktop) in session 1.

## Task
Services started by services.exe (and services.exe itself) should report
session 0 (process SessionId, token session). Check what else keys off the
session (window stations/desktops, ProcessIdToSessionId, WTS*), since session-0
processes normally have no interactive desktop. Cheap repro: a C service that
logs its parent's SessionId from SystemProcessInformation.
