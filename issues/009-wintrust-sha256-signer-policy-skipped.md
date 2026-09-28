# 009 WinVerifyTrust skips the chain policy for SHA-256-signed signer certs
Status: fixed · Owner: worker · Branch: fix/009-wintrust-signer-policy (wt/009, from integ) · Found in: issue 008 conformance test (wintrust softpub)

## Observed
- `SoftpubAuthenticode` (dlls/wintrust/softpub.c) reads the signer cert's
  `CERT_SIGNATURE_HASH_PROP_ID` into `BYTE hash[20]` for the Disallowed-store
  check. For a cert signed with sha256RSA the hash is 32 bytes: the call fails
  (ERROR_MORE_DATA), `ret = FALSE` with `policyStatus.dwError` still 0, so the
  step error stays 0 and WinVerifyTrust returns S_OK without ever running
  `CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE)`.
- I.e. any signature whose leaf is SHA-256-signed (practically all current
  Authenticode) verifies as trusted on Wine, even with an untrusted root or an
  expired cert. Security-relevant.

## Evidence / repro
- fix/008 test `test_wintrust_blob` (wintrust/tests/softpub.c): synthetic
  P7x blob chaining to an untrusted test root. Windows: CERT_E_UNTRUSTEDROOT
  (0x800b0109); Wine: S_OK. The check is `todo_wine` there.
- Changing the buffer to 64 bytes makes that check pass, and the whole softpub
  suite still passes (x86_64).

## Task
Fix the Disallowed check (size the buffer for the largest hash, or query the
size); make a failed property lookup not silently skip the policy. Remove the
`todo_wine` in `test_wintrust_blob`. Probably also worth a PE-file test with
a SHA-256 cert.

## Coordinator addition (same theme: wintrust too lenient)
fix/008 (merged in integ) takes the signer verification time from an RFC 3161
time-stamp token without verifying it (FIXME in softpub.c
WINTRUST_GetTimeFromTimeStampToken): token signature/chain, and that its
messageImprint matches the signer's encryptedDigest. A forged token can make an
expired cert pass. Handle both in this issue; ground truth on the VM (Windows
rejects a token with bad imprint / bad signature — verify how: which error).

## Windows ground truth (VM, `tests/p7x_gen.py [REAL.p7x]` + `tests/p7x_winverifytrust.c`)
Synthetic blobs (untrusted root, leaf valid 2000 only) / real Autodesk p7x:
- good token: signer and counter signer asof genTime, 1 counter signer
  (chain 0x20 = untrusted TSA root is fine at this step) -> CERT_E_UNTRUSTEDROOT.
- bad messageImprint (valid token signature): NTE_BAD_HASH, as SIGPROV step
  error (step 16 MSG_COUNTERSIGINFO = TRUST_E_TIME_STAMP); asof = now, 0 counters.
  Imprint is checked before the token signature (corrupting TSTInfo gives
  NTE_BAD_HASH, while CryptMsgControl alone says CRYPT_E_HASH_VALUE).
- bad token signature: SIGPROV 0xc000a000 (STATUS_INVALID_SIGNATURE, passed
  through from CryptMsgControl, which returns it on Windows).
- TSA cert without time-stamping EKU: token silently ignored (asof now, 0
  counters, no error).
- real p7x with the token replaced by one from an untrusted TSA (imprint and
  signature fine): TRUST_E_TIME_STAMP, no step error recorded; asof = genTime.
- Real p7x unchanged: S_OK.

## Outcome
Commits on fix/009-wintrust-signer-policy (1 applies to master as is except its
test hunk, which is in 008's test_wintrust_blob):
1. wintrust: Don't skip the Authenticode policy for certificates with long
   signature hashes. (hash[64]; a failed lookup no longer skips the policy)
2. wintrust: Support counter signers in the provider data helpers.
   (AddSgnr/AddCert with fCounterSigner, cleanup)
3. wintrust: Verify RFC 3161 time-stamp tokens. (imprint -> NTE_BAD_HASH,
   signature -> crypt32 error, no time-stamping EKU -> ignored; TSA added as
   counter signer; replaces 008's unverified genTime helper)
4. wintrust: Check the chains of time-stamp signers in SoftpubAuthenticode.
   (counter-signer chain as of genTime, base policy -> TRUST_E_TIME_STAMP)
Tests: test_wintrust_blob table (good / bad magic / corrupted imprint /
corrupted token signature; todo_wine only for Wine's NTE_BAD_SIGNATURE vs
0xc000a000). VM softpub x86_64 353, i386 341, 0 failures; Wine x86_64 + i386:
wintrust asn/crypt/register/softpub 0 failures.
Real AppxSignature.p7x (InvCoreLP and InvAnark, from prefixes/inv) on Wine:
S_OK, asof = TSA genTime, 1 counter signer with trusted chain.
Left: crypt32 accepts partial chains (issue 011), so a forged TSA whose chain
is incomplete still passes on Wine (Windows TRUST_E_TIME_STAMP); legacy
szOID_RSA_counterSign countersignatures are still unverified (FIXME);
Wine doesn't copy counter-signer chains into pasCertChain (1 cert vs 4) nor
continue to CERTPROV after a SIGPROV failure like Windows.
