# 009 WinVerifyTrust skips the chain policy for SHA-256-signed signer certs
Status: open (draft) · Owner: - · Branch: - · Found in: issue 008 conformance test (wintrust softpub)

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
