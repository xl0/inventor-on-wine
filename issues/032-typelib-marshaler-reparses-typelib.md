# 032 oleaut32: typelib marshaler re-parses the whole typelib for every dispinterface proxy/stub
Status: fixed · Owner: worker 032 · Branch: fix/032-typelib-cache · Found in: prefixes/inv (integ 6c63dc3c499), Inventor COM API scenario (tools/invscen)

## Symptom
Every out-of-process Inventor API call that yields a new interface pointer of a new
dispinterface IID costs ~2.2 s in the client. Harness connect step (GetActiveObject + 3
typed casts): 6.5 s on Wine vs 0.0 s on the VM; drawing base view 8.6 s vs 0.2 s; a
browser-node walk (~15 nodes, partly via C# `dynamic`) took 84 s. Loading RxInventor.tlb (4.2 MB, 2680 typeinfos)
in-process: 2.0-2.2 s on Wine, 0.0 s on the VM (`tools/invscen/run.sh [--vm] tlb`).

## Evidence (client, WINEDEBUG=+timestamp,+ole, `tools/invscen/run.sh hello`)
Inventor's interfaces are dispinterfaces registered with ProxyStubClsid32 =
{00020420-...} (PSOAInterface). For each QI of such an IID, dispatch_typelib_ps_CreateProxy
(dlls/oleaut32/oleaut.c) -> get_typeinfo_for_iid -> LoadTypeLib(RxInventor.tlb) parses the
whole file (ITypeLib2_Constructor_MSFT, ~2.2 s, thousands of ITypeInfoImpl_Constructor),
sees TKIND_DISPATCH, creates a plain IDispatch proxy, releases the typeinfo -> refcount 0
-> "removing from cache list", destroy (another ~0.1 s). Next IID: same again.
3 full loads during the connect step alone. dispatch_typelib_ps_CreateStub has the same
pattern, so the server (Inventor) side very likely pays it too per new stub (not traced).

## Notes / task
Windows ground truth to establish on the VM: is a typelib that was released to refcount 0
still cached (second LoadTypeLib of a big typelib immediately cheap / same pointer)? Also
Windows parses lazily. Options: keep typelibs cached after release (as Windows appears to),
cache IID -> typekind in the PS factory, or make MSFT loading lazy. Pick what matches Windows.

## Windows ground truth (VM, `tests/tlb_cache.c`: 2000-typeinfo synthetic typelib, cross-apartment)
- No typelib cache after the last Release: overwrite the file after release, reload sees the new
  contents. Loads are just cheap (~6-13 ms for 2000 typeinfos / RxInventor.tlb).
- PSDispatch {00020420} never touches the typelib: plain IDispatch proxy (static vtbl, same as
  for a dispinterface) even for a *dual* IID; works with the .tlb file moved away; ~0.1 ms/proxy.
- PSOAInterface {00020424}: loads the typelib per proxy (~7-17 ms; 0.4 ms if the app holds a
  typelib ref), fails (TYPE_E_CANTLOADLIBRARY) without the file, E_FAIL for a dispinterface.
- Stub creation is cheap (~0.15 ms) for both, even with the file gone (lazy).
- Proxies/stubs don't hold refs on the ITypeLib/ITypeInfo the app loaded (refcounts unchanged).

## Cause and fix (2 commits)
1. oleaut.c: PSDispatch and PSOAInterface shared one factory that loads the typelib to pick
   proxy kind. Now PSDispatch -> plain IDispatch proxy / dispinterface stub, no typelib
   (tmarshal test: dual IID re-registered with PSDispatch gets the IDispatch proxy vtbl).
2. typelib.c: MSFT_ReadName/MSFT_ReadString were linear list scans per reference -> quadratic
   load. Indexed by offset/4 during load: RxInventor.tlb 2.2 s -> 0.03 s, synthetic 8.9 s -> 0.03 s.
Remaining (not fixed, Windows-like): PSOAInterface still reloads the typelib per proxy/stub
(~40 ms each for the synthetic lib); Windows creates stubs lazily.

## Results
Synthetic (Wine, 10 IIDs): dispinterfaces stub+proxy 171 s -> 0.013 s; duals 167 s -> ~0.9 s.
Inventor (prefixes/inv, integ 6adad910faf + both commits, warm Inventor), per step Wine before /
after / VM: part: connect 6.1/0.1/0.0, new part 6.7/9.6/0.7, sketch 24.7/0.3/0.2, extrude 8.3/0.1/0.1,
reopen 12.9/0.6/0.7; asm: mate 99.3/0.4/0.3, place 16.6/0.2/0.8, verify after reopen 16.7/0.1/0.0;
drawing: new 12.0/1.7/0.7, base view 8.1/0.5/0.2. Whole scenario: part 70 s -> 12 s, asm 163 s -> 4 s,
drawing 43 s -> 5 s. Full logs: wt/032-inv/.
("new part" after = first document after Inventor start; before = warm, so not comparable.)
Regress (integ 6adad910faf + both, oleaut32/ole32/combase/rpcrt4, both arches): 64/64 pass, 0 worse.

## Side observations (not caused by 032)
- Cold start: harness connect fails ~1/3 of the time with InvalidCastException E_NOINTERFACE
  (QI for Application right after GetActiveObject while Inventor is still starting); same rate
  on the build without 032 (2/3). Harness.cs only retries COMException.
- Once (1 of 6 cold starts with 032) Inventor crashed at startup in ole32 IDropTarget_Release
  (read of a garbage pointer); not reproduced since.
