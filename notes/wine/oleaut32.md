# oleaut32 typelibs and typelib marshaling — checked at wine-11.18-218-g4e819f054dd (+032)

- PS factories (oleaut.c, DllGetClassObject): PSDispatch {00020420} = plain IDispatch
  proxy/stub, never loads the typelib (Windows does the same, even for dual IIDs).
  PSOAInterface {00020424} = dispatch_typelib_ps: get_typeinfo_for_iid() does
  LoadTypeLib per CreateProxy/CreateStub, then CreateProxyFromTypeInfo (rpcrt4).
  RegisterTypeLib writes PSOAInterface for TKIND_INTERFACE/dual, PSDispatch otherwise.
- Typelib cache (typelib.c tlb_cache, keyed by path+index): entries live only while
  referenced; last Release frees. Windows doesn't cache after release either.
- MSFT load is eager (every typeinfo/func/name parsed); Windows loads lazily (~10 ms for
  a 4 MB typelib). Keep ITypeLib2_Constructor_MSFT free of per-reference list scans.
- Probe/benchmark: tests/tlb_cache.c (builds a 2000-typeinfo typelib, measures
  marshal/unmarshal per IID; args hold/rename/swap/n=N).
