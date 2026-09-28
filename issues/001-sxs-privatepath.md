# 001 SxS: native app config privatePath probing
Status: fixed · Owner: worker · Branch: fix/001-sxs-privatepath · Found in: Inventor web installer (Setup.exe)

## Symptom
ODIS web installer `Setup.exe` fails to load its wxWidgets DLLs (rc=53, import
failures). They live in private assemblies `ODIS/odis.bs.win`, `ODIS/odis.bs.wx`
and are found on Windows only via `Setup.exe.config`:
`<configuration><windows><assemblyBinding xmlns="urn:schemas-microsoft-com:asm.v1">
<probing privatePath="ODIS"/>`. Workaround in prefix `inv`: assemblies copied
beside Setup.exe. Real files: `inst/webinstall/`.

## Windows ground truth (`tests/sxs_probe/`, windows.txt)
- Found: `sub` (both `sub\name\name.manifest` and `sub\name.manifest`),
  `a;b` list (missing dirs skipped), nested `x\y`, `..\lib`.
- App dir wins over privatePath.
- Not used (err 14001): no config, config named `child.config` instead of
  `child.exe.config`, missing xmlns, probing under `<runtime>` (the .NET one).
- Wine: never reads `<exe>.config` at all.

## Task
Implement in ntdll/actctx.c for the process default context (and whatever else
Windows applies it to — check CreateActCtx with an exe manifest path, and
whether ActivationContextDetailedInformation reports RootConfigurationPath).
Add a kernel32 actctx conformance test that passes on the VM and on Wine.

## Findings (probe `tests/sxs_probe/cfg.c`, results `cfg_windows.txt`, `cfg_windows2.txt`)
- Config path = root manifest path minus a trailing `.manifest`, plus `.Config`:
  `app.exe` (resource) / `app.exe.manifest` → `app.exe.Config`, `x.manifest` → `x.Config`,
  `noext` → `noext.Config`, `foo.xml` → `foo.xml.Config`. Applies to every context,
  CreateActCtx included, not just the process default.
- RootConfigurationPath: type WIN32_FILE + path when the file exists, else type NONE.
- privatePath is relative to the root manifest dir (for CreateActCtx from a file,
  Windows' AppDir = manifest dir, even with ACTCTX_FLAG_APPLICATION_NAME_VALID).
- Probed after the app dir. Multiple `<probing>`/`<assemblyBinding>` accumulate.
  `/` ok, `..\x` ok, `.\sub` ok, empty entries ok, spaces not trimmed.
- Element names case-sensitive; `<windows>` must be directly under `<configuration>`;
  assemblyBinding without asm.v1 xmlns is ignored.
- Fatal (context creation fails, 14001) even with no dependencies: malformed XML,
  absolute privatePath, trailing `\` or `/` on an entry, and one of the constructs
  in the `r2_extra` case (unknown element, extra attr, or `<dependentAssembly>` —
  not bisected).

## Outcome
Commits on fix/001-sxs-privatepath: factor out manifest text decoding;
ntdll `parse_app_config()` + privatePath loop in `lookup_assembly()`;
kernel32 test `test_app_config()` (passes on Win11 x64/x86 and Wine).
Deliberately more permissive than Windows: absolute/trailing-slash entries and
unknown elements/attrs are ignored rather than fatal. Malformed XML is fatal.
Real case: Setup.exe loads wx DLLs from ODIS\odis.bs.wx and proceeds to download
and launch the installer.
Review Q (manifest.info NULL in parse_app_config): unreachable. It only runs after
a successful parse of the root manifest, and parse_manifest() always sets info
(strdup of the file name, or the module file name for resources; any failure
aborts creation). system_actctx is static and never passes through
RtlCreateActivationContext. lookup_assembly() already relies on the same thing.
No guard added.
