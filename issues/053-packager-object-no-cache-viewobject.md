# 053 Packager object lacks the default-handler interfaces (IViewObject2, IOleCache, IDataObject...)
Status: fixed (awaiting Inventor retest) · Owner: 053 worker · Branch: fix/053-packager-viewobject · Found in: samples campaign, integ 228616fa47c + 38e4c1c00ce (prefixes/inv3, :100)

## Symptom
Opening Autodesk's sample `Models\Translation\Arm Rest\Arm_Rest.ipt` (Inventor 2022
sample set, see CODE.md) fails on Wine. The VM opens it fine (4 features, 3 sketches).
- COM `Documents.Open` returns E_FAIL.
- The UI (SilentOperation off) shows "An unknown error occurred while accessing
  an unnamed file." ![dialog](attachments/053-packager-open-error.png)

The part embeds an OLE Package object (CLSID_Package {0003000C-...-46}, an
Ole10Native import report, `C:\My Documents\Customer Data\Arm_Rest.igs`) in
`RSeStorage\RSeEmbeddings\Embedding 3`.

Only one other file in the set has a Package embedding:
`Assemblies\Personal Computer\Aip\Fan Connector.ipt` ("Embedding 1"; found by
scanning all 1250 compound files). As a component of Personal Computer.iam it
loads silently broken on Wine:
- `PartComponentDefinition.Features` and `MassProperties.Volume` return E_FAIL.
- The assembly volume is 5059.539 vs 5060.066 cm3 on the VM. That's 2 x 0.2635, the
  part's volume.
- `Rebuild2` returns False (True on the VM).

`WINEDEBUG=+packager` on Inventor:
- `PersistStorage_Load` succeeds.
- Inventor then asks for IOleLink {0000011d} and IViewObject2 {00000127}.
- `OleObject_QueryInterface` FIXMEs, E_NOINTERFACE, `Close`, `Release`, and the open fails.

## Windows ground truth (`tests/packager_ole.c`, Win11 VM vs Wine)
The probe runs `OleLoad` of that storage (temp copy, read-write transacted, like a
container) and also `CoCreateInstance(CLSID_Package)`. The results are the same for both.

| call | Windows | Wine |
|---|---|---|
| QI IOleObject, IPersistStorage | S_OK | S_OK |
| QI IDataObject, IViewObject, IViewObject2, IOleCache, IOleCache2, IRunnableObject, IOleCacheControl, IPersistFile, IExternalConnection | S_OK | E_NOINTERFACE |
| QI IOleLink | E_NOINTERFACE | E_NOINTERFACE |
| IOleObject::GetUserClassID | S_OK {0003000C-...} | E_NOTIMPL |
| IOleObject::GetExtent(CONTENT) | S_OK 2805x1429 (loaded); 1058x979 (new) | E_NOTIMPL |
| IViewObject2::GetExtent(CONTENT / ICON) | S_OK, same size | - |

On Windows the object behaves like the OLE default handler / embedding helper
with a data cache. The extent comes from the cached presentation. In Wine,
dlls/packager is a bare IOleObject + IPersistStorage with most methods stubbed.
Note: on Windows, OleLoad from a read-only storage fails (STG_E_UNKNOWN).

## Task
Give the Package object the interfaces Windows has. Likely by aggregating
ole32's data cache (CreateDataCache) or wrapping the object in
OleCreateEmbeddingHelper. Implement GetUserClassID/GetExtent. Tests in
dlls/packager/tests (QI set after CoCreateInstance and after OleLoad). Retest:
`INVSCEN_ONLY=Arm_Rest tools/invscen/run.sh samples` on an Inventor prefix.

## Findings (worker)
- Who asks for what: Wine's own `OleLoad` does the IOleLink QI (ole2.c, "handle OLE link");
  the IViewObject2 QI comes from MFC's COleClientItem (Inventor's `FWxOleClientItem`
  in FwSrv.dll wraps it; no Autodesk binary QIs it directly), so later calls are
  black-box: rerun Inventor with `WINEDEBUG=+packager` and look for new FIXMEs.
- Windows ground truth (`tests/packager_ole.c` extended, Win11): the Package object
  is one self-contained object, *not* a default handler/data cache:
  every interface has the same identity, aggregation → CLASS_E_NOAGGREGATION,
  IRunnableObject::IsRunning always TRUE, IOleCache2::EnumCache empty,
  own IDataObject formats (FileContents, FileGroupDescriptor, Embed Source,
  CF_METAFILEPICT, Object Descriptor). The extent comes from the object drawing
  its icon + label (the storage has no presentation stream): 2805x1429 loaded,
  1058x979 new. GetUserType: "Package" / "Packager Shell Object".
  PS IsDirty: S_OK when new, S_FALSE after Load/SaveCompleted. Save writes the
  class, `\1CompObj` (user type "OLE Package", ProgID "Package") and a *rewritten*
  `\1Ole10Native` (temp path of its extracted file; payload kept).
- `\1Ole10Native` layout: size, 2 bytes, ANSI label, ANSI icon path, WORD icon
  index, WORD type (3), temp path (len+ANSI), payload (len+data), then len+UTF-16
  temp path, label, icon path. Arm_Rest: "Arm Rest_3.htm", SHELL32.dll,0.

## Fix (branch fix/053-packager-viewobject, on master)
1. packager: Implement IViewObject2 and more IOleObject methods — IViewObject(2)
   draws/sizes OleMetafilePictFromIconAndLabel(icon from the stream's icon
   path/index, label); IOleObject::GetExtent via it; GetUserClassID/GetClassID
   = {0003000C}; GetClientSite, SetHostNames, Advise/Unadvise/EnumAdvise
   (OleAdviseHolder), Update/IsUpToDate S_OK, GetUserType from the registry.
2. packager: Implement IPersistStorage::Save() — keeps the loaded stream and
   writes it back byte-identical (Windows rewrites it; payload is what matters),
   plus WriteClassStg/WriteFmtUserTypeStg; IsDirty/InitNew/SaveCompleted/HandsOffStorage.
Left (todo_wine in the test): IDataObject, IOleCache(2), IOleCacheControl,
IRunnableObject, IPersistFile, IExternalConnection, IAdviseSink; EnumVerbs.
Wine's CompObj lacks the ProgID (ProgIDFromCLSID doesn't follow TreatAs) — harmless.
Tests: packager oleobj 52 tests pass on Win11 (32/64) and Wine (8 todo).
regress (packager ole32 oleaut32 olecli32 shell32) vs master 4e819f054dd: 0 worse.
Retest: `INVSCEN_ONLY=Arm_Rest tools/invscen/run.sh samples` and Personal Computer.iam
(Fan Connector Features/Volume, Rebuild2), with `WINEDEBUG=+packager` to catch the
next missing method.

## Verified in Inventor (build c09f08e4924, inv3)
Arm_Rest.ipt opens, rebuilds, saves, reopens: 4 features / 3 sketches, same as the VM.
Personal Computer.iam: 5060.066 cm3 (VM 5060.066) and Rebuild2 True; Fan Connector loads.
Remaining packager FIXMEs: QI IOleLink (Windows also E_NOINTERFACE), IRunnableObject {00000126}; harmless so far.
