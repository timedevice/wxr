/* rt_packages.c - Windows.Management.Deployment.PackageManager: the package inventory of the WXR environment, hand-written.
 *
 * Win10's PackageManager asks the AppX deployment service over RPC; Windows 7 has none. The WXR environment keeps its own records
 * (written by wxrpkg, see the old layer plan, "The WXR environment"):
 *     <root>\Users\<user>\packages.ini          [registered] PackageFullName=date     - what this user has
 *     <root>\ProgramData\Packages\<full>.ini    [package] Name/Publisher/Version/...  [dependencies] n=full name
 *     <root>\ProgramData\Shell\<aumid>.ini      [app] DisplayName=...  Package=<full> - start menu entries (wxr sync)
 * The Find* methods answer from them (read again on every call, so an install shows up at once) with Package objects of the generated,
 * hidden class Windows.ApplicationModel.Package (classes.txt "~"), whose important getters are below. <root> = the folder above the
 * Layer folder this DLL sits in. One user: the user-SID arguments are ignored.
 * Not here yet: Add/Remove/Update (the install queue comes with AppInstallManager), volumes (the inert default), Logo. */
#include "rt_core.h"
#include <stdio.h>
#include <stdlib.h>

HRESULT from_path(const WCHAR *cls, const GUID *iid, const WCHAR *path, IUnknown **op);   /* rt_pickers.c */
IUnknown *await_obj(IUnknown *op);
static const GUID IID_IStorageFolderStatics_p = { 0x08F327FF, 0x85D5, 0x48B9, { 0xAE, 0xE9, 0x28, 0x51, 0x1E, 0x33, 0x9F, 0x9F } };

typedef struct PkgRec {
    WCHAR name[128], publisher[256], pubid[16], version[40], arch[12], resid[64], full[200], family[150], path[MAX_PATH], display[128], bundle[200];
    int framework, resource; INT64 staged;   /* staged: FILETIME (UTC) */
    WCHAR deps[12][200]; int ndeps;
} PkgRec;

enum { PT_MAIN = 1, PT_FRAMEWORK = 2, PT_RESOURCE = 4, PT_BUNDLE = 8, PT_XAP = 16, PT_OPTIONAL = 32 };   /* Windows.Management.Deployment.PackageTypes */

static const WCHAR *raw(void *hs)
{
    static const WCHAR *(WINAPI *p)(void *, UINT32 *);
    if (!hs) return L"";
    if (!p) p = (const WCHAR *(WINAPI *)(void *, UINT32 *))rt_winrt_fn("WindowsGetStringRawBuffer");
    return p ? p(hs, NULL) : L"";
}
static int wxr_root(WCHAR *out)   /* "C:\WXR" */
{
    HMODULE m = NULL; WCHAR *b;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)wxr_root, &m) || !GetModuleFileNameW(m, out, MAX_PATH)) return 0;
    if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0;          /* ...\Layer */
    if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0;          /* root */
    return 1;
}
static INT64 parse_date(const WCHAR *s)   /* "2026-11-06 21:25:20" local time -> FILETIME UTC; 0 if unreadable */
{
    SYSTEMTIME st; FILETIME lf, uf; int y, mo, d, h = 0, mi = 0, se = 0;
    if (swscanf(s, L"%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3) return 0;
    memset(&st, 0, sizeof st); st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d; st.wHour = (WORD)h; st.wMinute = (WORD)mi; st.wSecond = (WORD)se;
    if (!SystemTimeToFileTime(&st, &lf) || !LocalFileTimeToFileTime(&lf, &uf)) return 0;
    return ((INT64)uf.dwHighDateTime << 32) | uf.dwLowDateTime;
}
static int load_rec(const WCHAR *root, const WCHAR *full, PkgRec *r)
{
    WCHAR ini[MAX_PATH], v[300], key[8]; int i;
    memset(r, 0, sizeof *r);
    if (_snwprintf(ini, MAX_PATH, L"%s\\ProgramData\\Packages\\%s.ini", root, full) < 0 || GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) return 0;
#define STR(k, f) GetPrivateProfileStringW(L"package", k, L"", r->f, sizeof r->f / sizeof(WCHAR), ini)
    STR(L"Name", name); STR(L"Publisher", publisher); STR(L"PublisherId", pubid); STR(L"Version", version); STR(L"Architecture", arch);
    STR(L"ResourceId", resid); STR(L"PackageFullName", full); STR(L"PackageFamilyName", family); STR(L"InstallLocation", path); STR(L"Bundle", bundle);
#undef STR
    if (!r->full[0]) lstrcpynW(r->full, full, 200);
    GetPrivateProfileStringW(L"package", L"IsFramework", L"", v, 300, ini); r->framework = !lstrcmpiW(v, L"True");
    GetPrivateProfileStringW(L"package", L"IsResourcePackage", L"", v, 300, ini); r->resource = !lstrcmpiW(v, L"True");
    GetPrivateProfileStringW(L"package", L"Staged", L"", v, 300, ini); r->staged = parse_date(v);
    for (i = 0; i < 12; i++) { _snwprintf(key, 8, L"%d", i); if (!GetPrivateProfileStringW(L"dependencies", key, L"", r->deps[r->ndeps], 200, ini)) break; r->ndeps++; }
    lstrcpynW(r->display, r->name, 128);
    return 1;
}
/* the display name wxr sync resolved for the package's start menu entry (the manifest only has an ms-resource: reference) */
static void load_display(const WCHAR *root, PkgRec *r)
{
    WCHAR pat[MAX_PATH], ini[MAX_PATH], v[200]; WIN32_FIND_DATAW fd; HANDLE h;
    if (r->framework || r->resource || _snwprintf(pat, MAX_PATH, L"%s\\ProgramData\\Shell\\%s!*.ini", root, r->family) < 0) return;
    if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return;
    _snwprintf(ini, MAX_PATH, L"%s\\ProgramData\\Shell\\%s", root, fd.cFileName); FindClose(h);
    if (GetPrivateProfileStringW(L"app", L"DisplayName", L"", v, 200, ini)) lstrcpynW(r->display, v, 128);
}
/* every package registered for the user; caller frees with HeapFree */
static PkgRec *load_all(int *n)
{
    WCHAR root[MAX_PATH], ini[MAX_PATH], user[128], *sec, *p; DWORD un = 128, cap = 64 * 1024; PkgRec *recs = NULL; int cnt = 0, max = 0;
    *n = 0;
    if (!wxr_root(root) || !GetUserNameW(user, &un)) return NULL;
    _snwprintf(ini, MAX_PATH, L"%s\\Users\\%s\\packages.ini", root, user);
    if (!(sec = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap * sizeof(WCHAR)))) return NULL;
    GetPrivateProfileSectionW(L"registered", sec, cap, ini);
    for (p = sec; *p; p += lstrlenW(p) + 1) max++;
    if (max && (recs = (PkgRec *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max * sizeof *recs)))
        for (p = sec; *p; p += lstrlenW(p) + 1) {
            WCHAR *eq = wcschr(p, L'='), full[200]; size_t len = eq ? (size_t)(eq - p) : (size_t)lstrlenW(p);
            if (!len || len >= 200) continue;
            memcpy(full, p, len * sizeof(WCHAR)); full[len] = 0;
            if (load_rec(root, full, &recs[cnt])) { if (eq && !recs[cnt].staged) recs[cnt].staged = parse_date(eq + 1); load_display(root, &recs[cnt]); cnt++; }
        }
    HeapFree(GetProcessHeap(), 0, sec);
    { static LONG once; if (!InterlockedExchange(&once, 1)) rt_log("PackageManager: %d package(s) registered in %ls", cnt, ini); }
    *n = cnt; return recs;
}

static int g_cls_pkg = -2, g_cls_id = -2, g_cls_status = -2;
static void classes(void)
{
    if (g_cls_pkg != -2) return;
    g_cls_id = rt_class_index(L"Windows.ApplicationModel.PackageId"); g_cls_status = rt_class_index(L"Windows.ApplicationModel.PackageStatus");
    g_cls_pkg = rt_class_index(L"Windows.ApplicationModel.Package");
}
static void *new_with_rec(int cls, const PkgRec *r)
{
    void *o; PkgRec *c;
    if (cls < 0 || !(o = rt_new_object(cls, NULL))) return NULL;
    if ((c = (PkgRec *)HeapAlloc(GetProcessHeap(), 0, sizeof *c))) { *c = *r; rt_set_user(o, c); }
    return o;
}
static int type_of(const PkgRec *r) { return r->framework ? PT_FRAMEWORK : r->resource ? PT_RESOURCE : PT_MAIN; }

/* the packages that pass the filters (NULL = any), as IIterable<Package> */
static HRESULT find(const WCHAR *name, const WCHAR *publisher, const WCHAR *family, UINT32 types, void **out, const char *what)
{
    const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IIterable`1<Windows.ApplicationModel.Package>");
    PkgRec *recs; IUnknown **items; int n, i, k = 0;
    if (!out) return E_POINTER; *out = NULL; if (!c) return E_NOTIMPL;
    classes(); recs = load_all(&n);
    items = n ? (IUnknown **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof *items) : NULL;
    for (i = 0; i < n && items; i++) {
        const PkgRec *r = &recs[i];
        if (!(type_of(r) & types)) continue;
        if (name && name[0] && lstrcmpiW(name, r->name)) continue;
        if (publisher && publisher[0] && lstrcmpiW(publisher, r->publisher)) continue;
        if (family && family[0] && lstrcmpiW(family, r->family)) continue;
        if ((items[k] = (IUnknown *)new_with_rec(g_cls_pkg, r))) k++;
    }
    *out = rt_new_coll_items(c, items, k);
    { static LONG nlog; if (InterlockedIncrement(&nlog) <= 40) rt_log("PackageManager.%s(name '%ls', family '%ls', types 0x%X) -> %d of %d", what, name ? name : L"*", family ? family : L"*", types, k, n); }
    for (i = 0; i < k; i++) items[i]->lpVtbl->Release(items[i]);
    if (items) HeapFree(GetProcessHeap(), 0, items); if (recs) HeapFree(GetProcessHeap(), 0, recs);
    return *out ? S_OK : E_OUTOFMEMORY;
}
static HRESULT find_full(const WCHAR *full, void **out)
{
    WCHAR root[MAX_PATH]; PkgRec r;
    if (!out) return E_POINTER; *out = NULL; classes();
    if (wxr_root(root) && full[0] && load_rec(root, full, &r)) { load_display(root, &r); *out = new_with_rec(g_cls_pkg, &r); }
    /* a bundle's full name (Name_Version_neutral_~_PublisherId): Windows finds the installed package of that bundle. wxrpkg records it
       as Bundle= in the main package's record. App Installer's Launch looked the bundle up, got nothing and did nothing. */
    if (!*out && full[0] && wcsstr(full, L"_~_")) { int n = 0, i; PkgRec *all = load_all(&n);
        for (i = 0; i < n && all; i++) if (!all[i].framework && !all[i].resource && !lstrcmpiW(all[i].bundle, full)) { *out = new_with_rec(g_cls_pkg, &all[i]); break; }
        if (all) HeapFree(GetProcessHeap(), 0, all);
        if (*out) rt_log("PackageManager.FindPackage(%ls): bundle -> its main package", full); }
    rt_log("PackageManager.FindPackage(%ls) -> %s", full, *out ? "found" : "none");
    return S_OK;   /* not found = null, as on Windows */
}
#define DEFAULT_TYPES (PT_MAIN | PT_FRAMEWORK)   /* what the overloads without PackageTypes return */
#define U32(a) ((UINT32)(UINT_PTR)(a))
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackages(void *This, void **out) { (void)This; return find(NULL, NULL, NULL, DEFAULT_TYPES, out, "FindPackages"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackagesForUser(void *This, void *sid, void **out) { (void)This; (void)sid; return find(NULL, NULL, NULL, DEFAULT_TYPES, out, "FindPackagesForUser"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackages_2(void *This, void *name, void *pub, void **out) { (void)This; return find(raw(name), raw(pub), NULL, DEFAULT_TYPES, out, "FindPackages(name, publisher)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackagesForUser_2(void *This, void *sid, void *name, void *pub, void **out) { (void)This; (void)sid; return find(raw(name), raw(pub), NULL, DEFAULT_TYPES, out, "FindPackagesForUser(name, publisher)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackage(void *This, void *full, void **out) { (void)This; return find_full(raw(full), out); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackages_3(void *This, void *fam, void **out) { (void)This; return find(NULL, NULL, raw(fam), DEFAULT_TYPES, out, "FindPackages(family)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackagesForUser_3(void *This, void *sid, void *fam, void **out) { (void)This; (void)sid; return find(NULL, NULL, raw(fam), DEFAULT_TYPES, out, "FindPackagesForUser(family)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager_FindPackageForUser(void *This, void *sid, void *full, void **out) { (void)This; (void)sid; return find_full(raw(full), out); }
HRESULT STDMETHODCALLTYPE S_IPackageManager2_FindPackagesWithPackageTypes(void *This, void *types, void **out) { (void)This; return find(NULL, NULL, NULL, U32(types), out, "FindPackagesWithPackageTypes"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager2_FindPackagesForUserWithPackageTypes(void *This, void *sid, void *types, void **out) { (void)This; (void)sid; return find(NULL, NULL, NULL, U32(types), out, "FindPackagesForUserWithPackageTypes"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager2_FindPackagesWithPackageTypes_2(void *This, void *name, void *pub, void *types, void **out) { (void)This; return find(raw(name), raw(pub), NULL, U32(types), out, "FindPackagesWithPackageTypes(name, publisher)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager2_FindPackagesForUserWithPackageTypes_2(void *This, void *sid, void *name, void *pub, void *types, void **out) { (void)This; (void)sid; return find(raw(name), raw(pub), NULL, U32(types), out, "FindPackagesForUserWithPackageTypes(name, publisher)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager2_FindPackagesWithPackageTypes_3(void *This, void *fam, void *types, void **out) { (void)This; return find(NULL, NULL, raw(fam), U32(types), out, "FindPackagesWithPackageTypes(family)"); }
HRESULT STDMETHODCALLTYPE S_IPackageManager2_FindPackagesForUserWithPackageTypes_3(void *This, void *sid, void *fam, void *types, void **out) { (void)This; (void)sid; return find(NULL, NULL, raw(fam), U32(types), out, "FindPackagesForUserWithPackageTypes(family)"); }

/* ---- Package ---- */
static const PkgRec g_none;
#define REC(This) (rt_user(This) ? (const PkgRec *)rt_user(This) : &g_none)
static HRESULT put_str(void **out, const WCHAR *s) { if (!out) return E_POINTER; *out = s[0] ? rt_hstring(s) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackage_get_Id(void *This, void **out) { if (!out) return E_POINTER; classes(); *out = new_with_rec(g_cls_id, REC(This)); return *out ? S_OK : E_OUTOFMEMORY; }
static HRESULT folder(void *This, void **out)
{
    IUnknown *op = NULL; const PkgRec *r = REC(This);
    if (!out) return E_POINTER; *out = NULL;
    if (r->path[0] && SUCCEEDED(from_path(L"Windows.Storage.StorageFolder", &IID_IStorageFolderStatics_p, r->path, &op)) && op) { *out = await_obj(op); op->lpVtbl->Release(op); }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IPackage_get_InstalledLocation(void *This, void **out) { return folder(This, out); }
HRESULT STDMETHODCALLTYPE S_IPackage7_get_EffectiveLocation(void *This, void **out) { return folder(This, out); }
HRESULT STDMETHODCALLTYPE S_IPackage_get_IsFramework(void *This, BYTE *out) { if (out) *out = (BYTE)REC(This)->framework; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackage_get_Dependencies(void *This, void **out)
{
    const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Package>");
    const PkgRec *r = REC(This); WCHAR root[MAX_PATH]; IUnknown *items[12]; PkgRec d; int i, k = 0;
    if (!out) return E_POINTER; *out = NULL; if (!c) return E_NOTIMPL; classes();
    if (wxr_root(root)) for (i = 0; i < r->ndeps; i++) if (load_rec(root, r->deps[i], &d) && (items[k] = (IUnknown *)new_with_rec(g_cls_pkg, &d))) k++;
    *out = rt_new_coll_items(c, items, k);
    for (i = 0; i < k; i++) items[i]->lpVtbl->Release(items[i]);
    return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IPackage2_get_DisplayName(void *This, void **out) { return put_str(out, REC(This)->display); }
HRESULT STDMETHODCALLTYPE S_IPackage2_get_PublisherDisplayName(void *This, void **out)
{   /* the CN of the publisher ("CN=Microsoft Corporation, O=..."): the manifest's display name is an ms-resource: reference */
    const WCHAR *p = wcsstr(REC(This)->publisher, L"CN="); WCHAR b[128]; int n = 0;
    if (p) for (p += 3; *p && *p != L',' && n < 127; p++) b[n++] = *p; b[n] = 0;
    return put_str(out, b);
}
HRESULT STDMETHODCALLTYPE S_IPackage2_get_IsResourcePackage(void *This, BYTE *out) { if (out) *out = (BYTE)REC(This)->resource; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackage3_get_Status(void *This, void **out) { (void)This; if (!out) return E_POINTER; classes(); *out = g_cls_status >= 0 ? rt_new_object(g_cls_status, NULL) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageStatus_VerifyIsOK(void *This, BYTE *out) { (void)This; if (out) *out = 1; return S_OK; }   /* every other status flag stays false */
HRESULT STDMETHODCALLTYPE S_IPackage3_get_InstalledDate(void *This, INT64 *out) { if (out) *out = REC(This)->staged; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageWithMetadata_get_InstallDate(void *This, INT64 *out) { if (out) *out = REC(This)->staged; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackage4_get_SignatureKind(void *This, int *out) { (void)This; if (out) *out = 3; return S_OK; }   /* PackageSignatureKind.Store: what these packages carry (wxrpkg does not verify signatures) */
HRESULT STDMETHODCALLTYPE S_IPackage8_get_InstalledPath(void *This, void **out) { return put_str(out, REC(This)->path); }
HRESULT STDMETHODCALLTYPE S_IPackage8_get_EffectivePath(void *This, void **out) { return put_str(out, REC(This)->path); }

/* ---- PackageId ---- */
HRESULT STDMETHODCALLTYPE S_IPackageId_get_Name(void *This, void **out) { return put_str(out, REC(This)->name); }
HRESULT STDMETHODCALLTYPE S_IPackageId_get_Version(void *This, UINT16 *out)   /* PackageVersion { Major, Minor, Build, Revision } */
{
    int v[4] = {0, 0, 0, 0}; if (!out) return E_POINTER;
    swscanf(REC(This)->version, L"%d.%d.%d.%d", &v[0], &v[1], &v[2], &v[3]);
    out[0] = (UINT16)v[0]; out[1] = (UINT16)v[1]; out[2] = (UINT16)v[2]; out[3] = (UINT16)v[3]; return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IPackageId_get_Architecture(void *This, int *out)   /* Windows.System.ProcessorArchitecture */
{
    const WCHAR *a = REC(This)->arch; if (!out) return E_POINTER;
    *out = !lstrcmpiW(a, L"x64") ? 9 : !lstrcmpiW(a, L"x86") ? 0 : !lstrcmpiW(a, L"arm") ? 5 : !lstrcmpiW(a, L"arm64") ? 12 : 11 /* Neutral */; return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IPackageId_get_ResourceId(void *This, void **out) { return put_str(out, REC(This)->resid); }
HRESULT STDMETHODCALLTYPE S_IPackageId_get_Publisher(void *This, void **out) { return put_str(out, REC(This)->publisher); }
HRESULT STDMETHODCALLTYPE S_IPackageId_get_PublisherId(void *This, void **out) { return put_str(out, REC(This)->pubid); }
HRESULT STDMETHODCALLTYPE S_IPackageId_get_FullName(void *This, void **out) { return put_str(out, REC(This)->full); }
HRESULT STDMETHODCALLTYPE S_IPackageId_get_FamilyName(void *This, void **out) { return put_str(out, REC(This)->family); }

/* ---- launchable entries: Package.GetAppListEntries[Async] -> AppListEntry.LaunchAsync (the Store's "Open") ----
 * An application of the package counts when wxr sync gave it a start menu entry (<root>\ProgramData\Shell\<family>!<id>.ini); helper
 * executables (Windows.FullTrustApplication entries without one) do not. Launching = what the start menu shortcut does:
 * <root>\Layer\wxr.exe activate <aumid>, i.e. through the broker. */
typedef struct AppRec { WCHAR aumid[220], display[128]; } AppRec;
static const AppRec g_noapp;
#define APP(This) (rt_user(This) ? (const AppRec *)rt_user(This) : &g_noapp)
static void *new_app(int cls, const AppRec *a)
{
    void *o; AppRec *c;
    if (cls < 0 || !(o = rt_new_object(cls, NULL))) return NULL;
    if ((c = (AppRec *)HeapAlloc(GetProcessHeap(), 0, sizeof *c))) { *c = *a; rt_set_user(o, c); }
    return o;
}
static void *app_entries(void *This)   /* IVectorView<AppListEntry> */
{
    const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Core.AppListEntry>");
    const PkgRec *r = REC(This); WCHAR root[MAX_PATH], pini[MAX_PATH], sini[MAX_PATH], *sec, *p; IUnknown *items[8]; int k = 0, i, cls = rt_class_index(L"Windows.ApplicationModel.Core.AppListEntry"); void *v;
    if (!c) return NULL;
    if (wxr_root(root) && r->full[0] && (sec = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 8192 * sizeof(WCHAR)))) {
        _snwprintf(pini, MAX_PATH, L"%s\\ProgramData\\Packages\\%s.ini", root, r->full);
        GetPrivateProfileSectionW(L"applications", sec, 8192, pini);
        for (p = sec; *p && k < 8; p += lstrlenW(p) + 1) {
            WCHAR *eq = wcschr(p, L'='); AppRec a; size_t len = eq ? (size_t)(eq - p) : 0;
            if (!len || len > 60) continue;
            memset(&a, 0, sizeof a); _snwprintf(a.aumid, 219, L"%s!%.*s", r->family, (int)len, p);
            if (_snwprintf(sini, MAX_PATH, L"%s\\ProgramData\\Shell\\%s.ini", root, a.aumid) < 0 || GetFileAttributesW(sini) == INVALID_FILE_ATTRIBUTES) continue;
            GetPrivateProfileStringW(L"app", L"DisplayName", r->display, a.display, 128, sini);
            if ((items[k] = (IUnknown *)new_app(cls, &a))) k++;
        }
        HeapFree(GetProcessHeap(), 0, sec);
    }
    v = rt_new_coll_items(c, items, k);
    for (i = 0; i < k; i++) items[i]->lpVtbl->Release(items[i]);
    return v;
}
HRESULT STDMETHODCALLTYPE S_IPackage8_GetAppListEntries(void *This, void **out) { if (!out) return E_POINTER; *out = app_entries(This); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IPackage3_GetAppListEntriesAsync(void *This, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Core.AppListEntry>>"); void *v;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    v = app_entries(This); if (!v) return E_OUTOFMEMORY;
    *out = rt_new_async_result(a, (IUnknown *)v); rt_Release(v); return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IAppListEntry2_get_AppUserModelId(void *This, void **out) { return put_str(out, APP(This)->aumid); }
HRESULT STDMETHODCALLTYPE S_IAppListEntry_get_DisplayInfo(void *This, void **out)
{ if (!out) return E_POINTER; *out = new_app(rt_class_index(L"Windows.ApplicationModel.AppDisplayInfo"), APP(This)); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IAppDisplayInfo_get_DisplayName(void *This, void **out) { return put_str(out, APP(This)->display); }
static HRESULT launch(void *This, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<bool>"); const AppRec *app = APP(This);
    WCHAR root[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH + 300]; STARTUPINFOW si; PROCESS_INFORMATION pi; BYTE ok = 0;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    if (app->aumid[0] && wxr_root(root)) {
        _snwprintf(exe, MAX_PATH, L"%s\\Layer\\wxr.exe", root); _snwprintf(cmd, MAX_PATH + 299, L"\"%s\" activate %s", exe, app->aumid);
        memset(&si, 0, sizeof si); si.cb = sizeof si;
        if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); ok = 1; }
        rt_log("AppListEntry.LaunchAsync: %ls -> %s (error %lu)", cmd, ok ? "started" : "FAILED", ok ? 0 : GetLastError());
    }
    *out = rt_new_async_value(a, &ok, 1); return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IAppListEntry_LaunchAsync(void *This, void **out) { return launch(This, out); }
HRESULT STDMETHODCALLTYPE S_IAppListEntry3_LaunchForUserAsync(void *This, void *user, void **out) { (void)user; return launch(This, out); }

/* ---- PackageVolume: the one volume of the environment = the drive of <root> ----
 * The inert stand-in answered 0 bytes free: the Store flashed "Free up some space" on Get. */
static void vol_drive(WCHAR *out) { WCHAR root[MAX_PATH]; if (wxr_root(root) && root[1] == L':') { out[0] = root[0]; out[1] = L':'; out[2] = L'\\'; out[3] = 0; } else lstrcpyW(out, L"C:\\"); }
HRESULT STDMETHODCALLTYPE S_IPackageVolume2_GetAvailableSpaceAsync(void *This, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<uint64>"); WCHAR d[8]; ULARGE_INTEGER fr; UINT64 v = 0;
    (void)This; if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    vol_drive(d); if (GetDiskFreeSpaceExW(d, &fr, NULL, NULL)) v = fr.QuadPart;
    { static LONG once; if (!InterlockedExchange(&once, 1)) rt_log("PackageVolume: %ls has %llu MB free", d, v / (1024 * 1024)); }
    *out = rt_new_async_value(a, &v, sizeof v); return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IPackageVolume_get_IsOffline(void *This, BYTE *out) { (void)This; if (out) *out = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageVolume_get_IsSystemVolume(void *This, BYTE *out) { (void)This; if (out) *out = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageVolume_get_SupportsHardLinks(void *This, BYTE *out) { (void)This; if (out) *out = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageVolume2_get_IsFullTrustPackageSupported(void *This, BYTE *out) { (void)This; if (out) *out = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageVolume2_get_IsAppxInstallSupported(void *This, BYTE *out) { (void)This; if (out) *out = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IPackageVolume_get_MountPoint(void *This, void **out) { WCHAR d[8]; (void)This; vol_drive(d); return put_str(out, d); }
HRESULT STDMETHODCALLTYPE S_IPackageVolume_get_Name(void *This, void **out) { WCHAR d[8], n[100] = L""; (void)This; vol_drive(d); if (!GetVolumeNameForVolumeMountPointW(d, n, 100)) lstrcpyW(n, d); return put_str(out, n); }
HRESULT STDMETHODCALLTYPE S_IPackageVolume_get_PackageStorePath(void *This, void **out) { WCHAR root[MAX_PATH], p[MAX_PATH] = L""; (void)This; if (wxr_root(root)) _snwprintf(p, MAX_PATH, L"%s\\WindowsApps", root); return put_str(out, p); }
