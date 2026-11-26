/* dcomp_visual.c - the IDCompositionDevice* / IDCompositionVisual* / transform / clip / animation adapters over the scene graph */
#include "dcomp_priv.h"
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

static DcVis *dcx(Visual *v) { if (!v->dcx) v->dcx = (DcVis *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DcVis)); return v->dcx; }
int dc_visual_free(Visual *v)
{
    DcVis *x = v->dcx; dc_note_visual(v, 0); if (!x) return 0;
    if (x->geoclip) comp_release(x->geoclip); if (x->rectgeo) comp_release(x->rectgeo); if (x->surfbrush) comp_release(x->surfbrush);
    if (x->xform) hv_Release(&((HObj *)x->xform)->views[0]);
    HeapFree(GetProcessHeap(), 0, x); v->dcx = NULL; return 1;
}
Visual *dc_visual_from(void *view)
{
    CObj *o = ((CView *)view)->obj; if (!o || o->cls < 0 || !comp_is_a(o->cls, CLS_Visual)) return NULL; return (Visual *)o;
}
#define VIS(T) ((Visual *)OBJ_OF(T))

/* ============================ transforms (HObj) ============================ */
typedef struct HTransform { HObj h; int kind; float p[8]; HObj *kids[8]; int nkids; Visual *att[8]; int natt; } HTransform;   /* kind: 1 translate 2 scale 3 rotate 4 skew 5 matrix 6 group */
static Mat tf_matrix(HTransform *t)
{
    Mat m = mat_id(); int i;
    switch (t->kind) {
    case 1: m = mat_translate(t->p[0], t->p[1]); break;
    case 2: m = mat_mul(mat_translate(-t->p[2], -t->p[3]), mat_mul(mat_scale(t->p[0], t->p[1]), mat_translate(t->p[2], t->p[3]))); break;
    case 3: m = mat_mul(mat_translate(-t->p[1], -t->p[2]), mat_mul(mat_rotate(t->p[0] * 3.14159265f / 180.0f), mat_translate(t->p[1], t->p[2]))); break;
    case 4: { Mat k = { 1, tanf(t->p[1] * 3.14159265f / 180.0f), tanf(t->p[0] * 3.14159265f / 180.0f), 1, 0, 0 }; m = mat_mul(mat_translate(-t->p[2], -t->p[3]), mat_mul(k, mat_translate(t->p[2], t->p[3]))); break; }
    case 5: m.a = t->p[0]; m.b = t->p[1]; m.c = t->p[2]; m.d = t->p[3]; m.e = t->p[4]; m.f = t->p[5]; break;
    case 6: for (i = 0; i < t->nkids; i++) m = mat_mul(m, tf_matrix((HTransform *)t->kids[i])); break;     /* applied in array order */
    }
    return m;
}
static void apply_matrix(Visual *v, Mat m) { memset(&v->tm, 0, sizeof v->tm); v->tm.M11 = m.a; v->tm.M12 = m.b; v->tm.M21 = m.c; v->tm.M22 = m.d; v->tm.M33 = 1; v->tm.M44 = 1; v->tm.M41 = m.e; v->tm.M42 = m.f; }
static void tf_changed(HTransform *t) { int i; for (i = 0; i < t->natt; i++) { apply_matrix(t->att[i], tf_matrix(t)); INVALIDATE(&t->att[i]->o); } }
static void tf_dtor(HObj *h) { HTransform *t = (HTransform *)h; int i; for (i = 0; i < t->nkids; i++) hv_Release(&t->kids[i]->views[0]); }
#define TF(T) ((HTransform *)((CView *)(T))->obj)
static HRESULT STDMETHODCALLTYPE tf_anim(void *This, void *anim) { (void)This; (void)anim; return dc_stub("Transform", 0, "SetXxx(IDCompositionAnimation) - animations are not evaluated", S_OK); }
#define TSET(name, idx) static HRESULT STDMETHODCALLTYPE name(void *This, float v) { TF(This)->p[idx] = v; tf_changed(TF(This)); return S_OK; }
TSET(tr_X, 0) TSET(tr_Y, 1) TSET(sc_X, 0) TSET(sc_Y, 1) TSET(sc_CX, 2) TSET(sc_CY, 3) TSET(ro_A, 0) TSET(ro_CX, 1) TSET(ro_CY, 2) TSET(sk_AX, 0) TSET(sk_AY, 1) TSET(sk_CX, 2) TSET(sk_CY, 3)
static HRESULT STDMETHODCALLTYPE mx_Set(void *This, const D2D_MATRIX_3X2_F *m) { HTransform *t = TF(This); if (!m) return E_POINTER; t->p[0] = m->_11; t->p[1] = m->_12; t->p[2] = m->_21; t->p[3] = m->_22; t->p[4] = m->_31; t->p[5] = m->_32; tf_changed(t); return S_OK; }
static HRESULT STDMETHODCALLTYPE mx_Elem(void *This, int row, int col, float v) { HTransform *t = TF(This); if (row < 0 || row > 2 || col < 0 || col > 1) return E_INVALIDARG; t->p[row * 2 + col] = v; tf_changed(t); return S_OK; }
static HRESULT STDMETHODCALLTYPE mx_ElemAnim(void *This, int row, int col, void *a) { (void)row; (void)col; return tf_anim(This, a); }
#define T3(...) { hv_QI, hv_AddRef, hv_Release, __VA_ARGS__ }
static const void *VTT_Translate[] = T3(tf_anim, tr_X, tf_anim, tr_Y);
static const void *VTT_Scale[] = T3(tf_anim, sc_X, tf_anim, sc_Y, tf_anim, sc_CX, tf_anim, sc_CY);
static const void *VTT_Rotate[] = T3(tf_anim, ro_A, tf_anim, ro_CX, tf_anim, ro_CY);
static const void *VTT_Skew[] = T3(tf_anim, sk_AX, tf_anim, sk_AY, tf_anim, sk_CX, tf_anim, sk_CY);
static const void *VTT_Matrix[] = T3(mx_Set, mx_ElemAnim, mx_Elem);
static const void *VTT_Group[] = { hv_QI, hv_AddRef, hv_Release };
static HRESULT make_tf(int kind, const void *vt, const GUID *iid, void **out)
{
    HIface *ifs; HTransform *t; static HIface tmpl[2];
    ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, 2 * sizeof(HIface)); if (!ifs) return E_OUTOFMEMORY; (void)tmpl;
    ifs[0].iid = iid; ifs[0].vt = vt; ifs[1].iid = &DCIID_Transform; ifs[1].vt = vt;                                     /* the base IDCompositionTransform / 3D interfaces are empty */
    t = (HTransform *)hobj_new(sizeof(HTransform), "DCompositionTransform", ifs, 2, tf_dtor); if (!t) return E_OUTOFMEMORY; t->kind = kind;
    if (kind == 2 || kind == 4) { if (kind == 2) { t->p[0] = t->p[1] = 1; } } if (kind == 5) { t->p[0] = t->p[3] = 1; }
    return hobj_return(&t->h, iid, out);
}
HRESULT STDMETHODCALLTYPE dcD_CreateTranslateTransform(void *T, void **o) { (void)T; return make_tf(1, VTT_Translate, &DCIID_TranslateTransform, o); }
HRESULT STDMETHODCALLTYPE dcD_CreateScaleTransform(void *T, void **o) { (void)T; return make_tf(2, VTT_Scale, &DCIID_ScaleTransform, o); }
HRESULT STDMETHODCALLTYPE dcD_CreateRotateTransform(void *T, void **o) { (void)T; return make_tf(3, VTT_Rotate, &DCIID_RotateTransform, o); }
HRESULT STDMETHODCALLTYPE dcD_CreateSkewTransform(void *T, void **o) { (void)T; return make_tf(4, VTT_Skew, &DCIID_SkewTransform, o); }
HRESULT STDMETHODCALLTYPE dcD_CreateMatrixTransform(void *T, void **o) { (void)T; return make_tf(5, VTT_Matrix, &DCIID_MatrixTransform, o); }
HRESULT STDMETHODCALLTYPE dcD_CreateTransformGroup(void *T, void **kids, UINT n, void **o)
{
    void *r = NULL; HTransform *g; UINT i; HRESULT hr = make_tf(6, VTT_Group, &DCIID_Transform, &r); (void)T; if (FAILED(hr)) return hr;
    g = (HTransform *)((CView *)r)->obj;
    for (i = 0; i < n && i < 8; i++) { CObj *k = kids[i] ? ((CView *)kids[i])->obj : NULL; if (k && k->cls < 0) { hv_AddRef(&((HObj *)k)->views[0]); g->kids[g->nkids++] = (HObj *)k; } }
    *o = r; return S_OK;
}

/* ============================ rectangle clip (HObj over a rounded-rect geometry clip) ============================ */
typedef struct HRClip { HObj h; CObj *geo, *clip; float l, t, r, b, rx, ry; } HRClip;
static void rc_dtor(HObj *h) { HRClip *c = (HRClip *)h; if (c->clip) comp_release(c->clip); if (c->geo) comp_release(c->geo); }
static void rc_update(HRClip *c) { RRectGeo *g = (RRectGeo *)c->geo; g->offset.X = c->l; g->offset.Y = c->t; g->size.X = c->r - c->l; g->size.Y = c->b - c->t; g->radius.X = c->rx; g->radius.Y = c->ry; if (c->clip) INVALIDATE(c->clip); }
#define RC(T) ((HRClip *)((CView *)(T))->obj)
static HRESULT STDMETHODCALLTYPE rc_anim(void *This, void *a) { (void)This; (void)a; return dc_stub("RectangleClip", 0, "animated clip value ignored", S_OK); }
#define RSET(name, f) static HRESULT STDMETHODCALLTYPE name(void *This, float v) { RC(This)->f = v; rc_update(RC(This)); return S_OK; }
RSET(rc_L, l) RSET(rc_T, t) RSET(rc_R, r) RSET(rc_B, b)
static HRESULT STDMETHODCALLTYPE rc_RX(void *This, float v) { RC(This)->rx = v; rc_update(RC(This)); return S_OK; }
static HRESULT STDMETHODCALLTYPE rc_RY(void *This, float v) { RC(This)->ry = v; rc_update(RC(This)); return S_OK; }
static HRESULT STDMETHODCALLTYPE rc_other(void *This, float v) { (void)This; (void)v; return dc_stub("RectangleClip", 1, "per-corner radius (only the top-left radius is used for all corners)", S_OK); }
static const void *VT_RectClip[] = { hv_QI, hv_AddRef, hv_Release,
    rc_anim, rc_L, rc_anim, rc_T, rc_anim, rc_R, rc_anim, rc_B,
    rc_anim, rc_RX, rc_anim, rc_RY, rc_anim, rc_other, rc_anim, rc_other, rc_anim, rc_other, rc_anim, rc_other, rc_anim, rc_other, rc_anim, rc_other };
HRESULT STDMETHODCALLTYPE dcD_CreateRectangleClip(void *This, void **out)
{
    CObj *comp = OBJ_OF(This); HIface *ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, 3 * sizeof(HIface)); HRClip *c; if (!ifs) return E_OUTOFMEMORY;
    ifs[0].iid = &DCIID_RectangleClip; ifs[0].vt = VT_RectClip; ifs[1].iid = &DCIID_Clip; ifs[1].vt = VT_RectClip;
    c = (HRClip *)hobj_new(sizeof(HRClip), "DCompositionRectangleClip", ifs, 2, rc_dtor); if (!c) return E_OUTOFMEMORY;
    c->geo = comp_new(CLS_CompositionRoundedRectangleGeometry, comp); c->clip = comp_new(CLS_CompositionGeometricClip, comp);
    if (!c->geo || !c->clip) { hv_Release(&c->h.views[0]); return E_OUTOFMEMORY; }
    ((GeoClip *)c->clip)->geo = c->geo; comp_addref(c->geo); c->r = c->b = 0; rc_update(c);
    return hobj_return(&c->h, &DCIID_RectangleClip, out);
}

/* ============================ animation object (final value only) ============================
   Not evaluated over time: the value it ends at is applied when a visual binds it (SetOpacity/SetOffsetX/SetOffsetY with an animation).
   That is the common "set through an animation" idiom - WinUI 3's Windowing (title bar) sets opacities this way; ignoring them left
   the title-bar background and title text opaque inside the caption-buttons window (black box with a ghost title). The final value:
   End's value if given, else the last AddCubic segment's constant. */
typedef struct HAnim { HObj h; float val; int has; } HAnim;
static HAnim *an_of(void *T) { HObj *o = ((HView *)T)->obj; return (o && o->name && !strcmp(o->name, "DCompositionAnimation")) ? (HAnim *)o : NULL; }
static const void *VT_Anim[9], *VT_AnimExt[16];
/* only OUR animation views: dwmcorei (WinUI 3's lifted compositor) calls these slots with other arguments (crashed on a small pointer) */
int dc_anim_value(void *animView, float *v) { HAnim *a; const void *vt;
  if ((ULONG_PTR)animView < 0x10000 || ((ULONG_PTR)animView & 7)) return 0;
  vt = *(const void *const *)animView; if (vt != (const void *)VT_Anim && vt != (const void *)VT_AnimExt) return 0;
  a = an_of(animView); if (!a || !a->has) return 0; *v = a->val; return 1; }
static HRESULT STDMETHODCALLTYPE an_Reset(void *T) { HAnim *a = an_of(T); if (a) a->has = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE an_Abs(void *T) { (void)T; return dc_stub("Animation", 4, "SetAbsoluteBeginTime", S_OK); }
static HRESULT STDMETHODCALLTYPE an_Cubic(void *T, double begin, float c0, float c1, float c2, float c3) { HAnim *a = an_of(T); (void)begin; (void)c1; (void)c2; (void)c3; if (a) { a->val = c0; a->has = 1; } return S_OK; }
static HRESULT STDMETHODCALLTYPE an_Sin(void *T) { (void)T; return dc_stub("Animation", 6, "AddSinusoidal", S_OK); }
static HRESULT STDMETHODCALLTYPE an_Rep(void *T) { (void)T; return dc_stub("Animation", 7, "AddRepeat", S_OK); }
static HRESULT STDMETHODCALLTYPE an_End(void *T, double end, float val) { HAnim *a = an_of(T); (void)end; if (a) { a->val = val; a->has = 1; } return S_OK; }
static const void *VT_Anim[9] = { hv_QI, hv_AddRef, hv_Release, an_Reset, an_Abs, an_Cubic, an_Sin, an_Rep, an_End };
/* {0BCA27EF-7885-4A7A-95B0-CC4079B194AD}: an unnamed extension of IDCompositionAnimation that 19041 XAML's storyboard conversion needs
   (DCompAnimationConversionContextDC::AddFreezeSegment QIs for it, then calls slot 13 with a float; E_NOINTERFACE there = fail-fast).
   Same slots 0-8, then inert S_OK slots 9-15 (logged once each with their number). */
static const GUID DCIID_AnimationExt = {0x0BCA27EF,0x7885,0x4A7A,{0x95,0xB0,0xCC,0x40,0x79,0xB1,0x94,0xAD}};
#define AN_X(n) static HRESULT STDMETHODCALLTYPE an_x##n(void *T) { (void)T; return dc_stub("AnimationExt", n, "slot " #n " (AddFreeze at 13; inert)", S_OK); }
AN_X(9) AN_X(10) AN_X(11) AN_X(12) AN_X(13) AN_X(14) AN_X(15)
static const void *VT_AnimExt[16] = { hv_QI, hv_AddRef, hv_Release, an_Reset, an_Abs, an_Cubic, an_Sin, an_Rep, an_End, an_x9, an_x10, an_x11, an_x12, an_x13, an_x14, an_x15 };
HRESULT STDMETHODCALLTYPE dcD_CreateAnimation(void *This, void **out)
{
    HIface *ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, 2 * sizeof(HIface)); HObj *o; (void)This; if (!ifs) return E_OUTOFMEMORY;
    ifs[0].iid = &DCIID_Animation; ifs[0].vt = VT_Anim; ifs[1].iid = &DCIID_AnimationExt; ifs[1].vt = VT_AnimExt;
    o = hobj_new(sizeof(HAnim), "DCompositionAnimation", ifs, 2, NULL); return hobj_return(o, &DCIID_Animation, out);
}

/* ============================ surface factory (HObj) ============================ */
typedef struct HFact { HObj h; CObj *comp; } HFact;
static void fact_dtor(HObj *h) { HFact *f = (HFact *)h; if (f->comp) comp_release(f->comp); }
HRESULT STDMETHODCALLTYPE dcF_CreateSurface(void *This, UINT w, UINT h, DXGI_FORMAT fmt, DXGI_ALPHA_MODE am, void **out)
{
    HFact *f = (HFact *)((CView *)This)->obj; CObj *s; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    s = dc_create_surface(f->comp, (int)w, (int)h, fmt, am, 0); if (!s) return E_FAIL; hr = comp_qi(s, &DCIID_Surface, out); comp_release(s); return hr;
}
HRESULT STDMETHODCALLTYPE dcF_CreateVirtualSurface(void *This, UINT w, UINT h, DXGI_FORMAT fmt, DXGI_ALPHA_MODE am, void **out)
{
    HFact *f = (HFact *)((CView *)This)->obj; CObj *s; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    s = dc_create_surface(f->comp, (int)w, (int)h, fmt, am, 1); if (!s) return E_FAIL; hr = comp_qi(s, &DCIID_VirtualSurface, out); comp_release(s); return hr;
}
static const void *VT_Fact[] = { hv_QI, hv_AddRef, hv_Release, dcF_CreateSurface, dcF_CreateVirtualSurface };
/* {380A04F1-...} private surface factory: XAML 19041 DCompSurface::InitializeSurface calls slot 7 (w, h, fmt, flags, bool, out) for every
   texture (text glyph runs first) and wraps the result with ICompositorPrivate5::CreateCompositionSurfaceForDCompositionSurface */
static HRESULT STDMETHODCALLTYPE dcF380_stub3(void *This) { (void)This; return dc_stub("Fact380", 3, "Slot3", E_NOTIMPL); }
static HRESULT STDMETHODCALLTYPE dcF380_stub4(void *This) { (void)This; return dc_stub("Fact380", 4, "Slot4", E_NOTIMPL); }
static HRESULT STDMETHODCALLTYPE dcF380_stub5(void *This) { (void)This; return dc_stub("Fact380", 5, "Slot5", E_NOTIMPL); }
static HRESULT STDMETHODCALLTYPE dcF380_stub6(void *This) { (void)This; return dc_stub("Fact380", 6, "Slot6", E_NOTIMPL); }
static HRESULT STDMETHODCALLTYPE dcF380_CreateSurface(void *This, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags, BOOLEAN b, void **out)
{
    static LONG n; if (InterlockedIncrement(&n) <= 4) comp_log("Fact380.CreateSurface(%u x %u, fmt %d, flags 0x%x, %d)\n", w, h, (int)fmt, flags, (int)b);
    return dcF_CreateSurface(This, w, h, fmt, DXGI_ALPHA_MODE_PREMULTIPLIED, out);
}
static const void *VT_Fact380[] = { hv_QI, hv_AddRef, hv_Release, dcF380_stub3, dcF380_stub4, dcF380_stub5, dcF380_stub6, dcF380_CreateSurface };

/* ============================ the device (methods live on the Compositor object) ============================ */
/* Batch ids (interop device slots 45/46, Win10 CDevice::GetCurrentBatchID / GetLastConfirmedBatchId(UINT *)): WinUI 3's lifted compositor
   (dwmcorei) paces its frames on them. The stubs left *out unwritten and its render thread stopped after its first commit. Here every
   commit is composed in this process at once: the open batch is g_dc_batch, Commit closes it, and everything committed is confirmed. */
static volatile LONG g_dc_batch = 1;
HRESULT STDMETHODCALLTYPE dcD_Commit(void *This) { dc_commit(OBJ_OF(This)); InterlockedIncrement(&g_dc_batch); return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_CommitMainDevice(void *This) { dc_commit(OBJ_OF(This)); InterlockedIncrement(&g_dc_batch); return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_GetCurrentBatchID(void *This, UINT *id) { (void)This; if (!id) return E_INVALIDARG; *id = (UINT)g_dc_batch; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_GetLastConfirmedBatchId(void *This, UINT *id) { (void)This; if (!id) return E_INVALIDARG; *id = (UINT)g_dc_batch - 1; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_WaitForCommitCompletion(void *This) { (void)This; return S_OK; }
typedef struct DCFrameStats { LARGE_INTEGER lastFrameTime; DXGI_RATIONAL currentCompositionRate; LARGE_INTEGER currentTime, timeFrequency, nextEstimatedFrameTime; } DCFrameStats;
HRESULT STDMETHODCALLTYPE dcD_GetFrameStatistics(void *This, DCFrameStats *st)
{
    LARGE_INTEGER n, f; (void)This; if (!st) return E_POINTER; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f);
    st->lastFrameTime = n; st->currentCompositionRate.Numerator = 60; st->currentCompositionRate.Denominator = 1; st->currentTime = n; st->timeFrequency = f; st->nextEstimatedFrameTime.QuadPart = n.QuadPart + f.QuadPart / 60; return S_OK;
}
HRESULT STDMETHODCALLTYPE dcD_CheckDeviceState(void *This, BOOL *valid) { (void)This; if (valid) *valid = TRUE; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_EnableWhitePixelOptimization(void *This, BOOL on) { (void)This; (void)on; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_DisableD2DStatePreservation(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_Flush(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_OfferSurfaceResources(void *This, void *a, UINT n) { (void)This; (void)a; (void)n; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_ReclaimSurfaceResources(void *This, BOOL *discarded) { (void)This; if (discarded) *discarded = FALSE; return S_OK; }
HRESULT STDMETHODCALLTYPE dcD_CreateVisual(void *This, void **out)
{
    CObj *comp = OBJ_OF(This), *o; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    o = comp_new(CLS_SpriteVisual, comp); if (!o) return E_OUTOFMEMORY; dcx((Visual *)o); dc_note_visual((Visual *)o, 1);
    hr = comp_qi(o, &DCIID_Visual, out); comp_release(o); return hr;
}
HRESULT STDMETHODCALLTYPE dcD_CreateSurfaceFactory(void *This, IUnknown *renderingDevice, void **out)
{
    HFact *f; HIface *ifs; if (!out) return E_POINTER; *out = NULL;
    if (renderingDevice) { HRESULT hr = dc_set_rendering_device(renderingDevice); if (FAILED(hr)) comp_log("CreateSurfaceFactory: rendering device not usable (0x%08lX), default device used\n", (unsigned long)hr); }
    /* XAML 19041 (DCompTreeHost::EnsureResources) also QIs the factory for three private interfaces and uses them unchecked:
       {6C530579} slot 3 = CreateSurface(w, h, fmt, alpha, out) like IDCompositionSurfaceFactory; {380A04F1}, {9C87DE30}: logging probes until a run shows them used */
    static const GUID iid_6c53 = {0x6C530579, 0xF7B7, 0x4884, {0xAA, 0xAE, 0xB1, 0xD4, 0x16, 0x68, 0x52, 0x5D}};
    static const GUID iid_380a = {0x380A04F1, 0xBB59, 0x4C8D, {0xAE, 0x04, 0xAF, 0xDB, 0x72, 0x7A, 0x42, 0xE7}};
    static const GUID iid_9c87 = {0x9C87DE30, 0x0EC3, 0x4DB5, {0x89, 0x74, 0x83, 0x6D, 0x33, 0x2E, 0x53, 0x79}};
    ifs = (HIface *)HeapAlloc(GetProcessHeap(), 0, 4 * sizeof(HIface)); if (!ifs) return E_OUTOFMEMORY; ifs[0].iid = &DCIID_SurfaceFactory; ifs[0].vt = VT_Fact;
    ifs[1].iid = &iid_6c53; ifs[1].vt = VT_Fact; ifs[2].iid = &iid_380a; ifs[2].vt = VT_Fact380; ifs[3].iid = &iid_9c87; ifs[3].vt = DCVT_Priv;
    f = (HFact *)hobj_new(sizeof(HFact), "DCompositionSurfaceFactory", ifs, 4, fact_dtor); if (!f) return E_OUTOFMEMORY; f->comp = OBJ_OF(This); comp_addref(f->comp);
    return hobj_return(&f->h, &DCIID_SurfaceFactory, out);
}
HRESULT STDMETHODCALLTYPE dcD_CreateSurface(void *This, UINT w, UINT h, DXGI_FORMAT fmt, DXGI_ALPHA_MODE am, void **out)
{
    CObj *s; HRESULT hr; if (!out) return E_POINTER; *out = NULL; s = dc_create_surface(OBJ_OF(This), (int)w, (int)h, fmt, am, 0); if (!s) return E_FAIL; hr = comp_qi(s, &DCIID_Surface, out); comp_release(s); return hr;
}
HRESULT STDMETHODCALLTYPE dcD_CreateVirtualSurface(void *This, UINT w, UINT h, DXGI_FORMAT fmt, DXGI_ALPHA_MODE am, void **out)
{
    CObj *s; HRESULT hr; if (!out) return E_POINTER; *out = NULL; s = dc_create_surface(OBJ_OF(This), (int)w, (int)h, fmt, am, 1); if (!s) return E_FAIL; hr = comp_qi(s, &DCIID_VirtualSurface, out); comp_release(s); return hr;
}
HRESULT STDMETHODCALLTYPE dcD_CreateTargetForHwnd(void *This, HWND hwnd, BOOL topmost, void **out)
{
    CObj *t; HRESULT hr; if (!out) return E_POINTER; *out = NULL; t = dc_create_target(OBJ_OF(This), hwnd, topmost); if (!t) return E_FAIL; hr = comp_qi(t, &DCIID_Target, out); comp_release(t); return hr;
}
/* partner / internal / debug interfaces */
HRESULT STDMETHODCALLTYPE dcP_MarkDirty(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcP_ClearCallback(void *This) { dc_commit(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcP_RealClose(void *This) { (void)This; return S_OK; }
/* IDCompositionDeviceInternal {6b556968-...}, 19041 layout (gen_dcomp_tables.py). XAML hints sizes and sets a telemetry id;
   DirectManipulation's compositor hands it a commit-completion event (commits complete synchronously here, so it is signalled at once).
   Slots 10/11 look like GetCurrentBatchId / GetFrameIdFromBatchId; answering them sent XAML's storyboards into batch tracking that
   crashed (CTimeline dirty propagation on a dead object), so they fail as they did when the table was shorter. */
HRESULT STDMETHODCALLTYPE dcI_HintSize(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcI_SetCommitCompletionEvent(void *This, HANDLE ev) { dcomp_set_commit_event(((CView *)This)->obj, ev); return S_OK; }
HRESULT STDMETHODCALLTYPE dcI_TelemetrySetApplicationId(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcI_GetCurrentBatchId(void *This, ULONG *id) { (void)This; (void)id; return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE dcI_GetFrameIdFromBatchId(void *This, ULONG id, UINT64 *frame) { (void)This; (void)id; (void)frame; return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE dcG_EnableDebugCounters(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcG_DisableDebugCounters(void *This) { (void)This; return S_OK; }

/* ============================ visuals ============================ */
static BOOL finite_ok(float f) { return f == f && fabsf(f) < 1e7f; }
HRESULT STDMETHODCALLTYPE dcV_SetOffsetX(void *This, float v) { if (!finite_ok(v)) return dc_stub("Visual", 4, "SetOffsetX non-finite value (vtable mismatch?) ignored", E_INVALIDARG); VIS(This)->offset.X = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetOffsetY(void *This, float v) { if (!finite_ok(v)) return dc_stub("Visual", 6, "SetOffsetY non-finite value (vtable mismatch?) ignored", E_INVALIDARG); VIS(This)->offset.Y = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetOpacityAnim(void *This, void *anim) { float v; if (dc_anim_value(anim, &v)) { VIS(This)->opacity = v; INVALIDATE(OBJ_OF(This)); } return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetOffsetXAnim(void *This, void *anim) { float v; if (dc_anim_value(anim, &v) && finite_ok(v)) { VIS(This)->offset.X = v; INVALIDATE(OBJ_OF(This)); } return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetOffsetYAnim(void *This, void *anim) { float v; if (dc_anim_value(anim, &v) && finite_ok(v)) { VIS(This)->offset.Y = v; INVALIDATE(OBJ_OF(This)); } return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetOpacity(void *This, float v) { VIS(This)->opacity = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetVisible(void *This, BOOL v) { VIS(This)->visible = v ? 1 : 0; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetWidth(void *This, float v) { if (!finite_ok(v)) return E_INVALIDARG; VIS(This)->size.X = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetHeight(void *This, float v) { if (!finite_ok(v)) return E_INVALIDARG; VIS(This)->size.Y = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE dcV_SetBorderMode(void *This, int mode) { VIS(This)->borderMode = mode == 1 ? Windows_UI_Composition_CompositionBorderMode_Hard : Windows_UI_Composition_CompositionBorderMode_Inherit; INVALIDATE(OBJ_OF(This)); return S_OK; }   /* DCOMPOSITION_BORDER_MODE_HARD = 1 */
HRESULT STDMETHODCALLTYPE dcV_RealClose(void *This) { (void)This; return S_OK; }

HRESULT STDMETHODCALLTYPE dcV_SetTransformMatrix(void *This, const D2D_MATRIX_3X2_F *m)
{
    Visual *v = VIS(This); Mat mm; if (!m) return E_POINTER;
    if (!finite_ok(m->_11) || !finite_ok(m->_22) || !finite_ok(m->_31) || !finite_ok(m->_32)) return dc_stub("Visual", 8, "SetTransform(matrix) with non-finite elements ignored", E_INVALIDARG);
    mm.a = m->_11; mm.b = m->_12; mm.c = m->_21; mm.d = m->_22; mm.e = m->_31; mm.f = m->_32; apply_matrix(v, mm);
    if (v->dcx && v->dcx->xform) { hv_Release(&((HObj *)v->dcx->xform)->views[0]); v->dcx->xform = NULL; }
    INVALIDATE(&v->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE dcV_SetTransformObject(void *This, void *tf)
{
    Visual *v = VIS(This); DcVis *x = dcx(v); HTransform *t = tf ? (HTransform *)((CView *)tf)->obj : NULL;
    if (t && t->h.cls != -1) return E_INVALIDARG;
    if (x->xform) { HTransform *o = (HTransform *)x->xform; int i; for (i = 0; i < o->natt; i++) if (o->att[i] == v) { o->att[i] = o->att[--o->natt]; break; } hv_Release(&o->h.views[0]); x->xform = NULL; }
    if (t) { hv_AddRef(&t->h.views[0]); x->xform = t; if (t->natt < 8) t->att[t->natt++] = v; apply_matrix(v, tf_matrix(t)); }
    else { Mat id = mat_id(); apply_matrix(v, id); }
    INVALIDATE(&v->o); return S_OK;
}
/* transform parent: the renderer positions the visual relative to it (dcomp_render.c draw_visual / world_of); held with a reference */
static HRESULT set_transform_parent(void *This, void *parent)
{
    Visual *v = VIS(This); Visual *p = parent ? dc_visual_from(parent) : NULL;
    if (p == v) p = NULL;
    comp_assign((CObj **)&v->parentForTransform, p ? &p->o : NULL); INVALIDATE(&v->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE dcV_SetTransformParent(void *This, void *parent) { return set_transform_parent(This, parent); }
HRESULT STDMETHODCALLTYPE dcV_SetTransformParent2(void *This, void *parent) { return set_transform_parent(This, parent); }
HRESULT STDMETHODCALLTYPE dcV_SetClipRect(void *This, const D2D_RECT_F *r)
{
    Visual *v = VIS(This); DcVis *x = dcx(v); RectGeo *g;
    if (!r) { if (x->geoclip && v->clip == x->geoclip) comp_assign(&v->clip, NULL); INVALIDATE(&v->o); return S_OK; }
    if (!finite_ok(r->left) || !finite_ok(r->top) || !finite_ok(r->right) || !finite_ok(r->bottom)) return dc_stub("Visual", 14, "SetClip(rect) with non-finite values ignored", E_INVALIDARG);
    if (!x->rectgeo) { x->rectgeo = comp_new(CLS_CompositionRectangleGeometry, v->o.comp); x->geoclip = comp_new(CLS_CompositionGeometricClip, v->o.comp); if (!x->rectgeo || !x->geoclip) return E_OUTOFMEMORY; ((GeoClip *)x->geoclip)->geo = x->rectgeo; comp_addref(x->rectgeo); }
    g = (RectGeo *)x->rectgeo; g->offset.X = r->left; g->offset.Y = r->top; g->size.X = r->right - r->left; g->size.Y = r->bottom - r->top;
    if (v->clip != x->geoclip) comp_assign(&v->clip, x->geoclip);
    INVALIDATE(&v->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE dcV_SetClipObject(void *This, void *clip)
{
    Visual *v = VIS(This); HRClip *c = clip ? (HRClip *)((CView *)clip)->obj : NULL; if (c && c->h.cls != -1) return E_INVALIDARG;
    comp_assign(&v->clip, c ? c->clip : NULL); INVALIDATE(&v->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE dcV_SetContent(void *This, IUnknown *content)
{
    Visual *v = VIS(This); DcVis *x = dcx(v); Sprite *sp; CObj *surf;
    if (!comp_is_a(v->o.cls, CLS_SpriteVisual)) return E_INVALIDARG; sp = (Sprite *)v;
    if (!content) { comp_assign(&sp->brush, NULL); INVALIDATE(&v->o); return S_OK; }
    surf = dc_surface_from_unknown(content);
    if (!surf) { void *p = NULL; if (SUCCEEDED(IUnknown_QueryInterface(content, &IID_IDXGISwapChain1, &p)) && p) { ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p); return dc_stub("Visual", 15, "SetContent(swap chain) is not drawn (SwapChainPanel unsupported)", S_OK); }
        { char g[40]; (void)g; return dc_stub("Visual", 15, "SetContent: object is not a drawing surface", E_INVALIDARG); } }
    if (!x->surfbrush) { x->surfbrush = comp_new(CLS_CompositionSurfaceBrush, v->o.comp); if (!x->surfbrush) return E_OUTOFMEMORY; ((SBrush *)x->surfbrush)->stretch = 0; ((SBrush *)x->surfbrush)->hr = ((SBrush *)x->surfbrush)->vr = 0.0f; }
    comp_assign(&((SBrush *)x->surfbrush)->surface, surf);
    if (sp->brush != x->surfbrush) comp_assign(&sp->brush, x->surfbrush);
    INVALIDATE(&v->o); return S_OK;
}
static W(IVisual) *ivis(Visual *v) { void *p = NULL; if (!v) return NULL; comp_qi(&v->o, &IID_(IVisual), &p); return (W(IVisual) *)p; }
static W(IVisualCollection) *icoll(Visual *v) { void *c = NULL, *r = NULL; if (comp_qi(&v->o, &IID_(IContainerVisual), &c) != S_OK) return NULL; CI_IContainerVisual_get_Children((W(IContainerVisual) *)c, (W(IVisualCollection) **)&r); ((IUnknown *)c)->lpVtbl->Release((IUnknown *)c); return (W(IVisualCollection) *)r; }
HRESULT STDMETHODCALLTYPE dcV_AddVisual(void *This, void *child, BOOL above, void *ref)
{
    Visual *p = VIS(This), *c = child ? dc_visual_from(child) : NULL, *r = ref ? dc_visual_from(ref) : NULL; W(IVisualCollection) *col; W(IVisual) *ic, *ir; HRESULT hr;
    if (!c) return E_INVALIDARG; if (ref && !r) return E_INVALIDARG; col = icoll(p); if (!col) return E_FAIL; ic = ivis(c); ir = ivis(r);
    hr = above ? CI_IVisualCollection_InsertAbove(col, ic, ir) : CI_IVisualCollection_InsertBelow(col, ic, ir);
    if (ic) ((IUnknown *)ic)->lpVtbl->Release((IUnknown *)ic); if (ir) ((IUnknown *)ir)->lpVtbl->Release((IUnknown *)ir); ((IUnknown *)col)->lpVtbl->Release((IUnknown *)col); return hr;
}
HRESULT STDMETHODCALLTYPE dcV_RemoveVisual(void *This, void *child)
{
    Visual *c = child ? dc_visual_from(child) : NULL; W(IVisualCollection) *col = icoll(VIS(This)); W(IVisual) *ic = ivis(c); HRESULT hr; if (!col) return E_FAIL; if (!ic) { ((IUnknown *)col)->lpVtbl->Release((IUnknown *)col); return E_INVALIDARG; }
    hr = CI_IVisualCollection_Remove(col, ic); ((IUnknown *)ic)->lpVtbl->Release((IUnknown *)ic); ((IUnknown *)col)->lpVtbl->Release((IUnknown *)col); return hr;
}
HRESULT STDMETHODCALLTYPE dcV_RemoveAllVisuals(void *This)
{
    W(IVisualCollection) *col = icoll(VIS(This)); HRESULT hr; if (!col) return E_FAIL; hr = CI_IVisualCollection_RemoveAll(col); ((IUnknown *)col)->lpVtbl->Release((IUnknown *)col); return hr;
}
HRESULT STDMETHODCALLTYPE dcV_GetVisualCollection(void *This, void **out) { W(IVisualCollection) *col; if (!out) return E_POINTER; col = icoll(VIS(This)); *out = col; return col ? S_OK : E_FAIL; }

/* IDCompositionVisualRestricted {F2011FDF-...}: slots 3..43 as the interop visual, then (19041 numbering, one below
   wine-uwp's 26100 reading) 44/45 get_VisualRelativePixelsToScreen...(float*), 46 EnablePixelSnapping(BOOL), 47 ProtectContents(BOOL),
   48 RedirectMouseToHwnd, 49/50 SetTransformParent2. XAML's HWCompTreeNodeWinRT::UpdatePrependPixelSnapping calls 46. */
static HRESULT STDMETHODCALLTYPE dcR_get_float(void *This, float *f) { (void)This; if (f) *f = 1.0f; return S_OK; }
static HRESULT STDMETHODCALLTYPE dcR_bool(void *This, BOOL b) { (void)This; (void)b; return S_OK; }
static HRESULT STDMETHODCALLTYPE dcR_redirect(void *This) { (void)This; return dc_stub("VisRestricted", 48, "RedirectMouseToHwnd", S_OK); }
static const void *DCVT_VisR[51];
static const void *const *visr_vt(void)
{
    static LONG done; int k;
    if (!InterlockedExchange(&done, 1)) {
        for (k = 0; k < 44; k++) DCVT_VisR[k] = DCVT_Vis[k];
        DCVT_VisR[44] = DCVT_VisR[45] = (void *)dcR_get_float; DCVT_VisR[46] = DCVT_VisR[47] = (void *)dcR_bool; DCVT_VisR[48] = (void *)dcR_redirect;
        DCVT_VisR[49] = DCVT_VisR[50] = (void *)dcV_SetTransformParent2;
    }
    return DCVT_VisR;
}

/* {83112A11-...} composition object partner (wine-uwp: CTimeManager::StartWUCAnimation, ImplicitAnimationDisabler; asked of any object,
   fail-fast or NULL use on E_NOINTERFACE). Slot 3 StartAnimation(const WCHAR *name, animation, out controller) goes to the object's own
   ICompositionObject.StartAnimation; the controller is an inert sink; every other slot succeeds without doing anything. Heap tear-off. */
static const GUID IID_ObjPartner_ = {0x83112A11, 0x8E47, 0x4B0C, {0xA5, 0x61, 0x9E, 0x33, 0x4D, 0x8F, 0x88, 0xE3}};
static const GUID IID_CompObject_ = {0xBCB4AD45, 0x7609, 0x4550, {0x93, 0x4F, 0x16, 0x00, 0x2A, 0x68, 0xFD, 0xED}};
static const GUID IID_CompAnimation_ = {0x464C4C2C, 0x1CAA, 0x4061, {0x9B, 0x40, 0xE1, 0x3F, 0xDE, 0x15, 0x03, 0xCA}};
static const GUID IID_PrivRectClip_ = {0x23238431, 0x2650, 0x42E9, {0xB0, 0xB8, 0x8F, 0x3C, 0x89, 0xEC, 0x51, 0x5D}};
typedef struct PartnerT { const void *const *vt; LONG ref; CObj *o; const GUID *iid; } PartnerT;
static HRESULT STDMETHODCALLTYPE pt_qi(PartnerT *t, REFIID r, void **p) { if (IsEqualGUID(r, t->iid)) { InterlockedIncrement(&t->ref); *p = t; return S_OK; } return comp_qi(t->o, r, p); }
static ULONG STDMETHODCALLTYPE pt_ar(PartnerT *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG STDMETHODCALLTYPE pt_rl(PartnerT *t) { LONG n = InterlockedDecrement(&t->ref); if (!n) { comp_release(t->o); HeapFree(GetProcessHeap(), 0, t); } return (ULONG)n; }
static HRESULT STDMETHODCALLTYPE sink_qi(void *s, REFIID r, void **p) { (void)r; *p = s; return S_OK; }
static ULONG STDMETHODCALLTYPE sink_ref(void *s) { (void)s; return 2; }
static HRESULT STDMETHODCALLTYPE sink_ok(void *s) { (void)s; return S_OK; }
static const void *g_sink_vt[32]; static const void *g_sink = g_sink_vt;
static HRESULT STDMETHODCALLTYPE pt_start(PartnerT *t, const WCHAR *name, IUnknown *anim, void **ctl)
{
    HSTRING hs = NULL; void *co = NULL, *a = NULL; HRESULT hr = S_OK;
    if (ctl) *ctl = (void *)&g_sink;
    if (!name || !anim) return S_OK;
    if (FAILED(WindowsCreateString(name, (UINT32)wcslen(name), &hs))) return S_OK;
    if (SUCCEEDED(comp_qi(t->o, &IID_CompObject_, &co)) && SUCCEEDED(anim->lpVtbl->QueryInterface(anim, &IID_CompAnimation_, &a))) {
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING, void *))(*(void ***)co)[9])(co, hs, a); }   /* ICompositionObject::StartAnimation */
    if (a) ((IUnknown *)a)->lpVtbl->Release((IUnknown *)a); if (co) ((IUnknown *)co)->lpVtbl->Release((IUnknown *)co);
    WindowsDeleteString(hs); return hr;
}
static HRESULT STDMETHODCALLTYPE pt_noop(PartnerT *t) { (void)t; return S_OK; }
static const void *g_pt_vt[32], *g_noop_vt[64];
static void pt_tables(void)
{
    static LONG done; int k; if (InterlockedExchange(&done, 1)) return;
    for (k = 3; k < 32; k++) g_sink_vt[k] = (void *)sink_ok; g_sink_vt[1] = g_sink_vt[2] = (void *)sink_ref; g_sink_vt[0] = (void *)sink_qi;
    for (k = 3; k < 64; k++) g_noop_vt[k] = (void *)pt_noop; g_noop_vt[0] = (void *)pt_qi; g_noop_vt[1] = (void *)pt_ar; g_noop_vt[2] = (void *)pt_rl;
    for (k = 0; k < 32; k++) g_pt_vt[k] = g_noop_vt[k]; g_pt_vt[3] = (void *)pt_start;
}
static HRESULT pt_new(CObj *o, const GUID *iid, const void *const *vt, void **p)
{
    PartnerT *t = (PartnerT *)HeapAlloc(GetProcessHeap(), 0, sizeof *t); if (!t) return E_OUTOFMEMORY;
    t->vt = vt; t->ref = 1; t->o = o; t->iid = iid; comp_addref(o); *p = t; return S_OK;
}
/* Private::IRectangleClip on the clip from {D57563C7} slot 11 (public IRectangleClip layout, IInspectable-based): 6/7 Bottom, 8/9 BottomLeftRadius,
   10/11 BottomRightRadius, 12/13 Left, 14/15 Right, 16/17 Top, 18/19 TopLeftRadius, 20/21 TopRightRadius. Stored on the InsetClip (rect mode). */
#define RC(t) ((InsetClip *)((PartnerT *)(t))->o)
#define RCF(name, f) static HRESULT STDMETHODCALLTYPE rc_get_##name(void *t, float *v) { *v = RC(t)->f; return S_OK; } \
                     static HRESULT STDMETHODCALLTYPE rc_put_##name(void *t, float v) { RC(t)->f = v; RC(t)->rect = 1; INVALIDATE(((PartnerT *)t)->o); return S_OK; }
#define RCR(name, k) static HRESULT STDMETHODCALLTYPE rc_get_##name(void *t, V2 *v) { *v = RC(t)->rad[k]; return S_OK; } \
                     static HRESULT STDMETHODCALLTYPE rc_put_##name(void *t, V2 v) { RC(t)->rad[k] = v; RC(t)->rect = 1; INVALIDATE(((PartnerT *)t)->o); return S_OK; }
RCF(Bottom, B) RCF(Left, L) RCF(Right, R) RCF(Top, T) RCR(TL, 0) RCR(TR, 1) RCR(BR, 2) RCR(BL, 3)
static const void *g_rclip_vt[22];
static const void *const *rclip_vt(void)
{
    static LONG done; int k;
    if (!InterlockedExchange(&done, 1)) {
        pt_tables(); for (k = 0; k < 6; k++) g_rclip_vt[k] = g_noop_vt[k];
        g_rclip_vt[6] = rc_get_Bottom; g_rclip_vt[7] = rc_put_Bottom; g_rclip_vt[8] = rc_get_BL; g_rclip_vt[9] = rc_put_BL; g_rclip_vt[10] = rc_get_BR; g_rclip_vt[11] = rc_put_BR;
        g_rclip_vt[12] = rc_get_Left; g_rclip_vt[13] = rc_put_Left; g_rclip_vt[14] = rc_get_Right; g_rclip_vt[15] = rc_put_Right; g_rclip_vt[16] = rc_get_Top; g_rclip_vt[17] = rc_put_Top;
        g_rclip_vt[18] = rc_get_TL; g_rclip_vt[19] = rc_put_TL; g_rclip_vt[20] = rc_get_TR; g_rclip_vt[21] = rc_put_TR;
    }
    return g_rclip_vt;
}
HRESULT dc_partner_qi_extra(CObj *o, REFIID r, void **p)
{
    /* Private::IRectangleClip {23238431-...} (19041; public IRectangleClip layout: 6/7 Bottom, 8-11 bottom radii, 12/13 Left, 14/15 Right, 16/17 Top,
       18-21 top radii). XAML's UpdateRoundedCornerClipVisual sets it on the clip from {D57563C7} slot 11 (an InsetClip here, zero insets = the
       visual's bounds). Rounded corners are not clipped yet: setters accepted and ignored */
    if (IsEqualGUID(r, &IID_PrivRectClip_) && o->cls == CLS_InsetClip) { pt_tables(); return pt_new(o, &IID_PrivRectClip_, rclip_vt(), p); }
    /* {10906888-...}: private projected-shadow interface. ProjectedShadowManager::ConfigureScenePair QIs both shadows of a popup's scene pair
       unchecked and calls slot 9 (a float setter). Projected shadows are not drawn: accepted and ignored */
    {   static const GUID iid_1090 = {0x10906888, 0xFB73, 0x5EB4, {0xA2, 0x25, 0x56, 0xB9, 0x26, 0x8F, 0xAE, 0x46}};
        if (IsEqualGUID(r, &iid_1090) && o->cls == CLS_CompositionProjectedShadow) { pt_tables(); return pt_new(o, &iid_1090, g_noop_vt, p); } }
    /* {FA92376B-...}: private listener registration (Windows' dcomp lists it among Visual's interfaces; XAML asks the facade property set).
       PropertySetListener::OnAnimationStarted QIs unchecked and calls slot 3 (property id, listener): change notifications for animated
       facade properties are not sent here, so registration is accepted and ignored */
    {   static const GUID iid_fa92 = {0xFA92376B, 0x164B, 0x432A, {0xBF, 0x53, 0xC7, 0xFE, 0x2D, 0x51, 0xCA, 0xBC}};
        if (IsEqualGUID(r, &iid_fa92)) { pt_tables(); return pt_new(o, &iid_fa92, g_noop_vt, p); } }
    /* {378518B8-...}: the caster's private interface, QI'd by ThemeShadowScene::UpdateCasters (fail-fast on E_NOINTERFACE): accepted and ignored */
    {   static const GUID iid_3785 = {0x378518B8, 0xFB66, 0x5BD4, {0xB9, 0x9A, 0x4F, 0xBD, 0x01, 0x03, 0xED, 0xE5}};
        if (IsEqualGUID(r, &iid_3785) && o->cls == CLS_CompositionProjectedShadowCaster) { pt_tables(); return pt_new(o, &iid_3785, g_noop_vt, p); } }
    if (!IsEqualGUID(r, &IID_ObjPartner_)) return E_NOINTERFACE;
    pt_tables(); return pt_new(o, &IID_ObjPartner_, g_pt_vt, p);
}

/* ============================ QueryInterface extras ============================ */
HRESULT dc_visual_qi_extra(CObj *o, REFIID r, void **p)
{
    Visual *v = (Visual *)o;
    {   static const GUID iid_f201 = {0xF2011FDF, 0x3A34, 0x4EC6, {0xAE, 0x59, 0x2D, 0xDD, 0x1B, 0xC9, 0xB6, 0xA1}};
        if (IsEqualGUID(r, &iid_f201)) { v->dcvr.vt = visr_vt(); v->dcvr.obj = o; comp_addref(o); *p = &v->dcvr; return S_OK; }
    }
    {   /* {C12A8AA7-...} IDCompositionVisualPartner2: HWCompTreeNodeWinRT::UpdatePrimaryVisualHitTestVisibility calls slot 43 (BOOL). Hit testing
           is XAML's own here: every slot accepted and ignored */
        static const GUID iid_c12a = {0xC12A8AA7, 0x363A, 0x4145, {0x9C, 0xC4, 0xB0, 0x11, 0xDA, 0x24, 0x7D, 0x4E}};
        if (IsEqualGUID(r, &iid_c12a)) { pt_tables(); return pt_new(o, &iid_c12a, g_noop_vt, p); }
        /* {FED2B808-...} IDCompositionDesktopDevicePartner4: DCompTreeHost::UpdateDebugSettings asks it of a visual it holds and calls the
           debug on/off pairs 22/23, 24/25 (no outputs); a failure propagates out of the frame. Debug overlays: accepted and ignored */
        {   static const GUID iid_fed2 = {0xFED2B808, 0x5EB4, 0x43A0, {0xAE, 0xA3, 0x35, 0xF6, 0x52, 0x80, 0xF9, 0x1B}};
            if (IsEqualGUID(r, &iid_fed2)) { pt_tables(); return pt_new(o, &iid_fed2, g_noop_vt, p); } }
        /* {BBED8DA5-...}: CPopup::ApplyHostBackdropTreatment (hamburger pane, flyouts) sets the host-backdrop brush through slot 6 and uses the
           QI result unchecked. Backdrop (acrylic) is not rendered: accepted and ignored */
        {   static const GUID iid_bbed = {0xBBED8DA5, 0x977F, 0x42CB, {0x9B, 0x28, 0xF0, 0xCE, 0xEB, 0xCE, 0xD3, 0xA7}};
            if (IsEqualGUID(r, &iid_bbed)) { pt_tables(); return pt_new(o, &iid_bbed, g_noop_vt, p); } }
    }
    if (IsEqualGUID(r, &DCIID_Visual) || IsEqualGUID(r, &DCIID_Visual2) || IsEqualGUID(r, &DCIID_InteropVisual)) { v->dcv.vt = DCVT_Vis; v->dcv.obj = o; comp_addref(o); *p = &v->dcv; return S_OK; }
    if (IsEqualGUID(r, &DCIID_Visual3)) { v->dcv3.vt = DCVT_Vis3; v->dcv3.obj = o; comp_addref(o); *p = &v->dcv3; return S_OK; }
    {   /* {FA92376B-...} (19045 Visual::s_InterfaceParts): XAML's DCompPropertyChangedListener::AttachToHandOffVisual registers property-change
           listeners through slot 3 (propertyId, listener); fail-fast if refused. We never raise changes: logging probe answering S_OK. */
        static const GUID iid_fa92 = {0xFA92376B, 0x164B, 0x432A, {0xBF, 0x53, 0xC7, 0xFE, 0x2D, 0x51, 0xCA, 0xBC}};
        if (IsEqualGUID(r, &iid_fa92)) { v->dcvpl.vt = DCVT_Priv; v->dcvpl.obj = o; comp_addref(o); *p = &v->dcvpl; return S_OK; }
    }
    {   /* twinapi.appcore CoreApplicationViewTitleBar (Window.SetTitleBar): {2C4EEF28-...} is the 19041 interop-visual IID (slot 35 SetInput gets the
           title bar's input sink) and {92179CC0-...} a visual partner interface (slot 10 also gets the sink). Logging probe for the partner. */
        static const GUID iid_2c4e = {0x2C4EEF28, 0x1BC0, 0x4736, {0xB7, 0xDD, 0xB6, 0x26, 0x92, 0xF9, 0xBD, 0x67}};
        static const GUID iid_9217 = {0x92179CC0, 0xAE1F, 0x48BC, {0xA3, 0x54, 0x8F, 0x7A, 0x88, 0x00, 0xF2, 0xFB}};
        /* {3B3E3DCF-...}: VisualContentRenderer::EnsureSpriteVisual uses it as the interop visual (slot 11, bitmap interpolation); wine-uwp maps it the same way */
        static const GUID iid_3b3e = {0x3B3E3DCF, 0xF20F, 0x4A83, {0x9B, 0xF2, 0x76, 0x84, 0xCD, 0xE0, 0x78, 0x61}};
        if (IsEqualGUID(r, &iid_2c4e) || IsEqualGUID(r, &iid_3b3e)) { v->dcv.vt = DCVT_Vis; v->dcv.obj = o; comp_addref(o); *p = &v->dcv; return S_OK; }
        if (IsEqualGUID(r, &iid_9217)) { v->dcvpl.vt = DCVT_Priv; v->dcvpl.obj = o; comp_addref(o); *p = &v->dcvpl; return S_OK; }
    }
    return E_NOINTERFACE;
}
/* {DDAABD43-226D-40DC-AA1B-E754922C37D4} on key-frame animations: XAML's DCompAnimationConversionContextWUC::SetRepeat calls slot 5 (float repeat
   count) and fail-fasts on E_NOINTERFACE. Logging probe answering S_OK (repeat counts are not applied yet). */
HRESULT dc_kfanim_qi_extra(CObj *o, REFIID r, void **p)
{
    static const GUID iid_ddaa = {0xDDAABD43, 0x226D, 0x40DC, {0xAA, 0x1B, 0xE7, 0x54, 0x92, 0x2C, 0x37, 0xD4}};
    Anim *a = (Anim *)o;
    if (IsEqualGUID(r, &iid_ddaa)) { a->priv.vt = DCVT_Priv; a->priv.obj = o; comp_addref(o); *p = &a->priv; return S_OK; }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE dcQ2_create_clip(void *This, void **out)
{
    CObj *comp = ((CView *)This)->obj, *o; HRESULT hr;
    if (!out) return E_POINTER; *out = NULL; o = comp_new(CLS_InsetClip, comp); if (!o) return E_OUTOFMEMORY; ((InsetClip *)o)->rect = 1;   /* empty rect until XAML sets it */
    hr = comp_qi(o, &IID_PrivRectClip_, out); comp_release(o); return hr;   /* slot 11 returns Private::IRectangleClip itself (XAML stores it, then As<ICompositionClip>) */
}
static HRESULT give(Compositor *c, int slot, const void *vt, CObj *o, void **p) { c->dcview[slot].vt = vt; c->dcview[slot].obj = o; comp_addref(o); *p = &c->dcview[slot]; return S_OK; }
/* ICompositorRestricted {36AF3EB2-...} (private, IUnknown-based): XAML's PointerSourceWrapper::Initialize (Reveal lights, Programmer mode)
   calls slot 3 (visual, out object) to get the visual's pointer source and QIs the result for ICompositionObject. Pointer-driven
   composition input is not available here: an empty CompositionPropertySet stands in (expressions reading it fail quietly, no glow). */
static HRESULT STDMETHODCALLTYPE cr_pointer_source(PartnerT *t, void *visual, void **out)
{
    CObj *ps; HRESULT hr; (void)visual; if (!out) return E_POINTER; *out = NULL;
    ps = comp_new(CLS_CompositionPropertySet, t->o); if (!ps) return E_OUTOFMEMORY;
    hr = comp_return_default(ps, out); comp_release(ps); return hr;
}
static const void *const *cr_vt(void)
{
    static const void *vt[64]; int k;
    if (!vt[0]) { pt_tables(); for (k = 0; k < 64; k++) vt[k] = g_noop_vt[k]; vt[3] = (void *)cr_pointer_source; }
    return vt;
}
HRESULT dc_compositor_qi_extra(CObj *o, REFIID r, void **p)
{
    Compositor *c = (Compositor *)o;
    {   static const GUID iid_restricted = {0x36AF3EB2, 0x0371, 0x49D7, {0x9B, 0xB8, 0x9C, 0x94, 0x14, 0xA9, 0x22, 0x9C}};
        if (IsEqualGUID(r, &iid_restricted)) return pt_new(o, &iid_restricted, cr_vt(), p); }
    if (IsEqualGUID(r, &DCIID_Device)) return give(c, 0, DCVT_Dev1, o, p);
    if (IsEqualGUID(r, &DCIID_Device2)) return give(c, 1, DCVT_Dev2, o, p);
    if (IsEqualGUID(r, &DCIID_DesktopDevice)) return give(c, 2, DCVT_Desktop, o, p);
    if (IsEqualGUID(r, &DCIID_Device3)) return give(c, 3, DCVT_Dev3, o, p);
    if (IsEqualGUID(r, &DCIID_InteropDev)) return give(c, 4, DCVT_Interop, o, p);
    /* {e7894c70} is not a second interop device but InteropCompositor's own part (dcomp.pdb Api@InteropCompositor: 3 MarkDirty,
       4 ClearCallback, 5 CreateManipulationTransform, 6 RealClose). With the device table, XAML's CreateManipulationTransform for a
       DirectManipulation shared transform landed on GetFrameStatistics and overwrote the transform (wine-uwp found the same). */
    if (IsEqualGUID(r, &DCIID_InteropDev2)) return give(c, 5, DCVT_Partner, o, p);
    {   /* IDCompositionDesktopDevicePartner3 {28d6ad3d}: DirectManipulation's _CreateInteractionForViewport creates its viewport
           interaction through slot 27 CreateSharedResource; the interop device table has the same layout */
        static const GUID iid_ddp3 = {0x28D6AD3D, 0xEE2A, 0x4BCD, {0x94, 0x19, 0x7D, 0x54, 0x80, 0x04, 0x35, 0xB1}};
        if (IsEqualGUID(r, &iid_ddp3)) return give(c, 23, DCVT_Interop, o, p); }
    if (IsEqualGUID(r, &DCIID_Partner)) return give(c, 6, DCVT_Partner, o, p);
    if (IsEqualGUID(r, &DCIID_DeviceInternal)) return give(c, 7, DCVT_Internal, o, p);
    if (IsEqualGUID(r, &DCIID_DeviceDebug)) return give(c, 8, DCVT_Debug, o, p);
    if (IsEqualGUID(r, &DCIID_Priv5)) return give(c, 9, DCVT_Priv5, o, p);
    if (IsEqualGUID(r, &DCIID_Priv2nd)) return give(c, 10, DCVT_Priv, o, p);
    if (IsEqualGUID(r, &DCIID_Priv1)) return give(c, 11, DCVT_Priv, o, p);
    if (IsEqualGUID(r, &DCIID_Priv2)) return give(c, 12, DCVT_Priv, o, p);
    if (IsEqualGUID(r, &DCIID_Priv3)) return give(c, 13, DCVT_Priv, o, p);
    if (IsEqualGUID(r, &DCIID_Priv4)) return give(c, 14, DCVT_Priv, o, p);
    if (IsEqualGUID(r, &DCIID_Priv6)) return give(c, 15, DCVT_Priv, o, p);
    {   /* {6083F66A-...}: Windows.UI.Xaml 19041 EnsureDCompDevice QIs it (fail-fast if refused); IUnknown-based, slot 3 = CreateAnimationInstance-like
           (DCompAnimationConversionContextDC). Not in wine's 26100 list. Probe vtable until a run needs the methods. */
        static const GUID iid_6083 = {0x6083F66A, 0x8533, 0x4F7E, {0xA3, 0x7E, 0x6C, 0x22, 0x21, 0x34, 0x28, 0x0B}};
        if (IsEqualGUID(r, &iid_6083)) return give(c, 16, DCVT_Priv, o, p);
    }
    {   /* {D57563C7-...}: HWCompTreeNodeWinRT::UpdateRoundedCornerClipVisual calls slot 11 (one out param) and uses the result as IInsetClip ->
           ICompositionClip (fail-fast on failure, NULL use on S_OK + NULL). Slot 11 makes an InsetClip; the other slots stay probe Q2 */
        static const GUID iid_d575 = {0xd57563c7, 0x32c5, 0x428a, {0x8e, 0xd9, 0xf4, 0xf9, 0x72, 0x26, 0xe4, 0x0d}};
        if (IsEqualGUID(r, &iid_d575)) { static const void *vt[40]; int k; if (!vt[0]) { for (k = 1; k < 40; k++) vt[k] = DCVT_Q[2][k]; vt[11] = (void *)dcQ2_create_clip; vt[0] = DCVT_Q[2][0]; }
            return give(c, 22, vt, o, p); }
    }
    {   /* ICompositorPartner {4BA72EC1-...} (probe Q1): slot 8 OpenSharedTargetFromHandle (WinUI 3 system backdrop, dcomp_manip.c) */
        static const GUID iid_cp = {0x4ba72ec1, 0x8968, 0x4103, {0x91, 0x11, 0x0d, 0x70, 0x57, 0x5a, 0xdd, 0x0f}};
        extern HRESULT STDMETHODCALLTYPE dcQ1_open_shared_target(void *This, HANDLE h, void **out);
        if (IsEqualGUID(r, &iid_cp)) { static const void *vt[40]; int k; if (!vt[0]) { for (k = 1; k < 40; k++) vt[k] = DCVT_Q[1][k]; vt[8] = (void *)dcQ1_open_shared_target; vt[0] = DCVT_Q[1][0]; }
            return give(c, 21, vt, o, p); }
    }
    {   /* the rest of Win10 19045's Compositor::s_InterfaceParts that the engine does not answer: named probe tables Q0..Q19 (log shows which slot XAML calls) */
        static const GUID probe[DC_NPROBE] = {
            {0xc9dd8ef0,0x6eb1,0x4e3c,{0xa6,0x58,0x67,0x5d,0x9c,0x64,0xd4,0xab}}, {0x4ba72ec1,0x8968,0x4103,{0x91,0x11,0x0d,0x70,0x57,0x5a,0xdd,0x0f}},
            {0xd57563c7,0x32c5,0x428a,{0x8e,0xd9,0xf4,0xf9,0x72,0x26,0xe4,0x0d}}, {0xe0ab257c,0x94b1,0x4c4d,{0x8f,0xe8,0xff,0x52,0x73,0x0f,0xb5,0x43}},
            {0x25297d5c,0x3ad4,0x4c9c,{0xb5,0xcf,0xe3,0x6a,0x38,0x51,0x23,0x30}}, {0x29e691fa,0x4567,0x4dca,{0xb3,0x19,0xd0,0xf2,0x07,0xeb,0x68,0x07}},
            {0x592c0f42,0xb2e4,0x4545,{0xaa,0xe1,0x66,0x1e,0x09,0xdc,0x0f,0xcf}}, {0x2a575c29,0x479b,0x4297,{0x92,0x28,0xc8,0x19,0x53,0xa1,0x81,0x28}},
            {0x2056f1e3,0x7dc8,0x4d28,{0xad,0x74,0xb8,0x17,0xf3,0x48,0x1b,0xb9}}, {0x98b9c1a7,0x8e71,0x4b53,{0xb4,0xa8,0x69,0xba,0x5d,0x19,0xdc,0x5b}},
            {0xcfa1658b,0x0123,0x4551,{0x88,0x91,0x89,0xbd,0xcc,0x40,0x32,0x2b}}, {0x1150dba5,0x1cfa,0x4d0d,{0xac,0x35,0xd0,0x64,0x7a,0x6f,0x8b,0xbf}},
            {0xa0245db1,0x4813,0x484e,{0xbf,0xbf,0x1c,0x72,0xa7,0x0f,0x7f,0xf9}}, {0xa2e6330e,0x8a60,0x5a38,{0xbb,0x85,0xb4,0x4e,0xa9,0x01,0x67,0x7c}},
            {0xb0055573,0xf481,0x4a01,{0xb9,0x24,0x59,0xa9,0x6e,0x1c,0x6b,0x14}}, {0x02f6bc74,0xed20,0x4773,{0xaf,0xe6,0xd4,0x9b,0x4a,0x93,0xdb,0x32}},
            {0xa1bea8ba,0xd726,0x4663,{0x81,0x29,0x6b,0x5e,0x79,0x27,0xff,0xa6}}, {0xdba1813c,0x60c5,0x4a42,{0xa4,0xd2,0x33,0x80,0xcd,0xdc,0xe8,0xa1}},
            {0xa48130a1,0xb7c4,0x46f7,{0xb9,0xbf,0xda,0xf4,0x3a,0x44,0xe6,0xee}}, {0x3b64a6bf,0x5f97,0x4c94,{0x86,0xe5,0x04,0x2d,0xd3,0x86,0xb2,0x7d}} };
        int k; for (k = 0; k < DC_NPROBE; k++) if (IsEqualGUID(r, &probe[k])) { char g[40]; comp_log("Compositor QI %s -> probe table Q%d\n", dc_guid(r, g), k); return give(c, 20 + k, DCVT_Q[k], o, p); }
    }
    if (IsEqualGUID(r, &DCIID_RefuseMica) || IsEqualGUID(r, &DCIID_RefusePointer)) return E_NOINTERFACE;               /* must stay refused (wine-uwp): answering makes WinUI use Mica / crashes the light code */
    if (dc_desktop_interop_qi(o, r, p) == S_OK) return S_OK;
    return dc_compositor_interop_qi(o, r, p);
}
