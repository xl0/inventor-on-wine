# wintrust / crypt32 signatures — checked at wine-11.18-218-g4e819f054dd (+fix/008, fix/009)

- WinVerifyTrust flow (GENERIC_VERIFY_V2): dlls/wintrust/softpub.c
  SoftpubLoadMessage (per dwUnionChoice: file/catalog/cert/blob) -> SIP pfGet ->
  CryptMsg -> SoftpubLoadSignature (signers, WINTRUST_GetTimeFromSigner) ->
  chain per signer as of `sftVerifyAsOf` -> SoftpubAuthenticode (policy).
- Built-in SIPs (PE, cab, cat, P7x) are in wintrust/crypt.c
  CryptSIPGetSignedDataMsg, registered by wintrust DllRegisterServer
  (register.c) into `Cryptography\OID\EncodingType 0\CryptSIPDll*` — a prefix
  needs `wineboot -u` after adding one. Windows: P7x SIP is AppxSip.dll.
- Signer time: szOID_RSA_counterSign signingTime (unverified) or RFC 3161
  token genTime (szOID_RFC3161_counterSign). Tokens are verified
  (WINTRUST_VerifyTimeStampToken, fix/009): imprint = hash of the signer's
  EncryptedHash else NTE_BAD_HASH, token signature, TSA needs time-stamping EKU
  (else ignored, as Windows). The TSA becomes pasCounterSigners[0]; its chain is
  built as of genTime in CERTPROV and SoftpubAuthenticode maps a failing base
  policy on it to TRUST_E_TIME_STAMP (after the signer's own policy).
- For blobs sftSystemTime is "now"; for files it's the file's creation time.
- crypt32 chain policies: partial chain -> CERT_E_CHAINING (base/Authenticode/
  _TS), CERT_E_UNTRUSTEDROOT (SSL), unless unknown CA allowed (issue 011).
  WTD_HASH_ONLY_FLAG skips the policy in SoftpubAuthenticode.
- crypt32 msg.c: SignedData content is unwrapped from OCTET STRING for
  szOID_RSA_data or version >= 3 (CMS), both for CMSG_CONTENT_PARAM and the
  signer hash (Windows behaviour). Authenticode (v1) hashes the SEQUENCE's
  content octets.
- CMS v3 certificates: Windows exposes `[1]` choices as attribute certs and drops
  other non-SEQUENCE choices; Wine drops every element that isn't a valid
  certificate (decode.c CRYPT_AsnDecodeArray skip path, issue 013). Microsoft
  time-stamp tokens carry a `[1]` attr cert.
- Handy harness: `tests/p7x_winverifytrust.c` dumps chain/policy/WinVerifyTrust
  signer state; `tests/p7x_gen.py` makes signed p7x test blobs (python3-cryptography).
