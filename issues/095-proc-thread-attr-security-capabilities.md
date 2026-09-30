# 095 UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES) unsupported
Status: open (draft) · Owner: - · Branch: - · Found in: 094 (wt/094 = integ 201dbe77eca + fix/094)

## Symptom
Edge 154's on-device-model service (`cr.sb.odm<hash>`, AppContainer sandbox, ~3 min after start) never
starts: chrome_debug.log `Unexpected on_device_model service disconnect; reason: 0`, and crashpad writes a
DumpWithoutCrashing report (exception 0x0517a7ed, browser launcher thread) in `<user-data-dir>\Crashpad\reports`.
Since 094 the AppContainer profile dir is created fine.

## Evidence
Launcher thread stderr right before: NtCreateLowBoxToken (x2), NtSetInformationJobObject stub,
`validate_proc_thread_attribute Unhandled attribute 26` and `Unhandled attribute 9`.
Attribute 9 = PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES (SECURITY_CAPABILITIES: AppContainer SID +
capabilities). kernelbase validate_proc_thread_attribute returns ERROR_NOT_SUPPORTED for it, so
UpdateProcThreadAttribute fails and Chromium gives up before CreateProcess (26 = ALL_APPLICATION_PACKAGES_POLICY,
also unhandled but Chromium tolerates that one elsewhere: renderers launch).

## Task
Accept the attribute (size check) and have CreateProcess run the child with a lowbox token built from it
(like CreateAppContainerToken, fix/026/090), or at least not fail. Ground truth on the VM first.
