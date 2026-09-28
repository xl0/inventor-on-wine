# 008 Inventor install fails with "Error 4005" (.adix package reader)
Status: fixed · Owner: worker · Branch: fix/008-wintrust-p7x-blob (wt/008, from master; wt/008-integ = integ + fix) · Found in: Inventor web installer, prefix `inv`, integ + fix/007 (wt/007-integ-build)

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

## Findings (worker)
- 0x8BAD0042 is `MSIX::Error::CertNotTrusted` (not XML): msix SDK
  `src/msix/PAL/Signature/Win32/SignatureValidator.cpp` found "Unknown signature
  origin". msix.dll (Autodesk build of the SDK, Win32 PAL) imports crypt32/wintrust.
- The Autodesk signing cert expired 2026-08-14 (payload signed 2026-02-10 with an
  RFC 3161 time-stamp). Chain policies then fail on Windows too (CERT_E_EXPIRED);
  the SDK's last resort `WinVerifyTrust(WTD_CHOICE_BLOB, P7x SIP
  {5598CFF1-68DB-4340-B57F-1CACF88C9A51}, whole .p7x)` is what passes on Windows.
- Windows (VM, `tests/p7x_winverifytrust.c`): real p7x -> S_OK, signer
  sftVerifyAsOf = TSA genTime, 1 counter-signer; timestamp stripped ->
  CERT_E_EXPIRED; bad "PKCX" magic -> TRUST_E_NOSIGNATURE. P7x SIP lives in
  AppxSip.dll (P7xSipGetSignedDataMsg etc.).
- Wine had three gaps: SoftpubLoadMessage FIXME for WTD_CHOICE_BLOB (0x57); no
  P7x SIP; WINTRUST_GetTimeFromSigner only knew szOID_RSA_counterSign. Plus
  crypt32: CMSG_CONTENT_PARAM of CMS (v3) SignedData with non-data content kept
  the eContent OCTET STRING wrapper (Windows: v3 always unwrapped, non-OCTET ->
  CRYPT_E_ASN1_BADTAG at Update; v1 returned as is unless szOID_RSA_data).
- Found 009 (SoftpubAuthenticode skips the chain policy for SHA-256 signer
  certs -> WinVerifyTrust S_OK for untrusted/expired chains); draft filed.

## Outcome
Commits on fix/008-wintrust-p7x-blob:
- crypt32: Decode the content of CMS signed messages as an OCTET STRING.
- wintrust: Get the signer verification time from RFC 3161 time-stamps.
- wintrust: Add a SIP for AppxSignature.p7x blobs. (in wintrust, registered by
  DllRegisterServer; Windows has it in AppxSip.dll)
- wintrust: Support WTD_CHOICE_BLOB in SoftpubLoadMessage.
Tests: wintrust softpub `test_wintrust_blob` (synthetic p7x from
`tests/p7x_gen.py`: own root, leaf valid in 2000, RFC 3161 token 2000-06-01;
result check todo_wine until 009), crypt32 msg v1/v3 content. VM x86_64+i386:
softpub 337/325 tests, msg 1078, 0 failures. Wine x86_64+i386: softpub 260,
msg 1077, 0 failures; full crypt32/wintrust suites clean. Unfixed Wine fails the
new checks (0x57, no signer; v3 content size 7).
Real flow (prefix inv on wt/008-integ-build): LP/Anark adix now pass, install
stops at "Inventor Core 2027" Error 4000 -> issue 010 (RegLoadKey of binary hive).
