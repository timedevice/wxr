/* comp_brush2.c - CompositionMaskBrush and CompositionNineGridBrush (state; drawing is in dcomp_render.c) */
#include "comp_types.h"

#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

/* ============================ CompositionMaskBrush ============================ */
#define MB(T) ((MaskB *)OBJ_OF(T))
static void mb_destroy(CObj *o) { MaskB *m = (MaskB *)o; if (m->src) comp_release(m->src); if (m->mask) comp_release(m->mask); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionMaskBrush, get_Mask)(W(ICompositionMaskBrush) *This, W(ICompositionBrush) **r) { return comp_return(MB(This)->mask, &IID_(ICompositionBrush), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionMaskBrush, put_Mask)(W(ICompositionMaskBrush) *This, W(ICompositionBrush) *v) { comp_assign(&MB(This)->mask, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionMaskBrush, get_Source)(W(ICompositionMaskBrush) *This, W(ICompositionBrush) **r) { return comp_return(MB(This)->src, &IID_(ICompositionBrush), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionMaskBrush, put_Source)(W(ICompositionMaskBrush) *This, W(ICompositionBrush) *v) { comp_assign(&MB(This)->src, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }

/* ============================ CompositionNineGridBrush ============================ */
#define NG(T) ((NineG *)OBJ_OF(T))
static void ng_init(CObj *o) { NineG *n = (NineG *)o; n->ls = n->ts = n->rs = n->bs = 1.0f; }
static void ng_destroy(CObj *o) { NineG *n = (NineG *)o; if (n->src) comp_release(n->src); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, get_Source)(W(ICompositionNineGridBrush) *This, W(ICompositionBrush) **r) { return comp_return(NG(This)->src, &IID_(ICompositionBrush), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, put_Source)(W(ICompositionNineGridBrush) *This, W(ICompositionBrush) *v) { comp_assign(&NG(This)->src, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
#define NGP(name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, get_##name)(W(ICompositionNineGridBrush) *This, type *r) { *r = NG(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, put_##name)(W(ICompositionNineGridBrush) *This, type v) { NG(This)->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
NGP(LeftInset, float, l) NGP(TopInset, float, t) NGP(RightInset, float, r) NGP(BottomInset, float, b)
NGP(LeftInsetScale, float, ls) NGP(TopInsetScale, float, ts) NGP(RightInsetScale, float, rs) NGP(BottomInsetScale, float, bs)
NGP(IsCenterHollow, boolean, hollow)
HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, SetInsets)(W(ICompositionNineGridBrush) *This, float v) { NineG *n = NG(This); n->l = n->t = n->r = n->b = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, SetInsets_2)(W(ICompositionNineGridBrush) *This, float l, float t, float r, float b)
{ NineG *n = NG(This); n->l = l; n->t = t; n->r = r; n->b = b; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, SetInsetScales)(W(ICompositionNineGridBrush) *This, float v) { NineG *n = NG(This); n->ls = n->ts = n->rs = n->bs = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionNineGridBrush, SetInsetScales_2)(W(ICompositionNineGridBrush) *This, float l, float t, float r, float b)
{ NineG *n = NG(This); n->ls = l; n->ts = t; n->rs = r; n->bs = b; INVALIDATE(OBJ_OF(This)); return S_OK; }

void comp_register_impls_brush2(void)
{
    g_impl[CLS_CompositionMaskBrush].size = sizeof(MaskB); g_impl[CLS_CompositionMaskBrush].destroy = mb_destroy;
    g_impl[CLS_CompositionNineGridBrush].size = sizeof(NineG); g_impl[CLS_CompositionNineGridBrush].init = ng_init; g_impl[CLS_CompositionNineGridBrush].destroy = ng_destroy;
}
