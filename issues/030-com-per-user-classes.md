# 030 COM activation ignores per-user class registrations
Status: fixed · Owner: worker 030 · Branch: fix/030-com-merged-classes (wt/030, on fix/028);
final series fix/028-030-series (wt/030s) · Found in: 028 (merged HKCR)

combase/ole32 kept their own HKLM-only classes root, so COM servers registered only under
HKCU\Software\Classes were invisible to CoCreateInstance, even with 028's merged HKCR view.
Worse with 028: writes through HKCR update an existing HKCU key, so a stale HKCU copy of a
class plus a re-registration (`regsvr32 quartz`) left the class HKCU-only -> 0x80040154.

## Windows ground truth (tests/com_peruser.c, Win11 VM; limited = RunLevel Limited task)
- Non-elevated (TokenElevationTypeLimited): COM uses the merged view. Per-user-only CLSID
  (InprocServer32), ProgID, Interface (CoGetPSClsid), OleRegGetUserType all resolve; HKCU
  overrides HKLM (InprocServer32, Interface\ProxyStubClsid32, user type name); a partial HKCU
  key (no InprocServer32) falls back to HKLM subkeys. Still true with RegOverridePredefKey(HKCR).
- Elevated (winrun, RunLevel Highest): COM ignores per-user registrations completely (all of the
  above: not registered / HKLM values), while registry HKCR stays merged.
- COM caches per process: a class/interface looked up once keeps its answer (probe per fresh process).
- RegOpenUserClassesRoot: tagged (`h & 3 == 2`) merged view (HKLM-only and HKCU-only keys open),
  unaffected by RegOverridePredefKey(HKCR). Wine had a semi-stub returning HKEY_CLASSES_ROOT.
- Wine processes are limited by default (elevated only with a requireAdministrator /
  highestAvailable manifest), so the merged path is the common one.

## Fix
- kernelbase: RegOpenUserClassesRoot returns the tagged HKCU classes key (028 merges it with HKLM);
  Wine's single user, token ignored.
- combase + ole32: the private root is RegOpenUserClassesRoot (non-elevated) or
  \Registry\Machine\Software\Classes (elevated); lookups go through RegOpenKeyExW/RegCreateKeyExW
  (merged, KEY_WOW64_* handled by kernelbase). Drops the NtCreateKey wrapper and the manual
  Wow6432Node handling. Still ignores RegOverridePredefKey (the reason for the private root).
- advapi32/tests: test_redirection no longer RegDeleteKey()s HKCR\Interface: with 028 it deleted
  the empty HKCU Wow6432Node\Interface marker, and 32-bit test_redirection then failed in the
  same prefix.

Series (fix/028-030-series on master): server Wow6432Node, advapi32/tests test_redirection,
kernelbase merged HKCR (fixup folded), shlwapi/tests, kernelbase RegOpenUserClassesRoot,
combase, ole32. Every commit builds (touched modules); final tree == fix/030 tip.

## Results
- Wine: ole32 compobj, advapi32 registry: 0 failures x86_64/i386 (new checks fail without the fix).
- VM: compobj 0 failures elevated + limited, both arches; registry 0 failures elevated
  (limited i386: pre-existing RegSetKeySecurity access-denied, unrelated).
- regress.sh integ(6adad91)+series vs integ baseline, 13 modules (advapi32 kernelbase ole32
  combase oleaut32 rpcrt4 shell32 shlwapi msi quartz urlmon mshtml wbemprox), both arches:
  0 worse of 258.
- prefixes/inv registry snapshot (user/system.reg) in two copies of inv-net48, master vs
  integ+series, 126 HKCU CLSIDs + 3 HKCU TypeLibs (wt/030-inv/): classes resolved on master
  resolve to the same file; 121 per-user-only classes now resolve (Autodesk DLL paths, absent in
  the copy); no lookup got worse; TypeLib acETransmit16 now found (028).

## Open / follow-ups
- Wine's Wow6432Node redirection relies on marker subkeys existing; the HKCU ones are empty and
  get deleted by anyone deleting empty HKCR keys (e.g. MSI cleanup of empty parents), silently
  switching 32-bit per-user classes to the shared view. Not fixed here.
