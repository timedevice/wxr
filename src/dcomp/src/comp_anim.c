/* comp_anim.c - CompositionObject members, key-frame animations, easing, animation engine */
#include "comp_types.h"
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

/* bumped by every change of a property (comp_invalidate, property set writes): running expressions are only re-evaluated when it moved */
volatile LONG g_comp_gen = 1;
char g_tick_stats[160];   /* WXR_PERFLOG: what the last tick did */
LONGLONG comp_clock(void)
{
    static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c); return (LONGLONG)((double)c.QuadPart * 1e7 / (double)f.QuadPart);
}

/* ============================ CompositionObject ============================ */
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject, get_Compositor)(W(ICompositionObject) *This, W(ICompositor) **r)
{ CObj *o = OBJ_OF(This); return comp_return(o->comp ? o->comp : o, &IID_(ICompositor), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject, get_Dispatcher)(W(ICompositionObject) *This, Windows_UI_Core_ICoreDispatcher **r) { (void)This; *r = NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject3, get_DispatcherQueue)(W(ICompositionObject3) *This, Windows_System_IDispatcherQueue **r) { (void)This; *r = NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject, get_Properties)(W(ICompositionObject) *This, W(ICompositionPropertySet) **r)
{
    CObj *o = OBJ_OF(This); if (!r) return E_POINTER;
    if (!o->props) { o->props = comp_new(CLS_CompositionPropertySet, o->comp ? o->comp : o); if (!o->props) return E_OUTOFMEMORY; }
    return comp_qi(o->props, &IID_(ICompositionPropertySet), (void **)r);
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject, StartAnimation)(W(ICompositionObject) *This, HSTRING name, W(ICompositionAnimation) *anim)
{ return comp_start_animation(OBJ_OF(This), hs(name), anim ? OBJ_OF(anim) : NULL); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject, StopAnimation)(W(ICompositionObject) *This, HSTRING name) { return comp_stop_animation(OBJ_OF(This), hs(name)); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject2, get_Comment)(W(ICompositionObject2) *This, HSTRING *r) { CObj *o = OBJ_OF(This); return WindowsCreateString(o->comment ? o->comment : L"", o->comment ? lstrlenW(o->comment) : 0, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject2, put_Comment)(W(ICompositionObject2) *This, HSTRING v) { CObj *o = OBJ_OF(This); WCHAR *d; HRESULT hr = comp_dup_hstring(v, &d); if (FAILED(hr)) return hr; if (o->comment) HeapFree(GetProcessHeap(), 0, o->comment); o->comment = d; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObjectStatics, StartAnimationWithIAnimationObject)(W(ICompositionObjectStatics) *This, Windows_UI_Composition_IAnimationObject *t, HSTRING n, W(ICompositionAnimation) *a)
{
    /* XAML animates its facade properties (UIElement.Translation, Scale, Opacity ...; also app code: UIElement.StartAnimation) this way.
       ApplyElevationEffect (every flyout's FlyoutPresenter, every tooltip) starts one on Translation; E_NOTIMPL failed the measure pass, so
       every following XAML frame failed too. The element names the composition object behind the property (a property set XAML made
       through us, holding the current value) when asked with PopulatePropertyInfo; the animation then runs on that object, and XAML's
       own expressions carry the value to the element's visual. */
    const WCHAR *name = hs(n); WCHAR base[64]; const WCHAR *dot = wcschr(name, L'.'); size_t bl = dot ? (size_t)(dot - name) : wcslen(name); CObj *o, *comp = NULL; HRESULT hr;
    static LONG nlog;
    if (!t || !a) return E_INVALIDARG;
    if (!comp_is_foreign(t)) return comp_start_animation(OBJ_OF(t), name, OBJ_OF(a));
    if (bl >= 64) return E_INVALIDARG; memcpy(base, name, bl * sizeof(WCHAR)); base[bl] = 0;
    comp = OBJ_OF(a)->comp;
    o = comp_resolve_animation_object(comp, (IUnknown *)t, base);
    if (!o) { if (InterlockedIncrement(&nlog) <= 20) comp_log("StartAnimationWithIAnimationObject('%ls'): the object names no composition object for it (ignored, S_OK)\n", name); return S_OK; }
    hr = comp_start_animation(o, name, OBJ_OF(a)); comp_release(o); (void)This;
    return hr;
}
static HRESULT start_group_on(CObj *comp, IUnknown *t, CObj *base)
{
    int i; HRESULT hr = S_OK;
    if (!base) return E_INVALIDARG;
    if (base->cls == CLS_CompositionAnimationGroup) { AGroup *g = (AGroup *)base; for (i = 0; i < g->n; i++) { HRESULT h = start_group_on(comp, t, g->a[i]); if (FAILED(h)) hr = h; } return hr; }
    if (comp_is_a(base->cls, CLS_CompositionAnimation)) {
        Anim *an = (Anim *)base; WCHAR nm[64]; const WCHAR *dot; size_t bl; CObj *o;
        if (!an->target || !an->target[0]) return S_OK;
        dot = wcschr(an->target, L'.'); bl = dot ? (size_t)(dot - an->target) : wcslen(an->target); if (bl >= 64) return S_OK;
        memcpy(nm, an->target, bl * sizeof(WCHAR)); nm[bl] = 0;
        o = comp_resolve_animation_object(comp, t, nm); if (!o) return S_OK;
        hr = comp_start_animation(o, an->target, base); comp_release(o);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionObjectStatics, StartAnimationGroupWithIAnimationObject)(W(ICompositionObjectStatics) *This, Windows_UI_Composition_IAnimationObject *t, W(ICompositionAnimationBase) *a)
{
    (void)This; if (!t || !a) return E_INVALIDARG;
    if (!comp_is_foreign(t)) return CI_ICompositionObject2_StartAnimationGroup((W(ICompositionObject2) *)comp_view(OBJ_OF(t), &IID_(ICompositionObject2)), a);
    return start_group_on(OBJ_OF(a)->comp, (IUnknown *)t, OBJ_OF(a));
}

/* ============================ easing ============================ */
static float bez(float a, float b, float s) { float u = 1 - s; return 3 * u * u * s * a + 3 * u * s * s * b + s * s * s; }
float comp_ease(CObj *e, float t)
{
    Easing *x;
    if (t <= 0) return 0; if (t >= 1) return 1;
    if (!e || e->cls == CLS_LinearEasingFunction) return t;
    x = (Easing *)e;
    if (e->cls == CLS_CubicBezierEasingFunction) {
        float lo = 0, hi = 1, s = t; int i;
        for (i = 0; i < 24; i++) { float v = bez(x->c1x, x->c2x, s); if (v < t) lo = s; else hi = s; s = (lo + hi) * 0.5f; }
        return bez(x->c1y, x->c2y, s);
    }
    if (e->cls == CLS_StepEasingFunction) { int n = x->steps > 0 ? x->steps : 1; return floorf(t * n) / (float)n; }
    return t;
}

/* ============================ key-frame animations ============================ */
static void anim_init(CObj *o) { Anim *a = (Anim *)o; a->duration = 10000000; a->iterCount = 1; a->dim = 1; }
static void anim_destroy(CObj *o) { Anim *a = (Anim *)o; int i; for (i = 0; i < a->nkf; i++) { if (a->kf[i].ease) comp_release(a->kf[i].ease); if (a->kf[i].xs) HeapFree(GetProcessHeap(), 0, a->kf[i].xs); }
    if (a->kf) HeapFree(GetProcessHeap(), 0, a->kf); if (a->expr) HeapFree(GetProcessHeap(), 0, a->expr); if (a->target) HeapFree(GetProcessHeap(), 0, a->target);
    for (i = 0; i < a->npar; i++) comp_anim_par_clear(&a->par[i]); if (a->par) HeapFree(GetProcessHeap(), 0, a->par); }
static void kf_scalar_init(CObj *o) { ((Anim *)o)->dim = 1; }
static void kf_v2_init(CObj *o) { ((Anim *)o)->dim = 2; }
static void kf_v3_init(CObj *o) { ((Anim *)o)->dim = 3; }
static void kf_v4_init(CObj *o) { ((Anim *)o)->dim = 4; }
static void kf_col_init(CObj *o) { ((Anim *)o)->dim = 4; ((Anim *)o)->isColor = 1; }
static void kf_bool_init(CObj *o) { ((Anim *)o)->dim = 1; ((Anim *)o)->isBool = 1; }
static HRESULT kf_insert(void *This, float t, const float *v, void *easeIface)
{
    Anim *a = (Anim *)OBJ_OF(This); int i; KFrame k;
    if (!(t >= 0.0f && t <= 1.0f)) return E_INVALIDARG;
    if (a->nkf == a->cap) {
        int nc = a->cap ? a->cap * 2 : 4; KFrame *n = (KFrame *)HeapAlloc(GetProcessHeap(), 0, (size_t)nc * sizeof(KFrame)); if (!n) return E_OUTOFMEMORY;
        if (a->kf) { memcpy(n, a->kf, (size_t)a->nkf * sizeof(KFrame)); HeapFree(GetProcessHeap(), 0, a->kf); } a->kf = n; a->cap = nc;
    }
    k.t = t; k.expr = 0; memcpy(k.v, v, sizeof k.v); k.ease = easeIface ? OBJ_OF(easeIface) : NULL; if (k.ease) comp_addref(k.ease);
    for (i = 0; i < a->nkf; i++) if (a->kf[i].t == t) { if (a->kf[i].ease) comp_release(a->kf[i].ease); a->kf[i] = k; return S_OK; }   /* same key replaces */
    for (i = a->nkf; i > 0 && a->kf[i - 1].t > t; i--) a->kf[i] = a->kf[i - 1];
    a->kf[i] = k; a->nkf++; return S_OK;
}
#define KFI(Iface, type, conv) \
 HRESULT STDMETHODCALLTYPE CI_(Iface, InsertKeyFrame)(W(Iface) *This, float t, type v) { float f[4] = {0}; conv; return kf_insert(This, t, f, NULL); } \
 HRESULT STDMETHODCALLTYPE CI_(Iface, InsertKeyFrame_2)(W(Iface) *This, float t, type v, W(ICompositionEasingFunction) *e) { float f[4] = {0}; conv; return kf_insert(This, t, f, e); }
KFI(IScalarKeyFrameAnimation, float, f[0] = v)
KFI(IVector2KeyFrameAnimation, V2, f[0] = v.X; f[1] = v.Y)
KFI(IVector3KeyFrameAnimation, V3, f[0] = v.X; f[1] = v.Y; f[2] = v.Z)
KFI(IVector4KeyFrameAnimation, V4, f[0] = v.X; f[1] = v.Y; f[2] = v.Z; f[3] = v.W)
KFI(IQuaternionKeyFrameAnimation, Quat, f[0] = v.X; f[1] = v.Y; f[2] = v.Z; f[3] = v.W)
KFI(IColorKeyFrameAnimation, Col, f[0] = v.A; f[1] = v.R; f[2] = v.G; f[3] = v.B)
HRESULT STDMETHODCALLTYPE CI_(IBooleanKeyFrameAnimation, InsertKeyFrame)(W(IBooleanKeyFrameAnimation) *This, float t, boolean v) { float f[4] = {0}; f[0] = v ? 1.0f : 0.0f; return kf_insert(This, t, f, NULL); }

#define AN(T) ((Anim *)OBJ_OF(T))
#define KPROP(name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, get_##name)(W(IKeyFrameAnimation) *This, type *r) { *r = (type)AN(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, put_##name)(W(IKeyFrameAnimation) *This, type v) { AN(This)->field = v; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, get_DelayTime)(W(IKeyFrameAnimation) *This, TimeSpan *r) { r->Duration = AN(This)->delay; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, put_DelayTime)(W(IKeyFrameAnimation) *This, TimeSpan v) { AN(This)->delay = v.Duration; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, get_Duration)(W(IKeyFrameAnimation) *This, TimeSpan *r) { r->Duration = AN(This)->duration; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, put_Duration)(W(IKeyFrameAnimation) *This, TimeSpan v) { if (v.Duration < 10000 || v.Duration > 24LL * 3600 * 10000000) return E_INVALIDARG; AN(This)->duration = v.Duration; return S_OK; }
KPROP(IterationBehavior, Windows_UI_Composition_AnimationIterationBehavior, iterBehavior)
KPROP(IterationCount, INT32, iterCount)
KPROP(StopBehavior, Windows_UI_Composition_AnimationStopBehavior, stopBehavior)
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, get_KeyFrameCount)(W(IKeyFrameAnimation) *This, INT32 *r) { *r = AN(This)->nkf; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation2, get_Direction)(W(IKeyFrameAnimation2) *This, Windows_UI_Composition_AnimationDirection *r) { *r = AN(This)->direction; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation2, put_Direction)(W(IKeyFrameAnimation2) *This, Windows_UI_Composition_AnimationDirection v) { AN(This)->direction = v; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation3, get_DelayBehavior)(W(IKeyFrameAnimation3) *This, Windows_UI_Composition_AnimationDelayBehavior *r) { *r = AN(This)->delayBehavior; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation3, put_DelayBehavior)(W(IKeyFrameAnimation3) *This, Windows_UI_Composition_AnimationDelayBehavior v) { AN(This)->delayBehavior = v; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation2, get_Target)(W(ICompositionAnimation2) *This, HSTRING *r) { Anim *a = AN(This); return WindowsCreateString(a->target ? a->target : L"", a->target ? lstrlenW(a->target) : 0, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation2, put_Target)(W(ICompositionAnimation2) *This, HSTRING v) { Anim *a = AN(This); WCHAR *d; HRESULT hr = comp_dup_hstring(v, &d); if (FAILED(hr)) return hr; if (a->target) HeapFree(GetProcessHeap(), 0, a->target); a->target = d; return S_OK; }

#define CCREATE(fn, Iface, cls) HRESULT STDMETHODCALLTYPE CI_(Iface, fn)(W(Iface) *This, void **r) { \
    CObj *o; HRESULT hr; if (!r) return E_POINTER; o = comp_new(cls, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_return_default(o, r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateScalarKeyFrameAnimation)(W(ICompositor) *This, W(IScalarKeyFrameAnimation) **r) { CObj *o = comp_new(CLS_ScalarKeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IScalarKeyFrameAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateVector2KeyFrameAnimation)(W(ICompositor) *This, W(IVector2KeyFrameAnimation) **r) { CObj *o = comp_new(CLS_Vector2KeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IVector2KeyFrameAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateVector3KeyFrameAnimation)(W(ICompositor) *This, W(IVector3KeyFrameAnimation) **r) { CObj *o = comp_new(CLS_Vector3KeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IVector3KeyFrameAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateVector4KeyFrameAnimation)(W(ICompositor) *This, W(IVector4KeyFrameAnimation) **r) { CObj *o = comp_new(CLS_Vector4KeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IVector4KeyFrameAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateColorKeyFrameAnimation)(W(ICompositor) *This, W(IColorKeyFrameAnimation) **r) { CObj *o = comp_new(CLS_ColorKeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IColorKeyFrameAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateQuaternionKeyFrameAnimation)(W(ICompositor) *This, W(IQuaternionKeyFrameAnimation) **r) { CObj *o = comp_new(CLS_QuaternionKeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IQuaternionKeyFrameAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor6, CreateBooleanKeyFrameAnimation)(W(ICompositor6) *This, W(IBooleanKeyFrameAnimation) **r) { CObj *o = comp_new(CLS_BooleanKeyFrameAnimation, OBJ_OF(This)); HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IBooleanKeyFrameAnimation), (void **)r); comp_release(o); return hr; }

/* ============================ property access by name ============================ */
typedef struct PropDef { const WCHAR *name; size_t off; int dim; } PropDef;
static const PropDef k_vis[] = {
    { L"Offset", offsetof(Visual, offset), 3 }, { L"Size", offsetof(Visual, size), 2 }, { L"Scale", offsetof(Visual, scale), 3 },
    { L"CenterPoint", offsetof(Visual, center), 3 }, { L"AnchorPoint", offsetof(Visual, anchor), 2 }, { L"Opacity", offsetof(Visual, opacity), 1 },
    { L"RotationAngle", offsetof(Visual, rotation), 1 }, { L"RelativeOffsetAdjustment", offsetof(Visual, relOffset), 3 },
    { L"RelativeSizeAdjustment", offsetof(Visual, relSize), 2 }, { L"Orientation", offsetof(Visual, orient), 4 }, { L"RotationAxis", offsetof(Visual, axis), 3 },
    { L"TransformMatrix", offsetof(Visual, tm), 16 }, { NULL, 0, 0 } };
static const PropDef k_inset[] = { { L"LeftInset", offsetof(InsetClip, l), 1 }, { L"TopInset", offsetof(InsetClip, t), 1 }, { L"RightInset", offsetof(InsetClip, r), 1 },
    { L"BottomInset", offsetof(InsetClip, b), 1 }, { L"Offset", offsetof(InsetClip, c.offset), 2 }, { L"Scale", offsetof(InsetClip, c.scale), 2 },
    { L"CenterPoint", offsetof(InsetClip, c.center), 2 }, { L"AnchorPoint", offsetof(InsetClip, c.anchor), 2 }, { L"RotationAngle", offsetof(InsetClip, c.rotation), 1 },
    { L"TransformMatrix", offsetof(InsetClip, c.tm), 6 }, { NULL, 0, 0 } };
static const PropDef k_grad[] = { { L"StartPoint", offsetof(GradBrush, start), 2 }, { L"EndPoint", offsetof(GradBrush, end), 2 }, { L"Offset", offsetof(GradBrush, offset), 2 },
    { L"Scale", offsetof(GradBrush, scale), 2 }, { L"CenterPoint", offsetof(GradBrush, center), 2 }, { L"AnchorPoint", offsetof(GradBrush, anchor), 2 }, { L"RotationAngle", offsetof(GradBrush, rotation), 1 },
    { L"TransformMatrix", offsetof(GradBrush, tm), 6 }, { NULL, 0, 0 } };
static const PropDef k_stop[] = { { L"Offset", offsetof(Stop, offset), 1 }, { NULL, 0, 0 } };
/* shapes and geometries (WinUI's animated icons animate these; RotationAngleInDegrees is handled by name like the visual's) */
static const PropDef k_shape[] = { { L"Offset", offsetof(Shape, offset), 2 }, { L"Scale", offsetof(Shape, scale), 2 }, { L"CenterPoint", offsetof(Shape, center), 2 },
    { L"RotationAngle", offsetof(Shape, rotation), 1 }, { L"TransformMatrix", offsetof(Shape, tm), 6 }, { NULL, 0, 0 } };
static const PropDef k_sprshape[] = { { L"Offset", offsetof(Shape, offset), 2 }, { L"Scale", offsetof(Shape, scale), 2 }, { L"CenterPoint", offsetof(Shape, center), 2 },
    { L"RotationAngle", offsetof(Shape, rotation), 1 }, { L"TransformMatrix", offsetof(Shape, tm), 6 }, { L"StrokeThickness", offsetof(SpriteShape, thickness), 1 }, { NULL, 0, 0 } };
static const PropDef k_geo[] = { { L"TrimStart", offsetof(Geometry, trimStart), 1 }, { L"TrimEnd", offsetof(Geometry, trimEnd), 1 }, { L"TrimOffset", offsetof(Geometry, trimOffset), 1 }, { NULL, 0, 0 } };
#define TRIMS { L"TrimStart", offsetof(Geometry, trimStart), 1 }, { L"TrimEnd", offsetof(Geometry, trimEnd), 1 }, { L"TrimOffset", offsetof(Geometry, trimOffset), 1 }
static const PropDef k_rectgeo[] = { TRIMS, { L"Offset", offsetof(RectGeo, offset), 2 }, { L"Size", offsetof(RectGeo, size), 2 }, { NULL, 0, 0 } };
static const PropDef k_rrectgeo[] = { TRIMS, { L"Offset", offsetof(RRectGeo, offset), 2 }, { L"Size", offsetof(RRectGeo, size), 2 }, { L"CornerRadius", offsetof(RRectGeo, radius), 2 }, { NULL, 0, 0 } };
static const PropDef k_ellgeo[] = { TRIMS, { L"Center", offsetof(EllipseGeo, center), 2 }, { L"Radius", offsetof(EllipseGeo, radius), 2 }, { NULL, 0, 0 } };
static const PropDef k_linegeo[] = { TRIMS, { L"Start", offsetof(LineGeo, start), 2 }, { L"End", offsetof(LineGeo, end), 2 }, { NULL, 0, 0 } };
static const PropDef k_vsurf[] = { { L"SourceOffset", offsetof(VisSurf, offset), 2 }, { L"SourceSize", offsetof(VisSurf, size), 2 }, { NULL, 0, 0 } };
static const PropDef k_ctl[] = { { L"Progress", offsetof(AnimCtl, progress), 1 }, { L"PlaybackRate", offsetof(AnimCtl, rate), 1 }, { NULL, 0, 0 } };
static const float DEG = 3.14159265358979f / 180.0f;

/* path = "Name" | "Name.X" | "Clip.LeftInset" | "Brush.Color" ... */
static int resolve(CObj *t, const WCHAR *path, CObj **tgt, WCHAR *name, int *chan)
{
    const WCHAR *dot = wcschr(path, L'.'); size_t n = dot ? (size_t)(dot - path) : wcslen(path); *chan = -1;
    if (dot && comp_is_a(t->cls, CLS_Visual) && n == 4 && !wcsncmp(path, L"Clip", 4)) { Visual *v = (Visual *)t; if (!v->clip) return 0; return resolve(v->clip, dot + 1, tgt, name, chan); }
    if (dot && comp_is_a(t->cls, CLS_SpriteVisual) && n == 5 && !wcsncmp(path, L"Brush", 5)) { Sprite *s = (Sprite *)t; if (!s->brush) return 0; return resolve(s->brush, dot + 1, tgt, name, chan); }
    if (n >= 63) return 0; memcpy(name, path, n * sizeof(WCHAR)); name[n] = 0;
    if (dot) { const WCHAR *c = dot + 1; *chan = (!wcscmp(c, L"X") || !wcscmp(c, L"A")) ? 0 : (!wcscmp(c, L"Y") || !wcscmp(c, L"R")) ? 1 : (!wcscmp(c, L"Z") || !wcscmp(c, L"G")) ? 2 : (!wcscmp(c, L"W") || !wcscmp(c, L"B")) ? 3 : !wcscmp(c, L"XY") ? -3 : !wcscmp(c, L"XYZ") ? -1 : -2; if (*chan == -2) return 0; }   /* -3 = the first two components ("Translation.XY": XAML connected / page transition animations) */
    *tgt = t; return 1;
}
static const PropDef *find_def(CObj *t, const WCHAR *name)
{
    const PropDef *d = NULL;
    if (comp_is_a(t->cls, CLS_Visual)) d = k_vis; else if (t->cls == CLS_InsetClip) d = k_inset;
    else if (comp_is_a(t->cls, CLS_CompositionGradientBrush)) d = k_grad; else if (t->cls == CLS_CompositionColorGradientStop) d = k_stop;
    else if (t->cls == CLS_CompositionSpriteShape) d = k_sprshape; else if (comp_is_a(t->cls, CLS_CompositionShape)) d = k_shape;
    else if (t->cls == CLS_CompositionRectangleGeometry) d = k_rectgeo; else if (t->cls == CLS_CompositionRoundedRectangleGeometry) d = k_rrectgeo;
    else if (t->cls == CLS_CompositionEllipseGeometry) d = k_ellgeo; else if (t->cls == CLS_CompositionLineGeometry) d = k_linegeo;
    else if (comp_is_a(t->cls, CLS_CompositionGeometry)) d = k_geo; else if (t->cls == CLS_AnimationController) d = k_ctl; else if (t->cls == CLS_CompositionVisualSurface) d = k_vsurf;
    for (; d && d->name; d++) if (!wcscmp(d->name, name)) return d;
    return NULL;
}
int comp_prop_get(CObj *t, const WCHAR *path, int *dim, float *v)
{
    CObj *x; WCHAR name[64]; int chan; const PropDef *d; float *p;
    if (t->cls == CLS_CompositionEffectBrush) { if (!comp_effect_prop(t, path, 0, v)) return 0; *dim = 1; return 1; }
    if (!resolve(t, path, &x, name, &chan)) return 0;
    if (x->cls == CLS_InteractionTracker) { float f[16] = {0}; int dd = 0; if (!comp_tracker_prop(x, name, 0, &dd, f)) return 0;
        if (chan >= 0) { if (chan >= dd) return 0; v[0] = f[chan]; *dim = 1; } else { if (chan == -3 && dd > 2) dd = 2; memcpy(v, f, (size_t)dd * sizeof(float)); *dim = dd; } return 1; }
    if (!wcscmp(name, L"RotationAngleInDegrees") && comp_is_a(x->cls, CLS_Visual)) { v[0] = ((Visual *)x)->rotation / DEG; *dim = 1; return 1; }
    if (!wcscmp(name, L"RotationAngleInDegrees") && comp_is_a(x->cls, CLS_CompositionShape)) { v[0] = ((Shape *)x)->rotation / DEG; *dim = 1; return 1; }
    if (!wcscmp(name, L"IsVisible") && comp_is_a(x->cls, CLS_Visual)) { v[0] = ((Visual *)x)->visible ? 1.0f : 0.0f; *dim = 1; return 1; }
    if (!wcscmp(name, L"Color") && x->cls == CLS_CompositionColorBrush) { Col c = ((ColorBrush *)x)->color; float f[4] = { c.A, c.R, c.G, c.B }; if (chan >= 0) { v[0] = f[chan]; *dim = 1; } else { memcpy(v, f, sizeof f); *dim = 4; } return 1; }
    if (!wcscmp(name, L"Color") && x->cls == CLS_CompositionColorGradientStop) { Col c = ((Stop *)x)->color; float f[4] = { c.A, c.R, c.G, c.B }; if (chan >= 0) { v[0] = f[chan]; *dim = 1; } else { memcpy(v, f, sizeof f); *dim = 4; } return 1; }
    if (x->cls == CLS_CompositionPropertySet) { PEntry *e = comp_pset_find(x, name); int dd; if (!e) return 0; dd = comp_pset_dim(e->kind);
        if (chan >= 0) { if (chan >= dd) return 0; v[0] = e->v[chan]; *dim = 1; } else { if (chan == -3 && dd > 2) dd = 2; memcpy(v, e->v, (size_t)dd * sizeof(float)); *dim = dd; } return 1; }
    d = find_def(x, name); if (!d) return 0; p = (float *)((char *)x + d->off);
    if (chan >= 0) { if (chan >= d->dim) return 0; v[0] = p[chan]; *dim = 1; } else { int dd = (chan == -3 && d->dim > 2) ? 2 : d->dim; memcpy(v, p, (size_t)dd * sizeof(float)); *dim = dd; }
    return 1;
}
int comp_prop_set(CObj *t, const WCHAR *path, int dim, const float *v)
{
    CObj *x; WCHAR name[64]; int chan; const PropDef *d; float *p; int i;
    if (t->cls == CLS_CompositionEffectBrush) { float f = v[0]; return comp_effect_prop(t, path, 1, &f); }
    if (!resolve(t, path, &x, name, &chan)) return 0;
    if (x->cls == CLS_InteractionTracker) { float f[16] = {0}; int dd = 0, d2; if (!comp_tracker_prop(x, name, 0, &dd, f)) return 0;
        if (chan >= 0) { if (chan >= dd) return 0; f[chan] = v[0]; } else { d2 = (chan == -3 && dd > 2) ? 2 : dd; for (i = 0; i < d2 && i < dim; i++) f[i] = v[i]; }
        return comp_tracker_prop(x, name, 1, &dd, f); }
    if (!wcscmp(name, L"RotationAngleInDegrees") && comp_is_a(x->cls, CLS_Visual)) { ((Visual *)x)->rotation = v[0] * DEG; INVALIDATE(x); return 1; }
    if (!wcscmp(name, L"RotationAngleInDegrees") && comp_is_a(x->cls, CLS_CompositionShape)) { ((Shape *)x)->rotation = v[0] * DEG; INVALIDATE(x); return 1; }
    if (!wcscmp(name, L"IsVisible") && comp_is_a(x->cls, CLS_Visual)) { ((Visual *)x)->visible = v[0] > 0.5f; INVALIDATE(x); return 1; }
    if (!wcscmp(name, L"Color") && (x->cls == CLS_CompositionColorBrush || x->cls == CLS_CompositionColorGradientStop)) {
        Col *c = x->cls == CLS_CompositionColorBrush ? &((ColorBrush *)x)->color : &((Stop *)x)->color; float f[4] = { c->A, c->R, c->G, c->B };
        if (chan >= 0) f[chan] = v[0]; else for (i = 0; i < 4 && i < dim; i++) f[i] = v[i];
        for (i = 0; i < 4; i++) f[i] = f[i] < 0 ? 0 : f[i] > 255 ? 255 : f[i];
        c->A = (UINT8)(f[0] + 0.5f); c->R = (UINT8)(f[1] + 0.5f); c->G = (UINT8)(f[2] + 0.5f); c->B = (UINT8)(f[3] + 0.5f); INVALIDATE(x); return 1;
    }
    if (x->cls == CLS_CompositionPropertySet) { PEntry *e = comp_pset_find(x, name); int dd; if (!e) return 0; dd = comp_pset_dim(e->kind);
        if (chan >= 0) { if (chan >= dd) return 0; e->v[chan] = v[0]; } else { if (chan == -3 && dd > 2) dd = 2; for (i = 0; i < dd && i < dim; i++) e->v[i] = v[i]; } InterlockedIncrement(&g_comp_gen); return 1; }
    d = find_def(x, name); if (!d) return 0; p = (float *)((char *)x + d->off);
    if (chan >= 0) { if (chan >= d->dim) return 0; p[chan] = v[0]; } else { int dd = (chan == -3 && d->dim > 2) ? 2 : d->dim; for (i = 0; i < dd && i < dim; i++) p[i] = v[i]; }
    INVALIDATE(x); return 1;
}

/* ============================ engine ============================ */
static int anim_dim_ok(Anim *a, int dim) { return a->dim == dim || (a->isColor && dim == 4); }
/* a name the object does not animate itself is one of its Properties (composition resolves obj.StartAnimation("TStart") against
   obj.Properties: LottieGen trim paths "TStart"/"TEnd" on geometries, XAML's "Translation" on element visuals) */
static CObj *anim_owner(CObj *t, const WCHAR *name)
{
    int dim; float v[16];
    if (!t || comp_prop_get(t, name, &dim, v)) return t;
    if (t->props && comp_prop_get(t->props, name, &dim, v)) return t->props;
    return t;
}
/* each (class, property) pair once: the Store starts thousands of animations on properties not modelled here (shadows, lights) */
static void unknown_prop(const char *what, CObj *target, const WCHAR *name)
{
    static DWORD seen[256]; static LONG nseen; DWORD h = (DWORD)target->cls * 2654435761u; const WCHAR *q; LONG k, ns = nseen;
    for (q = name; *q; q++) h = (h ^ *q) * 16777619u;
    for (k = 0; k < ns && k < 256; k++) if (seen[k] == h) return;
    if (ns < 256) { seen[ns] = h; nseen = ns + 1; }
    comp_log("%s: unknown animatable property '%ls' on %s (ignored, S_OK; logged once)\n", what, name, g_cd[target->cls].sname);
}
void comp_exprbind_free(CObj *comp, ExprBind *x) { comp_batch_detach_list(comp, x->batches, &x->nb); comp_release(x->anim); comp_release(x->target); if (x->ctx) comp_release(x->ctx); HeapFree(GetProcessHeap(), 0, x); }
HRESULT comp_start_animation(CObj *target, const WCHAR *name, CObj *anim)
{
    Compositor *c = (Compositor *)(target->comp ? target->comp : target); ActiveAnim *a, **pp; int dim; float v[16];
    CObj *ctx = target;      /* what expressions call this.Target: the object the caller named, also when the property lives in its Properties */
    if (!anim) return E_INVALIDARG;
    target = anim_owner(target, name);
    if (anim->cls == CLS_ExpressionAnimation) {          /* live binding: evaluated now and on every tick (comp_expr.c) */
        ExprBind *b, **bp; float ev[16]; int edim, er;
        for (bp = &c->exprs; *bp; ) { ExprBind *x = *bp; if (x->target == target && !_wcsicmp(x->prop, name)) { *bp = x->next; comp_exprbind_free(&c->o, x); } else bp = &x->next; }
        for (pp = &c->active; *pp; ) { ActiveAnim *x = *pp; if (x->target == target && !_wcsicmp(x->prop, name)) { *pp = x->next; comp_aa_free(&c->o, x); } else pp = &x->next; }
        if (!comp_prop_get(target, name, &dim, v)) { unknown_prop("StartAnimation(expression)", target, name); return S_OK; }
        b = (ExprBind *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *b); if (!b) return E_OUTOFMEMORY;
        b->target = target; comp_addref(target); b->ctx = ctx; comp_addref(ctx); b->anim = anim; comp_addref(anim); lstrcpynW(b->prop, name, 64);
        b->next = c->exprs; c->exprs = b;
        comp_batch_attach_list(&c->o, b->batches, &b->nb, 4);   /* an expression never ends: only InfiniteAnimation batches wait for it (XAML's facade animations rely on that) */
        er = comp_expr_eval(anim, ctx, name, ev, &edim);
        if (er == 1) { memcpy(b->last, ev, sizeof b->last); b->have = 1; comp_prop_set(target, name, edim, ev); }
        else if (er < 0) b->bad = 1;                              /* text not supported: never retried */
        /* er == 0: not evaluable yet (a referenced property set entry may not exist yet); retried on later ticks */
        comp_invalidate(&c->o); return S_OK;
    }
    if (!comp_is_a(anim->cls, CLS_KeyFrameAnimation)) { static LONG once; if (!InterlockedExchange(&once, 1)) comp_log("StartAnimation('%ls'): %s is not run (only key-frame animations are evaluated); returning S_OK\n", name, g_cd[anim->cls].sname); return S_OK; }
    if (!comp_prop_get(target, name, &dim, v)) { unknown_prop("StartAnimation", target, name); return S_OK; }
    (void)anim_dim_ok;
    for (pp = &c->active; *pp; ) { ActiveAnim *x = *pp; if (x->target == target && !_wcsicmp(x->prop, name)) { *pp = x->next; comp_aa_free(&c->o, x); } else pp = &x->next; }
    a = (ActiveAnim *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a); if (!a) return E_OUTOFMEMORY;
    a->target = target; comp_addref(target); a->ctx = ctx; comp_addref(ctx); a->anim = anim; comp_addref(anim); lstrcpynW(a->prop, name, 64);
    a->t0 = comp_clock(); memcpy(a->start, v, sizeof a->start); a->have_start = 1;
    a->next = c->active; c->active = a; comp_batch_attach(&c->o, a);
    comp_invalidate(&c->o); return S_OK;
}
HRESULT comp_stop_animation(CObj *target, const WCHAR *name)
{
    Compositor *c = (Compositor *)(target->comp ? target->comp : target); ActiveAnim **pp; ExprBind **bp;
    target = anim_owner(target, name);
    for (pp = &c->active; *pp; ) { ActiveAnim *x = *pp; if (x->target == target && !_wcsicmp(x->prop, name)) { *pp = x->next; comp_aa_free(&c->o, x); } else pp = &x->next; }
    for (bp = &c->exprs; *bp; ) { ExprBind *x = *bp; if (x->target == target && !_wcsicmp(x->prop, name)) { *bp = x->next; comp_exprbind_free(&c->o, x); } else bp = &x->next; }
    return S_OK;
}
/* animations paused by a controller only change when their Progress is written (which invalidates): they do not keep frames coming */
int comp_has_active(CObj *comp)
{
    Compositor *c = (Compositor *)comp; ActiveAnim *x;
    extern int comp_interact_busy(CObj *);
    if (!c) return 0; if (c->nfire > 0) return 1;
    if (c->trackers && comp_interact_busy(comp)) return 1;
    for (x = c->active; x; x = x->next) if (!(x->ctl && ((AnimCtl *)x->ctl)->paused)) return 1;
    return 0;
}

void comp_aa_free(CObj *comp, ActiveAnim *x) { comp_batch_detach(comp, x); if (x->ctl) comp_release(x->ctl); comp_release(x->anim); comp_release(x->target); if (x->ctx) comp_release(x->ctx); HeapFree(GetProcessHeap(), 0, x); }

/* ============================ AnimationController ============================
   TryGetAnimationController(prop) hands out the controller of the animation running on that property (created on first request). WinUI's
   animated icons (Lottie codegen StartProgressBoundAnimation) Pause it and bind its Progress to the icon's progress with an expression. */
#define CTL(T) ((AnimCtl *)OBJ_OF(T))
static void ctl_init(CObj *o) { ((AnimCtl *)o)->rate = 1.0f; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject4, TryGetAnimationController)(W(ICompositionObject4) *This, HSTRING propertyName, W(IAnimationController) **result)
{
    CObj *t = OBJ_OF(This), *o; Compositor *c = (Compositor *)(t->comp ? t->comp : t); ActiveAnim *x; const WCHAR *nm = WindowsGetStringRawBuffer(propertyName, NULL); HRESULT hr;
    if (!result) return E_POINTER; *result = NULL;
    if (nm) t = anim_owner(t, nm);
    for (x = c->active; x; x = x->next) if (x->target == t && nm && !_wcsicmp(x->prop, nm)) break;
    if (x && x->ctl) return comp_return_default(x->ctl, (void **)result);
    /* no running animation (e.g. one on a property not animated here): a detached controller, never NULL (callers use it unchecked) */
    o = comp_new(CLS_AnimationController, &c->o); if (!o) return E_OUTOFMEMORY;
    if (x) { x->ctl = o; comp_addref(o); }
    hr = comp_return_default(o, (void **)result); comp_release(o); return hr;
}
HRESULT STDMETHODCALLTYPE CI_(IAnimationController, Pause)(W(IAnimationController) *This) { CTL(This)->paused = 1; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IAnimationController, Resume)(W(IAnimationController) *This) { CTL(This)->paused = 0; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IAnimationController, get_Progress)(W(IAnimationController) *This, float *r) { if (!r) return E_POINTER; *r = CTL(This)->progress; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IAnimationController, put_Progress)(W(IAnimationController) *This, float v) { CTL(This)->progress = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IAnimationController, get_PlaybackRate)(W(IAnimationController) *This, float *r) { if (!r) return E_POINTER; *r = CTL(This)->rate; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IAnimationController, put_PlaybackRate)(W(IAnimationController) *This, float v) { CTL(This)->rate = v; return S_OK; }
/* value of key frame j: its constant, or its expression evaluated now with the animation's parameters (this.StartingValue = the value
   the property had when the animation started); an expression that cannot be evaluated holds the start value */
static const float *kf_val(Anim *a, ActiveAnim *aa, int j, float *tmp)
{
    const KFrame *k = &a->kf[j]; int dim = 0;
    if (!k->expr) return k->v;
    if (k->xs && comp_expr_eval_text(k->xs, &a->o, aa->ctx ? aa->ctx : aa->target, aa->prop, aa->start, a->isColor ? 4 : a->dim, tmp, &dim) == 1) { if (dim == 1 && a->dim > 1) { int i; for (i = 1; i < 4; i++) tmp[i] = tmp[0]; } return tmp; }
    return aa->start;
}
#define KV(j) kf_val(a, aa, (j), tmpv[(j) & 1])
static void kf_value(Anim *a, ActiveAnim *aa, float p, float *out)
{
    int i, d = a->dim; KFrame first; const KFrame *k = a->kf; int n = a->nkf; float tmpv[2][16];
    if (n == 0) { memcpy(out, aa->start, sizeof(float) * 4); return; }
    if (a->isBool) { float cur = k[0].t <= p ? KV(0)[0] : aa->start[0]; for (i = 0; i < n; i++) if (k[i].t <= p) cur = KV(i)[0]; out[0] = cur; return; }
    /* implicit start key at t=0 holding the value the property had when the animation started */
    if (k[0].t > 0.0f) {
        if (p < k[0].t) { float l = k[0].t > 0 ? p / k[0].t : 1; const float *v0 = KV(0); l = comp_ease(k[0].ease, l); for (i = 0; i < d; i++) out[i] = aa->start[i] + (v0[i] - aa->start[i]) * l; return; }
    }
    first = k[0]; (void)first;
    if (p <= k[0].t) { memcpy(out, KV(0), sizeof(float) * 4); return; }
    for (i = 1; i < n; i++) if (p <= k[i].t) {
        float span = k[i].t - k[i - 1].t, l = span > 0 ? (p - k[i - 1].t) / span : 1.0f; int j; const float *va = KV(i - 1), *vb = KV(i); l = comp_ease(k[i].ease, l);
        for (j = 0; j < 4; j++) out[j] = va[j] + (vb[j] - va[j]) * l; return;
    }
    memcpy(out, KV(n - 1), sizeof(float) * 4);
}
/* returns 1 if finished */
static int eval_anim(ActiveAnim *aa, LONGLONG now, float *out, int *valid)
{
    Anim *a = (Anim *)aa->anim; double el = (double)(now - aa->t0), dur = (double)a->duration; double t; double cyc; int iter; float p; int finished = 0;
    *valid = 1;
    if (aa->ctl && ((AnimCtl *)aa->ctl)->paused) {      /* paused by its controller: positioned by Progress, never finishes */
        p = ((AnimCtl *)aa->ctl)->progress; p = p < 0 ? 0 : p > 1 ? 1 : p; kf_value(a, aa, p, out); return 0; }
    if (el < (double)a->delay) { if (a->delayBehavior == 0) { kf_value(a, aa, 0.0f, out); return 0; } *valid = 0; return 0; }
    t = el - (double)a->delay; cyc = floor(t / dur); iter = (int)cyc;
    if (a->iterBehavior == Windows_UI_Composition_AnimationIterationBehavior_Count) {
        int cnt = a->iterCount > 0 ? a->iterCount : 1;
        if (iter >= cnt) { iter = cnt - 1; p = 1.0f; finished = 1; } else p = (float)((t - cyc * dur) / dur);
    } else p = (float)((t - cyc * dur) / dur);
    switch (a->direction) {
    case 1: p = 1.0f - p; break;
    case 2: if (iter & 1) p = 1.0f - p; break;
    case 3: if (!(iter & 1)) p = 1.0f - p; break;
    }
    kf_value(a, aa, p, out); return finished;
}
void comp_tick(CObj *co)
{
    Compositor *c = (Compositor *)co; ActiveAnim **pp; LONGLONG now = comp_clock(); int n_anim = 0, n_bind = 0, n_eval = 0, n_pass = 0; LONGLONG tq0 = now, tq1, tq2;
    if (!c) return;
    dcomp_manip_tick(co);   /* DirectManipulation offsets into ManipulationTransform "Matrix" before the expressions read them */
    for (pp = &c->active; *pp; ) {
        ActiveAnim *x = *pp; float v[16] = {0}; int valid, fin = eval_anim(x, now, v, &valid), dim = ((Anim *)x->anim)->dim; n_anim++;
        if (valid && !(x->have_last && !memcmp(v, x->last, sizeof x->last))) {
            memcpy(x->last, v, sizeof x->last); x->have_last = 1; comp_prop_set(x->target, x->prop, ((Anim *)x->anim)->isColor ? 4 : dim, v); }
        if (fin) { *pp = x->next; comp_aa_free(&c->o, x); } else pp = &x->next;
    }
    tq1 = comp_clock();
    {   /* expression bindings: re-evaluate, write only on change (a write invalidates = another frame). Bindings feed each other (XAML's
           transform property sets: Translate -> Expression -> Render -> the visual's TransformMatrix), so passes repeat until nothing changes */
        ExprBind *b; float v[16]; int dim, pass, changed; LONG g0 = 0;
        for (pass = 0, changed = 1; changed && pass < 4 && c->expr_gen != g_comp_gen; pass++) {
            changed = 0; g0 = g_comp_gen; n_pass++;
            for (b = c->exprs; b; b = b->next) {
                int er; n_bind++; if (b->bad) continue; n_eval++;
                er = comp_expr_eval(b->anim, b->ctx ? b->ctx : b->target, b->prop, v, &dim);
                if (er < 0) { b->bad = 1; continue; } if (er != 1) continue;
                if (b->have && !memcmp(v, b->last, (size_t)dim * sizeof(float))) continue;
                memcpy(b->last, v, sizeof b->last); b->have = 1; comp_prop_set(b->target, b->prop, dim, v); changed = 1;
            }
            if (!changed) c->expr_gen = g0;      /* a full pass that changed nothing: settled for this generation */
        } }
    tq2 = comp_clock();
    comp_interact_tick(co);
    comp_batch_frame(&c->o);
    _snprintf(g_tick_stats, sizeof g_tick_stats - 1, "%d animations %.1f ms, %d expression passes (%d evaluations of %d bindings) %.1f ms", n_anim, (double)(tq1 - tq0) / 10000.0, n_pass, n_eval, n_pass ? n_bind / n_pass : 0, (double)(tq2 - tq1) / 10000.0);
}

void comp_register_impls_anim(void)
{
    int c;
    g_impl[CLS_CompositionAnimation].init = anim_init; g_impl[CLS_CompositionAnimation].destroy = anim_destroy; g_impl[CLS_CompositionAnimation].size = sizeof(Anim);   /* every animation class (also ones only modelled as inert: path, spring, bounce) carries an Anim */
    g_impl[CLS_AnimationController].size = sizeof(AnimCtl); g_impl[CLS_AnimationController].init = ctl_init;
    g_impl[CLS_ScalarKeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_ScalarKeyFrameAnimation].init = kf_scalar_init;
    g_impl[CLS_Vector2KeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_Vector2KeyFrameAnimation].init = kf_v2_init;
    g_impl[CLS_Vector3KeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_Vector3KeyFrameAnimation].init = kf_v3_init;
    g_impl[CLS_Vector4KeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_Vector4KeyFrameAnimation].init = kf_v4_init;
    g_impl[CLS_QuaternionKeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_QuaternionKeyFrameAnimation].init = kf_v4_init;
    g_impl[CLS_ColorKeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_ColorKeyFrameAnimation].init = kf_col_init;
    g_impl[CLS_BooleanKeyFrameAnimation].size = sizeof(Anim); g_impl[CLS_BooleanKeyFrameAnimation].init = kf_bool_init;
    g_impl[CLS_ExpressionAnimation].size = sizeof(Anim);
    (void)c;
}
