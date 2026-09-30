# 081 Each cross-process COM call into Inventor costs ~500 wineserver requests (hooks, HKCR lookups)
Status: fixed (wt/081) · Owner: 081 worker · Branch: fix/081-com-call-roundtrips (562c0f48db0 466ee4e656d 8b709268302 6065886de20 e3ce20d1c49) · Found in: 057 (inv3, integ bbc7f82accb + fix/057)

## Symptom
After 057 (GetWindow via shared memory), walking an assembly's file references through the
API (`tools/invscen/run.sh openbench`, Test Station: ~692 calls of FullFileName,
ReferencedFileDescriptors, ReferenceMissing, ReferencedFile) takes 6-8 s on Wine vs
1.2-1.7 s on the VM (~9 ms vs ~2 ms per call; before 057: 16-32 s). The same per-call
cost is most of what is left of the samples gap for big assemblies: Buffer Prep Skid open
60 s vs 21 s (VM); ~50 s of it is the harness' Counts()/Missing() walk.

## Measuring
`INVSCEN_SYNC='C:\t\sync081'` makes openbench wait for `walk.go` after creating `walk.start`
(and `walk.end`/`walk.done` after the walk), so a tracer can attach to exactly one walk.
strace of all Inventor threads (`-e write,writev -xx -s 8`, only pipe fds = server requests),
first int = request number in `enum request`. Per API call, Inventor main thread, warm:

| request group | integ | + all 081 commits |
|---|---|---|
| hooks: get_hook_info / start / finish_hook_chain | 153 / 51 / 51 | 0 |
| registry: open_key, enum_key (NtQueryKey), close, get_key_value | ~210 | ~2 |
| get_window_property (GetProp) | 9 (up to 100, see below) | 0 |
| create_file/mapping/map_view (mscoree.tlb reloads) | ~8 | ~1 |
| get_message, set_queue_mask, select, event_op, send_message | ~80 | ~95 |
| total | 571 | 103 |

(set_queue_mask/select grow because the main thread now waits for the next call.)
GetProp: Inventor's "Autodesk Assistant" pane (WebView), when open, adds ~13 GetProps on 5
windows per message (atom 0xc02c, ADPWebViewInstance): ~100 requests per call.

## Causes and fixes (wt/081, all on integ ed241c72d09)
1. combase `marshal_object()` looked up the proxy/stub (registry incl. the KEY_WOW64_32KEY retry,
   typelib loads of mscoree.tlb for IManagedObject) before the stub's own QI failed: .NET QIs every
   new RCW for IAgileObject, INoMarshal, IInspectable, IRpcOptions, IManagedObject. Now QI first.
   Unobservable except for speed (same E_NOINTERFACE). ~190 requests per call.
2. Hook chains: N hooks = N+2 round trips per hooked message (Inventor: 3 WH_GETMESSAGE hooks,
   ~50 hooked peeks per API call). Server protocol change: queue_shm_t.hooks_serial (bumped in
   add_queue_hook_count, i.e. whenever a hook that runs in the thread is added/removed) +
   new request get_hook_chain (snapshot of the hooks of one id that run in the thread, with module
   names). win32u caches the snapshot per thread and id and walks it with no requests while the
   serial is unchanged. If it changed mid-chain, CallNextHookEx continues with the snapshot hooks
   that still exist (get_hook_info per hook, proc != 0); hooks added meanwhile aren't in the
   running chain, like on Windows. (A first version asked the server for the next hook and lost
   the chain when that hook unhooked itself: no start/finish counts are held on the cached path,
   so removed hooks are freed at once. Review repro, now in test_hook_chain_changes.) Snapshots
   bigger than 4 KB are fetched with a heap buffer (reply->total). LL/winevent hooks unchanged.
   queue_shm_t.hooks_serial is at offset 96 on x86_64 and i386 (NB_HOOKS = 17), no pad needed.
3. GetProp: window_shm_t.props_serial (bumped on every property change) + get_window_property
   reply `cacheable`. Per-thread 256-entry 2-way cache keyed by window shared object id + serial.
   By-atom results always cacheable; by-name only if the name is a property set by name (holds a
   ref on the atom) or the window has no property set by atom. Unknown names (atom lookup fails,
   last error set) aren't cached.
4. ntdll LdrUnloadDll: skip process_detach()/MODULE_FlushModrefs() list walks when the refcount
   stays > 0.
5. combase: cache IID -> ProxyStubClsid32 and PS CLSID -> dll path after the first successful
   lookup, cleared on the last CoUninitialize. Windows does exactly that (below). ~30 requests per call through the
   merged HKCR view (each HKCR open = NtQueryKey + both sides + close).

## Windows ground truth (VM)
- CoGetPSClsid: first successful registry result is kept until the last CoUninitialize (change
  and delete of HKLM or, non-elevated, HKCU `Interface\{iid}\ProxyStubClsid32` not seen before);
  failures aren't cached (a later registration is found).
- CoGetClassObject: after the class dll was loaded, InprocServer32 changes/removal aren't seen
  (plain and CLSCTX_PS_DLL) until the last CoUninitialize; a failed load (missing dll) isn't cached. Wine now matches
  for CLSCTX_PS_DLL only (plain: todo_wine in ole32 compobj).
- Hook chains (`tests/hook_chain.c`, WH_MSGFILTER A->B->C via CallMsgFilter): A unhooks itself ->
  B, C still called; A unhooks B -> C; other thread unhooks B while A runs -> C; a hook added
  while A runs (same or other thread) runs first next time, not in the running chain.

## Tests
- user32 msg: test_hook_chain_changes (VM + Wine pass, both arches). user32 win:
  test_GetProp_changes (other-thread Set/RemoveProp, props by atom, atom deleted/re-added).
  ole32 compobj: PS clsid/dll caching (VM elevated + limited, both arches: 0 failures).
- Micro-benchmarks, Wine integ / 081 / VM:
  CallMsgFilter with 3 hooks 37.5 us / 0.9 us / 5.8 us (`tests/hook_chain.exe`);
  PeekMessage(NOREMOVE) hook overhead of 3 WH_GETMESSAGE hooks +38 us / +3.5 us / +5.2 us;
  GetProp set by name 21 us / 0.18 us / 0.92 us, unset by atom 20 us / 0.13 us / 0.54 us
  (`tests/getwindow_perf.exe 20 500`; unset *unknown* name stays 21 us vs 0.7 us);
  FreeLibrary of a DLL that stays loaded, 300 modules: 716 ns / 21 ns / 50 ns (`tests/freelib_perf.c`).

After the review fixes: requests per call unchanged (95); Test Station walk 1.8-2.3 s or 3.6-3.7 s
(bimodal on the rechecking day, same request count, Assistant WebView processes busy on inv4).

## Results (inv4, same session conditions, s)
openbench Test Station walk refs (692 calls): integ 5.5-10.7 / 081 1.7-1.9 (after the first) /
VM 1.2-1.7. Buffer Prep Skid (3194 calls): walk 45-49 / 7.0-7.3; open 10.5-13.2 / 7.6-9.3.

## Left
Remaining ~100 requests per call are the message path (PeekMessage NOREMOVE+REMOVE pairs of
WM_USER/WM_KICKIDLE, MsgWaitForMultipleObjects' set_queue_mask/select, RPC event_op/send_message).
Plain in-process CoGetClassObject still re-reads the registry each time (Windows caches).
