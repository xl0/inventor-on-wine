# 011 crypt32 chain policies accept partial chains
Status: fixed · Owner: worker · Branch: fix/011-crypt32-partial-chain (wt/011, from master) + fix/011-integ-wintrust (from integ) · Found in: issue 009 (wintrust time-stamp verification)

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

## Windows ground truth (VM; partial = test chains with the root left out)
- BASE / AUTHENTICODE / AUTHENTICODE_TS: CERT_E_CHAINING, lChainIndex 0,
  lElementIndex -1. Wins over expiry, wrong usage, critical ext (also with
  IGNORE_*_NOT_TIME_VALID). ALLOW_UNKNOWN_CA ignores it (then the next error).
  Signature errors still come first (chain1).
- SSL: CERT_E_UNTRUSTEDROOT (0,-1), ignored by ALLOW_UNKNOWN_CA or
  SECURITY_FLAG_IGNORE_UNKNOWN_CA. BASIC_CONSTRAINTS: no error. MS_ROOT:
  CERT_E_UNTRUSTEDROOT at the top element. NT_AUTH (unimplemented in Wine):
  CERT_E_CHAINING / CERT_E_UNTRUSTEDCA.
- AUTHENTICODE_TS == BASE in every probed case.
- WinVerifyTrust: p7x signer without root (`p7x_noroot.p7x` from
  tests/p7x_gen.py) -> CERT_E_CHAINING; real p7x with forged TSA
  (`real_ourtsa.p7x`) -> TRUST_E_TIME_STAMP. With WTD_HASH_ONLY_FLAG a partial
  signer chain gives S_OK (wintrust test_wintrust_digest).
- Not done (gap, separate): Windows' base policy also reports
  TRUST_E_BASIC_CONSTRAINTS (0x80096019) when unknown CA is allowed; Wine's
  doesn't check basic constraints. 0xc000a000 vs NTE_BAD_SIGNATURE: not
  touched (msg.c/bcrypt, not small).

## Outcome
fix/011-crypt32-partial-chain (master):
1. crypt32: Don't accept partial chains in the base and SSL chain policies.
   (+ openssl.org name tests ignore unknown CA: GlobalSign R1 root is gone
   from newer host stores, so on this host that chain is partial)
2. crypt32: Implement CERT_CHAIN_POLICY_AUTHENTICODE_TS. (= base policy)
fix/011-integ-wintrust (integ d4d52733ba7 + cherry-picks of 1, 2):
3. wintrust: Don't check the chain policy with WTD_HASH_ONLY_FLAG.
   (needed once 009 + 1 are both in; flips test_wintrust_digest and
   test_multiple_signatures status todo_wines)
Tests: crypt32 chain VM x86_64/i386 only the 3 pre-existing failures
(USERTrust cross-sign); Wine only the 2 pre-existing incompleteOpenssl
failures (host lacks GlobalSign R1). wintrust softpub VM 353/341, Wine 276,
0 failures. Real AppxSignature.p7x (InvCoreLP, InvAnark) on integ+fix: S_OK.
