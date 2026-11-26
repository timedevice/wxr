/* comp_visual.c - Compositor factory methods, Visual / ContainerVisual / SpriteVisual / VisualCollection, clips, brushes, gradient stops */
#include "comp_types.h"

#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

static HRESULT create(void *This, int cls, const GUID *iid, void **out)
{
    CObj *o; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    o = comp_new(cls, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY;
    hr = comp_qi(o, iid, out); comp_release(o); return hr;
}

/* ============================ Compositor ============================ */
static void compositor_init(CObj *o) { ((Compositor *)o)->rate = 1.0f; }
static void compositor_destroy(CObj *o)
{
    Compositor *c = (Compositor *)o; ActiveAnim *a = c->active;
    while (a) { ActiveAnim *n = a->next; comp_aa_free(o, a); a = n; }
    { int i; for (i = 0; i < c->nscoped; i++) comp_release(c->scoped[i]); if (c->commit) comp_release(c->commit); for (i = 0; i < c->nfire; i++) comp_release(c->fireq[i]); }
    if (c->targets) HeapFree(GetProcessHeap(), 0, c->targets);
}
CObj *comp_create_compositor(void) { return comp_new(CLS_Compositor, NULL); }

HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateContainerVisual)(W(ICompositor) *This, W(IContainerVisual) **r) { return create(This, CLS_ContainerVisual, &IID_(IContainerVisual), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateSpriteVisual)(W(ICompositor) *This, W(ISpriteVisual) **r) { return create(This, CLS_SpriteVisual, &IID_(ISpriteVisual), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateColorBrush)(W(ICompositor) *This, W(ICompositionColorBrush) **r) { HRESULT hr = create(This, CLS_CompositionColorBrush, &IID_(ICompositionColorBrush), (void **)r); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateColorBrush_2)(W(ICompositor) *This, Col c, W(ICompositionColorBrush) **r)
{
    HRESULT hr = create(This, CLS_CompositionColorBrush, &IID_(ICompositionColorBrush), (void **)r);
    if (SUCCEEDED(hr)) ((ColorBrush *)OBJ_OF(*r))->color = c; return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateInsetClip)(W(ICompositor) *This, W(IInsetClip) **r) { return create(This, CLS_InsetClip, &IID_(IInsetClip), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateInsetClip_2)(W(ICompositor) *This, float l, float t, float rr, float b, W(IInsetClip) **r)
{
    HRESULT hr = create(This, CLS_InsetClip, &IID_(IInsetClip), (void **)r); InsetClip *c;
    if (SUCCEEDED(hr)) { c = (InsetClip *)OBJ_OF(*r); c->l = l; c->t = t; c->r = rr; c->b = b; } return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor4, CreateLinearGradientBrush)(W(ICompositor4) *This, W(ICompositionLinearGradientBrush) **r) { return create(This, CLS_CompositionLinearGradientBrush, &IID_(ICompositionLinearGradientBrush), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor4, CreateColorGradientStop)(W(ICompositor4) *This, W(ICompositionColorGradientStop) **r) { return create(This, CLS_CompositionColorGradientStop, &IID_(ICompositionColorGradientStop), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor4, CreateColorGradientStop_2)(W(ICompositor4) *This, float off, Col c, W(ICompositionColorGradientStop) **r)
{
    HRESULT hr = create(This, CLS_CompositionColorGradientStop, &IID_(ICompositionColorGradientStop), (void **)r);
    if (SUCCEEDED(hr)) { Stop *s = (Stop *)OBJ_OF(*r); s->offset = off; s->color = c; } return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor5, get_GlobalPlaybackRate)(W(ICompositor5) *This, float *r) { *r = ((Compositor *)OBJ_OF(This))->rate; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor5, put_GlobalPlaybackRate)(W(ICompositor5) *This, float v) { ((Compositor *)OBJ_OF(This))->rate = v; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor5, get_Comment)(W(ICompositor5) *This, HSTRING *r) { CObj *o = OBJ_OF(This); if (!r) return E_POINTER; return WindowsCreateString(o->comment ? o->comment : L"", o->comment ? lstrlenW(o->comment) : 0, r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor5, put_Comment)(W(ICompositor5) *This, HSTRING v) { CObj *o = OBJ_OF(This); WCHAR *d; HRESULT hr = comp_dup_hstring(v, &d); if (FAILED(hr)) return hr; if (o->comment) HeapFree(GetProcessHeap(), 0, o->comment); o->comment = d; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositorStatics, get_MaxGlobalPlaybackRate)(W(ICompositorStatics) *This, float *r) { *r = 4.0f; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositorStatics, get_MinGlobalPlaybackRate)(W(ICompositorStatics) *This, float *r) { (void)This; *r = 0.0f; return S_OK; }

/* easing functions */
static void easing_init(CObj *o) { Easing *e = (Easing *)o; e->c1x = e->c1y = 0.25f; e->c2x = e->c2y = 0.75f; e->steps = 1; e->initialStepSingle = e->finalStepSingle = 0; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateLinearEasingFunction)(W(ICompositor) *This, W(ILinearEasingFunction) **r) { return create(This, CLS_LinearEasingFunction, &IID_(ILinearEasingFunction), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateCubicBezierEasingFunction)(W(ICompositor) *This, V2 p1, V2 p2, W(ICubicBezierEasingFunction) **r)
{
    HRESULT hr = create(This, CLS_CubicBezierEasingFunction, &IID_(ICubicBezierEasingFunction), (void **)r); Easing *e;
    if (SUCCEEDED(hr)) { e = (Easing *)OBJ_OF(*r); e->c1x = p1.X; e->c1y = p1.Y; e->c2x = p2.X; e->c2y = p2.Y; } return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor2, CreateStepEasingFunction)(W(ICompositor2) *This, W(IStepEasingFunction) **r) { return create(This, CLS_StepEasingFunction, &IID_(IStepEasingFunction), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor2, CreateStepEasingFunction_2)(W(ICompositor2) *This, INT32 n, W(IStepEasingFunction) **r)
{ HRESULT hr = create(This, CLS_StepEasingFunction, &IID_(IStepEasingFunction), (void **)r); if (SUCCEEDED(hr)) ((Easing *)OBJ_OF(*r))->steps = n; return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICubicBezierEasingFunction, get_ControlPoint1)(W(ICubicBezierEasingFunction) *This, V2 *r) { Easing *e = (Easing *)OBJ_OF(This); r->X = e->c1x; r->Y = e->c1y; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICubicBezierEasingFunction, get_ControlPoint2)(W(ICubicBezierEasingFunction) *This, V2 *r) { Easing *e = (Easing *)OBJ_OF(This); r->X = e->c2x; r->Y = e->c2y; return S_OK; }
#define STEP_PROP_I(name, field) \
 HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, get_##name)(W(IStepEasingFunction) *This, INT32 *r) { *r = ((Easing *)OBJ_OF(This))->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, put_##name)(W(IStepEasingFunction) *This, INT32 v) { ((Easing *)OBJ_OF(This))->field = v; return S_OK; }
#define STEP_PROP_B(name, field) \
 HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, get_##name)(W(IStepEasingFunction) *This, boolean *r) { *r = (boolean)((Easing *)OBJ_OF(This))->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, put_##name)(W(IStepEasingFunction) *This, boolean v) { ((Easing *)OBJ_OF(This))->field = v; return S_OK; }
STEP_PROP_I(StepCount, steps)
STEP_PROP_B(IsInitialStepSingleFrame, initialStepSingle)
STEP_PROP_B(IsFinalStepSingleFrame, finalStepSingle)
static INT32 g_zero_step;
HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, get_InitialStep)(W(IStepEasingFunction) *This, INT32 *r) { (void)This; *r = g_zero_step; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, put_InitialStep)(W(IStepEasingFunction) *This, INT32 v) { (void)This; (void)v; return comp_unimpl("IStepEasingFunction.put_InitialStep"); }
HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, get_FinalStep)(W(IStepEasingFunction) *This, INT32 *r) { *r = ((Easing *)OBJ_OF(This))->steps; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IStepEasingFunction, put_FinalStep)(W(IStepEasingFunction) *This, INT32 v) { (void)This; (void)v; return comp_unimpl("IStepEasingFunction.put_FinalStep"); }

/* ============================ Visual math ============================ */
int mat_invert(const Mat *m, Mat *o)
{
    float det = m->a * m->d - m->b * m->c; if (fabsf(det) < 1e-12f) return 0;
    o->a = m->d / det; o->b = -m->b / det; o->c = -m->c / det; o->d = m->a / det;
    o->e = -(m->e * o->a + m->f * o->c); o->f = -(m->e * o->b + m->f * o->d); return 1;
}
void visual_effective(const Visual *v, const Visual *parent, float *ox, float *oy, float *w, float *h)
{
    float pw = 0, ph = 0;
    if (parent) { pw = parent->size.X; ph = parent->size.Y; }
    *ox = v->offset.X + v->relOffset.X * pw; *oy = v->offset.Y + v->relOffset.Y * ph;
    *w = v->size.X + v->relSize.X * pw; *h = v->size.Y + v->relSize.Y * ph;
}
/* local matrix = TransformMatrix * T(-C) * S * R * T(C) * T(Offset - Anchor*Size)   (applied left to right to a point in the visual's own space) */
Mat visual_local_matrix(const Visual *v, float w, float h, float pw, float ph, float *ew, float *eh)
{
    float ox = v->offset.X + v->relOffset.X * pw, oy = v->offset.Y + v->relOffset.Y * ph, sw = v->size.X + v->relSize.X * pw, sh = v->size.Y + v->relSize.Y * ph;
    Mat m, tm; (void)w; (void)h;
    tm.a = v->tm.M11; tm.b = v->tm.M12; tm.c = v->tm.M21; tm.d = v->tm.M22; tm.e = v->tm.M41; tm.f = v->tm.M42;
    m = tm;
    m = mat_mul(m, mat_translate(-v->center.X, -v->center.Y));
    m = mat_mul(m, mat_scale(v->scale.X, v->scale.Y));
    if (v->rotation != 0.0f) m = mat_mul(m, mat_rotate(v->rotation));
    m = mat_mul(m, mat_translate(v->center.X, v->center.Y));
    m = mat_mul(m, mat_translate(ox - v->anchor.X * sw, oy - v->anchor.Y * sh));
    if (ew) *ew = sw; if (eh) *eh = sh;
    return m;
}

/* ============================ Visual ============================ */
/* every live visual, whatever API created it (diagnostics: the tree dump lists parentless subtrees) */
Visual *g_all_vis[8192]; volatile LONG g_all_nvis; static CRITICAL_SECTION g_all_lock; static LONG g_all_init;
static void all_lock(int on) { if (!InterlockedExchange(&g_all_init, 1)) InitializeCriticalSection(&g_all_lock); if (on) EnterCriticalSection(&g_all_lock); else LeaveCriticalSection(&g_all_lock); }
void comp_all_visuals_lock(int on) { all_lock(on); }
static void visual_init(CObj *o)
{
    Visual *v = (Visual *)o;
    v->visible = 1; v->hittest = 1; v->opacity = 1.0f; v->scale.X = v->scale.Y = v->scale.Z = 1.0f; v->orient.W = 1.0f; v->axis.Z = 1.0f;
    v->tm.M11 = v->tm.M22 = v->tm.M33 = v->tm.M44 = 1.0f;
    all_lock(1); if (g_all_nvis < 8192) g_all_vis[g_all_nvis++] = v; all_lock(0);
}
static void visual_destroy(CObj *o) { Visual *v = (Visual *)o; int i;
    all_lock(1); for (i = 0; i < g_all_nvis; i++) if (g_all_vis[i] == v) { g_all_vis[i] = g_all_vis[--g_all_nvis]; break; } all_lock(0);
    if (v->clip) comp_release(v->clip); if (v->parentForTransform) comp_release(&v->parentForTransform->o); if (v->dcx) dc_visual_free(v); }
#define VIS(T) ((Visual *)OBJ_OF(T))
#define VPROP(Iface, name, type, field, ...) \
 HRESULT STDMETHODCALLTYPE CI_(Iface, get_##name)(W(Iface) *This, type *r) { *r = VIS(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(Iface, put_##name)(W(Iface) *This, type v) { VIS(This)->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
VPROP(IVisual, AnchorPoint, V2, anchor)
VPROP(IVisual, BorderMode, Windows_UI_Composition_CompositionBorderMode, borderMode)
VPROP(IVisual, BackfaceVisibility, Windows_UI_Composition_CompositionBackfaceVisibility, backface)
VPROP(IVisual, CenterPoint, V3, center)
VPROP(IVisual, CompositeMode, Windows_UI_Composition_CompositionCompositeMode, composite)
VPROP(IVisual, IsVisible, boolean, visible)
VPROP(IVisual, Offset, V3, offset)
VPROP(IVisual, Opacity, float, opacity)
VPROP(IVisual, Orientation, Quat, orient)
VPROP(IVisual, RotationAxis, V3, axis)
VPROP(IVisual, Scale, V3, scale)
VPROP(IVisual, Size, V2, size)
VPROP(IVisual, TransformMatrix, M44, tm)
VPROP(IVisual2, RelativeOffsetAdjustment, V3, relOffset)
VPROP(IVisual2, RelativeSizeAdjustment, V2, relSize)
VPROP(IVisual3, IsHitTestVisible, boolean, hittest)
HRESULT STDMETHODCALLTYPE CI_(IVisual, get_RotationAngle)(W(IVisual) *This, float *r) { *r = VIS(This)->rotation; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisual, put_RotationAngle)(W(IVisual) *This, float v) { VIS(This)->rotation = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisual, get_RotationAngleInDegrees)(W(IVisual) *This, float *r) { *r = VIS(This)->rotation * (180.0f / 3.14159265358979f); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisual, put_RotationAngleInDegrees)(W(IVisual) *This, float v) { VIS(This)->rotation = v * (3.14159265358979f / 180.0f); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisual, get_Clip)(W(IVisual) *This, W(ICompositionClip) **r) { return comp_return(VIS(This)->clip, &IID_(ICompositionClip), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(IVisual, put_Clip)(W(IVisual) *This, W(ICompositionClip) *v) { comp_assign(&VIS(This)->clip, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisual, get_Parent)(W(IVisual) *This, W(IContainerVisual) **r) { Visual *p = VIS(This)->parent; return comp_return(p ? &p->o : NULL, &IID_(IContainerVisual), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(IVisual2, get_ParentForTransform)(W(IVisual2) *This, W(IVisual) **r) { Visual *p = VIS(This)->parentForTransform; return comp_return(p ? &p->o : NULL, &IID_(IVisual), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(IVisual2, put_ParentForTransform)(W(IVisual2) *This, W(IVisual) *v) { VIS(This)->parentForTransform = v ? (Visual *)OBJ_OF(v) : NULL; INVALIDATE(OBJ_OF(This)); return S_OK; }

/* ============================ ContainerVisual / VisualCollection ============================ */
static void container_destroy(CObj *o)
{
    Container *c = (Container *)o; int i;
    for (i = 0; i < c->nkids; i++) { c->kids[i]->parent = NULL; comp_release(&c->kids[i]->o); }
    if (c->kids) HeapFree(GetProcessHeap(), 0, c->kids);
}
static void coll_destroy(CObj *o)
{
    VisualColl *k = (VisualColl *)o;
    if (k->owner) { if (k->owner->coll == o) k->owner->coll = NULL; comp_release(&k->owner->v.o); }
}
HRESULT STDMETHODCALLTYPE CI_(IContainerVisual, get_Children)(W(IContainerVisual) *This, W(IVisualCollection) **r)
{
    Container *c = (Container *)OBJ_OF(This);
    if (!r) return E_POINTER;
    if (c->coll) return comp_qi(c->coll, &IID_(IVisualCollection), (void **)r);
    { CObj *o = comp_new(CLS_VisualCollection, c->v.o.comp); HRESULT hr; VisualColl *k;
      if (!o) return E_OUTOFMEMORY;
      k = (VisualColl *)o; k->owner = c; comp_addref(&c->v.o); c->coll = o;
      hr = comp_qi(o, &IID_(IVisualCollection), (void **)r); comp_release(o); return hr; }
}
static void container_detach(Container *c, Visual *child)
{
    int i; for (i = 0; i < c->nkids; i++) if (c->kids[i] == child) {
        memmove(&c->kids[i], &c->kids[i + 1], (size_t)(c->nkids - i - 1) * sizeof(Visual *)); c->nkids--; child->parent = NULL; comp_release(&child->o); return; }
}
static HRESULT container_insert(Container *c, W(IVisual) *iv, int index)
{
    Visual *ch, *p;
    if (!iv) return E_INVALIDARG;
    ch = (Visual *)OBJ_OF(iv);
    if (!comp_is_a(ch->o.cls, CLS_Visual)) return E_INVALIDARG;
    for (p = &c->v; p; p = p->parent) if (p == ch) return E_INVALIDARG;     /* would create a cycle */
    comp_addref(&ch->o);
    if (ch->parent) { Container *old = (Container *)ch->parent; int i; for (i = 0; i < old->nkids; i++) if (old->kids[i] == ch && old == c && i < index) index--; container_detach(old, ch); }
    if (c->nkids == c->cap) {
        int nc = c->cap ? c->cap * 2 : 8; Visual **n = (Visual **)HeapAlloc(GetProcessHeap(), 0, (size_t)nc * sizeof(Visual *));
        if (!n) { comp_release(&ch->o); return E_OUTOFMEMORY; }
        if (c->kids) { memcpy(n, c->kids, (size_t)c->nkids * sizeof(Visual *)); HeapFree(GetProcessHeap(), 0, c->kids); }
        c->kids = n; c->cap = nc;
    }
    if (index < 0 || index > c->nkids) index = c->nkids;
    memmove(&c->kids[index + 1], &c->kids[index], (size_t)(c->nkids - index) * sizeof(Visual *));
    c->kids[index] = ch; c->nkids++; ch->parent = &c->v;
    INVALIDATE(&c->v.o); return S_OK;
}
static int container_index(Container *c, W(IVisual) *s) { int i; Visual *v = s ? (Visual *)OBJ_OF(s) : NULL; for (i = 0; i < c->nkids; i++) if (c->kids[i] == v) return i; return -1; }
#define COLL(T) (((VisualColl *)OBJ_OF(T))->owner)
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, get_Count)(W(IVisualCollection) *This, INT32 *r) { *r = COLL(This)->nkids; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, InsertAtTop)(W(IVisualCollection) *This, W(IVisual) *c) { return container_insert(COLL(This), c, -1); }
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, InsertAtBottom)(W(IVisualCollection) *This, W(IVisual) *c) { return container_insert(COLL(This), c, 0); }
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, InsertAbove)(W(IVisualCollection) *This, W(IVisual) *c, W(IVisual) *sib)
{ int i; if (!sib) return container_insert(COLL(This), c, -1); i = container_index(COLL(This), sib); if (i < 0) return E_INVALIDARG; return container_insert(COLL(This), c, i + 1); }
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, InsertBelow)(W(IVisualCollection) *This, W(IVisual) *c, W(IVisual) *sib)
{ int i; if (!sib) return container_insert(COLL(This), c, 0); i = container_index(COLL(This), sib); if (i < 0) return E_INVALIDARG; return container_insert(COLL(This), c, i); }
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, Remove)(W(IVisualCollection) *This, W(IVisual) *c)
{ int i = container_index(COLL(This), c); if (i < 0) return S_OK; container_detach(COLL(This), (Visual *)OBJ_OF(c)); INVALIDATE(&COLL(This)->v.o); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IVisualCollection, RemoveAll)(W(IVisualCollection) *This)
{ Container *c = COLL(This); while (c->nkids) container_detach(c, c->kids[c->nkids - 1]); INVALIDATE(&c->v.o); return S_OK; }

/* IIterable<Visual>.First (XAML 19041 walks visual collections with it and fail-fasts on E_NOTIMPL): an iterator over a snapshot of the children.
   IIterator<Visual> {ECA15496-DCAA-5095-A898-F8602A40E10A} (pinterface signature IID): 6 get_Current, 7 get_HasCurrent, 8 MoveNext, 9 GetMany. */
static const GUID IID_IIteratorVisual_ = {0xECA15496, 0xDCAA, 0x5095, {0xA8, 0x98, 0xF8, 0x60, 0x2A, 0x40, 0xE1, 0x0A}};
typedef struct VIter { const void *const *vt; LONG ref; Visual **v; int n, i; } VIter;
static HRESULT STDMETHODCALLTYPE vit_qi(VIter *t, REFIID r, void **p)
{
    static const GUID unk = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, insp = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}},
        agile = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
    if (IsEqualGUID(r, &unk) || IsEqualGUID(r, &insp) || IsEqualGUID(r, &agile) || IsEqualGUID(r, &IID_IIteratorVisual_)) { InterlockedIncrement(&t->ref); *p = t; return S_OK; }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE vit_ar(VIter *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG STDMETHODCALLTYPE vit_rl(VIter *t)
{
    LONG n = InterlockedDecrement(&t->ref); int i;
    if (!n) { for (i = 0; i < t->n; i++) comp_release(&t->v[i]->o); if (t->v) HeapFree(GetProcessHeap(), 0, t->v); HeapFree(GetProcessHeap(), 0, t); }
    return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE vit_iids(VIter *t, ULONG *n, IID **p) { (void)t; *n = 0; *p = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE vit_name(VIter *t, HSTRING *s) { (void)t; return WindowsCreateString(L"Windows.Foundation.Collections.IIterator`1<Windows.UI.Composition.Visual>", 72, s); }
static HRESULT STDMETHODCALLTYPE vit_trust(VIter *t, TrustLevel *l) { (void)t; *l = BaseTrust; return S_OK; }
static HRESULT STDMETHODCALLTYPE vit_current(VIter *t, void **r) { if (!r) return E_POINTER; *r = NULL; if (t->i >= t->n) return E_BOUNDS; return comp_qi(&t->v[t->i]->o, &IID_(IVisual), r); }
static HRESULT STDMETHODCALLTYPE vit_has(VIter *t, boolean *r) { *r = t->i < t->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE vit_next(VIter *t, boolean *r) { if (t->i < t->n) t->i++; *r = t->i < t->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE vit_many(VIter *t, UINT32 cap, void **items, UINT32 *got)
{
    UINT32 k = 0; while (k < cap && t->i < t->n) { if (FAILED(comp_qi(&t->v[t->i]->o, &IID_(IVisual), &items[k]))) break; k++; t->i++; } *got = k; return S_OK;
}
static const void *const VT_VIter[] = { (void *)vit_qi, (void *)vit_ar, (void *)vit_rl, (void *)vit_iids, (void *)vit_name, (void *)vit_trust, (void *)vit_current, (void *)vit_has, (void *)vit_next, (void *)vit_many };
HRESULT comp_visual_coll_first(CObj *coll, void **result)
{
    Container *c = ((VisualColl *)coll)->owner; VIter *t; int i;
    if (!result) return E_POINTER; *result = NULL;
    t = (VIter *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *t); if (!t) return E_OUTOFMEMORY;
    t->vt = VT_VIter; t->ref = 1;
    if (c && c->nkids) { t->v = (Visual **)HeapAlloc(GetProcessHeap(), 0, (size_t)c->nkids * sizeof(Visual *)); if (!t->v) { HeapFree(GetProcessHeap(), 0, t); return E_OUTOFMEMORY; }
        for (i = 0; i < c->nkids; i++) { t->v[i] = c->kids[i]; comp_addref(&c->kids[i]->o); } t->n = c->nkids; }
    *result = t; return S_OK;
}

/* ============================ SpriteVisual ============================ */
static void sprite_destroy(CObj *o) { Sprite *s = (Sprite *)o; if (s->brush) comp_release(s->brush); if (s->shadow) comp_release(s->shadow); }
HRESULT STDMETHODCALLTYPE CI_(ISpriteVisual, get_Brush)(W(ISpriteVisual) *This, W(ICompositionBrush) **r) { return comp_return(((Sprite *)OBJ_OF(This))->brush, &IID_(ICompositionBrush), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ISpriteVisual, put_Brush)(W(ISpriteVisual) *This, W(ICompositionBrush) *v) { comp_assign(&((Sprite *)OBJ_OF(This))->brush, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ISpriteVisual2, get_Shadow)(W(ISpriteVisual2) *This, W(ICompositionShadow) **r) { return comp_return(((Sprite *)OBJ_OF(This))->shadow, &IID_(ICompositionShadow), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ISpriteVisual2, put_Shadow)(W(ISpriteVisual2) *This, W(ICompositionShadow) *v) { comp_assign(&((Sprite *)OBJ_OF(This))->shadow, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }

/* ============================ clips ============================ */
static void clip_init(CObj *o) { Clip *c = (Clip *)o; c->scale.X = c->scale.Y = 1.0f; c->tm.M11 = c->tm.M22 = 1.0f; }
#define CPROP(name, field) \
 HRESULT STDMETHODCALLTYPE CI_(IInsetClip, get_##name)(W(IInsetClip) *This, float *r) { *r = ((InsetClip *)OBJ_OF(This))->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(IInsetClip, put_##name)(W(IInsetClip) *This, float v) { ((InsetClip *)OBJ_OF(This))->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
CPROP(LeftInset, l) CPROP(TopInset, t) CPROP(RightInset, r) CPROP(BottomInset, b)
#define C2PROP(name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionClip2, get_##name)(W(ICompositionClip2) *This, type *r) { *r = ((Clip *)OBJ_OF(This))->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionClip2, put_##name)(W(ICompositionClip2) *This, type v) { ((Clip *)OBJ_OF(This))->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
C2PROP(AnchorPoint, V2, anchor) C2PROP(CenterPoint, V2, center) C2PROP(Offset, V2, offset) C2PROP(RotationAngle, float, rotation) C2PROP(Scale, V2, scale) C2PROP(TransformMatrix, M32, tm)
HRESULT STDMETHODCALLTYPE CI_(ICompositionClip2, get_RotationAngleInDegrees)(W(ICompositionClip2) *This, float *r) { *r = ((Clip *)OBJ_OF(This))->rotation * (180.0f / 3.14159265358979f); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionClip2, put_RotationAngleInDegrees)(W(ICompositionClip2) *This, float v) { ((Clip *)OBJ_OF(This))->rotation = v * (3.14159265358979f / 180.0f); INVALIDATE(OBJ_OF(This)); return S_OK; }

/* ============================ brushes ============================ */
HRESULT STDMETHODCALLTYPE CI_(ICompositionColorBrush, get_Color)(W(ICompositionColorBrush) *This, Col *r) { *r = ((ColorBrush *)OBJ_OF(This))->color; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionColorBrush, put_Color)(W(ICompositionColorBrush) *This, Col v)
{
    ((ColorBrush *)OBJ_OF(This))->color = v; INVALIDATE(OBJ_OF(This)); return S_OK;
}

#define STOP(T) ((Stop *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionColorGradientStop, get_Color)(W(ICompositionColorGradientStop) *This, Col *r) { *r = STOP(This)->color; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionColorGradientStop, put_Color)(W(ICompositionColorGradientStop) *This, Col v) { STOP(This)->color = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionColorGradientStop, get_Offset)(W(ICompositionColorGradientStop) *This, float *r) { *r = STOP(This)->offset; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionColorGradientStop, put_Offset)(W(ICompositionColorGradientStop) *This, float v) { STOP(This)->offset = v; INVALIDATE(OBJ_OF(This)); return S_OK; }

static void stopcoll_destroy(CObj *o) { StopColl *c = (StopColl *)o; UINT32 i; for (i = 0; i < c->n; i++) comp_release(&c->s[i]->o); if (c->s) HeapFree(GetProcessHeap(), 0, c->s); }
static void grad_init(CObj *o)
{
    GradBrush *g = (GradBrush *)o; g->scale.X = g->scale.Y = 1.0f; g->end.X = g->end.Y = 1.0f; g->tm.M11 = g->tm.M22 = 1.0f;
    g->stopsObj = comp_new(CLS_CompositionColorGradientStopCollection, o->comp); g->stops = (StopColl *)g->stopsObj;
}
static void grad_destroy(CObj *o) { GradBrush *g = (GradBrush *)o; if (g->stopsObj) comp_release(g->stopsObj); }
#define GB(T) ((GradBrush *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush, get_ColorStops)(W(ICompositionGradientBrush) *This, W(ICompositionColorGradientStopCollection) **r) { return comp_return(GB(This)->stopsObj, &IID_(ICompositionColorGradientStopCollection), (void **)r); }
#define GPROP(name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush, get_##name)(W(ICompositionGradientBrush) *This, type *r) { *r = GB(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush, put_##name)(W(ICompositionGradientBrush) *This, type v) { GB(This)->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
GPROP(AnchorPoint, V2, anchor) GPROP(CenterPoint, V2, center) GPROP(Offset, V2, offset) GPROP(RotationAngle, float, rotation) GPROP(Scale, V2, scale) GPROP(TransformMatrix, M32, tm)
GPROP(ExtendMode, Windows_UI_Composition_CompositionGradientExtendMode, extend) GPROP(InterpolationSpace, Windows_UI_Composition_CompositionColorSpace, space)
HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush, get_RotationAngleInDegrees)(W(ICompositionGradientBrush) *This, float *r) { *r = GB(This)->rotation * (180.0f / 3.14159265358979f); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush, put_RotationAngleInDegrees)(W(ICompositionGradientBrush) *This, float v) { GB(This)->rotation = v * (3.14159265358979f / 180.0f); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush2, get_MappingMode)(W(ICompositionGradientBrush2) *This, Windows_UI_Composition_CompositionMappingMode *r) { *r = GB(This)->mapping; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGradientBrush2, put_MappingMode)(W(ICompositionGradientBrush2) *This, Windows_UI_Composition_CompositionMappingMode v) { GB(This)->mapping = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionLinearGradientBrush, get_StartPoint)(W(ICompositionLinearGradientBrush) *This, V2 *r) { *r = GB(This)->start; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionLinearGradientBrush, put_StartPoint)(W(ICompositionLinearGradientBrush) *This, V2 v) { GB(This)->start = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionLinearGradientBrush, get_EndPoint)(W(ICompositionLinearGradientBrush) *This, V2 *r) { *r = GB(This)->end; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionLinearGradientBrush, put_EndPoint)(W(ICompositionLinearGradientBrush) *This, V2 v) { GB(This)->end = v; INVALIDATE(OBJ_OF(This)); return S_OK; }

/* IVector<CompositionColorGradientStop> */
#define VEC Windows_Foundation_Collections_IVector_1__Windows_UI_Composition_CompositionColorGradientStop
#define CIV(m) CI_Windows_Foundation_Collections_IVector_1__Windows_UI_Composition_CompositionColorGradientStop_##m
#define SC(T) ((StopColl *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CIV(GetAt)(VEC *This, UINT32 i, W(ICompositionColorGradientStop) **r) { StopColl *c = SC(This); if (i >= c->n) return E_BOUNDS; return comp_qi(&c->s[i]->o, &IID_(ICompositionColorGradientStop), (void **)r); }
HRESULT STDMETHODCALLTYPE CIV(get_Size)(VEC *This, UINT32 *r) { *r = SC(This)->n; return S_OK; }
static HRESULT sc_insert(StopColl *c, UINT32 i, W(ICompositionColorGradientStop) *v)
{
    Stop *s; if (!v) return E_INVALIDARG; if (i > c->n) return E_BOUNDS; s = (Stop *)OBJ_OF(v);
    if (c->n == c->cap) {
        UINT32 nc = c->cap ? c->cap * 2 : 4; Stop **n = (Stop **)HeapAlloc(GetProcessHeap(), 0, nc * sizeof(Stop *)); if (!n) return E_OUTOFMEMORY;
        if (c->s) { memcpy(n, c->s, c->n * sizeof(Stop *)); HeapFree(GetProcessHeap(), 0, c->s); } c->s = n; c->cap = nc;
    }
    comp_addref(&s->o); memmove(&c->s[i + 1], &c->s[i], (c->n - i) * sizeof(Stop *)); c->s[i] = s; c->n++; INVALIDATE(&c->o); return S_OK;
}
static void sc_remove(StopColl *c, UINT32 i) { comp_release(&c->s[i]->o); memmove(&c->s[i], &c->s[i + 1], (c->n - i - 1) * sizeof(Stop *)); c->n--; INVALIDATE(&c->o); }
HRESULT STDMETHODCALLTYPE CIV(IndexOf)(VEC *This, W(ICompositionColorGradientStop) *v, UINT32 *idx, boolean *r)
{ StopColl *c = SC(This); UINT32 i; *r = 0; *idx = 0; for (i = 0; i < c->n; i++) if (v && &c->s[i]->o == OBJ_OF(v)) { *idx = i; *r = 1; break; } return S_OK; }
HRESULT STDMETHODCALLTYPE CIV(SetAt)(VEC *This, UINT32 i, W(ICompositionColorGradientStop) *v) { StopColl *c = SC(This); if (i >= c->n) return E_BOUNDS; if (!v) return E_INVALIDARG; comp_assign((CObj **)&c->s[i], OBJ_OF(v)); INVALIDATE(&c->o); return S_OK; }
HRESULT STDMETHODCALLTYPE CIV(InsertAt)(VEC *This, UINT32 i, W(ICompositionColorGradientStop) *v) { return sc_insert(SC(This), i, v); }
HRESULT STDMETHODCALLTYPE CIV(RemoveAt)(VEC *This, UINT32 i) { StopColl *c = SC(This); if (i >= c->n) return E_BOUNDS; sc_remove(c, i); return S_OK; }
HRESULT STDMETHODCALLTYPE CIV(Append)(VEC *This, W(ICompositionColorGradientStop) *v) { return sc_insert(SC(This), SC(This)->n, v); }
HRESULT STDMETHODCALLTYPE CIV(RemoveAtEnd)(VEC *This) { StopColl *c = SC(This); if (!c->n) return E_BOUNDS; sc_remove(c, c->n - 1); return S_OK; }
HRESULT STDMETHODCALLTYPE CIV(Clear)(VEC *This) { StopColl *c = SC(This); while (c->n) sc_remove(c, c->n - 1); return S_OK; }
HRESULT STDMETHODCALLTYPE CIV(GetMany)(VEC *This, UINT32 start, UINT32 cnt, W(ICompositionColorGradientStop) **items, UINT32 *r)
{ StopColl *c = SC(This); UINT32 i, n = 0; for (i = start; i < c->n && n < cnt; i++, n++) { items[n] = (W(ICompositionColorGradientStop) *)&c->s[i]->o.views[0]; comp_qi(&c->s[i]->o, &IID_(ICompositionColorGradientStop), (void **)&items[n]); } *r = n; return S_OK; }
HRESULT STDMETHODCALLTYPE CIV(ReplaceAll)(VEC *This, UINT32 n, W(ICompositionColorGradientStop) **items)
{ StopColl *c = SC(This); UINT32 i; while (c->n) sc_remove(c, c->n - 1); for (i = 0; i < n; i++) { HRESULT hr = sc_insert(c, i, items[i]); if (FAILED(hr)) return hr; } return S_OK; }

/* ============================ registration ============================ */
void comp_register_impls_visual(void)
{
    g_impl[CLS_Compositor].size = sizeof(Compositor); g_impl[CLS_Compositor].init = compositor_init; g_impl[CLS_Compositor].destroy = compositor_destroy;
    g_impl[CLS_Visual].size = sizeof(Visual); g_impl[CLS_Visual].init = visual_init; g_impl[CLS_Visual].destroy = visual_destroy;
    g_impl[CLS_ContainerVisual].size = sizeof(Container); g_impl[CLS_ContainerVisual].destroy = container_destroy;
    g_impl[CLS_SpriteVisual].size = sizeof(Sprite); g_impl[CLS_SpriteVisual].destroy = sprite_destroy;
    g_impl[CLS_VisualCollection].size = sizeof(VisualColl); g_impl[CLS_VisualCollection].destroy = coll_destroy;
    g_impl[CLS_CompositionClip].init = clip_init;
    g_impl[CLS_CompositionColorBrush].size = sizeof(ColorBrush);
    g_impl[CLS_CompositionColorGradientStop].size = sizeof(Stop);
    g_impl[CLS_CompositionColorGradientStopCollection].size = sizeof(StopColl); g_impl[CLS_CompositionColorGradientStopCollection].destroy = stopcoll_destroy;
    g_impl[CLS_CompositionLinearGradientBrush].size = sizeof(GradBrush); g_impl[CLS_CompositionGradientBrush].init = grad_init; g_impl[CLS_CompositionGradientBrush].destroy = grad_destroy;
    g_impl[CLS_CompositionRadialGradientBrush].size = sizeof(GradBrush);
    g_impl[CLS_LinearEasingFunction].size = sizeof(Easing); g_impl[CLS_CubicBezierEasingFunction].size = sizeof(Easing); g_impl[CLS_StepEasingFunction].size = sizeof(Easing);
    g_impl[CLS_CompositionEasingFunction].init = easing_init;
}
