# 016 GeneralizedTime with 1-2 fraction digits fails to decode → VC++ redist signature check "Error 15"
Status: open · Owner: - · Branch: - · Found in: Inventor web installer, prefix `inv`, build/ at integ 2d9c0d96550 (001–013)

## Observed
- Re-run of the web installer (all optional components unticked; Content
  Libraries already installed from the previous run) ends ~1 min into install:
  "Install error: Inventor Professional 2027 — The install couldn't finish. Error 15"
  (dialog left open, Exit not clicked).
  ![Error 15](attachments/016-error-15-vcredist.png)
- Install.log 2026-09-28T00:29:04:
  `SignatureUtils::VerifyCertificate WinVerifyTrust returns error code: -2146881277`
  (0x80093103 CRYPT_E_ASN1_EOD), then `Failed to validate the signature of the package:
  ...\{447AF3F0-...}\3rdParty\x64\VCRedist\2022\VC_redist.x64.exe`; rollback follows.
- `tests/cms/wvt.c` (WinVerifyTrust GENERIC_VERIFY_V2, file, no revocation) on the
  bundle's third-party payloads, Wine (integ 2d9c0d96550) vs VM:
  - VC_redist.x64.exe: Wine 80093103 (step 33 SIGPROV, 0 counter signers);
    VM S_OK, 1 counter signer, asof 2025-11-22 00:22.
  - aspnetcore-runtime-10.0.9-win-x64.exe: Wine 80093103 (would fail next).
  - VC_redist.x86.exe, windowsdesktop-runtime-10.0.9, WebView2, and the Autodesk
    exes/msis (ASC.exe, InvAcNGEN.exe, AceInvAddIn-ca.msi, DCLibrary64.msi,
    InstallCustomAction.exe): S_OK on Wine.
- The failing files' RFC 3161 TSTInfo genTime has 1 or 2 fraction digits:
  VC_redist.x64 `20251122002231.7Z`, aspnetcore `20260520203719.78Z`;
  the passing ones have 3 (`...14.007Z`, `...30.456Z`).
  `+crypt` trace: the last call before the failure is
  `CryptDecodeObjectEx(X509_CHOICE_OF_TIME, 19 bytes)` → 0, from integ's token
  verification (dlls/wintrust/softpub.c, 009).

## Windows ground truth (VM, `tests/cms/gentime.c`: X509_CHOICE_OF_TIME on a GeneralizedTime)
| input | VM | Wine |
|---|---|---|
| `20251122002231Z` | ok .000 | ok .000 |
| `20251122002231.7Z` | ok .700 | FAIL 80093103 |
| `20251122002231.96Z` | ok .960 | FAIL 80093103 |
| `20251122002231.969Z` | ok .969 | ok .969 |
| `20251122002231.9691Z` | ok .969 | ok .969 |
| `20251122002231.7` | ok .700 | ok .007 (wrong) |

## Suspected cause (unverified by a fix)
Pre-existing master bug in `dlls/crypt32/decode.c` `CRYPT_AsnDecodeGeneralizedTime`
(Juan Lang 2007): the fraction is read with `digits = min(len, 3)` where `len` still
includes the trailing `Z`/zone, so `.7Z` tries to read `Z` as a digit → fail; and the
fraction is taken as an integer count of ms (`.7` → 7, not 700). Integ's time-stamp
token verification (009) only made it reachable for Authenticode.

## Task
Parse the fraction as Windows does (digits only, up to 3 significant, scaled to ms;
extra digits ignored), then the zone. Conformance test in crypt32 encode.c time tests
(values above). Rerun `wvt` on VC_redist.x64 / aspnetcore payloads
(copies: prefix inv `%TEMP%\{447AF3F0-5A11-3D54-B8C1-9D1C88C26148}\3rdParty\x64\`).
