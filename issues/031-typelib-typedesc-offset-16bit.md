# 031 oleaut32: MSFT typelib loader truncates typedesc offsets to signed 16 bits
Status: open (draft) · Owner: – · Branch: – · Found in: prefixes/inv (integ 6c63dc3c499), Inventor COM API scenario (tools/invscen part/asm)

## Symptom
Inventor's COM API breaks for any member whose type is a pointer to a typedesc entry
past byte offset 0x7fff of the typelib's typedesc table. Out-of-process client
(tools/invscen, .NET 4.8, late-bound via IDispatch):
- `PartDocument.ComponentDefinition` -> Invoke returns E_INVALIDARG (0x80070057), so no
  sketch/feature can be created in a part at all. `PartDocument.ComponentDefinitions`
  -> E_OUTOFMEMORY, `PartDocument.Thumbnail` -> RPC "Call failed".
- `AssemblyComponentDefinition.Occurrences` -> Invoke succeeds but returns VT_I4 holding
  the low 32 bits of a pointer (2073400368) instead of VT_DISPATCH; the CLR fails the
  cast with DISP_E_TYPEMISMATCH. So no component can be placed in an assembly.
- The same scenarios pass on the Windows VM (see tools/invscen, `run.sh --vm part|asm`).

## Windows ground truth (VM, same Inventor 2027.1 build, `tools/invscen/run.sh --vm tlb`)
LoadTypeLibEx in-process, then GetFuncDesc:
- RxInventor.tlb `PartDocument.ComponentDefinition`: VT_PTR -> VT_USERDEFINED PartComponentDefinition
- RxInventorImpl.impl `ComponentDefinition.Occurrences`: VT_PTR -> VT_USERDEFINED ComponentOccurrences
Wine (`tools/invscen/run.sh tlb`): VT_PTR -> vt 4 / vt 0 / vt 5240 (varies: garbage) and
VT_I4 respectively. Inventor's IDispatch::Invoke evidently derives the result VARTYPE
from RxInventorImpl.impl (a plain MSFT typelib despite its extension), hence the VT_I4.

## Cause (dlls/oleaut32/typelib.c, ITypeLib2_Constructor_MSFT, "fill in type descriptions")
Typedesc table entries are read as `INT16 td[4]`; for VT_PTR/VT_SAFEARRAY the pointee is
`&pTypeDesc[td[2]/8]` when `td[3] >= 0`. The pointee is a 32-bit offset
MAKELONG(td[2], td[3]) (Wine's own writer, WMSFT_append_typedesc, stores it as a full DWORD).
With a table > 32 KB, td[2] >= 0x8000 goes negative and the index points before the array
(heap garbage).
- RxInventor.tlb: typedesc table 34032 bytes (4254 entries), 78 pointer entries affected,
  195 members (e.g. PartDocument.ComponentDefinition/ComponentDefinitions/Thumbnail).
- RxInventorImpl.impl: 48504 bytes, 1180 pointer entries, 1481 members
  (e.g. ComponentDefinition.Occurrences, AssemblyComponentDefinition.Sketches, many .Parent).
  Member lists (parsed from the files with a throwaway script): inst/invscen/*-broken-members.txt.
Check the array-description path in the same function for the same pattern (`td[0]/8`
with `td[1] < 0` test) and MSFT_GetTdesc callers.

## Repro / test idea
oleaut32 typelib test: ICreateTypeLib2 a function returning a VT_PTR chain > 4096 deep
(each level is a distinct typedesc entry) or > 4096 distinct VT_PTR -> VT_USERDEFINED
types, SaveAllChanges, LoadTypeLib, compare GetFuncDesc with what was written. Verify the
writer output loads correctly on the VM too. App-level check: `tools/invscen/run.sh tlb`
(needs the running Inventor only for the harness connect step), then `part` and `asm`.
