# 008 Inventor install fails with "Error 4005" (.adix package reader)
Status: open · Owner: - · Branch: - · Found in: Inventor web installer, prefix `inv`, integ + fix/007 (wt/007-integ-build)

## Observed
- After 007 (ODIS package now installs, exit 0), Install at ~5% ends in
  "Install error ... Error 4005" (UI dialog left open with Exit).
- Summary.log: 4005 for "Inventor Core 2027 Language Pack - English" and
  "Inventor Anark 2027" — the first two `.adix` packages (MSIX-like, handled by
  ODIS `Setup\plugins\adixhandler.dll`, embedded MsixCoreLib / msix SDK).
- Install.log (`%LOCALAPPDATA%\Autodesk\ODIS\Install.log`, 22:02:57):
  `MsixCoreLib::PopulatePackageInfo::GetPackageInfoFromPackage ... appxFactory->CreatePackageReader(packageStream, &packageReader) error code: -1951596478`
  = 0x8BAD0042 (msix SDK error range; believed to be an XML-parser error,
  unverified — the SDK parses AppxManifest/BlockMap via MSXML6 on Windows).
- Rollback then logs `CreateStreamOnFileUTF16 ... -1951596543` (0x8BAD0001)
  for the not-installed packages; probably just a consequence.
- VM installed the same packages fine (inst/vmlogs).

## Repro material
- Payloads still in the prefix:
  `%TEMP%\{447AF3F0-5A11-3D54-B8C1-9D1C88C26148}\x64\InvAnark\InvAnark.adix`
  (643 MB) and `...\x64\en-US\InvCore\InvCoreLP.adix`.

## Task
Find the failing Wine API behind CreatePackageReader (msxml6 DOM/SAX? xmllite?
urlmon/shlwapi stream?), e.g. `WINEDEBUG=+msxml,+xmllite` or relay on
install_manager.exe; adixhandler.dll is Autodesk code (tools/decomp.sh OK).

## Coordinator notes
- The msix SDK is open source (github.com/microsoft/msix-packaging, MIT):
  reading its source is fine and much better than decompiling; its error codes
  are in src/inc/public/MsixErrors.hpp (or similar).
- build/ is now integ incl. 007; prefix inv was switched to it (wineboot -u).
