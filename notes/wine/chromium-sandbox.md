# Chromium/Edge Windows sandbox under Wine — checked at integ 220b08678ea + fix/026

- Spawn path (sandbox/win/src/broker_services.cc): PreSpawnTarget on the process
  launcher thread (MakeTokens -> UpdateDesktopIntegrity -> InitJob -> proc thread
  attributes), then CreateProcessAsUserW suspended. Any failure before
  CreateProcessAsUser is silent: no log even with --v=3, the child is just never
  created (Edge renderers: blank pages).
- Finding the failing call: +relay with a RelayExclude of the hot functions
  (critical sections, heap, SRW, TLS, last error; ~1.8 GB for 60 s of Edge), then
  follow the launcher thread (the one calling CreateRestrictedToken / CreateJobObjectW).
  A missing export shows up as a failing GetProcAddress right before it gives up.
- Edge 154 renderers: lowbox app container, token built by kernelbase
  CreateAppContainerToken (fix/026). Wine only fakes it: the token is a plain primary
  copy, nothing enforces the sandbox. WebView2 renderers don't use an app container.
- Harmless noise: NtFilterToken "restricting sids not yet implemented",
  TokenIntegrityLevel set stub, CreateProcess "Unsupported attribute" 0x2000e/0x2001a (child policy, component filter: ignored),
  TokenSecurityAttributes query unhandled.
- AppContainer profiles (Chromium app_container_base.cc CreateProfile; Edge 154's on-device-model
  service `cr.sb.odm<hash>`, ~3 min after start): userenv DeriveAppContainerSidFromAppContainerName
  (delay-imported by msedge.exe: missing = 0xc06d007f crash) + kernelbase AppContainerRegisterSid /
  UnregisterSid / LookupMoniker / FreeMemory (GetProcAddress + CHECK: missing = int3 crash). Mapping in
  HKCU\Software\Classes\Local Settings\...\AppContainer\Mappings\<SID>. Implemented in fix/090.
  Then %LOCALAPPDATA%\Packages\<name>\AC gets a low label via SetNamedSecurityInfo (fixed in 094), then
  CreateProcessAsUser with PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES (fix/095: kernelbase makes the lowbox token,
  needs the Mappings registration, rewrites LOCALAPPDATA/TEMP/TMP to Packages\<moniker>\AC). The service then runs
  briefly and exits (logs "Edge LLM: Error getting component directory", D3D12 fence failure): normal, no disconnect.
- Since fix/095 lowbox tokens are real in wineserver (TokenIsAppContainer, package SID, capabilities, low IL,
  2 privileges) but nothing enforces app container access checks or named-object isolation.
- After a server/protocol.def change, rebuild everything (`make`), not just server/ntdll: modules with
  SERVER_START_REQ (ntoskrnl, win32u, ...) keep stale request numbers -> drivers fail with c0000022,
  crashes everywhere (cost an afternoon in 095).
- Crashpad dumps: the delay-load DelayLoadInfo (exception param 0) lies on the captured stack; the
  DLL/function name pointers are RVAs into the exe's .rdata (read the strings from the file).
