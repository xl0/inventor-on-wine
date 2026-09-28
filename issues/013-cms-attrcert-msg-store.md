# 013 WinVerifyTrust fails (CRYPT_E_ASN1_BADTAG) on Microsoft-signed .NET runtime → Inventor install "Error 15"
Status: fixed · Owner: worker · Branch: fix/013-crypt32-attrcert (wt/013, from master) + fix/013-integ-wintrust (wt/013-integ, integ 35e8f24953f + cherry-pick) · Found in: Inventor web installer, prefix `inv`, integ d4d52733ba7 + fix/010 (wt/010-integ-build)

## Observed
- With 010 fixed, Inventor Core installs; the bundle ends with "Installation
  incomplete" (DWG TrueView, Electrical Catalog Browser optional components failed;
  dialog left open, not clicked).
  ![Installation incomplete](attachments/013-install-incomplete.png)
- Install.log 23:17:19: `SignatureUtils::VerifyCertificate WinVerifyTrust returns
  error code: -2146881269` (0x8009310B CRYPT_E_ASN1_BADTAG) for
  `%TEMP%\{447AF3F0-...}\3rdParty\x64\dotNet\100\windowsdesktop-runtime-10.0.9-win-x64.exe`
  → ".NET Windows Desktop Runtime 10.0.9 (x64)" Error 15 (Summary.log), needed by
  TrueView and Inventor. Also `AceInvAddIn-ca.msi` returned 1603 (not looked at;
  possibly a consequence).
- Regression from the integ wintrust time-stamp work: `wt/010-work/wvt.exe FILE`
  (WinVerifyTrust GENERIC_VERIFY_V2, file, no revocation; source
  `wt/010-work/wvt.c`, copy of the exe at `wt/010-work/wdr.exe`):
  VM → 0; old `build/` (integ incl. 007+008) → 0; integ d4d52733ba7 → 0x8009310B.

## Cause (first look)
- The Authenticode signature's RFC 3161 token (unsigned attr 1.3.6.1.4.1.311.3.3.1,
  CMS v3 SignedData from "Microsoft Time-Stamp Service") has, inside its
  `certificates [0]` SET, a `[1]` CertificateChoices element (v1AttrCert per
  RFC 5652) after the X.509 certs.
- crypt32 hands it out as a CRL (CMSG_CRL_COUNT_PARAM/CMSG_CRL_PARAM); in
  `CRYPT_MsgOpenStore` `CertAddEncodedCRLToStore` fails to decode it
  (tag a1, expected 06) and the whole `CertOpenStore(CERT_STORE_PROV_MSG)` fails.
  The caller is the new token verification in wintrust softpub.c
  (8d07e7a6cf8 "Verify RFC 3161 time-stamp tokens"), so WinVerifyTrust fails.
- Trace: `WINEDEBUG=+crypt,+wintrust,+cryptasn` (kept in `wt/010-work/wvt-trace.log`).

## Task
Decode CMS SignedData `certificates` choices other than plain certificates the
way Windows does (skip / don't count as CRLs), and/or make the msg store not fail
on them; conformance test with a synthetic token. Check against current integ tip.

## Windows ground truth (VM; `wt/013-work/cmsprobe.c`, token variants from `variants.py`)
- CMS v3 SignedData `certificates`: plain SEQUENCE -> CMSG_CERT_PARAM; any `[1]`
  (constructed, whatever its content, even `a1 00`) -> CMSG_ATTR_CERT_PARAM (re-tagged
  0x30); `[0]`, `[2]`, `[3]`, primitive `[1]`, INTEGER etc. silently dropped. Position
  (first/middle/last) doesn't matter; store opens with only the X.509 certs.
- PKCS #7 v1 SignedData: every element counts as a certificate, raw (a1/a2 included),
  and CertOpenStore(MSG) then fails (CRYPT_E_ASN1_BADTAG).
- A junk SEQUENCE in certificates is counted as a cert and makes the store fail
  (CRYPT_E_ASN1_EOD); Wine skips such certs (1875620466d, bug 45757) - left alone.
  Junk CRL: store fails on both (Windows EOD, Wine CRYPT_E_ASN1_CORRUPT).
- WinVerifyTrust when the token's store can't be opened (runtime exe / real p7x
  with a junk CRL in the token): result = the store error, step 16
  (MSG_COUNTERSIGINFO) TRUST_E_TIME_STAMP, step 33 (SIGPROV) = the error, signer
  asof now, 0 counter signers. Wine: same result/SIGPROV/asof/counters; no step 16
  (same gap as 009's bad-imprint case), no CERTPROV after the SIGPROV failure (known).

## Outcome
Root cause predates us (master): CRYPT_AsnDecodeArray's "skip" path (used for
certificates CertCreateCertificateContext rejects, 1875620466d) didn't add the
skipped item to the decoded length, and its second pass decoded skipped items into
the next slot. So a trailing `[1]` attr cert was re-read as the `crls [1]` field
(-> CertAddEncodedCRLToStore BADTAG); leading/middle ones broke CryptMsgUpdate.
Integ's token verification (009) only made it visible.
1. crypt32: Correctly skip items the array decoder is asked to skip. (+ msg.c test:
   CMS v3 with `[1]`, cert, `[2]`: 1 cert, 0 CRLs, 1 signer, store opens;
   CMSG_ATTR_CERT_COUNT_PARAM 1 is todo_wine - Wine doesn't expose attr certs.)
No wintrust change needed (behaves as above).
Tests: crypt32 msg VM x86_64/i386 1082, 0 failures; Wine 0 failures (fails without
the fix); crypt32 suite on Wine only the 2 known chain failures. Integ+fix, Wine
x86_64/i386: wintrust asn/crypt/register/softpub 0 failures.
Regression checks (integ+fix): windowsdesktop-runtime-10.0.9 S_OK (asof = token
genTime, 1 counter); InvCoreLP/InvAnark p7x S_OK; p7x_noroot CERT_E_CHAINING,
real_ourtsa TRUST_E_TIME_STAMP, p7x_ts CERT_E_UNTRUSTEDROOT (as Windows).
Left: attribute certs not exposed (CMSG_ATTR_CERT_*), v1 vs v3 difference not
modelled, junk-cert skipping differs from Windows, step 16 not set on token errors.
