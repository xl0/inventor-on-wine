# 072 COM: stubs of dead clients never run down; stub-manager lookups slow a long session down
Status: wip (lookups + handles fixed; rundown proposed) · Owner: worker 072 · Branch: fix/072-com-rundown · Found in: soak test (inst/soak/2026-09-28, integ c036c687c47)

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

## Findings (worker 072)
- Windows ground truth (`tests/com_rundown.c`, Win11 VM): STA server, 10 client processes each create
  20 objects through a table-marshaled class factory and QI them (200 objects held).
  Client `kill` (TerminateProcess) or `exit` (ExitProcess, no CoUninitialize): server objects stay
  for 6 min, then ~55-60 % are run down at ~365 s and the rest at ~730 s (batches, not per client).
  `uninit` (CoUninitialize without Release) and `release`: freed at once. Wine: `uninit`/`release`
  same as Windows; `kill`/`exit` never freed (no pinging/rundown at all).
- Inventor (inv3, integ + `trace+comleak` instrumentation, one `run.sh all`): 1297 stub managers
  left in Inventor's main STA (IDispatch + API interfaces, marshaled to the clients). The .NET
  scenario exes exit without releasing (MTA, no CoUninitialize: asmbig 462 proxies created, 32
  released). So each suite pins ~1300 Inventor API objects until Inventor exits; Windows frees
  them after 6-12 min.
- The +81 mutexes/suite: proxy managers in Inventor pointing to two short-lived local servers that
  Inventor itself starts per suite (40 objects each + IRemUnknown). The servers exit cleanly; Inventor
  just keeps its proxies (Windows too presumably, but there a proxy has no kernel object).

## Fix (branch fix/072-com-rundown on master 4e819f054dd; cherry-picks cleanly onto integ)
- `combase: Use a critical section instead of a mutex for proxy manager remote references.`
- `combase: Look up stub managers by object and interface stubs by IPID in rb trees.`
  (apartment trees next to the list; get_stub_manager by OID still walks, it isn't hot.)
- Not done: rundown of dead clients' references (below).
- Tests: ole32 marshal/compobj/moniker/usrmarshal pass on Wine x86_64 + i386 (no new tests: handle
  count isn't observable on Wine, GetProcessHandleCount returns 0). regress ole32 combase oleaut32
  rpcrt4 rpcss shell32 msi urlmon, integ 16941b53211 + fix vs baseline: 138 units, 0 worse.
- Inventor (inv3/:100, integ 16941b53211 + fix, 7 x `run.sh all` in one session, all PASS):
  per suite mutants +1 (soak +82, VM +1), events +205 (soak +224), keys +33, sections +5.
  perf during suite 7 (~9000 live stub managers): combase 0.05 % of Inventor samples (soak late 3.8 %).
  Suite wall 135/119/115/116/119/170/146 s; the 6th-7th are host-load noise or 073, not combase.

## Rundown proposal (not implemented)
Process-death rundown ("release the refs of a client whose connection/process went away") is not
safe: the server can't attribute references. A NORMAL OBJREF's refs go to whoever unmarshals it,
Proxy_MarshalInterface hands half of a proxy's refs to a third process without contacting the
server, and marshals for nested outgoing calls look like reply marshals. Dropping "the dead
client's" refs would disconnect live holders. The Windows-shaped fix is ping rundown, in combase:
- server: per stub manager last-ping tick (set on NORMAL marshal and on ping); per-apartment timer
  (STA: SetTimer on the apartment window, MTA: thread pool) every 2 min runs down managers with
  external refs, not table-marshaled, not NOPING, unpinged for 6 min (release all external refs);
- client: process-wide thread pool timer (2 min) while cross-process proxies exist; joins the MTA,
  collects per server OXID the IPIDs of live proxy managers of all apartments, pings each server
  apartment through IRemUnknown (e.g. RemAddRef with 0 refs, or a Wine-private IRundown use).
~150-250 lines, no conformance test practical (6 min timeouts). Faithful alternative:
IObjectExporter ping sets in rpcss (MS-DCOM), much larger.
