/* comp_effect.c - CompositionEffectFactory / CompositionEffectBrush, drawn as a solid colour (acrylic's fallback look).
 *
 * XAML and WinUI build acrylic as an effect graph (backdrop -> blur -> saturation, blended with luminosity and tint colour layers). Effects
 * are not rendered here (a real blur costs every frame). Instead the factory walks the
 * graph once, collects every colour layer (D2D1 Flood effects = Win2D ColorSourceEffect), composites them in graph order and makes the result
 * opaque; brushes from that factory fill with it. This is what acrylic looks like when Windows itself falls back (transparency effects off).
 * A graph without colour layers keeps drawing nothing, as before. */
#include "comp_types.h"

#define CI_(i, m) CI_##i##_##m

/* Composite graphs: WinUI's animated icons (LottieGen code) build mattes and masks as CompositeEffect(Mode, sources =
   CompositionEffectSourceParameter "source"/"mask"...) whose parameters are surface brushes over CompositionVisualSurfaces. The factory
   records the mode and the parameter names in input order; dcomp_render.c draws such brushes with the D2D Composite effect. */
#define EFF_MAXSRC 8
/* Layered graphs: any other graph without colour layers whose leaves are source parameters - the Store draws every
   tile icon as Opacity("OpacityEffect")/Blend/Composite over '_backgroundBrush' and '_imageSurfaceBrush' and fades the picture in by
   animating 'OpacityEffect.Opacity'. Such a brush draws its source parameters bottom to top, each with the product of the Opacity /
   CrossFade weights above it (lop); lopname = "<effect name>." of the nearest such effect, the prefix of its animatable property.
   Blend modes, blurs, saturation ... are not applied. */
typedef struct EffFact { CObj o; Col color; BYTE has; BYTE composite; BYTE layered; UINT32 mode; int nsrc; WCHAR names[EFF_MAXSRC][40]; float lop[EFF_MAXSRC]; WCHAR lopname[EFF_MAXSRC][40]; } EffFact;
typedef struct EffBrush { CObj o; Col color; BYTE has; BYTE composite; BYTE layered; UINT32 mode; int nsrc; WCHAR names[EFF_MAXSRC][40]; float lop[EFF_MAXSRC]; WCHAR lopname[EFF_MAXSRC][40]; CObj *srcs[EFF_MAXSRC]; } EffBrush;

static const GUID IID_GfxEffectD2D1Interop_ = {0x2FC57384, 0xA068, 0x44D7, {0xA3, 0x31, 0x30, 0x98, 0x2F, 0xCF, 0x71, 0x77}};
static const GUID CLSID_D2D1Flood_ = {0x61C23C20, 0xAE69, 0x4D8E, {0x94, 0xCF, 0x50, 0x07, 0x8D, 0xF6, 0x38, 0xF2}};
#define VT(o, k, T) ((T)(*(void ***)(o))[k])

/* IGraphicsEffectD2D1Interop (IUnknown-based): 3 GetEffectId, 4 GetNamedPropertyMapping, 5 GetPropertyCount, 6 GetProperty, 7 GetSource,
   8 GetSourceCount. Colour layers are visited sources-first, so later (upper) layers are composited over earlier ones. */
static void eff_walk(IUnknown *e, float acc[4], int *found, int depth)
{
    IUnknown *ip = NULL; GUID id; UINT n = 0, i;
    if (!e || depth > 16 || FAILED(e->lpVtbl->QueryInterface(e, &IID_GfxEffectD2D1Interop_, (void **)&ip)) || !ip) return;
    if (SUCCEEDED(VT(ip, 8, HRESULT (STDMETHODCALLTYPE *)(void *, UINT *))(ip, &n)))
        for (i = 0; i < n && i < 16; i++) { IUnknown *src = NULL;
            if (SUCCEEDED(VT(ip, 7, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(ip, i, &src)) && src) { eff_walk(src, acc, found, depth + 1); src->lpVtbl->Release(src); } }
    if (SUCCEEDED(VT(ip, 3, HRESULT (STDMETHODCALLTYPE *)(void *, GUID *))(ip, &id)) && IsEqualGUID(&id, &CLSID_D2D1Flood_)) {
        IUnknown *pv = NULL; UINT32 len = 0; float *v = NULL;
        /* property 0 = D2D1_FLOOD_PROP_COLOR, a Vector4 (r, g, b, a) handed out as IPropertyValue::GetSingleArray (slot 33) */
        if (SUCCEEDED(VT(ip, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(ip, 0, &pv)) && pv) {
            if (SUCCEEDED(VT(pv, 33, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32 *, float **))(pv, &len, &v)) && v && len >= 4) {
                float a = v[3] < 0 ? 0 : v[3] > 1 ? 1 : v[3]; int k;
                for (k = 0; k < 3; k++) acc[k] = v[k] * a + acc[k] * (1 - a);          /* source-over, straight alpha */
                acc[3] = a + acc[3] * (1 - a); (*found)++;
                comp_log("effect graph: colour layer %.3f %.3f %.3f a %.3f\n", v[0], v[1], v[2], v[3]);
            }
            if (v) CoTaskMemFree(v); pv->lpVtbl->Release(pv);
        }
    }
    ip->lpVtbl->Release(ip);
}

static const GUID CLSID_D2D1Composite_ = {0x48FC9F51, 0xF6AC, 0x48F1, {0x8B, 0x58, 0x3B, 0x28, 0xAC, 0x46, 0xF7, 0x6D}};
static const GUID IID_EffSrcParam_ = {0x858AB13A, 0x3292, 0x4E4E, {0xB3, 0xBB, 0x2B, 0x6C, 0x65, 0x44, 0xA6, 0xEE}};   /* ICompositionEffectSourceParameter */
/* root = Composite effect whose inputs are all source parameters: record mode + names. 1 = recognised */
static int eff_composite(IUnknown *e, EffFact *f)
{
    IUnknown *ip = NULL; GUID id; UINT n = 0, i; int ok = 0;
    if (!e || FAILED(e->lpVtbl->QueryInterface(e, &IID_GfxEffectD2D1Interop_, (void **)&ip)) || !ip) return 0;
    if (SUCCEEDED(VT(ip, 3, HRESULT (STDMETHODCALLTYPE *)(void *, GUID *))(ip, &id)) && IsEqualGUID(&id, &CLSID_D2D1Composite_)
        && SUCCEEDED(VT(ip, 8, HRESULT (STDMETHODCALLTYPE *)(void *, UINT *))(ip, &n)) && n >= 1 && n <= EFF_MAXSRC) {
        IUnknown *pv = NULL; f->mode = 0;   /* D2D1_COMPOSITE_PROP_MODE: IPropertyValue get_Type 6, GetInt32 11, GetUInt32 12 */
        if (SUCCEEDED(VT(ip, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(ip, 0, &pv)) && pv) {
            int ty = 0; VT(pv, 6, HRESULT (STDMETHODCALLTYPE *)(void *, int *))(pv, &ty);
            if (ty == 4) { INT32 v = 0; if (SUCCEEDED(VT(pv, 11, HRESULT (STDMETHODCALLTYPE *)(void *, INT32 *))(pv, &v))) f->mode = (UINT32)v; }
            else { UINT32 v = 0; if (SUCCEEDED(VT(pv, 12, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32 *))(pv, &v))) f->mode = v; }
            pv->lpVtbl->Release(pv); }
        ok = 1;
        for (i = 0; i < n; i++) { IUnknown *src = NULL, *sp = NULL; HSTRING nm = NULL;
            if (SUCCEEDED(VT(ip, 7, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(ip, i, &src)) && src && SUCCEEDED(src->lpVtbl->QueryInterface(src, &IID_EffSrcParam_, (void **)&sp)) && sp
                && SUCCEEDED(VT(sp, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(sp, &nm))) {
                UINT32 l = 0; const WCHAR *s = WindowsGetStringRawBuffer(nm, &l); lstrcpynW(f->names[i], s ? s : L"", 40); WindowsDeleteString(nm);
            } else ok = 0;
            if (sp) sp->lpVtbl->Release(sp); if (src) src->lpVtbl->Release(src); }
        if (ok) f->nsrc = (int)n;
    }
    ip->lpVtbl->Release(ip); return ok;
}
static const GUID CLSID_D2D1Opacity_ = {0x811D79A4, 0xDE28, 0x4454, {0x80, 0x94, 0xC6, 0x46, 0x85, 0xF8, 0xBD, 0x4C}};
static const GUID CLSID_D2D1CrossFade_ = {0x12F575E8, 0x4DB1, 0x485F, {0x9A, 0x84, 0x03, 0xA0, 0x7D, 0xD3, 0x82, 0x9F}};
static const GUID IID_GraphicsEffect_ = {0xCB51C0CE, 0x8FE6, 0x4636, {0xB2, 0x02, 0x86, 0x1F, 0xAA, 0x07, 0xD8, 0xF3}};   /* IGraphicsEffect: 6 get_Name */
/* collects the source parameters of a graph in drawing order with their opacity; logs the graph (first few factories) */
static void eff_layers(IUnknown *e, EffFact *f, float op, const WCHAR *opname, int depth, int log)
{
    IUnknown *ip = NULL, *sp = NULL, *ge = NULL; GUID id; UINT n = 0, i; WCHAR name[40] = L"", mine[40]; HSTRING hn = NULL; float w = 1.0f; int isop = 0, iscf = 0;
    if (!e || depth > 16) return;
    if (SUCCEEDED(e->lpVtbl->QueryInterface(e, &IID_EffSrcParam_, (void **)&sp)) && sp) {
        if (SUCCEEDED(VT(sp, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(sp, &hn))) { const WCHAR *s = WindowsGetStringRawBuffer(hn, NULL); lstrcpynW(name, s ? s : L"", 40); WindowsDeleteString(hn); }
        if (log) comp_log("effect graph: %*ssource parameter '%ls' (opacity %.2f, animated by '%ls*')\n", depth * 2, "", name, op, opname);
        if (f->nsrc < EFF_MAXSRC) { int k = f->nsrc++; lstrcpynW(f->names[k], name, 40); f->lop[k] = op; lstrcpynW(f->lopname[k], opname, 40); }
        sp->lpVtbl->Release(sp); return; }
    if (FAILED(e->lpVtbl->QueryInterface(e, &IID_GfxEffectD2D1Interop_, (void **)&ip)) || !ip) return;
    memset(&id, 0, sizeof id); VT(ip, 3, HRESULT (STDMETHODCALLTYPE *)(void *, GUID *))(ip, &id);
    if (SUCCEEDED(e->lpVtbl->QueryInterface(e, &IID_GraphicsEffect_, (void **)&ge)) && ge) {
        if (SUCCEEDED(VT(ge, 6, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(ge, &hn)) && hn) { const WCHAR *s = WindowsGetStringRawBuffer(hn, NULL); lstrcpynW(name, s ? s : L"", 38); WindowsDeleteString(hn); }
        ge->lpVtbl->Release(ge); }
    isop = IsEqualGUID(&id, &CLSID_D2D1Opacity_); iscf = IsEqualGUID(&id, &CLSID_D2D1CrossFade_);
    if (isop || iscf) { IUnknown *pv = NULL;      /* property 0: the opacity / the weight of source 0 (IPropertyValue::GetSingle, slot 15) */
        if (SUCCEEDED(VT(ip, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(ip, 0, &pv)) && pv) { VT(pv, 15, HRESULT (STDMETHODCALLTYPE *)(void *, float *))(pv, &w); pv->lpVtbl->Release(pv); } }
    VT(ip, 8, HRESULT (STDMETHODCALLTYPE *)(void *, UINT *))(ip, &n);
    if (log) comp_log("effect graph: %*seffect {%08lX-%04X-...} '%ls', %u source(s)%s\n", depth * 2, "", (unsigned long)id.Data1, id.Data2, name, n, isop ? " = Opacity" : iscf ? " = CrossFade" : "");
    lstrcpynW(mine, name, 38); lstrcatW(mine, L".");
    for (i = 0; i < n && i < 16; i++) { IUnknown *src = NULL;
        if (SUCCEEDED(VT(ip, 7, HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(ip, i, &src)) && src) {
            if (isop) eff_layers(src, f, op * w, name[0] ? mine : opname, depth + 1, log);
            else if (iscf) eff_layers(src, f, op * (i == 0 ? w : 1.0f - w), i == 0 && name[0] ? mine : opname, depth + 1, log);
            else eff_layers(src, f, op, opname, depth + 1, log);
            src->lpVtbl->Release(src); } }
    ip->lpVtbl->Release(ip);
}
static HRESULT make_factory(W(ICompositor) *This, void *graph, W(ICompositionEffectFactory) **result)
{
    CObj *c = OBJ_OF(This), *o; EffFact *f; HRESULT hr; float acc[4] = { 0, 0, 0, 0 }; int found = 0;
    if (!result) return E_POINTER; *result = NULL;
    o = comp_new(CLS_CompositionEffectFactory, c->cls == CLS_Compositor ? c : c->comp); if (!o) return E_OUTOFMEMORY;
    f = (EffFact *)o;
    if (eff_composite((IUnknown *)graph, f)) { static LONG n; f->composite = 1;
        if (InterlockedIncrement(&n) <= 5) comp_log("effect factory: Composite mode %u over %d source parameter(s) ('%ls', '%ls') -> drawn with D2D Composite\n", f->mode, f->nsrc, f->names[0], f->nsrc > 1 ? f->names[1] : L"");
        hr = comp_return_default(o, (void **)result); comp_release(o); return hr; }
    eff_walk((IUnknown *)graph, acc, &found, 0);
    if (found && acc[3] > 0) {    /* un-premultiply against the accumulated coverage, then opaque */
        f->color.R = (BYTE)(acc[0] * 255.0f + 0.5f); f->color.G = (BYTE)(acc[1] * 255.0f + 0.5f); f->color.B = (BYTE)(acc[2] * 255.0f + 0.5f); f->color.A = 255; f->has = 1;
        comp_log("effect factory: %d colour layer(s) -> solid #%02X%02X%02X (effects drawn as their colour, no blur)\n", found, f->color.R, f->color.G, f->color.B);
    } else { static LONG nl; int log = InterlockedIncrement(&nl) <= 6;
        f->nsrc = 0; eff_layers((IUnknown *)graph, f, 1.0f, L"", 0, log);
        if (f->nsrc > 0) { f->layered = 1; if (log) comp_log("effect factory: %d source parameter(s), no colour layer -> drawn as layers (opacity only)\n", f->nsrc); }
        else if (log) comp_log("effect factory: no colour layer and no source parameter in the graph; its brushes draw nothing\n"); }
    hr = comp_return_default(o, (void **)result); comp_release(o); return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateEffectFactory)(W(ICompositor) *This, Windows_Graphics_Effects_IGraphicsEffect *g, W(ICompositionEffectFactory) **r)
{ return make_factory(This, g, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateEffectFactory_2)(W(ICompositor) *This, Windows_Graphics_Effects_IGraphicsEffect *g, Windows_Foundation_Collections_IIterable_1__HSTRING *anim, W(ICompositionEffectFactory) **r)
{ (void)anim; return make_factory(This, g, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionEffectFactory, CreateBrush)(W(ICompositionEffectFactory) *This, W(ICompositionEffectBrush) **result)
{
    EffFact *f = (EffFact *)OBJ_OF(This); CObj *o; HRESULT hr;
    if (!result) return E_POINTER; *result = NULL;
    o = comp_new(CLS_CompositionEffectBrush, f->o.comp); if (!o) return E_OUTOFMEMORY;
    { EffBrush *b = (EffBrush *)o; b->color = f->color; b->has = f->has; b->composite = f->composite; b->layered = f->layered; b->mode = f->mode; b->nsrc = f->nsrc; memcpy(b->names, f->names, sizeof b->names); memcpy(b->lop, f->lop, sizeof b->lop); memcpy(b->lopname, f->lopname, sizeof b->lopname); }
    hr = comp_return_default(o, (void **)result); comp_release(o); return hr;
}
static int eb_index(EffBrush *b, HSTRING name)
{ const WCHAR *s = name ? WindowsGetStringRawBuffer(name, NULL) : L""; int i; for (i = 0; i < b->nsrc; i++) if (!_wcsicmp(b->names[i], s)) return i; return -1; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionEffectBrush, GetSourceParameter)(W(ICompositionEffectBrush) *This, HSTRING name, W(ICompositionBrush) **result)
{ EffBrush *b = (EffBrush *)OBJ_OF(This); int i = eb_index(b, name); if (!result) return E_POINTER; *result = NULL; return i < 0 ? E_INVALIDARG : comp_return(b->srcs[i], &IID_Windows_UI_Composition_ICompositionBrush, (void **)result); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionEffectBrush, SetSourceParameter)(W(ICompositionEffectBrush) *This, HSTRING name, W(ICompositionBrush) *source)
{
    EffBrush *b = (EffBrush *)OBJ_OF(This); int i = eb_index(b, name);
    if (i < 0) { static LONG n; if (InterlockedIncrement(&n) <= 5) comp_log("EffectBrush.SetSourceParameter('%ls'): not a parameter of a drawn graph (ignored)\n", name ? WindowsGetStringRawBuffer(name, NULL) : L""); return S_OK; }
    comp_assign(&b->srcs[i], source ? comp_obj_of_any(source, "EffectBrush.SetSourceParameter") : NULL); INVALIDATE(OBJ_OF(This)); return S_OK;
}
static void eb_destroy(CObj *o) { EffBrush *b = (EffBrush *)o; int i; for (i = 0; i < EFF_MAXSRC; i++) if (b->srcs[i]) comp_release(b->srcs[i]); }
int comp_effect_composite(CObj *o, UINT32 *mode, CObj **srcs, int max)
{
    EffBrush *b = (EffBrush *)o; int i;
    if (!o || o->cls != CLS_CompositionEffectBrush || !b->composite) return 0;
    *mode = b->mode; for (i = 0; i < b->nsrc && i < max; i++) srcs[i] = b->srcs[i]; return i;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionEffectFactory, get_LoadStatus)(W(ICompositionEffectFactory) *This, Windows_UI_Composition_CompositionEffectFactoryLoadStatus *r)
{ (void)This; if (!r) return E_POINTER; *r = Windows_UI_Composition_CompositionEffectFactoryLoadStatus_Success; return S_OK; }

/* layered brush: its sources bottom to top with their opacities; 0 = not such a brush */
int comp_effect_layers(CObj *o, CObj **srcs, float *op, int max)
{
    EffBrush *b = (EffBrush *)o; int i;
    if (!o || o->cls != CLS_CompositionEffectBrush || !b->layered) return 0;
    for (i = 0; i < b->nsrc && i < max; i++) { srcs[i] = b->srcs[i]; op[i] = b->lop[i]; } return i;
}
/* "<effect name>.<property>" of a layered brush ('OpacityEffect.Opacity'): the opacity of the layers under that effect */
int comp_effect_prop(CObj *o, const WCHAR *path, int set, float *v)
{
    EffBrush *b = (EffBrush *)o; int i, hit = 0;
    if (!o || o->cls != CLS_CompositionEffectBrush || !b->layered) return 0;
    for (i = 0; i < b->nsrc; i++) { int l = lstrlenW(b->lopname[i]); if (l && !_wcsnicmp(path, b->lopname[i], l)) { if (set) b->lop[i] = v[0] < 0 ? 0 : v[0] > 1 ? 1 : v[0]; else if (!hit) v[0] = b->lop[i]; hit = 1; } }
    if (hit && set) INVALIDATE(o);
    return hit;
}
/* the solid colour an effect brush draws with; 0 when it has none */
int comp_effect_brush_color(CObj *b, Col *out) { EffBrush *e = (EffBrush *)b; if (!b || b->cls != CLS_CompositionEffectBrush || !e->has) return 0; *out = e->color; return 1; }

static void vsurf_destroy(CObj *o) { VisSurf *v = (VisSurf *)o; if (v->src) comp_release(v->src); }
/* Windows.UI.Composition.Internal.ICompositionVisualSurfacePartner {F26DA89E-683D-4C67-AEA7-BA29B2217A70}: 19041 XAML's connected
   animations (CConnectedAnimation::CreateSnapshotBrush / ConvertSourceBrushToClipped: the Store's tile -> product page transition)
   ask every visual surface for it; E_NOINTERFACE failed the whole XAML frame (CXcpDispatcher::Tick -> unhandled error) and left the
   page half built. Seen calls: slot 7 (Vector2 by value: the size the snapshot is realised at) and slot 9 (1: freeze the picture).
   Names are guesses from the call sites; the surface stays live (not frozen), the other slots are inert. */
static HRESULT STDMETHODCALLTYPE vsp_inert(void *This) { static LONG n; (void)This; if (InterlockedIncrement(&n) <= 5) comp_log("VisualSurfacePartner: an unknown slot was called (inert, S_OK)\n"); return S_OK; }
static HRESULT STDMETHODCALLTYPE vsp_size(void *This, V2 v) { VisSurf *s = (VisSurf *)OBJ_OF(This); s->realization = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE vsp_freeze(void *This, int on) { VisSurf *s = (VisSurf *)OBJ_OF(This); s->frozen = on; return S_OK; }
static const void *const VT_VSPartner[] = { (void *)cv_QI, (void *)cv_AddRef, (void *)cv_Release, (void *)cv_GetIids, (void *)cv_GetRuntimeClassName, (void *)cv_GetTrustLevel,
    (void *)vsp_inert, (void *)vsp_size, (void *)vsp_inert, (void *)vsp_freeze, (void *)vsp_inert, (void *)vsp_inert, (void *)vsp_inert, (void *)vsp_inert, (void *)vsp_inert, (void *)vsp_inert };
static HRESULT vsurf_qi_extra(CObj *o, REFIID r, void **p)
{
    static const GUID IID_VSPartner = {0xF26DA89E, 0x683D, 0x4C67, {0xAE, 0xA7, 0xBA, 0x29, 0xB2, 0x21, 0x7A, 0x70}}; VisSurf *v = (VisSurf *)o;
    if (IsEqualGUID(r, &IID_VSPartner)) { v->partner.vt = VT_VSPartner; v->partner.obj = o; comp_addref(o); *p = &v->partner; return S_OK; }
    return E_NOINTERFACE;
}
void comp_register_impls_effect(void)
{
    g_impl[CLS_CompositionEffectFactory].size = sizeof(EffFact);
    g_impl[CLS_CompositionEffectBrush].size = sizeof(EffBrush); g_impl[CLS_CompositionEffectBrush].destroy = eb_destroy;
    g_impl[CLS_CompositionVisualSurface].size = sizeof(VisSurf); g_impl[CLS_CompositionVisualSurface].destroy = vsurf_destroy; g_impl[CLS_CompositionVisualSurface].qi_extra = vsurf_qi_extra;
}
