/* comp_expr.c - ExpressionAnimation, expression key frames, CompositionPropertySet, CompositionAnimationGroup, ImplicitAnimations
 *
 * What is real here: CompositionPropertySet stores, returns and can be key-frame animated.  AnimationGroup / StartAnimationGroup start its member animations.
 * ExpressionAnimation: a running one is a live binding (comp_anim.c keeps it in Compositor.exprs and re-evaluates it every tick with the small
 * evaluator below; WinUI's animated icons bind their brush colours to a theme property set this way).  Animation parameters are stored.
 * What is inert: expression key frames (hold the value the property had when the animation started), implicit animations (the collection
 * stores nothing and never triggers).  Reason: XAML libraries create these while building templates and treat E_NOTIMPL as fatal. */
#include "comp_types.h"
#include <math.h>
#include <wctype.h>
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

/* ============================ expression key frames / ExpressionAnimation ============================ */
static HRESULT kf_insert_expr(void *This, float t, HSTRING expr, void *easeIface)
{
    Anim *a = (Anim *)OBJ_OF(This); int i; KFrame k; WCHAR *xs = NULL;
    if (!(t >= 0.0f && t <= 1.0f)) return E_INVALIDARG;
    if (FAILED(comp_dup_hstring(expr, &xs))) return E_OUTOFMEMORY;
    if (a->nkf == a->cap) {
        int nc = a->cap ? a->cap * 2 : 4; KFrame *n = (KFrame *)HeapAlloc(GetProcessHeap(), 0, (size_t)nc * sizeof(KFrame)); if (!n) return E_OUTOFMEMORY;
        if (a->kf) { memcpy(n, a->kf, (size_t)a->nkf * sizeof(KFrame)); HeapFree(GetProcessHeap(), 0, a->kf); } a->kf = n; a->cap = nc;
    }
    memset(&k, 0, sizeof k); k.t = t; k.expr = 1; k.xs = xs; k.ease = easeIface ? OBJ_OF(easeIface) : NULL; if (k.ease) comp_addref(k.ease);
    for (i = 0; i < a->nkf; i++) if (a->kf[i].t == t) { if (a->kf[i].ease) comp_release(a->kf[i].ease); if (a->kf[i].xs) HeapFree(GetProcessHeap(), 0, a->kf[i].xs); a->kf[i] = k; return S_OK; }
    for (i = a->nkf; i > 0 && a->kf[i - 1].t > t; i--) a->kf[i] = a->kf[i - 1];
    a->kf[i] = k; a->nkf++; return S_OK;
}
/* expression key frames ("this.FinalValue", "this.StartingValue + delta", a parameter ...) are evaluated while the animation runs (comp_anim.c kf_value) */
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, InsertExpressionKeyFrame)(W(IKeyFrameAnimation) *This, float t, HSTRING expr) { return kf_insert_expr(This, t, expr, NULL); }
HRESULT STDMETHODCALLTYPE CI_(IKeyFrameAnimation, InsertExpressionKeyFrame_2)(W(IKeyFrameAnimation) *This, float t, HSTRING expr, W(ICompositionEasingFunction) *e) { return kf_insert_expr(This, t, expr, e); }

HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateExpressionAnimation)(W(ICompositor) *This, W(IExpressionAnimation) **r)
{ CObj *o; HRESULT hr; if (!r) return E_POINTER; o = comp_new(CLS_ExpressionAnimation, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(IExpressionAnimation), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateExpressionAnimation_2)(W(ICompositor) *This, HSTRING expr, W(IExpressionAnimation) **r)
{
    HRESULT hr = CI_ICompositor_CreateExpressionAnimation(This, r); if (FAILED(hr)) return hr;
    { Anim *a = (Anim *)OBJ_OF(*r); WCHAR *d; if (SUCCEEDED(comp_dup_hstring(expr, &d))) a->expr = d; } return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(IExpressionAnimation, get_Expression)(W(IExpressionAnimation) *This, HSTRING *r)
{ Anim *a = (Anim *)OBJ_OF(This); if (!r) return E_POINTER; return WindowsCreateString(a->expr ? a->expr : L"", a->expr ? lstrlenW(a->expr) : 0, r); }
HRESULT STDMETHODCALLTYPE CI_(IExpressionAnimation, put_Expression)(W(IExpressionAnimation) *This, HSTRING v)
{ Anim *a = (Anim *)OBJ_OF(This); WCHAR *d; HRESULT hr = comp_dup_hstring(v, &d); if (FAILED(hr)) return hr; if (a->expr) HeapFree(GetProcessHeap(), 0, a->expr); a->expr = d; return S_OK; }

/* ============================ CompositionPropertySet ============================ */
int comp_pset_dim(int kind) { static const int d[] = { 0, 1, 2, 3, 4, 4, 4, 6, 16, 1 }; return kind >= 1 && kind <= 9 ? d[kind] : 1; }
static void pset_destroy(CObj *o) { PSet *p = (PSet *)o; if (p->e) HeapFree(GetProcessHeap(), 0, p->e); }
PEntry *comp_pset_find(CObj *o, const WCHAR *name) { PSet *p = (PSet *)o; int i; for (i = 0; i < p->n; i++) if (!wcscmp(p->e[i].name, name)) return &p->e[i]; return NULL; }
static HRESULT pset_insert(void *This, HSTRING name, int kind, const float *v, int n)
{
    PSet *p = (PSet *)OBJ_OF(This); const WCHAR *nm = hs(name); PEntry *e;
    if (!nm[0] || lstrlenW(nm) >= 48) { comp_log("property set: name '%.60ls' not usable (E_INVALIDARG)\n", nm); return E_INVALIDARG; }
    e = comp_pset_find(&p->o, nm);
    if (!e) {
        if (p->n == p->cap) { int nc = p->cap ? p->cap * 2 : 8; PEntry *x = (PEntry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)nc * sizeof(PEntry)); if (!x) return E_OUTOFMEMORY;
            if (p->e) { memcpy(x, p->e, (size_t)p->n * sizeof(PEntry)); HeapFree(GetProcessHeap(), 0, p->e); } p->e = x; p->cap = nc; }
        e = &p->e[p->n++]; memset(e, 0, sizeof *e); lstrcpynW(e->name, nm, 48);
    }
    if (e->kind == kind && !memcmp(e->v, v, (size_t)n * sizeof(float))) return S_OK;
    e->kind = kind; memset(e->v, 0, sizeof e->v); memcpy(e->v, v, (size_t)n * sizeof(float));
    INVALIDATE(&p->o);        /* running expressions may read it (XAML keeps element properties such as ActualOffset current this way) */
    return S_OK;
}
/* internal writer (dcomp_manip.c keeps a ManipulationTransform's "Matrix" current); invalidates only on change */
void comp_pset_put(CObj *o, const WCHAR *name, int kind, const float *v, int n)
{
    PSet *p = (PSet *)o; PEntry *e = comp_pset_find(o, name); float f[16] = {0};
    memcpy(f, v, (size_t)n * sizeof(float));
    if (e && e->kind == kind && !memcmp(e->v, f, sizeof f)) return;
    if (!e) {
        if (p->n == p->cap) { int nc = p->cap ? p->cap * 2 : 8; PEntry *x = (PEntry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)nc * sizeof(PEntry)); if (!x) return;
            if (p->e) { memcpy(x, p->e, (size_t)p->n * sizeof(PEntry)); HeapFree(GetProcessHeap(), 0, p->e); } p->e = x; p->cap = nc; }
        e = &p->e[p->n++]; memset(e, 0, sizeof *e); lstrcpynW(e->name, name, 48);
    }
    e->kind = kind; memset(e->v, 0, sizeof e->v); memcpy(e->v, f, sizeof f); INVALIDATE(o);
}
static HRESULT pset_get(void *This, HSTRING name, int kind, float *out, int n, Windows_UI_Composition_CompositionGetValueStatus *st)
{
    PSet *p = (PSet *)OBJ_OF(This); PEntry *e; if (!st) return E_POINTER;
    e = comp_pset_find(&p->o, hs(name)); if (!e) { *st = Windows_UI_Composition_CompositionGetValueStatus_NotFound; return S_OK; }
    if (e->kind != kind) { *st = Windows_UI_Composition_CompositionGetValueStatus_TypeMismatch; return S_OK; }
    memcpy(out, e->v, (size_t)n * sizeof(float)); *st = Windows_UI_Composition_CompositionGetValueStatus_Succeeded; return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreatePropertySet)(W(ICompositor) *This, W(ICompositionPropertySet) **r)
{ CObj *o; HRESULT hr; if (!r) return E_POINTER; o = comp_new(CLS_CompositionPropertySet, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionPropertySet), (void **)r); comp_release(o); return hr; }
#define PS(Name, type, kind, n, ...) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionPropertySet, Insert##Name)(W(ICompositionPropertySet) *This, HSTRING nm, type v) { float f[16] = {0}; __VA_ARGS__; return pset_insert(This, nm, kind, f, n); } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionPropertySet, TryGet##Name)(W(ICompositionPropertySet) *This, HSTRING nm, type *r, Windows_UI_Composition_CompositionGetValueStatus *st) { float f[16] = {0}; HRESULT hr = pset_get(This, nm, kind, f, n, st); \
    if (SUCCEEDED(hr) && *st == Windows_UI_Composition_CompositionGetValueStatus_Succeeded) { memset(r, 0, sizeof *r); UNPACK_##Name } return hr; }
#define UNPACK_Scalar *r = f[0];
#define UNPACK_Vector2 r->X = f[0]; r->Y = f[1];
#define UNPACK_Vector3 r->X = f[0]; r->Y = f[1]; r->Z = f[2];
#define UNPACK_Vector4 r->X = f[0]; r->Y = f[1]; r->Z = f[2]; r->W = f[3];
#define UNPACK_Quaternion r->X = f[0]; r->Y = f[1]; r->Z = f[2]; r->W = f[3];
#define UNPACK_Color r->A = (UINT8)f[0]; r->R = (UINT8)f[1]; r->G = (UINT8)f[2]; r->B = (UINT8)f[3];
#define UNPACK_Matrix3x2 r->M11 = f[0]; r->M12 = f[1]; r->M21 = f[2]; r->M22 = f[3]; r->M31 = f[4]; r->M32 = f[5];
PS(Scalar, float, 1, 1, f[0] = v)
PS(Vector2, V2, 2, 2, f[0] = v.X; f[1] = v.Y)
PS(Vector3, V3, 3, 3, f[0] = v.X; f[1] = v.Y; f[2] = v.Z)
PS(Vector4, V4, 4, 4, f[0] = v.X; f[1] = v.Y; f[2] = v.Z; f[3] = v.W)
PS(Quaternion, Quat, 5, 4, f[0] = v.X; f[1] = v.Y; f[2] = v.Z; f[3] = v.W)
PS(Color, Col, 6, 4, f[0] = v.A; f[1] = v.R; f[2] = v.G; f[3] = v.B)
PS(Matrix3x2, M32, 7, 6, f[0] = v.M11; f[1] = v.M12; f[2] = v.M21; f[3] = v.M22; f[4] = v.M31; f[5] = v.M32)
HRESULT STDMETHODCALLTYPE CI_(ICompositionPropertySet, InsertMatrix4x4)(W(ICompositionPropertySet) *This, HSTRING nm, M44 v) { return pset_insert(This, nm, 8, &v.M11, 16); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionPropertySet, TryGetMatrix4x4)(W(ICompositionPropertySet) *This, HSTRING nm, M44 *r, Windows_UI_Composition_CompositionGetValueStatus *st)
{ float f[16] = {0}; HRESULT hr = pset_get(This, nm, 8, f, 16, st); if (SUCCEEDED(hr) && *st == Windows_UI_Composition_CompositionGetValueStatus_Succeeded) memcpy(r, f, sizeof f); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionPropertySet2, InsertBoolean)(W(ICompositionPropertySet2) *This, HSTRING nm, boolean v) { float f = v ? 1.0f : 0.0f; return pset_insert(This, nm, 9, &f, 1); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionPropertySet2, TryGetBoolean)(W(ICompositionPropertySet2) *This, HSTRING nm, boolean *r, Windows_UI_Composition_CompositionGetValueStatus *st)
{ float f = 0; HRESULT hr = pset_get(This, nm, 9, &f, 1, st); if (SUCCEEDED(hr) && *st == Windows_UI_Composition_CompositionGetValueStatus_Succeeded) *r = f > 0.5f; return hr; }

/* animation parameters and the expression evaluator: comp_eval.c */
/* ============================ AnimationGroup ============================ */
static void agroup_destroy(CObj *o) { AGroup *g = (AGroup *)o; int i; for (i = 0; i < g->n; i++) comp_release(g->a[i]); if (g->a) HeapFree(GetProcessHeap(), 0, g->a); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor2, CreateAnimationGroup)(W(ICompositor2) *This, W(ICompositionAnimationGroup) **r)
{ CObj *o; HRESULT hr; if (!r) return E_POINTER; o = comp_new(CLS_CompositionAnimationGroup, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionAnimationGroup), (void **)r); comp_release(o); return hr; }
#define AG(T) ((AGroup *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimationGroup, get_Count)(W(ICompositionAnimationGroup) *This, INT32 *r) { *r = AG(This)->n; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimationGroup, Add)(W(ICompositionAnimationGroup) *This, W(ICompositionAnimation) *v)
{
    AGroup *g = AG(This); if (!v) return E_INVALIDARG;
    if (g->n == g->cap) { int nc = g->cap ? g->cap * 2 : 4; CObj **n = (CObj **)HeapAlloc(GetProcessHeap(), 0, (size_t)nc * sizeof(CObj *)); if (!n) return E_OUTOFMEMORY;
        if (g->a) { memcpy(n, g->a, (size_t)g->n * sizeof(CObj *)); HeapFree(GetProcessHeap(), 0, g->a); } g->a = n; g->cap = nc; }
    comp_addref(OBJ_OF(v)); g->a[g->n++] = OBJ_OF(v); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimationGroup, Remove)(W(ICompositionAnimationGroup) *This, W(ICompositionAnimation) *v)
{ AGroup *g = AG(This); int i; for (i = 0; i < g->n; i++) if (g->a[i] == OBJ_OF(v)) { comp_release(g->a[i]); memmove(&g->a[i], &g->a[i + 1], (size_t)(g->n - i - 1) * sizeof(CObj *)); g->n--; break; } return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimationGroup, RemoveAll)(W(ICompositionAnimationGroup) *This) { AGroup *g = AG(This); while (g->n) comp_release(g->a[--g->n]); return S_OK; }
static HRESULT group_op(CObj *target, CObj *base, int start)
{
    int i; HRESULT hr = S_OK;
    if (!base) return E_INVALIDARG;
    if (base->cls == CLS_CompositionAnimationGroup) { AGroup *g = (AGroup *)base; for (i = 0; i < g->n; i++) { HRESULT h = group_op(target, g->a[i], start); if (FAILED(h)) hr = h; } return hr; }
    if (comp_is_a(base->cls, CLS_CompositionAnimation)) {
        Anim *a = (Anim *)base; if (!a->target || !a->target[0]) return S_OK;
        return start ? comp_start_animation(target, a->target, base) : comp_stop_animation(target, a->target);
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject2, StartAnimationGroup)(W(ICompositionObject2) *This, W(ICompositionAnimationBase) *v) { return group_op(OBJ_OF(This), v ? OBJ_OF(v) : NULL, 1); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject2, StopAnimationGroup)(W(ICompositionObject2) *This, W(ICompositionAnimationBase) *v) { return group_op(OBJ_OF(This), v ? OBJ_OF(v) : NULL, 0); }

/* ============================ implicit animations: collection exists, never triggers ============================ */
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject2, get_ImplicitAnimations)(W(ICompositionObject2) *This, W(IImplicitAnimationCollection) **r)
{
    CObj *o = OBJ_OF(This); if (!r) return E_POINTER;
    if (!o->implicit) { o->implicit = comp_new(CLS_ImplicitAnimationCollection, o->comp ? o->comp : o); if (!o->implicit) return E_OUTOFMEMORY; }
    return comp_qi(o->implicit, &IID_(IImplicitAnimationCollection), (void **)r);
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionObject2, put_ImplicitAnimations)(W(ICompositionObject2) *This, W(IImplicitAnimationCollection) *v) { comp_assign(&OBJ_OF(This)->implicit, v ? OBJ_OF(v) : NULL); return S_OK; }

/* the collection is a real map (key -> animation), so code that fills and reads it works (the Windows Community Toolkit's
   ImplicitAnimationSet: an inert Lookup returning NULL made Media Player's XAML fail to load). Nothing triggers the animations yet. */
#define IMAP(m) CI_Windows_Foundation_Collections_IMap_2__HSTRING__Windows_UI_Composition_ICompositionAnimationBase_##m
typedef Windows_Foundation_Collections_IMap_2__HSTRING__Windows_UI_Composition_ICompositionAnimationBase IAMap;
typedef struct ImplColl { CObj o; int n, cap; WCHAR **keys; IUnknown **vals; } ImplColl;
static int ic_find(ImplColl *c, HSTRING key) { int i; const WCHAR *k = hs(key); for (i = 0; i < c->n; i++) if (!wcscmp(c->keys[i], k)) return i; return -1; }
static void ic_destroy(CObj *o) { ImplColl *c = (ImplColl *)o; int i; for (i = 0; i < c->n; i++) { free(c->keys[i]); if (c->vals[i]) c->vals[i]->lpVtbl->Release(c->vals[i]); } free(c->keys); free(c->vals); }
HRESULT STDMETHODCALLTYPE IMAP(Lookup)(IAMap *This, HSTRING key, W(ICompositionAnimationBase) **r)
{
    ImplColl *c = (ImplColl *)OBJ_OF(This); int i; if (!r) return E_POINTER; *r = NULL;
    if ((i = ic_find(c, key)) < 0) return E_BOUNDS;
    if (c->vals[i]) c->vals[i]->lpVtbl->AddRef(c->vals[i]); *r = (W(ICompositionAnimationBase) *)c->vals[i]; return S_OK;
}
HRESULT STDMETHODCALLTYPE IMAP(get_Size)(IAMap *This, UINT32 *r) { if (!r) return E_POINTER; *r = (UINT32)((ImplColl *)OBJ_OF(This))->n; return S_OK; }
HRESULT STDMETHODCALLTYPE IMAP(HasKey)(IAMap *This, HSTRING key, boolean *r) { if (!r) return E_POINTER; *r = ic_find((ImplColl *)OBJ_OF(This), key) >= 0; return S_OK; }
HRESULT STDMETHODCALLTYPE IMAP(Insert)(IAMap *This, HSTRING key, W(ICompositionAnimationBase) *v, boolean *replaced)
{
    ImplColl *c = (ImplColl *)OBJ_OF(This); int i = ic_find(c, key); IUnknown *u = (IUnknown *)v;
    if (u) u->lpVtbl->AddRef(u);
    if (i >= 0) { if (c->vals[i]) c->vals[i]->lpVtbl->Release(c->vals[i]); c->vals[i] = u; if (replaced) *replaced = 1; return S_OK; }
    if (c->n == c->cap) { int nc = c->cap ? c->cap * 2 : 4; WCHAR **k = (WCHAR **)realloc(c->keys, nc * sizeof *k); IUnknown **vv;
        if (!k) { if (u) u->lpVtbl->Release(u); return E_OUTOFMEMORY; } c->keys = k; vv = (IUnknown **)realloc(c->vals, nc * sizeof *vv);
        if (!vv) { if (u) u->lpVtbl->Release(u); return E_OUTOFMEMORY; } c->vals = vv; c->cap = nc; }
    c->keys[c->n] = _wcsdup(hs(key)); c->vals[c->n] = u; c->n++; if (replaced) *replaced = 0; return S_OK;
}
HRESULT STDMETHODCALLTYPE IMAP(Remove)(IAMap *This, HSTRING key)
{
    ImplColl *c = (ImplColl *)OBJ_OF(This); int i = ic_find(c, key); if (i < 0) return E_BOUNDS;
    free(c->keys[i]); if (c->vals[i]) c->vals[i]->lpVtbl->Release(c->vals[i]);
    memmove(c->keys + i, c->keys + i + 1, (c->n - i - 1) * sizeof *c->keys); memmove(c->vals + i, c->vals + i + 1, (c->n - i - 1) * sizeof *c->vals); c->n--; return S_OK;
}
HRESULT STDMETHODCALLTYPE IMAP(Clear)(IAMap *This) { ImplColl *c = (ImplColl *)OBJ_OF(This); ic_destroy(&c->o); c->n = c->cap = 0; c->keys = NULL; c->vals = NULL; return S_OK; }

void comp_register_impls_eval(void);
void comp_register_impls_expr(void)
{
    comp_register_impls_eval(); comp_register_impls_interact();
    g_impl[CLS_CompositionPropertySet].size = sizeof(PSet); g_impl[CLS_CompositionPropertySet].destroy = pset_destroy;
    g_impl[CLS_CompositionAnimationGroup].size = sizeof(AGroup); g_impl[CLS_CompositionAnimationGroup].destroy = agroup_destroy;
    g_impl[CLS_ImplicitAnimationCollection].size = sizeof(ImplColl); g_impl[CLS_ImplicitAnimationCollection].destroy = ic_destroy;
}
