# 031 oleaut32: MSFT typelib loader truncates typedesc offsets to signed 16 bits
Status: fixed · Owner: worker 031 · Branch: fix/031-typelib-typedesc-offset (98532bfcad0) · Found in: prefixes/inv (integ 6c63dc3c499), Inventor COM API scenario (tools/invscen part/asm)

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

## Outcome
Format (checked against both Inventor files): typedesc entry = 2 DWORDs; for VT_PTR /
VT_SAFEARRAY the 2nd DWORD is either inline (bit 31 set, VT in low word) or a plain
32-bit table offset (high word 0 in the real files, offsets up to 0xbd70). Array
descriptors: 1st DWORD is a typedesc in the same encoding; VT_CARRAY entries hold a
32-bit arraydesc offset. Writers (oleaut32 WMSFT_append_typedesc, widl write_msft.c)
already emit full DWORDs; only the loader was wrong. Fix: MAKELONG the word pairs in
all three places in ITypeLib2_Constructor_MSFT.

Test `test_large_typedesc_table` (oleaut32 typelib): 50 distinct 100-deep VT_PTR chains
ending in VT_CARRAY (40 KB table) + a CARRAY whose element is such a chain; save, load,
walk. VM (Win11) x64/i386: pass; unfixed Wine fails; fixed Wine passes. Note: Windows x64
ICreateTypeLib2 crashes (writer recursion) on a single 4200-deep chain, hence the shape.

App check (integ 6c63dc3c499 + fix, wt/031-integ-build, prefixes/inv): invscen tlb, part,
asm, drawing all PASS, same steps as the VM. Remaining gap is speed (steps 2-100 s vs
0-1 s on the VM): the typelib marshaler re-parses RxInventor.tlb (~2 s) per new proxy,
33 times in `part` = ~67 s -> issue 032.
