/* dcomp_manip.c - the DirectManipulation <-> XAML bridge (shared manipulation transforms).
 *
 * Win10 DirectManipulation (directmanipulation.dll 19041) moves scrolled content without XAML redrawing:
 *  - its compositor (CDCompManipulationCompositorImpl::_AddContent) creates a dcomp device of its own and, through the interop device
 *    {D14B6158}, slot 27 CreateSharedResource(IID_IDCompositionManipulationTransform {fc882465}, &mt), slot 28
 *    OpenSharedResourceHandle(mt, &handle), then on XAML's device slot 29 OpenSharedResource(handle, {a06cd146}, &shared) - the
 *    "shared content transform" it hands back to XAML;
 *  - on every change (_SetManipulationTransform) it writes the content transform into mt: SetTranslation(3), SetScale(4),
 *    SetCenterPoint(5), SetPan(6), each (x, y, z) (slot order from Win10 dcomp.pdb, CManipulationTransformProxy), then commits;
 *  - XAML turns the shared transform into a composition object with the partner's CreateManipulationTransform {e7894c70} slot 5 and
 *    reads its "Matrix" in the expression that places the content visual.
 * Here the transform values live in a page-file section (a real kernel handle: DM CloseHandles it), so both ends see the same memory.
 * The XAML-side object is a CompositionPropertySet whose "Matrix" is refreshed from the section at each composition tick. */
#include "dcomp_priv.h"

/* kind: 0 manipulation transform, 1 anything else (an inert object: DM's viewport interaction {6a432e05}, whose methods are all setters),
   2 shared visual (#1040 DCompositionCreateSharedVisualHandle; vis = the one visual every opener gets - same process) */
typedef struct DmShared { volatile LONG ver; int kind; GUID iid; float t[3], s[3], c[3], p[3]; CObj *vis; } DmShared;
#define SH_KIND_VISUAL 2

static const GUID IID_MT_ = {0xfc882465, 0xa93e, 0x489d, {0x8d, 0xec, 0xe3, 0xd9, 0x53, 0xeb, 0x8a, 0x69}};        /* IDCompositionManipulationTransform */

typedef struct MtObj { const void *const *vt; LONG ref; HANDLE sec; DmShared *sh; GUID iid; DmShared own; } MtObj;   /* own: storage of objects never shared by handle */

static volatile LONG g_dm_dirty;   /* DM wrote a transform since the last commit */

/* ---- DM-side IDCompositionManipulationTransform, opened shared transforms and inert shared objects ---- */
static HRESULT STDMETHODCALLTYPE mt_qi(MtObj *t, REFIID r, void **p)
{
    char g[40];
    if (!p) return E_POINTER;
    if (IsEqualGUID(r, &IID_IUnknown) || IsEqualGUID(r, &t->iid)) { *p = t; InterlockedIncrement(&t->ref); return S_OK; }
    *p = NULL; comp_log("manip: shared object %p QI %s -> E_NOINTERFACE\n", (void *)t, dc_guid(r, g)); return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE mt_addref(MtObj *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG STDMETHODCALLTYPE mt_release(MtObj *t)
{
    LONG r = InterlockedDecrement(&t->ref);
    if (!r) { if (t->sh && t->sh != &t->own) UnmapViewOfFile(t->sh); if (t->sec) CloseHandle(t->sec); HeapFree(GetProcessHeap(), 0, t); }
    return (ULONG)r;
}
static HRESULT mt_set(MtObj *t, float *dst, float x, float y, float z)
{
    static LONG n;
    if (dst[0] == x && dst[1] == y && dst[2] == z) return S_OK;
    if (InterlockedIncrement(&n) <= 60) comp_log("manip: %p %s (%g, %g, %g)\n", (void *)t, dst == t->sh->t ? "translation" : dst == t->sh->s ? "scale" : dst == t->sh->c ? "center" : "pan", x, y, z);
    dst[0] = x; dst[1] = y; dst[2] = z; InterlockedIncrement(&t->sh->ver); g_dm_dirty = 1; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mt_translation(MtObj *t, float x, float y, float z) { return mt_set(t, t->sh->t, x, y, z); }
static HRESULT STDMETHODCALLTYPE mt_scale(MtObj *t, float x, float y, float z) { return mt_set(t, t->sh->s, x, y, z); }
static HRESULT STDMETHODCALLTYPE mt_center(MtObj *t, float x, float y, float z) { return mt_set(t, t->sh->c, x, y, z); }
static HRESULT STDMETHODCALLTYPE mt_pan(MtObj *t, float x, float y, float z) { return mt_set(t, t->sh->p, x, y, z); }
static const void *const VT_MT[] = { (void *)mt_qi, (void *)mt_addref, (void *)mt_release, (void *)mt_translation, (void *)mt_scale, (void *)mt_center, (void *)mt_pan };
/* every other shared object: slots beyond IUnknown log their index. The opened transform (Win10 CSharedManipulationTransformProxy) has
   no methods of its own, so a call there is a surprise (E_NOTIMPL); the interaction's are setters (S_OK). */
#define SH_SLOT(n) static HRESULT STDMETHODCALLTYPE sh_slot##n(MtObj *t) { static LONG c; char g[40]; if (InterlockedIncrement(&c) <= 4) comp_log("manip: shared %s %p slot %d\n", dc_guid(&t->iid, g), (void *)t, n); return t->sh->kind ? S_OK : E_NOTIMPL; }
SH_SLOT(3) SH_SLOT(4) SH_SLOT(5) SH_SLOT(6) SH_SLOT(7) SH_SLOT(8) SH_SLOT(9) SH_SLOT(10) SH_SLOT(11) SH_SLOT(12) SH_SLOT(13) SH_SLOT(14)
SH_SLOT(15) SH_SLOT(16) SH_SLOT(17) SH_SLOT(18) SH_SLOT(19) SH_SLOT(20) SH_SLOT(21) SH_SLOT(22) SH_SLOT(23)
static const void *const VT_SH[] = { (void *)mt_qi, (void *)mt_addref, (void *)mt_release, (void *)sh_slot3, (void *)sh_slot4, (void *)sh_slot5, (void *)sh_slot6,
    (void *)sh_slot7, (void *)sh_slot8, (void *)sh_slot9, (void *)sh_slot10, (void *)sh_slot11, (void *)sh_slot12, (void *)sh_slot13, (void *)sh_slot14,
    (void *)sh_slot15, (void *)sh_slot16, (void *)sh_slot17, (void *)sh_slot18, (void *)sh_slot19, (void *)sh_slot20, (void *)sh_slot21, (void *)sh_slot22, (void *)sh_slot23 };
static int is_ours(IUnknown *u) { return u && (((MtObj *)u)->vt == VT_MT || ((MtObj *)u)->vt == VT_SH); }

static MtObj *mt_new(HANDLE sec, REFIID iid)
{
    MtObj *t = (MtObj *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t);
    if (!t) return NULL;
    t->ref = 1; t->sec = sec; t->iid = *iid;
    t->sh = (DmShared *)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DmShared));
    if (!t->sh) { HeapFree(GetProcessHeap(), 0, t); return NULL; }
    t->vt = IsEqualGUID(iid, &IID_MT_) ? VT_MT : VT_SH;
    return t;
}

/* ---- interop device slots 27..29 (also IDCompositionDesktopDevicePartner3 {28d6ad3d}, the same table) ---- */
HRESULT STDMETHODCALLTYPE dcD_CreateSharedResource(void *This, REFIID iid, void **out)
{
    HANDLE sec; MtObj *t; char g[40]; (void)This;
    if (!out) return E_POINTER; *out = NULL;
    sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL); if (!sec) return HRESULT_FROM_WIN32(GetLastError());
    t = mt_new(sec, iid); if (!t) { CloseHandle(sec); return E_OUTOFMEMORY; }
    t->sh->kind = !IsEqualGUID(iid, &IID_MT_); t->sh->iid = *iid;
    t->sh->s[0] = t->sh->s[1] = t->sh->s[2] = 1.0f;
    comp_log("manip: shared %s %p created\n", dc_guid(iid, g), (void *)t);
    *out = t; return S_OK;
}
HRESULT STDMETHODCALLTYPE dcD_OpenSharedResourceHandle(void *This, IUnknown *res, HANDLE *h)
{
    MtObj *t = (MtObj *)res; (void)This;
    if (!h) return E_POINTER; *h = NULL;
    if (!is_ours(res)) { comp_log("manip: OpenSharedResourceHandle on a foreign object %p\n", (void *)res); return E_INVALIDARG; }
    if (!DuplicateHandle(GetCurrentProcess(), t->sec, GetCurrentProcess(), h, 0, FALSE, DUPLICATE_SAME_ACCESS)) return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}
/* ---- IDCompositionDevice CreateEffectGroup (slot 21 of Desktop/Dev3/Interop, 23 of Dev1) ----
   IDCompositionEffectGroup {A7929A74-E6B2-4BD6-8B95-4040119CA34D} : IDCompositionEffect {EC81B08F-...} : IUnknown:
   3 SetOpacity(animation), 4 SetOpacity(float), 5 SetTransform3D(transform). WinUI 3's title bar (Windowing.Core) creates one per
   caption button. KNOWN GAP: accepted and stored, not yet applied when a visual's SetEffect gets it. */
typedef struct EffGroup { const void *const *vt; LONG ref; float opacity; } EffGroup;
static const GUID IID_EffGroup_ = {0xa7929a74, 0xe6b2, 0x4bd6, {0x8b, 0x95, 0x40, 0x40, 0x11, 0x9c, 0xa3, 0x4d}};
static const GUID IID_Effect_ = {0xec81b08f, 0xbfcb, 0x4e8d, {0xb1, 0x93, 0xa9, 0x15, 0x58, 0x79, 0x99, 0xe8}};
static HRESULT STDMETHODCALLTYPE eg_qi(EffGroup *e, REFIID r, void **p)
{
    char g[40]; if (!p) return E_POINTER;
    if (IsEqualGUID(r, &IID_IUnknown) || IsEqualGUID(r, &IID_EffGroup_) || IsEqualGUID(r, &IID_Effect_)) { *p = e; InterlockedIncrement(&e->ref); return S_OK; }
    *p = NULL; comp_log("effect group %p QI %s -> E_NOINTERFACE\n", (void *)e, dc_guid(r, g)); return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE eg_addref(EffGroup *e) { return (ULONG)InterlockedIncrement(&e->ref); }
static ULONG STDMETHODCALLTYPE eg_release(EffGroup *e) { LONG r = InterlockedDecrement(&e->ref); if (!r) HeapFree(GetProcessHeap(), 0, e); return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE eg_opacity_anim(EffGroup *e, void *anim) { (void)e; (void)anim; return S_OK; }
static HRESULT STDMETHODCALLTYPE eg_opacity(EffGroup *e, float o) { e->opacity = o; return S_OK; }
static HRESULT STDMETHODCALLTYPE eg_transform3d(EffGroup *e, void *t) { (void)e; (void)t; return S_OK; }
static const void *const VT_EG[] = { (void *)eg_qi, (void *)eg_addref, (void *)eg_release, (void *)eg_opacity_anim, (void *)eg_opacity, (void *)eg_transform3d };
HRESULT STDMETHODCALLTYPE dcD_CreateEffectGroup(void *This, void **out)
{
    EffGroup *e; static LONG n; (void)This; if (!out) return E_POINTER;
    e = (EffGroup *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *e); if (!e) { *out = NULL; return E_OUTOFMEMORY; }
    e->vt = VT_EG; e->ref = 1; e->opacity = 1.0f; if (InterlockedIncrement(&n) <= 4) comp_log("effect group %p created (inert)\n", (void *)e);
    *out = e; return S_OK;
}

/* ---- shared visuals (WinUI 3) ----
   Windows App SDK's FrameworkUdk (DCompPrivates_CreateSharedVisualHandle) calls system dcomp #1040 DCompositionCreateSharedVisualHandle
   (HANDLE *out) (name from Win10 19045 dcomp.pdb), then opens the handle on its system devices with the interop device's slot 29
   OpenSharedResource(handle, riid, out) (DCompPrivates_OpenSharedDCompositionObject). On Win10 that is a DWM-side visual several devices
   share; here every device lives in this process, so the section just carries the pointer of one visual all openers get. */
HRESULT WINAPI dcOrd_1040(HANDLE *out)
{
    HANDLE sec; DmShared *sh;
    if (!out) return E_POINTER; *out = NULL;
    sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL); if (!sec) return HRESULT_FROM_WIN32(GetLastError());
    sh = (DmShared *)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, sizeof *sh);
    if (!sh) { CloseHandle(sec); return E_OUTOFMEMORY; }
    sh->kind = SH_KIND_VISUAL; UnmapViewOfFile(sh);
    comp_log("shared visual handle %p created (#1040)\n", (void *)sec);
    *out = sec; return S_OK;
}
HRESULT STDMETHODCALLTYPE dcD_CreateVisual(void *This, void **out);
HRESULT STDMETHODCALLTYPE dcV_AddVisual(void *This, void *child, BOOL above, void *ref);
HRESULT STDMETHODCALLTYPE dcV_RemoveAllVisuals(void *This);
extern const GUID DCIID_Target, DCIID_Visual;
/* The same handle opened as IDCompositionTarget (Microsoft.UI.Input dcomptarget.cpp): the content side's target. SetRoot(v) makes v
   the only child of the shared visual, which the host side has placed in its HWND target's tree. */
/* Several targets can feed one shared visual (WinUI 3: the XAML content through the interop device, the system backdrop through the
   WinRT compositor's ICompositorPartner): each replaces only its own child. bottom = insert below the others (backdrops). */
HRESULT STDMETHODCALLTYPE dcV_RemoveVisual(void *This, void *child);
typedef struct ShTarget { const void *const *vt; LONG ref; CObj *vis; void *root; int bottom; } ShTarget;
static HRESULT sht_set(ShTarget *t, void *root)
{
    void *pv = NULL; HRESULT hr = comp_qi(t->vis, &DCIID_Visual, &pv); if (FAILED(hr)) return hr;
    if (t->root) { dcV_RemoveVisual(pv, t->root); ((IUnknown *)t->root)->lpVtbl->Release((IUnknown *)t->root); t->root = NULL; }
    hr = S_OK;
    if (root) { hr = dcV_AddVisual(pv, root, !t->bottom, NULL); if (SUCCEEDED(hr)) { t->root = root; ((IUnknown *)root)->lpVtbl->AddRef((IUnknown *)root); } }
    ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
    comp_log("shared target %p: SetRoot(%p) -> %s child of shared visual %p 0x%08lX\n", (void *)t, root, t->bottom ? "bottom" : "top", (void *)t->vis, (unsigned long)hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE sht_qi(ShTarget *t, REFIID r, void **p)
{
    char g[40]; if (!p) return E_POINTER;
    if (IsEqualGUID(r, &IID_IUnknown) || IsEqualGUID(r, &DCIID_Target)) { *p = t; InterlockedIncrement(&t->ref); return S_OK; }
    *p = NULL; comp_log("shared target %p QI %s -> E_NOINTERFACE\n", (void *)t, dc_guid(r, g)); return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sht_addref(ShTarget *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG STDMETHODCALLTYPE sht_release(ShTarget *t) { LONG r = InterlockedDecrement(&t->ref); if (!r) { if (t->root) ((IUnknown *)t->root)->lpVtbl->Release((IUnknown *)t->root); comp_release(t->vis); HeapFree(GetProcessHeap(), 0, t); } return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE sht_setroot(ShTarget *t, void *root) { return sht_set(t, root); }
static const void *const VT_SHT[] = { (void *)sht_qi, (void *)sht_addref, (void *)sht_release, (void *)sht_setroot };
/* the WinRT side: ICompositorPartner {4BA72EC1} slot 8 OpenSharedTargetFromHandle(HANDLE, IVisualTargetPartner **) (FrameworkUdk
   DCompPrivates_OpenAndAttachSharedTarget, used by WinUI 3's system backdrop): IUnknown-based, slot 4 = SetRoot(IVisual *); slot 3
   unknown (logged). */
static HRESULT STDMETHODCALLTYPE vtp_qi(ShTarget *t, REFIID r, void **p)
{
    char g[40]; if (!p) return E_POINTER; *p = t; InterlockedIncrement(&t->ref);
    if (!IsEqualGUID(r, &IID_IUnknown)) comp_log("shared visual target %p QI %s -> self\n", (void *)t, dc_guid(r, g)); return S_OK;
}
static HRESULT STDMETHODCALLTYPE vtp_slot3(ShTarget *t, void *a) { comp_log("shared visual target %p slot 3 (%p) - unknown, S_OK\n", (void *)t, a); return S_OK; }
static const void *const VT_VTP[] = { (void *)vtp_qi, (void *)sht_addref, (void *)sht_release, (void *)vtp_slot3, (void *)sht_setroot };
static HRESULT open_shared_visual(void *This, HANDLE h, DmShared *sh, REFIID iid, void **out);
HRESULT STDMETHODCALLTYPE dcQ1_open_shared_target(void *This, HANDLE h, void **out)
{
    DmShared *sh; ShTarget *t; HRESULT hr = S_OK; CObj *comp = OBJ_OF(This);
    if (!out) return E_POINTER; *out = NULL;
    sh = h ? (DmShared *)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DmShared)) : NULL;
    if (!sh) { comp_log("OpenSharedTargetFromHandle(%p): not a shared visual handle\n", (void *)h); return E_INVALIDARG; }
    if (sh->kind != SH_KIND_VISUAL) { UnmapViewOfFile(sh); return E_INVALIDARG; }
    if (!sh->vis) { CObj *v = comp_new(CLS_SpriteVisual, comp); if (!v) hr = E_OUTOFMEMORY; else sh->vis = v; }   /* normally the interop device made it first */
    if (SUCCEEDED(hr)) { t = (ShTarget *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t); if (!t) hr = E_OUTOFMEMORY;
        else { t->vt = VT_VTP; t->ref = 1; t->vis = sh->vis; comp_addref(t->vis); t->bottom = 1; *out = t;
            comp_log("OpenSharedTargetFromHandle(%p) on compositor %p -> backdrop target %p (shared visual %p)\n", (void *)h, (void *)comp, (void *)t, (void *)sh->vis); } }
    UnmapViewOfFile(sh); return hr;
}
static HRESULT open_shared_visual(void *This, HANDLE h, DmShared *sh, REFIID iid, void **out)
{
    HRESULT hr; char g[40];
    if (IsEqualGUID(iid, &DCIID_Target)) {
        ShTarget *t;
        if (!sh->vis) { void *v = NULL; hr = dcD_CreateVisual(This, &v); if (FAILED(hr)) return hr; sh->vis = OBJ_OF(v); comp_addref(sh->vis); ((IUnknown *)v)->lpVtbl->Release((IUnknown *)v); }
        t = (ShTarget *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t); if (!t) return E_OUTOFMEMORY;
        t->vt = VT_SHT; t->ref = 1; t->vis = sh->vis; comp_addref(t->vis);
        comp_log("shared visual %p opened on device %p as target %p\n", (void *)h, This, (void *)t);
        *out = t; return S_OK;
    }
    if (!sh->vis) {   /* first opener creates it on its device */
        void *v = NULL; hr = dcD_CreateVisual(This, &v); if (FAILED(hr)) return hr;
        sh->vis = OBJ_OF(v); comp_addref(sh->vis); ((IUnknown *)v)->lpVtbl->Release((IUnknown *)v);
        comp_log("shared visual %p: visual %p created on device %p\n", (void *)h, (void *)sh->vis, This);
    }
    hr = comp_qi(sh->vis, iid, out);
    comp_log("shared visual %p opened on device %p as %s -> 0x%08lX\n", (void *)h, This, dc_guid(iid, g), (unsigned long)hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE dcD_OpenSharedResource(void *This, HANDLE h, REFIID iid, void **out)
{
    HANDLE sec; MtObj *t; char g[40], g2[40]; (void)This;
    if (!out) return E_POINTER; *out = NULL;
    { DmShared *sh = h ? (DmShared *)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DmShared)) : NULL; HRESULT hr;
      if (sh) { if (sh->kind == SH_KIND_VISUAL) { hr = open_shared_visual(This, h, sh, iid, out); UnmapViewOfFile(sh); return hr; } UnmapViewOfFile(sh); } }
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &sec, 0, FALSE, DUPLICATE_SAME_ACCESS)) return HRESULT_FROM_WIN32(GetLastError());
    t = mt_new(sec, iid); if (!t) { CloseHandle(sec); return E_OUTOFMEMORY; }
    if (t->vt == VT_MT && t->sh->kind) t->vt = VT_SH;   /* only a manipulation transform section opens as one */
    comp_log("manip: shared %s opened as %s %p\n", dc_guid(&t->sh->iid, g), dc_guid(iid, g2), (void *)t);
    *out = t; return S_OK;
}

/* slot 43, Win10's second OpenSharedResource overload (handle, unknown - 0 so far, iid, out): DirectManipulation opens its viewport
   interaction {6a432e05} this way */
HRESULT STDMETHODCALLTYPE dcD_OpenSharedResource2(void *This, HANDLE h, void *unk, REFIID iid, void **out)
{
    if (unk) comp_log("manip: OpenSharedResource#43 second argument %p\n", unk);
    return dcD_OpenSharedResource(This, h, iid, out);
}/* interop device slots 38..41 (Win10 dcomp.pdb IDCompositionDesktopDevicePartner6): DirectManipulation's viewport interaction and its
   touch / touchpad / pen configurations (this, 1, 0xFFFFFFFF, value, out). Input reaches DM through the dispatch hooks here, not
   through DWM, so these are inert objects whose setters log and succeed. */
static HRESULT inert_new(const char *what, void **out)
{
    static LONG n; MtObj *t;
    if (!out) return E_POINTER; *out = NULL;
    /* DM rebuilds these on every viewport update: heap storage, no section (they are never shared by handle) */
    t = (MtObj *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t); if (!t) return E_OUTOFMEMORY;
    t->vt = VT_SH; t->ref = 1; t->sh = &t->own; t->sh->kind = 1;
    if (InterlockedIncrement(&n) <= 8) comp_log("manip: %s %p created\n", what, (void *)t);
    *out = t; return S_OK;
}
HRESULT STDMETHODCALLTYPE dcD_CreateInteraction(void *This, void **out) { (void)This; return inert_new("interaction", out); }
HRESULT STDMETHODCALLTYPE dcD_CreateTouchInteractionConfiguration(void *This, int a, int b, int c, void **out) { (void)This; (void)a; (void)b; (void)c; return inert_new("touch configuration", out); }
HRESULT STDMETHODCALLTYPE dcD_CreateTouchpadInteractionConfiguration(void *This, int a, int b, int c, void **out) { (void)This; (void)a; (void)b; (void)c; return inert_new("touchpad configuration", out); }
HRESULT STDMETHODCALLTYPE dcD_CreatePenInteractionConfiguration(void *This, int a, int b, int c, void **out) { (void)This; (void)a; (void)b; (void)c; return inert_new("pen configuration", out); }
/* ---- XAML side: partner {e7894c70} slot 5 CreateManipulationTransform(transform, iid, out) ---- */
typedef struct MtLink { struct MtLink *next; CObj *pset; DmShared *sh; LONG ver; MtObj *src; } MtLink;
static MtLink *g_links; static CRITICAL_SECTION g_links_cs; static LONG g_links_init;
static void links_lock(int on)
{
    if (!g_links_init) { if (!InterlockedCompareExchange(&g_links_init, 1, 0)) { InitializeCriticalSection(&g_links_cs); g_links_init = 2; } while (g_links_init != 2) Sleep(0); }
    if (on) EnterCriticalSection(&g_links_cs); else LeaveCriticalSection(&g_links_cs);
}
/* Matrix4x4 (row vectors, translation in M41..M43): scale about the center point, then the translation. DM writes the same offset into
   translation and pan (_SetManipulationTransform), so pan is not added again. */
static void mt_matrix(const DmShared *s, float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = s->s[0]; m[5] = s->s[1]; m[10] = s->s[2] ? s->s[2] : 1.0f; m[15] = 1.0f;
    m[12] = s->c[0] - s->c[0] * s->s[0] + s->t[0];
    m[13] = s->c[1] - s->c[1] * s->s[1] + s->t[1];
    m[14] = s->t[2];
}
/* what expressions read from the transform: "Matrix" (XAML's content placement) and the components XAML binds the ScrollViewer
   manipulation property set to ("manipTransform.Translation+offset" -> Translation, likewise Pan, CenterPoint; "manipTransform.Scale") */
static void mt_publish(CObj *ps, const DmShared *s)
{
    float m[16];
    mt_matrix(s, m); comp_pset_put(ps, L"Matrix", 8, m, 16);
    comp_pset_put(ps, L"Translation", 3, s->t, 3); comp_pset_put(ps, L"Scale", 3, s->s, 3);
    comp_pset_put(ps, L"CenterPoint", 3, s->c, 3); comp_pset_put(ps, L"Pan", 3, s->p, 3);
}
HRESULT STDMETHODCALLTYPE dcP_CreateManipulationTransform(void *This, IUnknown *transform, REFIID iid, void **out)
{
    CObj *comp = ((CView *)This)->obj, *ps; MtObj *t = (MtObj *)transform; MtLink *l; float m[16]; HRESULT hr; char g[40];
    if (!out) return E_POINTER; *out = NULL;
    if (!is_ours(transform) || t->sh->kind) { comp_log("manip: CreateManipulationTransform on a foreign transform %p\n", (void *)transform); return E_INVALIDARG; }
    ps = comp_new(CLS_CompositionPropertySet, comp); if (!ps) return E_OUTOFMEMORY;
    (void)m; mt_publish(ps, t->sh);
    l = (MtLink *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *l);
    if (l) { l->pset = ps; comp_addref(ps); l->src = t; mt_addref(t); l->sh = t->sh; l->ver = t->sh->ver; links_lock(1); l->next = g_links; g_links = l; links_lock(0); }
    hr = comp_qi(ps, iid, out); comp_release(ps);
    comp_log("manip: CreateManipulationTransform(%p, %s) -> property set %p 0x%08lX\n", (void *)transform, dc_guid(iid, g), (void *)ps, (unsigned long)hr);
    return hr;
}
/* comp_tick: copy changed transforms into their property sets (the expressions then move the content) */
void dcomp_manip_tick(CObj *comp)
{
    MtLink *l; float m[16];
    if (!g_links) return;
    links_lock(1);
    for (l = g_links; l; l = l->next) {
        LONG v = l->sh->ver; if (l->pset->comp != comp || v == l->ver) continue;
        l->ver = v; (void)m; mt_publish(l->pset, l->sh);
    }
    links_lock(0);
}
/* dc_commit: DM commits its own device after moving content; XAML must then tick so the new matrix reaches the screen */
int dcomp_manip_take_dirty(void) { return InterlockedExchange(&g_dm_dirty, 0) != 0; }

/* ---- commit completion events ----
   DirectManipulation hands its device an event (IDCompositionDeviceInternal slot 8) and paces its inertia / animation frames on it.
   Win10 signals it once the commit has been composed, about one frame later; signalling it at once let DM commit thousands of
   times a second. It is now signalled once at registration (nothing pending) and 16 ms after every commit of that device. */
typedef struct { CObj *comp; HANDLE ev; } CommitEv;
static CommitEv g_cev[8]; static SRWLOCK g_cev_lock = SRWLOCK_INIT;
typedef struct { HANDLE timer, ev; } EvTimer;
static VOID CALLBACK commit_ev_fire(PVOID ctx, BOOLEAN fired)
{
    EvTimer *et = (EvTimer *)ctx; (void)fired;
    SetEvent(et->ev); CloseHandle(et->ev); DeleteTimerQueueTimer(NULL, et->timer, NULL); HeapFree(GetProcessHeap(), 0, et);
}
void dcomp_set_commit_event(CObj *comp, HANDLE ev)
{
    int i, slot = -1; HANDLE mine = NULL;
    if (ev && !DuplicateHandle(GetCurrentProcess(), ev, GetCurrentProcess(), &mine, 0, FALSE, DUPLICATE_SAME_ACCESS)) mine = NULL;
    AcquireSRWLockExclusive(&g_cev_lock);
    for (i = 0; i < 8; i++) { if (g_cev[i].comp == comp) { slot = i; break; } if (!g_cev[i].comp && slot < 0) slot = i; }
    if (slot >= 0) { if (g_cev[slot].ev) CloseHandle(g_cev[slot].ev); g_cev[slot].comp = mine ? comp : NULL; g_cev[slot].ev = mine; }
    else if (mine) CloseHandle(mine);
    ReleaseSRWLockExclusive(&g_cev_lock);
    if (ev) SetEvent(ev);
}
void dcomp_commit_done(CObj *comp)
{
    int i; HANDLE ev = NULL; EvTimer *et;
    AcquireSRWLockShared(&g_cev_lock);
    for (i = 0; i < 8; i++) if (g_cev[i].comp == comp && g_cev[i].ev) { DuplicateHandle(GetCurrentProcess(), g_cev[i].ev, GetCurrentProcess(), &ev, 0, FALSE, DUPLICATE_SAME_ACCESS); break; }
    ReleaseSRWLockShared(&g_cev_lock);
    if (!ev) return;
    et = (EvTimer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *et);
    if (!et) { SetEvent(ev); CloseHandle(ev); return; }
    et->ev = ev;
    if (!CreateTimerQueueTimer(&et->timer, NULL, commit_ev_fire, et, 16, 0, WT_EXECUTEONLYONCE)) { SetEvent(ev); CloseHandle(ev); HeapFree(GetProcessHeap(), 0, et); }
}