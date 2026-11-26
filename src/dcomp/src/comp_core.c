/* comp_core.c - logging, object lifetime, QueryInterface, activation factories, DLL entry points */
#include "comp_types.h"
#include <stdarg.h>

/* ---------------- logging (same file and format as wxrrt's rt_log; we never delete it) ---------------- */
/* The UI and render threads log while holding g_logcs, so a slow log slows composition (a huge log once changed the Store's
   behaviour). Hence: one handle kept open, the file is moved to .old when over 32 MB at start, and at most 64 MB per process. */
static CRITICAL_SECTION g_logcs; static INIT_ONCE g_logonce = INIT_ONCE_STATIC_INIT; static WCHAR g_logpath[MAX_PATH]; static DWORD g_t0;
static HANDLE g_logf = INVALID_HANDLE_VALUE; static LONGLONG g_logbytes;
static BOOL CALLBACK log_init(PINIT_ONCE o, PVOID p, PVOID *c)
{
    HMODULE self = NULL; WCHAR *s, old[MAX_PATH]; WIN32_FILE_ATTRIBUTE_DATA fa; (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_logcs);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)log_init, &self);
    GetModuleFileNameW(self, g_logpath, MAX_PATH); s = wcsrchr(g_logpath, L'\\'); if (s) s[1] = 0;
    lstrcatW(g_logpath, L"wxr_activation.log"); g_t0 = GetTickCount();
    if (GetFileAttributesExW(g_logpath, GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 32u * 1024 * 1024)) {
        lstrcpynW(old, g_logpath, MAX_PATH - 8); lstrcatW(old, L".old"); MoveFileExW(g_logpath, old, MOVEFILE_REPLACE_EXISTING); }
    g_logf = CreateFileW(g_logpath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return TRUE;
}
void comp_log(const char *fmt, ...)
{
    char body[1500], line[1600]; va_list ap; DWORD w, n;
#ifdef WXR_RETAIL
    (void)fmt; return;   /* retail build: no wxr_activation.log, no debug output */
#endif
    InitOnceExecuteOnce(&g_logonce, log_init, NULL, NULL);
    va_start(ap, fmt); _vsnprintf(body, sizeof body - 1, fmt, ap); va_end(ap); body[sizeof body - 1] = 0;
    _snprintf(line, sizeof line - 1, "%6lu t%-5lu [comp] %s", (unsigned long)(GetTickCount() - g_t0), (unsigned long)GetCurrentThreadId(), body); line[sizeof line - 1] = 0;
    n = (DWORD)strlen(line);
    EnterCriticalSection(&g_logcs);
    if (g_logf != INVALID_HANDLE_VALUE && g_logbytes < 64LL * 1024 * 1024) { WriteFile(g_logf, line, n, &w, NULL); g_logbytes += n; }
    LeaveCriticalSection(&g_logcs);
    OutputDebugStringA(line);
}
/* diagnostics: "<what> <obj> stack: mod+off ..." (first 400 calls) */
void comp_log_stack(const char *what, void *obj)
{
    static LONG n; void *fr[12]; USHORT nf, i; char st[600]; int l = 0;
#ifdef WXR_RETAIL
    (void)what; (void)obj; return;
#endif
    if (InterlockedIncrement(&n) > 4000) return;
    nf = RtlCaptureStackBackTrace(1, 12, fr, NULL); st[0] = 0;
    for (i = 0; i < nf && l < 540; i++) { HMODULE m = NULL; char nm[64] = "?";
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fr[i], &m);
        if (m) { char p[MAX_PATH]; const char *s; GetModuleFileNameA(m, p, MAX_PATH); s = strrchr(p, '\\'); lstrcpynA(nm, s ? s + 1 : p, 40); }
        l += _snprintf(st + l, sizeof st - l, " %s+%llX", nm, (unsigned long long)((char *)fr[i] - (char *)m)); }
    comp_log("%s %p stack:%s\n", what, obj, st);
}
HRESULT comp_unimpl(const char *what) { comp_log("UNIMPLEMENTED %s\n", what); return E_NOTIMPL; }
static const char *guid_str(const GUID *g, char *b)
{
    _snprintf(b, 40, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]); return b;
}
HRESULT comp_dup_hstring(HSTRING s, WCHAR **out)
{
    UINT32 n = 0; const WCHAR *w = s ? WindowsGetStringRawBuffer(s, &n) : NULL; WCHAR *d;
    d = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((size_t)n + 1) * sizeof(WCHAR)); if (!d) return E_OUTOFMEMORY;
    if (w) memcpy(d, w, n * sizeof(WCHAR)); d[n] = 0; *out = d; return S_OK;
}

static const GUID IID_IUnknown_ = {0x00000000,0,0,{0xC0,0,0,0,0,0,0,0x46}};
static const GUID IID_IInspectable_ = {0xAF86E2E0,0xB12D,0x4C6A,{0x9C,0x5A,0xD7,0xAA,0x65,0x10,0x1E,0x90}};
#define IID_IUnknown IID_IUnknown_
#define IID_IInspectable IID_IInspectable_
/* ---------------- class table helpers ---------------- */
ClassImpl g_impl[CLS__COUNT];
int comp_is_a(int cls, int anc) { while (cls >= 0) { if (cls == anc) return 1; cls = g_cd[cls].base; } return 0; }
static int iface_count(const IfaceEntry *e) { int n = 0; while (e[n].iid) n++; return n; }

/* ---------------- objects ---------------- */
CObj *comp_new(int cls, CObj *comp)
{
    int nv = iface_count(g_cd[cls].ifs), chain[16], nc = 0, c, i; size_t sz = g_impl[cls].size, vo; CObj *o; char *mem;
    { int b; for (b = g_cd[cls].base; b >= 0; b = g_cd[b].base) if (g_impl[b].size > sz) sz = g_impl[b].size; }   /* a base's init writes its own extension: size by the largest in the chain (classes added without a size of their own overran into the views) */
    if (sz < sizeof(CObj)) sz = sizeof(CObj);
    vo = (sz + 15) & ~(size_t)15;
    mem = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, vo + (size_t)nv * sizeof(CView)); if (!mem) return NULL;
    o = (CObj *)mem; o->ref = 1; o->cls = cls; o->views = (CView *)(mem + vo); o->nviews = nv; o->ents = g_cd[cls].ifs;
    for (i = 0; i < nv; i++) { o->views[i].vt = g_cd[cls].ifs[i].vt; o->views[i].obj = o; }
    if (comp) { o->comp = comp; comp_addref(comp); }
    for (c = cls; c >= 0 && nc < 16; c = g_cd[c].base) chain[nc++] = c;
    for (i = nc - 1; i >= 0; i--) if (g_impl[chain[i]].init) g_impl[chain[i]].init(o);
    return o;
}
static void comp_destroy(CObj *o)
{
    int c; CObj *comp = o->comp;
    o->ref = 0x40000000;                         /* guard against AddRef/Release games during teardown */
    for (c = o->cls; c >= 0; c = g_cd[c].base) if (g_impl[c].destroy) g_impl[c].destroy(o);
    if (o->props) comp_release(o->props);
    if (o->implicit) comp_release(o->implicit);
    if (o->comment) HeapFree(GetProcessHeap(), 0, o->comment);
    HeapFree(GetProcessHeap(), 0, o);
    if (comp) comp_release(comp);
}
void comp_release(CObj *o) { if (o && !(o->flags & OBJ_STATIC) && InterlockedDecrement(&o->ref) == 0) comp_destroy(o); }
/* the object behind a caller-supplied interface, or NULL when the interface is not one of ours (its vtable lies outside this module):
   XAML hands out its own ICompositionSurface implementations (LoadedImageSurface), which have no CView layout to unwrap. */
CObj *comp_obj_of_any(void *iface, const char *what)
{
    static BYTE *lo, *hi; const BYTE *vt;
    if (!iface) return NULL;
    if (!lo) { HMODULE m = NULL; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)comp_obj_of_any, &m);
        if (m) { IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew); hi = (BYTE *)m + nt->OptionalHeader.SizeOfImage; lo = (BYTE *)m; } }
    vt = *(const BYTE **)iface;
    if (lo && (vt < lo || vt >= hi)) {
        /* 19041 XAML's LoadedImageSurface (and other XAML-made surfaces) implement the private
           facade interface {3C1C3D30-7C6F-40C2-8943-B151F36EE08D} (DirectUI::LoadedImageSurface::QueryInterfaceImpl, object + 0xC8): slot 6 = GetRealSurface
           (NOT {0020BF06-9182-4CB7-8A6C-BD15873899EB}: that is the class's internal identity; its slot 6 is an
           unrelated C++ virtual that ran DependencyObject::GetValue on our stack slot - E_UNEXPECTED or a crash in CClassInfo::IsEnum),
           the composition surface XAML created through us (its DCompSurface keeps it alive, so the reference is dropped here; callers
           comp_assign/addref the CObj). The same object later receives the decoded pixels. */
        static const GUID IID_FacadePartner = {0x3C1C3D30,0x7C6F,0x40C2,{0x89,0x43,0xB1,0x51,0xF3,0x6E,0xE0,0x8D}};
        IUnknown *fp = NULL;
        { static LONG npre; if (InterlockedIncrement(&npre) <= 30) { HSTRING n = NULL; UINT32 len = 0; const WCHAR *s = L"?";
            if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(*(void ***)iface)[4])(iface, &n)) && n) s = WindowsGetStringRawBuffer(n, &len);
            comp_log("%s: foreign %p (%ls) vt=%p: asking for its real surface\n", what, iface, s, (void *)vt); if (n) WindowsDeleteString(n); } }
        if (SUCCEEDED(((IUnknown *)iface)->lpVtbl->QueryInterface((IUnknown *)iface, &IID_FacadePartner, (void **)&fp)) && fp) {
            void *real = NULL; HRESULT hr2 = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)fp)[6])(fp, &real);
            fp->lpVtbl->Release(fp);
            if (SUCCEEDED(hr2) && real) {
                const BYTE *rv = *(const BYTE **)real; CObj *ro = (rv >= lo && rv < hi) ? OBJ_OF(real) : NULL; static LONG nfl;
                if (InterlockedIncrement(&nfl) <= 10) comp_log("%s: XAML surface %p -> real surface %p (%s)\n", what, iface, real, ro ? "ours" : "foreign, treated as empty");
                ((IUnknown *)real)->lpVtbl->Release((IUnknown *)real);
                return ro; }
            { static LONG nnl; if (InterlockedIncrement(&nnl) <= 10) comp_log("%s: XAML surface %p has no real surface yet (0x%08lX)\n", what, iface, (unsigned long)hr2); }
            return NULL; }
    }
    if (lo && (vt < lo || vt >= hi)) {
        static LONG nlog; HSTRING n = NULL; UINT32 len = 0; const WCHAR *s = L"?";
        if (InterlockedIncrement(&nlog) <= 10) {
            if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(*(void ***)iface)[4])(iface, &n)) && n) s = WindowsGetStringRawBuffer(n, &len);
            comp_log("%s: foreign object %p (%ls) is not drawable yet; treated as empty\n", what, iface, s);
            if (n) WindowsDeleteString(n); }
        return NULL; }
    return OBJ_OF(iface);
}
int comp_is_foreign(void *iface)   /* an interface whose vtable lies outside this module (XAML's own surfaces, ...) */
{
    HMODULE m = NULL; const BYTE *vt, *lo; IMAGE_NT_HEADERS *nt;
    if (!iface || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)comp_is_foreign, &m) || !m) return 0;
    lo = (const BYTE *)m; nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew); vt = *(const BYTE **)iface;
    return vt < lo || vt >= lo + nt->OptionalHeader.SizeOfImage;
}
void comp_assign(CObj **slot, CObj *n) { CObj *old = *slot; if (n) comp_addref(n); *slot = n; if (old) comp_release(old); }

static HRESULT qi_extra_chain(CObj *o, REFIID r, void **p)
{
    int c; for (c = o->cls; c >= 0; c = g_cd[c].base) if (g_impl[c].qi_extra && g_impl[c].qi_extra(o, r, p) == S_OK) return S_OK;
    return E_NOINTERFACE;
}
HRESULT comp_qi(CObj *o, REFIID r, void **p)
{
    int i;
    if (!p) return E_POINTER; *p = NULL;
    if (IsEqualGUID(r, &IID_IUnknown) || IsEqualGUID(r, &IID_IInspectable)) { *p = &o->views[0]; comp_addref(o); return S_OK; }
    for (i = 0; i < o->nviews; i++) if (IsEqualGUID(o->ents[i].iid, r)) { *p = &o->views[i]; comp_addref(o); return S_OK; }
    if (!(o->flags & OBJ_FACTORY) && qi_extra_chain(o, r, p) == S_OK) return S_OK;
    return E_NOINTERFACE;
}
HRESULT comp_return(CObj *o, REFIID iid, void **out) { if (!out) return E_POINTER; if (!o) { *out = NULL; return S_OK; } return comp_qi(o, iid, out); }
HRESULT comp_return_default(CObj *o, void **out)
{
    int i; const char *d;
    if (!out) return E_POINTER; if (!o) { *out = NULL; return S_OK; }
    d = g_cd[o->cls].def_iface;
    for (i = 0; i < o->nviews; i++) if (!strcmp(o->ents[i].name, d)) { *out = &o->views[i]; comp_addref(o); return S_OK; }
    *out = &o->views[0]; comp_addref(o); return S_OK;
}

HRESULT STDMETHODCALLTYPE cv_QI(void *This, REFIID r, void **p)
{
    CObj *o = OBJ_OF(This); HRESULT hr = comp_qi(o, r, p);
    if (FAILED(hr)) {   /* each (class, interface) once: these are asked constantly (CsWinRT probes), logging them all cost real time */
        static DWORD seen[512]; static LONG nseen; DWORD h = (DWORD)o->cls * 2654435761u ^ r->Data1 ^ ((DWORD)r->Data2 << 16) ^ r->Data4[7]; LONG k, ns = nseen; int dup = 0;
        for (k = 0; k < ns && k < 512; k++) if (seen[k] == h) { dup = 1; break; }
        if (!dup) { if (ns < 512) { seen[ns] = h; nseen = ns + 1; } { char g[40]; comp_log("QI %s iid=%s -> 0x%08lX (logged once)\n", g_cd[o->cls].sname, guid_str(r, g), (unsigned long)hr); } } }
    return hr;
}
ULONG STDMETHODCALLTYPE cv_AddRef(void *This) { CObj *o = OBJ_OF(This); if (o->flags & OBJ_STATIC) return 2; return (ULONG)InterlockedIncrement(&o->ref); }
ULONG STDMETHODCALLTYPE cv_Release(void *This) { CObj *o = OBJ_OF(This); LONG n; if (o->flags & OBJ_STATIC) return 1; n = InterlockedDecrement(&o->ref); if (n == 0) comp_destroy(o); return (ULONG)n; }
HRESULT STDMETHODCALLTYPE cv_GetIids(void *This, ULONG *c, IID **ids)
{
    CObj *o = OBJ_OF(This); IID *a; int i;
    a = (IID *)CoTaskMemAlloc(sizeof(IID) * (o->nviews ? o->nviews : 1)); if (!a) return E_OUTOFMEMORY;
    for (i = 0; i < o->nviews; i++) a[i] = *o->ents[i].iid;
    *c = (ULONG)o->nviews; *ids = a; return S_OK;
}
HRESULT STDMETHODCALLTYPE cv_GetRuntimeClassName(void *This, HSTRING *n) { const WCHAR *w = g_cd[OBJ_OF(This)->cls].name; return WindowsCreateString(w, (UINT32)lstrlenW(w), n); }
HRESULT STDMETHODCALLTYPE cv_GetTrustLevel(void *This, TrustLevel *t) { (void)This; *t = BaseTrust; return S_OK; }

/* ---------------- activation factories ---------------- */
static const GUID IID_IActivationFactory_ = {0x00000035, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
typedef struct AFVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *, ULONG *, IID **); HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *, HSTRING *);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *, TrustLevel *); HRESULT (STDMETHODCALLTYPE *ActivateInstance)(void *, IInspectable **); } AFVtbl;
CObj *comp_create_compositor(void);
static HRESULT STDMETHODCALLTYPE af_Activate(void *This, IInspectable **out)
{
    CObj *f = OBJ_OF(This); CObj *o; HRESULT hr;
    if (!out) return E_POINTER; *out = NULL;
    if (f->cls == CLS_Compositor) { o = comp_create_compositor(); if (!o) return E_OUTOFMEMORY; hr = comp_return_default(o, (void **)out); comp_release(o); return hr; }
    return comp_unimpl("ActivateInstance (class has no default constructor)");
}
static const AFVtbl VT_AF = { cv_QI, cv_AddRef, cv_Release, cv_GetIids, cv_GetRuntimeClassName, cv_GetTrustLevel, af_Activate };
static CObj *g_fact[CLS__COUNT];
#ifdef DCOMP_BUILD
/* IInteropCompositorFactoryPartner on the Compositor *factory* (dcomp_main.c): XAML gets its interop compositor through it */
HRESULT STDMETHODCALLTYPE dc_ifp_create(void *This, IUnknown *dev, IUnknown *cb, REFIID iid, void **out);
HRESULT STDMETHODCALLTYPE dc_ifp_check(void *This, boolean *interop, boolean *expose);
static const GUID IID_InteropCompositorFactoryPartner_ = {0x22118ADF, 0x23F1, 0x4801, {0xBC, 0xFA, 0x66, 0xCB, 0xF4, 0x8C, 0xC5, 0x1B}};
static const void *const VT_IFP[] = { (void *)cv_QI, (void *)cv_AddRef, (void *)cv_Release, (void *)cv_GetIids, (void *)cv_GetRuntimeClassName, (void *)cv_GetTrustLevel, (void *)dc_ifp_create, (void *)dc_ifp_check };
#endif
static CObj *get_factory(int cls)
{
    CObj *f, *prev; int ns = iface_count(g_cd[cls].statics), i, nx = 0; IfaceEntry *ents; char *mem;
    if (g_fact[cls]) return g_fact[cls];
#ifdef DCOMP_BUILD
    if (cls == CLS_Compositor) nx = 1;
#endif
    mem = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CObj) + sizeof(CView) * (ns + 1 + nx) + sizeof(IfaceEntry) * (ns + 2 + nx));
    if (!mem) return NULL;
    f = (CObj *)mem; f->views = (CView *)(mem + sizeof(CObj)); ents = (IfaceEntry *)(mem + sizeof(CObj) + sizeof(CView) * (ns + 1 + nx));
    ents[0].iid = &IID_IActivationFactory_; ents[0].vt = &VT_AF; ents[0].name = "IActivationFactory";
    for (i = 0; i < ns; i++) ents[i + 1] = g_cd[cls].statics[i];
#ifdef DCOMP_BUILD
    if (nx) { ents[ns + 1].iid = &IID_InteropCompositorFactoryPartner_; ents[ns + 1].vt = VT_IFP; ents[ns + 1].name = "IInteropCompositorFactoryPartner"; }
#endif
    ns += nx;
    f->ref = 1; f->cls = cls; f->flags = OBJ_STATIC | OBJ_FACTORY; f->nviews = ns + 1; f->ents = ents;
    for (i = 0; i <= ns; i++) { f->views[i].vt = ents[i].vt; f->views[i].obj = f; }
    prev = (CObj *)InterlockedCompareExchangePointer((void *volatile *)&g_fact[cls], f, NULL);
    if (prev) { HeapFree(GetProcessHeap(), 0, mem); return prev; }
    return f;
}

__declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(HSTRING id, void **factory)
{
    UINT32 n = 0; const WCHAR *name = id ? WindowsGetStringRawBuffer(id, &n) : NULL; int i; char b[200];
    if (!factory) return E_POINTER; *factory = NULL; if (!name) return E_INVALIDARG;
    comp_register_impls();
    for (i = 0; i < CLS__COUNT; i++)
        if ((UINT32)lstrlenW(g_cd[i].name) == n && CompareStringOrdinal(g_cd[i].name, (int)n, name, (int)n, TRUE) == CSTR_EQUAL) {
            CObj *f; if (!g_cd[i].activatable && !g_cd[i].statics[0].iid) break;
            f = get_factory(i); if (!f) return E_OUTOFMEMORY;
            comp_addref(f); *factory = &f->views[0]; return S_OK;
        }
    WideCharToMultiByte(CP_UTF8, 0, name, (int)n, b, sizeof b - 1, NULL, NULL); b[sizeof b - 1] = 0;
    comp_log("DllGetActivationFactory %.*s -> not registered\n", (int)n, b);
    return REGDB_E_CLASSNOTREG;
}
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r; if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); comp_register_impls(); } return TRUE;
}

/* own-mode build: dcomp.dll provides the real one */
#ifndef DCOMP_BUILD
int dc_visual_free(Visual *v) { (void)v; return 0; }
#endif
