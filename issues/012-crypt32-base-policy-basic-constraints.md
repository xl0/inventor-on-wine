# 012 crypt32 base chain policy doesn't report TRUST_E_BASIC_CONSTRAINTS
Status: open (draft, low) · Owner: - · Branch: - · Found in: issue 011 ground truth

## Observed
With unknown CAs allowed, Windows' CERT_CHAIN_POLICY_BASE also reports
TRUST_E_BASIC_CONSTRAINTS for chains violating basic constraints (e.g. a CA
cert without cA=TRUE in the chain); Wine's verify_base_policy
(dlls/crypt32/chain.c) doesn't check basic constraints. Details / test chains:
see issue 011 and tests/p7x_gen.py.

## Task
Low priority (security strictness, no known app impact). Ground truth for
which chain elements/flags trigger it, match, crypt32 chain test.
