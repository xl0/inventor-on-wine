# 011 crypt32 chain policies accept partial chains
Status: open (draft) · Owner: - · Branch: - · Found in: issue 009 (wintrust time-stamp verification)

## Observed
- `CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE / _AUTHENTICODE)`
  (dlls/crypt32/chain.c `verify_base_policy`) never looks at
  `CERT_TRUST_IS_PARTIAL_CHAIN` (0x10000). A chain whose issuer can't be found
  passes with NO_ERROR if nothing else is wrong.
- Windows (VM, black-box): partial chain -> CERT_E_CHAINING (0x800b010a), and
  it wins over expiry (0x10001 -> CERT_E_CHAINING; Wine: CERT_E_EXPIRED).
- Consequences: WinVerifyTrust accepts Authenticode signatures from signers
  with an incomplete chain; after 009, also RFC 3161 time-stamps from a forged
  TSA whose chain is incomplete (Windows: TRUST_E_TIME_STAMP, Wine: S_OK; repro
  `real_ourtsa.p7x` from `tests/p7x_gen.py REAL.p7x` + `tests/p7x_winverifytrust.c`).
  Likely also the todo_wines in wintrust softpub `test_multiple_signatures`
  (expects CERT_E_UNTRUSTEDROOT or CERT_E_CHAINING, Wine S_OK, chain 0x10000).
- Related, same file: `CERT_CHAIN_POLICY_AUTHENTICODE_TS` unimplemented (FIXME,
  returns FALSE); Windows returns TRUE with the base-policy errors.
- Minor: Windows CryptMsgControl(CMSG_CTRL_VERIFY_SIGNATURE) on a bad RSA
  signature fails with 0xc000a000 (STATUS_INVALID_SIGNATURE), Wine with
  NTE_BAD_SIGNATURE (todo_wine in wintrust test_wintrust_blob).

## Task
Map CERT_TRUST_IS_PARTIAL_CHAIN to CERT_E_CHAINING in the base policy with
Windows' precedence; check the multiple_signatures todo_wines.
