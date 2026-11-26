/* dcomp_main.c - dcomp.dll exports (named + ordinals), activation (DllGetActivationFactory), registration */
#include "dcomp_priv.h"
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

/* ---------------- registration ---------------- */
void comp_register_impls_visual(void); void comp_register_impls_anim(void); void comp_register_impls_shapes(void); void comp_register_impls_batch(void); void comp_register_impls_expr(void);
void comp_register_impls(void)
{
    static LONG done; if (InterlockedExchange(&done, 1)) return;
    comp_register_impls_visual(); comp_register_impls_anim(); comp_register_impls_shapes(); comp_register_impls_batch(); comp_register_impls_expr();
    dc_surface_register(); dc_register_render(); comp_register_impls_brush2(); comp_register_impls_effect();
    g_impl[CLS_Visual].qi_extra = dc_visual_qi_extra;
    g_impl[CLS_Compositor].qi_extra = dc_compositor_qi_extra;
    g_impl[CLS_KeyFrameAnimation].qi_extra = dc_kfanim_qi_extra;
    g_impl[CLS_CompositionObject].qi_extra = dc_partner_qi_extra;
}
__attribute__((constructor)) static void dc_ctor(void) { InitializeCriticalSection(&g_dc_lock); }

/* ---------------- logging helper for the ordinal stubs ---------------- */
static CObj *g_last_comp;
static CObj *new_compositor(void) { CObj *c; comp_register_impls(); c = comp_create_compositor(); if (c) { g_last_comp = c; } return c; }

/* ---------------- public exports ---------------- */
static HRESULT make_device(IUnknown *dev, REFIID iid, void **out, const char *who)
{
    CObj *c; HRESULT hr; char g[40]; if (!out) return E_POINTER; *out = NULL;
    c = new_compositor(); if (!c) return E_OUTOFMEMORY;
    if (dev) { HRESULT h2 = dc_set_rendering_device(dev); if (FAILED(h2) && h2 != S_FALSE) comp_log("%s: rendering device not usable (0x%08lX), default device used\n", who, (unsigned long)h2);
        dc_note_client_device(c, dev); }
    hr = comp_qi(c, iid, out); comp_release(c);
    comp_log("%s(iid=%s) -> 0x%08lX\n", who, dc_guid(iid, g), (unsigned long)hr); return hr;
}
/* IInteropCompositorFactoryPartner {22118ADF-23F1-4801-BCFA-66CBF48CC51B} on the Compositor factory. Windows.UI.Xaml 19041
   (DCompTreeHost::CreateWinRTInteropCompositionDevice) calls slot 6 with (renderingDevice = NULL, its IInteropCompositorPartnerCallback,
   riid = interop device {D14B6158-...}, out). The callback (slot 3 NotifyDirty, slot 4 NotifyDeferralState) asks XAML for a frame. */
/* XAML creates the interop device twice (background init thread and UI thread), each with its own callback; every one must hear NotifyDirty
   or that thread's scheduler never ticks again (wine-uwp) */
IUnknown *g_dc_partner_cbs[4]; LONG g_dc_npartner;
HRESULT STDMETHODCALLTYPE dc_ifp_create(void *This, IUnknown *dev, IUnknown *cb, REFIID iid, void **out)
{
    (void)This; if (cb) { LONG k = InterlockedIncrement(&g_dc_npartner) - 1; if (k < 4) { cb->lpVtbl->AddRef(cb); g_dc_partner_cbs[k] = cb; } else InterlockedDecrement(&g_dc_npartner); }
    comp_log("InteropCompositorFactory.CreateInteropCompositor(device=%p, callback=%p)\n", (void *)dev, (void *)cb);
    return make_device(dev, iid, out, "CreateInteropCompositor");
}
HRESULT STDMETHODCALLTYPE dc_ifp_check(void *This, boolean *interop, boolean *expose) { (void)This; if (interop) *interop = 1; if (expose) *expose = 0; return S_OK; }
HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi, REFIID iid, void **out) { return make_device((IUnknown *)dxgi, iid, out, "DCompositionCreateDevice"); }
HRESULT WINAPI DCompositionCreateDevice2(IUnknown *dev, REFIID iid, void **out) { return make_device(dev, iid, out, "DCompositionCreateDevice2"); }
HRESULT WINAPI DCompositionCreateDevice3(IUnknown *dev, REFIID iid, void **out) { return make_device(dev, iid, out, "DCompositionCreateDevice3"); }
HRESULT WINAPI DCompositionCreateSurfaceHandle(DWORD access, SECURITY_ATTRIBUTES *sa, HANDLE *h)
{
    (void)access; if (!h) return E_POINTER; *h = CreateEventW(sa, TRUE, FALSE, NULL); return *h ? S_OK : HRESULT_FROM_WIN32(GetLastError());      /* a closable handle; CreateSurfaceFromHandle is not implemented yet */
}
HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, void **p) { (void)c; (void)i; if (p) *p = NULL; return CLASS_E_CLASSNOTAVAILABLE; }
HRESULT WINAPI DwmFlush(void) { return S_OK; }
HRESULT WINAPI DwmEnableMMCSS(BOOL on) { (void)on; return S_OK; }
HRESULT WINAPI DwmpEnableDDASupport(void) { return S_OK; }
HRESULT WINAPI DCompositionAttachMouseDragToHwnd(void *v, HWND h, BOOL on) { (void)v; (void)h; (void)on; return dc_stub("exports", 0, "DCompositionAttachMouseDragToHwnd", S_OK); }
HRESULT WINAPI DCompositionAttachMouseWheelToHwnd(void *v, HWND h, BOOL on) { (void)v; (void)h; (void)on; return dc_stub("exports", 1, "DCompositionAttachMouseWheelToHwnd", S_OK); }
HRESULT WINAPI CompileEffectDescription(void) { return dc_stub("exports", 2, "CompileEffectDescription", E_NOTIMPL); }
HRESULT WINAPI CreateEffectDescription(void) { return dc_stub("exports", 3, "CreateEffectDescription", E_NOTIMPL); }
HRESULT WINAPI DeserializeEffectDescription(void) { return dc_stub("exports", 4, "DeserializeEffectDescription", E_NOTIMPL); }
HRESULT WINAPI SerializeEffectDescription(void) { return dc_stub("exports", 5, "SerializeEffectDescription", E_NOTIMPL); }

/* ---------------- ordinal exports (numbers as in the real dcomp.dll; signatures mostly unknown: log the four integer-register arguments) ---------------- */
#define ORD(n) UINT_PTR WINAPI dcOrd_##n(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d) { comp_log("dcomp ordinal #%d called (%p, %p, %p, %p) from %p\n", n, (void *)a, (void *)b, (void *)c, (void *)d, __builtin_return_address(0)); return 0; }
ORD(1017) ORD(1028) ORD(1031) ORD(1033) ORD(1039) ORD(1041) ORD(1042) ORD(1043) ORD(1044) ORD(1045) ORD(2006) ORD(2503)
/* 1046..1051: our own named exports used to land on these ordinals by accident (the linker fills ordinal gaps with them), so a caller
   by ordinal ran an unrelated function - twinapi.appcore calls #1047 when a XAML carousel arrow is clicked. Now explicit and inert;
   the named exports have ordinals from 3001 (dcomp.def). */
/* they FAIL: #1047 is a creator (twinapi.appcore passes (3, &out, ...) and uses *out after success; it handles the failure it always got) */
#define ORDF(n) HRESULT WINAPI dcOrd_##n(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d) { static LONG k; if (InterlockedIncrement(&k) <= 5) comp_log("dcomp ordinal #%d called (%p, %p, %p, %p) from %p -> E_INVALIDARG\n", n, (void *)a, (void *)b, (void *)c, (void *)d, __builtin_return_address(0)); return E_INVALIDARG; }
ORDF(1046) ORDF(1047) ORDF(1048) ORDF(1049) ORDF(1050) ORDF(1051)
/* 1019: create_composition_input_sink(desc*, HANDLE*) - "a closable handle of our own is enough" (wine-uwp) */
HRESULT WINAPI dcOrd_1019(void *desc, HANDLE *h)
{
    comp_log("dcomp ordinal #1019 CreateCompositionInputSink(%p, %p)\n", desc, (void *)h); if (!h) return E_POINTER; *h = CreateEventW(NULL, TRUE, FALSE, NULL); return *h ? S_OK : E_FAIL;
}
/* 1038: DCompositionCreateTargetForHandle(HANDLE, IDCompositionTarget**): a handle means nothing on Win7: the target binds to the window given to WxrDcomp_SetTargetWindow */
HRESULT WINAPI dcOrd_1038(HANDLE handle, void **target)
{
    CObj *t; HRESULT hr; comp_log("dcomp ordinal #1038 CreateTargetForHandle(handle=%p)\n", (void *)handle); if (!target) return E_POINTER; *target = NULL;
    if (!g_last_comp) return E_FAIL; t = dc_create_target(g_last_comp, NULL, FALSE); if (!t) return E_OUTOFMEMORY; hr = comp_qi(t, &DCIID_Target, target); comp_release(t); return hr;
}
/* 2502: DCompositionCreateAnimationStats(ptr, ptr): the client delay-loads it and aborts when missing; layout unknown */
HRESULT WINAPI dcOrd_2502(void *a, void *b) { comp_log("dcomp ordinal #2502 CreateAnimationStats(%p, %p)\n", a, b); return S_OK; }

/* ---------------- activation ---------------- */
HRESULT WINAPI comp_engine_GetFactory(HSTRING id, void **factory);          /* the engine's class table (comp_core.c, renamed) */
typedef struct AFV { HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AR)(void *); ULONG (STDMETHODCALLTYPE *RL)(void *);
    HRESULT (STDMETHODCALLTYPE *Iids)(void *, ULONG *, IID **); HRESULT (STDMETHODCALLTYPE *Name)(void *, HSTRING *); HRESULT (STDMETHODCALLTYPE *Trust)(void *, TrustLevel *);
    HRESULT (STDMETHODCALLTYPE *Activate)(void *, IInspectable **); } AFV;
static const GUID IID_IInspectable_dc = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID IID_AF_ = {0x00000035, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static HRESULT STDMETHODCALLTYPE af_iids(void *T, ULONG *n, IID **p) { (void)T; *n = 0; *p = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE af_name(void *T, HSTRING *n) { (void)T; return WindowsCreateString(L"Windows.UI.Composition.InteropVisual", 36, n); }
static HRESULT STDMETHODCALLTYPE af_trust(void *T, TrustLevel *t) { (void)T; *t = BaseTrust; return S_OK; }
static HRESULT STDMETHODCALLTYPE af_activate_iv(void *T, IInspectable **out)
{
    CObj *c, *o; HRESULT hr; (void)T; if (!out) return E_POINTER; *out = NULL;
    c = g_last_comp ? g_last_comp : new_compositor(); if (!c) return E_FAIL;
    o = comp_new(CLS_SpriteVisual, c); if (!o) return E_OUTOFMEMORY; dc_note_visual((Visual *)o, 1);
    if (!((Visual *)o)->dcx) { ((Visual *)o)->dcx = (DcVis *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DcVis)); }
    hr = comp_qi(o, &IID_IInspectable_dc, (void **)out); comp_release(o); comp_log("activated Windows.UI.Composition.InteropVisual -> 0x%08lX\n", (unsigned long)hr); return hr;
}
static const AFV VT_AFIV = { hv_QI, hv_AddRef, hv_Release, af_iids, af_name, af_trust, af_activate_iv };
/* Windows.UI.Composition.CompositionEffectSourceParameter (not a composition object, so not in the engine's class table): XAML's
   CPopup::ApplyHostBackdropTreatment names the backdrop effect's source with one (fail-fast when the class is missing). Factory
   ICompositionEffectSourceParameterFactory {B3D9F276} slot 6 Create(name, out); the object answers ICompositionEffectSourceParameter
   {858AB13A} (slot 6 get_Name) and IGraphicsEffectSource {2D8F9DDC} (no methods). Effects themselves are not rendered (capabilities say so). */
typedef struct EspObj { HObj h; HSTRING name; } EspObj;
static const GUID IID_ESP_ = {0x858AB13A, 0x3292, 0x4E4E, {0xB3, 0xBB, 0x2B, 0x6C, 0x65, 0x44, 0xA6, 0xEE}};
static const GUID IID_ESPF_ = {0xB3D9F276, 0xABA3, 0x4724, {0xAC, 0xF3, 0xD0, 0x39, 0x74, 0x64, 0xDB, 0x1C}};
static const GUID IID_GES_ = {0x2D8F9DDC, 0x4339, 0x4EB9, {0x92, 0x16, 0xF9, 0xDE, 0xB7, 0x56, 0x58, 0xA2}};
static void esp_dtor(HObj *h) { EspObj *e = (EspObj *)h; if (e->name) WindowsDeleteString(e->name); }
static HRESULT STDMETHODCALLTYPE esp_name_rc(void *T, HSTRING *n) { (void)T; return WindowsCreateString(L"Windows.UI.Composition.CompositionEffectSourceParameter", 55, n); }
static HRESULT STDMETHODCALLTYPE esp_get_name(void *T, HSTRING *r)
{ EspObj *e = (EspObj *)((CView *)T)->obj; UINT32 n = 0; const WCHAR *s; if (!r) return E_POINTER; s = e->name ? WindowsGetStringRawBuffer(e->name, &n) : L""; return WindowsCreateString(s, n, r); }
static const void *const VT_ESP[] = { (void *)hv_QI, (void *)hv_AddRef, (void *)hv_Release, (void *)af_iids, (void *)esp_name_rc, (void *)af_trust, (void *)esp_get_name };
static HRESULT STDMETHODCALLTYPE espf_create(void *T, HSTRING name, void **out)
{
    static HIface ifs[3]; EspObj *e; UINT32 n = 0; const WCHAR *s; (void)T; if (!out) return E_POINTER; *out = NULL;
    if (!ifs[0].iid) { ifs[0].iid = &IID_ESP_; ifs[0].vt = VT_ESP; ifs[1].iid = &IID_GES_; ifs[1].vt = VT_ESP; ifs[2].iid = &IID_IInspectable_dc; ifs[2].vt = VT_ESP; }
    e = (EspObj *)hobj_new(sizeof(EspObj), "CompositionEffectSourceParameter", ifs, 3, esp_dtor); if (!e) return E_OUTOFMEMORY;
    s = name ? WindowsGetStringRawBuffer(name, &n) : L""; WindowsCreateString(s, n, &e->name);
    return hobj_return(&e->h, &IID_ESP_, out);
}
static HRESULT STDMETHODCALLTYPE espf_activate(void *T, IInspectable **o) { (void)T; if (o) *o = NULL; return E_NOTIMPL; }
static const void *const VT_ESPF_AF[] = { (void *)hv_QI, (void *)hv_AddRef, (void *)hv_Release, (void *)af_iids, (void *)esp_name_rc, (void *)af_trust, (void *)espf_activate };
static const void *const VT_ESPF[] = { (void *)hv_QI, (void *)hv_AddRef, (void *)hv_Release, (void *)af_iids, (void *)esp_name_rc, (void *)af_trust, (void *)espf_create };
/* Windows.UI.Composition.Effects.SceneLightingEffect (activatable, default constructor): XAML's RevealBrush (Programmer mode's buttons)
   builds its border effect from one and uses the object unchecked. Without the class a host fallback produced a NULL instance and
   CreateRevealBorderEffect crashed, which surfaced as a .NET fail-fast. The effect is a description only (properties stored, never lit:
   effects are drawn as their colour layers, comp_effect.c). Layout from Windows.UI.winmd: ISceneLightingEffect {91BB5E52} 6-15
   Ambient/Diffuse/NormalMapSource/SpecularAmount/SpecularShine get+put, ISceneLightingEffect2 {9E270E81} 6/7 ReflectanceModel,
   IGraphicsEffect {CB51C0CE} 6/7 Name, IGraphicsEffectSource {2D8F9DDC} (no methods). */
typedef struct SleObj { HObj h; float ambient, diffuse, specular, shine; IUnknown *normal; INT32 model; HSTRING name; } SleObj;
static const GUID IID_SLE_ = {0x91BB5E52, 0x95D1, 0x4F8B, {0x9A, 0x5A, 0x64, 0x08, 0xB2, 0x4B, 0x8C, 0x6A}};
static const GUID IID_SLE2_ = {0x9E270E81, 0x72F0, 0x4C5C, {0x95, 0xF8, 0x8A, 0x6E, 0x00, 0x24, 0xF4, 0x09}};
static const GUID IID_GFXEFFECT_ = {0xCB51C0CE, 0x8FE6, 0x4636, {0xB2, 0x02, 0x86, 0x1F, 0xAA, 0x07, 0xD8, 0xF3}};
#define SLE(T) ((SleObj *)((CView *)(T))->obj)
static void sle_dtor(HObj *h) { SleObj *s = (SleObj *)h; if (s->normal) s->normal->lpVtbl->Release(s->normal); if (s->name) WindowsDeleteString(s->name); }
static HRESULT STDMETHODCALLTYPE sle_name_rc(void *T, HSTRING *n) { (void)T; return WindowsCreateString(L"Windows.UI.Composition.Effects.SceneLightingEffect", 50, n); }
#define SLE_F(fld) \
  static HRESULT STDMETHODCALLTYPE sle_get_##fld(void *T, float *r) { if (!r) return E_POINTER; *r = SLE(T)->fld; return S_OK; } \
  static HRESULT STDMETHODCALLTYPE sle_put_##fld(void *T, float v) { SLE(T)->fld = v; return S_OK; }
SLE_F(ambient) SLE_F(diffuse) SLE_F(specular) SLE_F(shine)
static HRESULT STDMETHODCALLTYPE sle_get_normal(void *T, IUnknown **r) { SleObj *s = SLE(T); if (!r) return E_POINTER; *r = s->normal; if (*r) (*r)->lpVtbl->AddRef(*r); return S_OK; }
static HRESULT STDMETHODCALLTYPE sle_put_normal(void *T, IUnknown *v) { SleObj *s = SLE(T); if (v) v->lpVtbl->AddRef(v); if (s->normal) s->normal->lpVtbl->Release(s->normal); s->normal = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE sle_get_model(void *T, INT32 *r) { if (!r) return E_POINTER; *r = SLE(T)->model; return S_OK; }
static HRESULT STDMETHODCALLTYPE sle_put_model(void *T, INT32 v) { SLE(T)->model = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE sle_get_name(void *T, HSTRING *r) { SleObj *s = SLE(T); UINT32 n = 0; const WCHAR *p; if (!r) return E_POINTER; p = s->name ? WindowsGetStringRawBuffer(s->name, &n) : L""; return WindowsCreateString(p, n, r); }
static HRESULT STDMETHODCALLTYPE sle_put_name(void *T, HSTRING v) { SleObj *s = SLE(T); UINT32 n = 0; const WCHAR *p = v ? WindowsGetStringRawBuffer(v, &n) : L""; if (s->name) WindowsDeleteString(s->name); s->name = NULL; return WindowsCreateString(p, n, &s->name); }
#define SLE_BASE (void *)hv_QI, (void *)hv_AddRef, (void *)hv_Release, (void *)af_iids, (void *)sle_name_rc, (void *)af_trust
static const void *const VT_SLE[] = { SLE_BASE, (void *)sle_get_ambient, (void *)sle_put_ambient, (void *)sle_get_diffuse, (void *)sle_put_diffuse,
    (void *)sle_get_normal, (void *)sle_put_normal, (void *)sle_get_specular, (void *)sle_put_specular, (void *)sle_get_shine, (void *)sle_put_shine };
static const void *const VT_SLE2[] = { SLE_BASE, (void *)sle_get_model, (void *)sle_put_model };
static const void *const VT_SLEGFX[] = { SLE_BASE, (void *)sle_get_name, (void *)sle_put_name };
static const void *const VT_SLESRC[] = { SLE_BASE };
static HRESULT STDMETHODCALLTYPE slef_activate(void *T, IInspectable **o)
{
    static HIface ifs[5]; SleObj *s; (void)T; if (!o) return E_POINTER; *o = NULL;
    if (!ifs[0].iid) { ifs[0].iid = &IID_SLE_; ifs[0].vt = VT_SLE; ifs[1].iid = &IID_SLE2_; ifs[1].vt = VT_SLE2; ifs[2].iid = &IID_GFXEFFECT_; ifs[2].vt = VT_SLEGFX;
        ifs[3].iid = &IID_GES_; ifs[3].vt = VT_SLESRC; ifs[4].iid = &IID_IInspectable_dc; ifs[4].vt = VT_SLE; }
    s = (SleObj *)hobj_new(sizeof(SleObj), "SceneLightingEffect", ifs, 5, sle_dtor); if (!s) return E_OUTOFMEMORY;
    s->diffuse = 1.0f; s->shine = 16.0f;          /* the API's defaults (Ambient 0, Specular 0) */
    return hobj_return(&s->h, &IID_IInspectable_dc, (void **)o);
}
static const void *const VT_SLEF_AF[] = { SLE_BASE, (void *)slef_activate };
HRESULT WINAPI DllGetActivationFactory(HSTRING id, void **factory)
{
    UINT32 n = 0; const WCHAR *name = id ? WindowsGetStringRawBuffer(id, &n) : NULL; static HObj *iv; HRESULT hr;
    if (!factory) return E_POINTER; *factory = NULL; if (!name) return E_INVALIDARG; comp_register_impls();
    if (n == 36 && !wcsncmp(name, L"Windows.UI.Composition.InteropVisual", 36)) {
        if (!iv) { HIface *ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, sizeof(HIface)); ifs[0].iid = &IID_AF_; ifs[0].vt = &VT_AFIV; iv = hobj_new(sizeof(HObj), "InteropVisualFactory", ifs, 1, NULL); iv->flags |= 1; }
        return hobj_return((HObj *)(hv_AddRef(&iv->views[0]), iv), &IID_AF_, factory);                      /* keeps one reference forever */
    }
    if (n == 55 && !wcsncmp(name, L"Windows.UI.Composition.CompositionEffectSourceParameter", 55)) {
        static HObj *ef; if (!ef) { HIface *ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, 2 * sizeof(HIface)); ifs[0].iid = &IID_AF_; ifs[0].vt = VT_ESPF_AF; ifs[1].iid = &IID_ESPF_; ifs[1].vt = VT_ESPF;
            ef = hobj_new(sizeof(HObj), "CompositionEffectSourceParameterFactory", ifs, 2, NULL); }
        if (!ef) return E_OUTOFMEMORY; return hobj_return((HObj *)(hv_AddRef(&ef->views[0]), ef), &IID_AF_, factory);       /* keeps one reference forever */
    }
    if (n == 50 && !wcsncmp(name, L"Windows.UI.Composition.Effects.SceneLightingEffect", 50)) {
        static HObj *sf; if (!sf) { HIface *ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, sizeof(HIface)); ifs[0].iid = &IID_AF_; ifs[0].vt = VT_SLEF_AF;
            sf = hobj_new(sizeof(HObj), "SceneLightingEffectFactory", ifs, 1, NULL); }
        if (!sf) return E_OUTOFMEMORY; return hobj_return((HObj *)(hv_AddRef(&sf->views[0]), sf), &IID_AF_, factory);       /* keeps one reference forever */
    }
    hr = comp_engine_GetFactory(id, factory);
    if (hr == REGDB_E_CLASSNOTREG) { char b[160]; WideCharToMultiByte(CP_UTF8, 0, name, (int)n, b, sizeof b - 1, NULL, NULL); b[n < sizeof b - 1 ? n : sizeof b - 1] = 0; comp_log("dcomp DllGetActivationFactory %s -> not provided (add it when the run needs it)\n", b); }
    return hr;
}
/* CompositionCapabilities: effects UNSUPPORTED so XAML takes its plain-brush paths */
HRESULT STDMETHODCALLTYPE CI_(ICompositionCapabilitiesStatics, GetForCurrentView)(W(ICompositionCapabilitiesStatics) *This, W(ICompositionCapabilities) **r)
{ CObj *o; HRESULT hr; (void)This; if (!r) return E_POINTER; o = comp_new(CLS_CompositionCapabilities, g_last_comp); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionCapabilities), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionCapabilities, AreEffectsSupported)(W(ICompositionCapabilities) *This, boolean *r) { (void)This; *r = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionCapabilities, AreEffectsFast)(W(ICompositionCapabilities) *This, boolean *r) { (void)This; *r = 0; return S_OK; }
