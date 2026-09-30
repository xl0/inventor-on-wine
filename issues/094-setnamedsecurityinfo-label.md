# 094 SetNamedSecurityInfo(LABEL_SECURITY_INFORMATION) fails with ERROR_ACCESS_DENIED
Status: open (draft) · Owner: - · Branch: - · Found in: 090 (wt/090 = integ 1d006ebdafb + fix/090)

## Symptom
Edge 154 (msedge.exe, Chromium sandbox `app_container_base.cc` CreateAppContainerDirectory) can't create
the AppContainer profile dir for its on-device-model service (`cr.sb.odm<hash>`, ~3 min after start):
chrome_debug.log `Failed to create the AppContainer profile directory for cr.sb.odm...`, then it unregisters
the SID and the service launch fails (`Unexpected on_device_model service disconnect`). No crash (since 090).

## Evidence
`tests/ac_profile_dir.c` (Chromium's steps on %TEMP%\...\AC): Win11 all 0; Wine
`SetNamedSecurityInfo DACL|LABEL` and `LABEL` alone -> 5 (ERROR_ACCESS_DENIED); DACL alone and
SetSecurityInfo on a handle opened with READ_CONTROL|WRITE_DAC|WRITE_OWNER -> 0.
Cause (by reading Wine): advapi32 SetNamedSecurityInfoW maps OWNER/GROUP -> WRITE_OWNER, DACL -> WRITE_DAC,
SACL -> ACCESS_SYSTEM_SECURITY, but not LABEL_SECURITY_INFORMATION (Windows needs WRITE_OWNER for it),
so the handle lacks the right. Probably one line plus an advapi32 test.
