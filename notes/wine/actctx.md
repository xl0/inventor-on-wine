# Activation contexts (SxS) — checked at wine-11.18-218-g4e819f054dd

- Code: `dlls/ntdll/actctx.c` (all logic), kernel32/kernelbase are thin
  wrappers. Tests: `dlls/kernel32/tests/actctx.c` (big, manifest strings inline;
  START_TEST at the end).
- Process default context: `actctx_init()` → RtlCreateActivationContext from
  the exe's RT_MANIFEST resource 1 (or `<exe>.manifest`), stored in
  `process_actctx` (default `system_actctx`).
- Dependent assembly lookup: `parse_depend_manifests()` → `lookup_assembly()`
  probes appdir, the root manifest's dir, then each privatePath dir (each flat
  and `name\`, via `open_manifest_file()`: name.dll, then name.manifest).
- App config (`parse_app_config()`, called for every context before dependency
  lookup): `<root manifest minus .manifest>.Config`; sets `actctx->config`
  (RootConfigurationPath) when present, collects privatePath into
  `acl->private_path` (';'-joined). Windows rules: see issues/001.
- Windows AppDir for CreateActCtx from a manifest file is the manifest dir;
  Wine reports the exe dir (lookup still works: it also probes the manifest dir).
- `find_query_actctx()` resolves the handle for QueryActCtx. Windows: a NULL
  result from any mode (NULL handle, USE_ACTIVE with no frame, HMODULE/ADDRESS
  of a module without its own context) means the process default context for
  every class except Basic, which returns hActCtx NULL (fixed in issue 002).
- Note GetCurrentActCtx returns NULL when nothing is activated, on Windows too.
- CreateProcess fails with ERROR_SXS_CANT_GEN_ACTCTX if the new exe's context
  can't be generated (Windows does it parent side). Wine: check in kernelbase
  CreateProcessInternalW, same-arch children only (issue 004). The child's own
  actctx_init still silently falls back to the empty context.
- Wine's WinSxS lacks some in-box Windows assemblies (IsolationAutomation,
  SystemCompatible); Common-Controls 5.82 and 6.0 are provided by comctl32(_v6).
- `<comClass>`/`<clrClass threadingModel>`: case-insensitive; "Single" = No; empty/unknown
  (even with spaces) fails CreateActCtx with 14001. Absent: comClass No, clrClass Both (063).
- COM (070): an in-process call into another apartment runs in the caller's active context
  (or none); cross-process calls get none; nothing is captured at creation/marshal time.
  Wine: combase rpc.c carries it in dispatch_params (bypass path, STA and MTA). Windows also runs
  wndproc-dispatched cross-process calls into an STA under the context its OLE window was
  created in (Wine: not implemented). MFC's AFX_MANAGE_STATE doesn't activate a regular DLL's
  manifest context (probe: tests/actctx_comcall/mfc_state.c).
