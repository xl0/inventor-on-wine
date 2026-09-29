# 072 COM: stubs of dead clients never run down; stub-manager lookups slow a long session down
Status: open (draft) · Owner: - · Branch: - · Found in: soak test (inst/soak/2026-09-28, integ c036c687c47)

## Symptom
A 4 h soak of one Inventor session (tools/soak/soak.sh: `tools/invscen/run.sh all` x20, `samples` x6)
gets steadily slower: suite wall time 130 s → 250-300 s, e.g. asmbig "place 200 occurrences"
20 s → 41 s, asmcon 16 → 29 s, samples "mass Personal Computer.iam" 16 → 33 s (`trends.txt`,
`timings.png`). A fresh Inventor on the same prefix is back at the start values (`matched/fresh-table.txt`).

perf of Inventor during one suite (199 Hz, `matched/{late,fresh}-inv.perf`, not in git):

| | late (4.2 h session) | fresh session |
|---|---|---|
| combase `get_stub_manager_from_ipid` → `stub_manager_ipid_to_ifstub` (combase+1fc70) | 2.6% | 0.07% |
| combase `get_stub_manager_from_object` (combase+1ebb6) | 1.2% | 0.03% |

(steady 3-9% in every 10% time slice of the late run, not a burst). Both walk the apartment's
`stubmgrs` list (and each manager's `ifstubs` list) linearly, so they get slower as stub managers
pile up. (Heap part of the slowdown: 073.)

## Why they pile up (suspicion, to verify)
Every scenario is a separate .NET client process that exits without releasing all its RCWs.
On Windows the OXID resolver pings; when a client stops pinging, its references are run down
(~6 min) and the stubs (and the Inventor objects they pin) are freed. Wine has no pinging /
rundown at all (no ping code in combase or programs/rpcss), so each dead client's stubs, and the
objects they hold alive, stay until Inventor exits. That also fits the Inventor memory growth
(RSS 3 → 14 GB over 4 h, +2.5 GB/h) — not separated from Inventor's own caching yet.

Related, same session: Inventor keeps ~81 more combase proxy managers alive after each suite
(each owns an unnamed `remoting_mutex`: mutant handles +82/suite on Wine vs +1/suite for
the same 3 suite rounds on the Windows VM; relay attribution of the live mutexes: 81 of 105 from
`proxy_manager_construct`, `relay/` + `matched/` in inst/soak/2026-09-28). Who holds these proxies
is not known.

## Next steps
- Confirm: count stub managers per apartment in a long session (gdb on combase `apt->stubmgrs`
  or a +ole trace of stub_manager create/destroy) and check which clients they belong to.
- Windows ground truth: a client that gets an Inventor-like out-of-proc object, takes refs and
  exits without Release; on Windows the server object is released after the ping timeout.
- Fix direction (not small): client-death rundown (per-OXID/process refs; the server can learn a
  client died from its RPC connection closing) and/or hash lookup by IPID/object instead of list walks.
