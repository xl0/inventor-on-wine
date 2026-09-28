# wintrust / crypt32 signatures — checked at wine-11.18-218-g4e819f054dd (+fix/008)

- WinVerifyTrust flow (GENERIC_VERIFY_V2): dlls/wintrust/softpub.c
  SoftpubLoadMessage (per dwUnionChoice: file/catalog/cert/blob) -> SIP pfGet ->
  CryptMsg -> SoftpubLoadSignature (signers, WINTRUST_GetTimeFromSigner) ->
  chain per signer as of `sftVerifyAsOf` -> SoftpubAuthenticode (policy).
- Built-in SIPs (PE, cab, cat, P7x) are in wintrust/crypt.c
  CryptSIPGetSignedDataMsg, registered by wintrust DllRegisterServer
  (register.c) into `Cryptography\OID\EncodingType 0\CryptSIPDll*` — a prefix
  needs `wineboot -u` after adding one. Windows: P7x SIP is AppxSip.dll.
- Signer time: szOID_RSA_counterSign signingTime or RFC 3161 token genTime
  (szOID_RFC3161_counterSign); neither countersignature is verified, and Wine
  doesn't fill pasCounterSigners (Windows does).
- For blobs sftSystemTime is "now"; for files it's the file's creation time.
- SoftpubAuthenticode Disallowed check breaks for SHA-256 certs (issue 009):
  policy silently skipped, WinVerifyTrust S_OK.
- crypt32 msg.c: SignedData content is unwrapped from OCTET STRING for
  szOID_RSA_data or version >= 3 (CMS), both for CMSG_CONTENT_PARAM and the
  signer hash (Windows behaviour). Authenticode (v1) hashes the SEQUENCE's
  content octets.
- Handy harness: `tests/p7x_winverifytrust.c` dumps chain/policy/WinVerifyTrust
  signer state; `tests/p7x_gen.py` makes signed p7x test blobs (python3-cryptography).
