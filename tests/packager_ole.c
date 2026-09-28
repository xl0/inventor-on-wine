/* OLE Packager (CLSID_Package) object as a container sees it (053).
 * CoCreateInstance(CLSID_Package) (+InitNew), then OleLoad of the
 * "RSeStorage\RSeEmbeddings\Embedding N" storage of an Inventor sample
 * (Ole10Native package, no presentation stream). For each: QI set, identity,
 * cache/data/view state, extents, Draw into an EMF, Save into a fresh storage.
 * Usage: packager_ole.exe [file.ipt [N]] (default: Arm_Rest.ipt, 3 on C:\t). */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static const CLSID CLSID_Package = {0x0003000c, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const struct { const IID *iid; const char *name; } iids[] = {
    {&IID_IOleObject, "IOleObject"}, {&IID_IPersistStorage, "IPersistStorage"},
    {&IID_IDataObject, "IDataObject"}, {&IID_IViewObject, "IViewObject"},
    {&IID_IViewObject2, "IViewObject2"}, {&IID_IOleCache, "IOleCache"},
    {&IID_IOleCache2, "IOleCache2"}, {&IID_IOleLink, "IOleLink"},
    {&IID_IRunnableObject, "IRunnableObject"}, {&IID_IOleCacheControl, "IOleCacheControl"},
    {&IID_IPersistFile, "IPersistFile"}, {&IID_IExternalConnection, "IExternalConnection"},
    {&IID_IPersist, "IPersist"}, {&IID_IAdviseSink, "IAdviseSink"},
    {&IID_IOleInPlaceObject, "IOleInPlaceObject"}, {&IID_IPersistStream, "IPersistStream"},
    {&IID_IEnumOLEVERB, "IEnumOLEVERB"}, {&IID_IMarshal, "IMarshal"},
    {&IID_IStdMarshalInfo, "IStdMarshalInfo"}, {&IID_IClassFactory, "IClassFactory"},
};

static void dump_stg(IStorage *stg, const char *indent)
{
    IEnumSTATSTG *e;
    STATSTG st;
    if (FAILED(IStorage_EnumElements(stg, 0, NULL, 0, &e))) return;
    while (IEnumSTATSTG_Next(e, 1, &st, NULL) == S_OK)
    {
        printf("%s%s '", indent, st.type == STGTY_STORAGE ? "stg" : "stm");
        for (WCHAR *p = st.pwcsName; *p; p++) printf(*p < 32 ? "\\%d" : "%lc", *p);
        printf("' %lu\n", st.cbSize.LowPart);
        CoTaskMemFree(st.pwcsName);
    }
    IEnumSTATSTG_Release(e);
    IStorage_Stat(stg, &st, STATFLAG_NONAME);
    printf("%sclsid {%08lx-...}\n", indent, st.clsid.Data1);
}

static void read_stream(IStorage *stg, const WCHAR *name, BYTE **data, ULONG *size)
{
    IStream *stm;
    STATSTG st;
    *data = NULL; *size = 0;
    if (FAILED(IStorage_OpenStream(stg, name, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stm))) return;
    IStream_Stat(stm, &st, STATFLAG_NONAME);
    *data = malloc(st.cbSize.LowPart);
    IStream_Read(stm, *data, st.cbSize.LowPart, size);
    IStream_Release(stm);
}

static void probe(IUnknown *unk, IStorage *loaded)
{
    IOleObject *ole;
    IViewObject2 *vo;
    IOleCache2 *cache;
    IDataObject *dobj;
    IRunnableObject *run;
    IPersistStorage *ps;
    IUnknown *id1, *id2;
    CLSID clsid;
    SIZEL sz;
    DWORD misc;
    HRESULT hr;
    int i;

    for (i = 0; i < ARRAYSIZE(iids); i++)
    {
        IUnknown *p = NULL;
        hr = IUnknown_QueryInterface(unk, iids[i].iid, (void **)&p);
        printf("  QI %-19s %08lx", iids[i].name, hr);
        if (p)
        {
            IUnknown_QueryInterface(p, &IID_IUnknown, (void **)&id1);
            printf(" identity %s", id1 == unk ? "same" : "DIFFERENT");
            IUnknown_Release(id1);
            IUnknown_Release(p);
        }
        printf("\n");
    }
    IUnknown_QueryInterface(unk, &IID_IUnknown, (void **)&id2);
    printf("  QI IUnknown same ptr %d\n", id2 == unk);
    IUnknown_Release(id2);

    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IRunnableObject, (void **)&run)))
    {
        printf("  IsRunning %d\n", IRunnableObject_IsRunning(run));
        IRunnableObject_Release(run);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IPersistStorage, (void **)&ps)))
    {
        memset(&clsid, 0, sizeof(clsid));
        hr = IPersistStorage_GetClassID(ps, &clsid);
        printf("  PS GetClassID %08lx {%08lx-...}\n", hr, clsid.Data1);
        printf("  PS IsDirty %08lx\n", IPersistStorage_IsDirty(ps));
        IPersistStorage_Release(ps);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IOleCache2, (void **)&cache)))
    {
        IEnumSTATDATA *e;
        STATDATA sd;
        hr = IOleCache2_EnumCache(cache, &e);
        printf("  EnumCache %08lx\n", hr);
        if (hr == S_OK)
        {
            while (IEnumSTATDATA_Next(e, 1, &sd, NULL) == S_OK)
                printf("    cf %d aspect %ld tymed %lx advf %lx conn %ld\n", sd.formatetc.cfFormat,
                       sd.formatetc.dwAspect, sd.formatetc.tymed, sd.advf, sd.dwConnection);
            IEnumSTATDATA_Release(e);
        }
        IOleCache2_Release(cache);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IDataObject, (void **)&dobj)))
    {
        static const struct { CLIPFORMAT cf; DWORD tymed; } fmts[] = {
            {CF_METAFILEPICT, TYMED_MFPICT}, {CF_ENHMETAFILE, TYMED_ENHMF}, {CF_DIB, TYMED_HGLOBAL}};
        IEnumFORMATETC *ef;
        FORMATETC fe;
        STGMEDIUM med;
        hr = IDataObject_EnumFormatEtc(dobj, DATADIR_GET, &ef);
        printf("  EnumFormatEtc %08lx\n", hr);
        if (hr == S_OK)
        {
            char name[64];
            while (IEnumFORMATETC_Next(ef, 1, &fe, NULL) == S_OK)
            {
                name[0] = 0;
                GetClipboardFormatNameA(fe.cfFormat, name, sizeof(name));
                printf("    cf %d '%s' aspect %ld tymed %lx\n", fe.cfFormat, name, fe.dwAspect, fe.tymed);
            }
            IEnumFORMATETC_Release(ef);
        }
        for (i = 0; i < ARRAYSIZE(fmts); i++)
        {
            DWORD aspect;
            for (aspect = DVASPECT_CONTENT; aspect <= DVASPECT_ICON; aspect <<= 2)
            {
                FORMATETC f = {fmts[i].cf, NULL, aspect, -1, fmts[i].tymed};
                hr = IDataObject_QueryGetData(dobj, &f);
                printf("  QueryGetData cf %d aspect %ld %08lx", f.cfFormat, aspect, hr);
                hr = IDataObject_GetData(dobj, &f, &med);
                printf(" GetData %08lx", hr);
                if (hr == S_OK && med.tymed == TYMED_MFPICT)
                {
                    METAFILEPICT *mfp = GlobalLock(med.hMetaFilePict);
                    printf(" mm %ld %ldx%ld bits %u", mfp->mm, mfp->xExt, mfp->yExt, GetMetaFileBitsEx(mfp->hMF, 0, NULL));
                    GlobalUnlock(med.hMetaFilePict);
                }
                if (hr == S_OK) ReleaseStgMedium(&med);
                printf("\n");
            }
        }
        IDataObject_Release(dobj);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IOleObject, (void **)&ole)))
    {
        IEnumOLEVERB *ev;
        OLEVERB verb;
        LPOLESTR str;
        memset(&clsid, 0, sizeof(clsid));
        hr = IOleObject_GetUserClassID(ole, &clsid);
        printf("  GetUserClassID %08lx {%08lx-...}\n", hr, clsid.Data1);
        for (i = 1; i <= 3; i++)
        {
            str = NULL;
            hr = IOleObject_GetUserType(ole, i, &str);
            printf("  GetUserType(%d) %08lx %ls\n", i, hr, str ? str : L"(null)");
            CoTaskMemFree(str);
        }
        sz.cx = sz.cy = -1;
        hr = IOleObject_GetExtent(ole, DVASPECT_CONTENT, &sz);
        printf("  IOleObject GetExtent(CONTENT) %08lx %ldx%ld\n", hr, sz.cx, sz.cy);
        sz.cx = sz.cy = -1;
        hr = IOleObject_GetExtent(ole, DVASPECT_ICON, &sz);
        printf("  IOleObject GetExtent(ICON) %08lx %ldx%ld\n", hr, sz.cx, sz.cy);
        hr = IOleObject_GetMiscStatus(ole, DVASPECT_CONTENT, &misc);
        printf("  GetMiscStatus(CONTENT) %08lx %lx\n", hr, misc);
        hr = IOleObject_GetMiscStatus(ole, DVASPECT_ICON, &misc);
        printf("  GetMiscStatus(ICON) %08lx %lx\n", hr, misc);
        printf("  IsUpToDate %08lx\n", IOleObject_IsUpToDate(ole));
        hr = IOleObject_EnumVerbs(ole, &ev);
        printf("  EnumVerbs %08lx\n", hr);
        if (hr == S_OK)
        {
            while (IEnumOLEVERB_Next(ev, 1, &verb, NULL) == S_OK)
            {
                printf("    verb %ld '%ls' flags %lx attribs %lx\n", verb.lVerb, verb.lpszVerbName, verb.fuFlags, verb.grfAttribs);
                CoTaskMemFree(verb.lpszVerbName);
            }
            IEnumOLEVERB_Release(ev);
        }
        IOleObject_Release(ole);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IViewObject2, (void **)&vo)))
    {
        DWORD aspect;
        for (aspect = DVASPECT_CONTENT; aspect <= DVASPECT_ICON; aspect <<= 2)
        {
            HDC dc;
            HENHMETAFILE emf;
            RECTL rc = {0, 0, 200, 100};
            sz.cx = sz.cy = -1;
            hr = IViewObject2_GetExtent(vo, aspect, -1, NULL, &sz);
            printf("  IViewObject2 GetExtent(%ld) %08lx %ldx%ld\n", aspect, hr, sz.cx, sz.cy);
            dc = CreateEnhMetaFileW(NULL, NULL, NULL, NULL);
            hr = IViewObject2_Draw(vo, aspect, -1, NULL, NULL, NULL, dc, &rc, NULL, NULL, 0);
            emf = CloseEnhMetaFile(dc);
            printf("  IViewObject2 Draw(%ld) %08lx emf bytes %u\n", aspect, hr, GetEnhMetaFileBits(emf, 0, NULL));
            DeleteEnhMetaFile(emf);
        }
        IViewObject2_Release(vo);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IPersistStorage, (void **)&ps)))
    {
        ILockBytes *lb;
        IStorage *dst;
        CreateILockBytesOnHGlobal(NULL, TRUE, &lb);
        StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &dst);
        hr = IPersistStorage_Save(ps, dst, FALSE);
        printf("  PS Save(new, FALSE) %08lx\n", hr);
        dump_stg(dst, "    ");
        if (loaded)
        {
            BYTE *a, *b;
            ULONG as, bs;
            read_stream(loaded, L"\1Ole10Native", &a, &as);
            read_stream(dst, L"\1Ole10Native", &b, &bs);
            printf("    Ole10Native identical to loaded: %d (%lu vs %lu)\n", as == bs && a && b && !memcmp(a, b, as), as, bs);
            free(a); free(b);
        }
        {
            BYTE *c; ULONG cs, k;
            read_stream(dst, L"\1CompObj", &c, &cs);
            printf("    CompObj:");
            for (k = 0; k < cs; k++) printf(" %02x", c[k]);
            printf("\n");
            free(c);
        }
        hr = IPersistStorage_SaveCompleted(ps, NULL);
        printf("  PS SaveCompleted(NULL) %08lx\n", hr);
        printf("  PS IsDirty %08lx\n", IPersistStorage_IsDirty(ps));
        IStorage_Release(dst);
        ILockBytes_Release(lb);
        IPersistStorage_Release(ps);
    }
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "C:\\t\\samples\\2022.orig\\Models\\Translation\\Arm Rest\\Arm_Rest.ipt";
    WCHAR emb[32];
    const WCHAR *names[] = {L"RSeStorage", L"RSeEmbeddings", emb};
    WCHAR wpath[MAX_PATH], src[MAX_PATH];
    IStorage *stg, *sub;
    IUnknown *unk;
    IPersistStorage *ps;
    HRESULT hr;
    int i;

    swprintf(emb, ARRAYSIZE(emb), L"Embedding %d", argc > 2 ? atoi(argv[2]) : 3);
    OleInitialize(NULL);
    hr = CoCreateInstance(&CLSID_Package, NULL, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                          &IID_IUnknown, (void **)&unk);
    printf("CoCreateInstance(CLSID_Package) %08lx\n", hr);
    if (SUCCEEDED(hr)) { probe(unk, NULL); IUnknown_Release(unk); }

    hr = CoCreateInstance(&CLSID_Package, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    if (SUCCEEDED(hr) && SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IPersistStorage, (void **)&ps)))
    {
        ILockBytes *lb;
        CreateILockBytesOnHGlobal(NULL, TRUE, &lb);
        StgCreateDocfileOnILockBytes(lb, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg);
        hr = IPersistStorage_InitNew(ps, stg);
        printf("CoCreateInstance + InitNew %08lx\n", hr);
        dump_stg(stg, "  after InitNew: ");
        probe(unk, NULL);
        IPersistStorage_Release(ps);
        IUnknown_Release(unk);
        IStorage_Release(stg);
        ILockBytes_Release(lb);
    }
    {
        IUnknown *outer = (IUnknown *)0xdeadbeef;
        hr = CoCreateInstance(&CLSID_Package, outer, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
        printf("CoCreateInstance aggregated %08lx\n", hr);
    }

    /* OleLoad needs a writable storage (read-only: STG_E_UNKNOWN on Windows); use a temp copy */
    GetTempPathW(MAX_PATH, wpath);
    lstrcatW(wpath, L"packager_probe.ipt");
    MultiByteToWideChar(CP_ACP, 0, path, -1, src, MAX_PATH);
    if (!CopyFileW(src, wpath, FALSE)) { printf("CopyFile %lu\n", GetLastError()); return 1; }
    hr = StgOpenStorage(wpath, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_TRANSACTED, NULL, 0, &stg);
    printf("StgOpenStorage %08lx\n", hr);
    if (FAILED(hr)) return 1;
    for (i = 0; i < ARRAYSIZE(names); i++)  /* parents stay open: releasing one reverts its children */
    {
        hr = IStorage_OpenStorage(stg, names[i], NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, NULL, 0, &sub);
        if (FAILED(hr)) { printf("OpenStorage %ls %08lx\n", names[i], hr); return 1; }
        stg = sub;
    }
    dump_stg(stg, "  before OleLoad: ");
    hr = OleLoad(stg, &IID_IUnknown, NULL, (void **)&unk);
    printf("OleLoad(%ls) %08lx\n", emb, hr);
    if (SUCCEEDED(hr)) { probe(unk, stg); IUnknown_Release(unk); }
    dump_stg(stg, "  after release: ");
    IStorage_Release(stg);
    OleUninitialize();
    return 0;
}
