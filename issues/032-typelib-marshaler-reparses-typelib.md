# 032 oleaut32: typelib marshaler re-parses the whole typelib for every dispinterface proxy/stub
Status: open (draft, perf) · Owner: – · Branch: – · Found in: prefixes/inv (integ 6c63dc3c499), Inventor COM API scenario (tools/invscen)

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
