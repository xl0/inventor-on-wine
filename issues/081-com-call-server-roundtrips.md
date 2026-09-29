# 081 Each cross-process COM call into Inventor costs ~500 wineserver requests (hooks, HKCR lookups)
Status: open (draft, perf) · Owner: - · Branch: - · Found in: 057 (inv3, integ bbc7f82accb + fix/057)

## Symptom
After 057 (GetWindow via shared memory), walking an assembly's file references through the
API (`tools/invscen/run.sh openbench`, Test Station: ~692 calls of FullFileName,
ReferencedFileDescriptors, ReferenceMissing, ReferencedFile) takes 6-8 s on Wine vs
1.2-1.7 s on the VM (~9 ms vs ~2 ms per call; before 057: 16-32 s). The same per-call
cost is most of what is left of the samples gap for big assemblies: Buffer Prep Skid open
60 s vs 21 s (VM); ~50 s of it is the harness' Counts()/Missing() walk, main thread and
wineserver ping-ponging at ~50 % CPU each. Opening itself (~17 s main-thread CPU) is
near VM speed.

## Evidence (strace of Inventor's main thread over the whole walk; requests per API call)
~500 server requests per call: get_hook_info 128, start/finish_hook_chain 43+43,
get_message 50, open_key 103, close_handle 57, enum_key 33 (NtQueryKey name lookups of
the KEY_WOW64_32KEY path in `kernelbase/registry.c:open_key`), get_window_property 7,
send_message 7, create_file/mapping/map_view ~1-2. A round trip costs ~20 us on this host.
- Registry (`+reg`): combase `CoGetPSClsid` / `CoGetClassObject` lookups of
  `Interface\{iid}\ProxyStubClsid32`, `CLSID\{..}\InprocServer32`, TypeLib keys through the
  HKCR merged view for every marshalled interface (the .NET client QIs each new RCW for
  IManagedObject, IProvideClassInfo, IDispatch, INoMarshal, IInspectable, IAgileObject,
  IRpcOptions); failed 64-bit lookups retry with KEY_WOW64_32KEY (component-by-component
  walk with Wow6432Node probes and NtQueryKey per component).
- perf: `LdrUnloadDll` (`MODULE_FlushModrefs` + `process_detach` walk the whole module list
  on every FreeLibrary, even when nothing unloads; Inventor has hundreds of modules) and
  `map_view`/`map_free_area` are each ~5-10 % of the main thread.
- Hooks: Inventor has message hooks installed; every PeekMessage/GetMessage runs a hook
  chain with 3 get_hook_info + start/finish requests.

## Task
Profile per component and cut the biggest: candidates are a per-process cache of
IID -> PS factory in combase (check Windows behaviour when the registry changes), the
32-bit-view retry in CoGetPSClsid, FreeLibrary's module-list walks when the refcount
stays > 0, and the hook chain protocol. Benchmark: `INVSCEN_OPEN=... run.sh openbench`
(walk refs step) on Wine and `--vm`.
