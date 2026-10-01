# 097 Placing 200 assembly occurrences is ~4x slower than Windows
Status: in progress (perf) · Owner: 097 worker · Branch: fix/097 (wt/097) · Found in: invscen asmbig (inv, integ 2cfee5f132e)

## Symptom
asmbig "place 200 occurrences" (COM API, one part placed 200 times into a new assembly):
Wine 24-40 s (noisy with host load), VM 7.3 s. Soak #1's 074 crash also happened in this step.
081 cut per-COM-call overhead (~103 wineserver requests per call now); this step is still 4x.

## Measuring
`tools/invscen/place.cs` (scenario `place`): only that step, `INVSCEN_N` rounds, per call kind split
(CreateVector / SetTranslation / Occurrences.Add / Grounded); `INVSCEN_PLACE=hidden` adds invisible
documents, `cheap` also times a trivial call; `INVSCEN_SYNC` brackets each placement like openbench.
Numbers are bimodal per Inventor session: 13-16 s or 26-30 s for the same build (both seen on fresh
sessions; within one session stable). Hidden documents: 5-10 s.

## Where the time goes (inv3, integ, visible, ~26 s run)
- Inventor main thread busy ~85 % of the step (22 s CPU of 26 s). wineserver 7 s. ~115 TBB worker
  threads (tbb12; Wine reports 64 CPUs) burn ~60 s CPU spinning (SwitchToThread loops) but do not
  gate the main thread: making NtYieldExecution cheaper (no getrusage) changed nothing.
- Main thread CPU (perf, managed names from an EventPipe JIT/Loader rundown, `epmeth.py` idea:
  MethodLoadVerbose payloads scanned out of the .nettrace): ~45 % .NET 10 code (WPF model browser:
  XAML template instantiation, tree-change walks, PropertyChangedEventManager.Purge) + ~10 % clrgc,
  ~20 % Inventor native, ~10 % Wine user code, ~18 % kernel (server IPC ~2 s, mprotect/mmap ~1 s).
  The managed/native app work itself dominates; it is not obviously Wine-specific.
- Per occurrence the client makes 19 cross-process COM calls: 4 IDispatch::Invoke, 1 RemAddRef and
  14 RemQueryInterface (.NET RCW creation for the 2 new objects: IManagedObject, IProvideClassInfo,
  IInspectable, INoMarshal, IAgileObject, IRpcOptions + 2 Inventor IIDs). Every incoming call is a
  posted message to Inventor's STA and is followed by a full MFC idle (WM_KICKIDLE) + WPF dispatcher
  cycle; a cheap call costs ~1-3 ms on Wine. The heavy post-Add UI work lands on the first call
  after Add (46-63 ms), whichever it is.
- Main thread server requests: ~710 per occurrence: get_message 149, select 118, release_mutex 113
  (AdpSDKCore analytics + FwUI mutex lock pairs, ~80+40 per occurrence), event_op 68,
  map_view/unmap_view/get_mapping_info 44 each (utx.dll maps 128 KB views of one section ~40x per
  occurrence), open_key 26 (combase HKCR `_Mechatronics_iProperties\CLSID` lookups, 8 per occurrence),
  send_message 20, create_file 10.
- rse.dll does ~315 VirtualProtect (8 KB, RW/RO) and ~135 access-violation faults per occurrence
  (its own write tracking).
- Micro costs, Wine / VM (`tests/mapview_perf.c`): map+unmap 128 KB view 37.6 / 6.2 us, mutex
  wait+release 15.2 / 0.42 us, SetEvent 7.0 / 0.19 us, VirtualProtect RO+RW 3.6 / 0.8 us. Together
  ~4.5 ms per occurrence on Wine (~0.9 s per 200): real but small; needs ntsync for the mutexes.
- Trivial cross-process QI round trip (`tests/qi_remote.c`): Wine ~400-540 us, Windows ~80 us
  (named pipe I/O via wineserver + 4 thread hops per call).

## Windows ground truth (`tests/qi_remote.c`, `ole32:marshal` test_proxy_rpc_options)
- A proxy answers QI for IUnknown, IMarshal, IMultiQI, IClientSecurity, IRpcOptions and ICallFactory
  (S_OK) and IAgileObject, INoMarshal (E_NOINTERFACE) without asking the object; every other IID
  goes to the object every time (no negative cache).
- IRpcOptions::Query/Set: NULL proxy E_INVALIDARG; a pointer that isn't one of the object's interface
  proxies (IMarshal, IRpcOptions itself, a non-proxy, the identity unless IUnknown was unmarshaled)
  E_NOINTERFACE; COMBND_SERVER_LOCALITY = PROCESS_LOCAL (0) for other apartments of this process,
  MACHINE_LOCAL (1) for other processes; COMBND_RPCTIMEOUT only for other processes (default 5,
  Set > 10 = 0x800106ad, Set 7 then Query 7), in-process E_INVALIDARG; Set(locality) E_INVALIDARG.

## Fixes (wt/097, fix/097, on integ 2cfee5f132e; no server protocol change)
- 695aef546e1 combase: Implement IRpcOptions on proxies.
- 1c7a986097a combase: Don't query the server for IAgileObject and INoMarshal on proxies.
  Together: 6 of 19 cross-process calls per placed occurrence gone (as on Windows).
- 1f06b88c902 combase: Use the cached call event to wait for calls dispatched to an STA.
- 73f2207439b rpcrt4: Cache two events per named pipe connection (server io thread reads while a
  worker writes; the single cache slot made every reply create+close an event).
- cf670484882 rpcrt4: Read whole messages from named pipes (was 3 pipe reads = server requests per
  fragment: common header, rest of header, body).
  Reads only continue into the next pipe message when the read started there or the previous read
  stopped mid-message (STATUS_BUFFER_OVERFLOW), like direct reads; zero-length "wait for data"
  reads don't consume (async notifier); buffer = RPC_MAX_PACKET_SIZE.
- d08143577f2 combase: Don't reuse the call event of a call cancelled by the message filter (fixes
  integ's 086 event caching; the late completion's use-after-free predates it: draft 099).
  Remote QI loop (`qi_remote.exe`, 3 alternating runs) 567-583 us -> 307-328 us
  (Windows 161 us).

## Results (inv3, `place`, fresh Inventor per run, 3 rounds per run, interleaved, medians of 9 rounds)
- integ vs QI fixes only: 26.45 s vs 23.04 s.
- integ vs all five: 26.78 s vs 18.27 s (-32 %); run medians A 26.2/27.5/26.8, B 18.3/18.2/18.5.

## Tests
- ole32:marshal test_proxy_interfaces (IAgileObject/INoMarshal never reach the object) and
  test_proxy_rpc_options: VM x86_64 + i386 0 failures; Wine both arches 0 failures.
- regress rpcrt4|ole32|combase|oleaut32|rpcss|ole2|olecli32|shell32|urlmon vs the integ
  2cfee5f132e baseline: 0 worse of 116 units (also after the review fixes).
- Review fixes: rpcrt4 server/rpc/rpc_async + ole32 marshal on Wine x86_64/i386 0 failures; on the
  VM ole32 marshal 0, rpcrt4 rpc 1 (I_RpcMapWin32Status) and i386 server 8 failures, same with the
  integ test binaries. Reviewer's echo.c stress (ncalrpc, ncacn_np, 64/32-bit) 0 errors; raw2.c
  (8 raw-PDU cases) output identical to integ.

## Left / ideas
- Remaining per call (~320 us vs 160 us): every pipe read/write is a wineserver request, plus 4
  thread hops; uncontended mutex/event ops are server calls (AdpSDKCore/FwUI ~120 mutex pairs
  per occurrence, 15 us vs 0.4 us) -> ntsync (host kernel 6.8 lacks it).
- NtYieldExecution's two getrusage(RUSAGE_THREAD) calls contend on mm_users with many threads
  (9 us at 100 threads vs 0.2 us alone); Inventor's ~115 TBB workers spin on SwitchToThread.
  Removing them didn't change this step.
- ICallFactory is local on Windows (async calls; Wine has none) — still a remote QI, not asked by .NET.
- Per-call transport cost (rpcrt4 ncalrpc over named pipes through wineserver).
