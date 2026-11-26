/* comp_eval.c - animation parameters (Set*Parameter) and the composition expression evaluator
 *
 * An expression is parsed and evaluated in one pass from its text every time its value is needed (a running ExpressionAnimation: every
 * composition tick, see comp_anim.c). The language: numbers, + - * / %, unary - and !, comparisons, && ||, ?:, parentheses, parameters,
 * this.Target / this.StartingValue / this.CurrentValue, member access (properties of referenced objects, entries of property sets,
 * swizzles .X .XY ..., matrix elements ._11 / .M11), the constructors and static members of Vector2/3/4, Quaternion, Matrix3x2 and
 * Matrix4x4, and the functions listed in ev_call.
 *
 * Reference parameters are either composition objects of this module or foreign animation objects (IAnimationObject: XAML UIElements,
 * "visual0.ActualOffset.Y"). A foreign object names the composition object behind one of its properties when asked with
 * IAnimationObject::PopulatePropertyInfo (XAML: a property set it keeps current); the answer is kept per parameter.
 *
 * Results: 1 = value, 0 = not evaluable now (a referenced member does not exist yet; retried), -1 = the text is not supported. */
#include "comp_types.h"
#include <math.h>
#include <wctype.h>
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

/* ============================ animation parameters ============================ */
void comp_anim_par_clear(AnimPar *p)
{
    int i;
    if (p->obj) comp_release(p->obj);
    if (p->ext) p->ext->lpVtbl->Release(p->ext);
    for (i = 0; i < p->nxc; i++) if (p->xc[i].obj) comp_release(p->xc[i].obj);
    p->obj = NULL; p->ext = NULL; p->nxc = 0; p->n = 0;
}
static AnimPar *par_slot(void *This, HSTRING key)
{
    Anim *a = (Anim *)OBJ_OF(This); const WCHAR *k = hs(key); int i;
    if (!k[0] || lstrlenW(k) >= 64) { comp_log("animation parameter '%.80ls': name not usable, dropped\n", k); return NULL; }
    for (i = 0; i < a->npar; i++) if (!_wcsicmp(a->par[i].name, k)) { comp_anim_par_clear(&a->par[i]); return &a->par[i]; }
    if (a->npar == a->cappar) {
        int nc = a->cappar ? a->cappar * 2 : 4; AnimPar *n = (AnimPar *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)nc * sizeof(AnimPar)); if (!n) return NULL;
        if (a->par) { memcpy(n, a->par, (size_t)a->npar * sizeof(AnimPar)); HeapFree(GetProcessHeap(), 0, a->par); } a->par = n; a->cappar = nc;
    }
    i = a->npar++; memset(&a->par[i], 0, sizeof a->par[i]); lstrcpynW(a->par[i].name, k, 64); return &a->par[i];
}
static HRESULT par_floats(void *This, HSTRING key, const float *v, int n) { AnimPar *p = par_slot(This, key); if (p) { memcpy(p->v, v, (size_t)n * sizeof(float)); p->n = n; } return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetReferenceParameter)(W(ICompositionAnimation) *This, HSTRING key, W(ICompositionObject) *obj)
{
    AnimPar *p = par_slot(This, key); IUnknown *u = (IUnknown *)obj; static LONG nlog;
    if (!p) return S_OK;
    if (!u) { if (InterlockedIncrement(&nlog) <= 20) comp_log("SetReferenceParameter('%ls'): NULL object - expressions using it cannot be evaluated\n", hs(key)); return S_OK; }
    if (comp_is_foreign(u)) {      /* XAML element: resolved member by member when the expression is evaluated */
        IUnknown *ao = NULL;
        if (SUCCEEDED(u->lpVtbl->QueryInterface(u, &IID_(IAnimationObject), (void **)&ao)) && ao) p->ext = ao;
        else if (InterlockedIncrement(&nlog) <= 20) comp_log("SetReferenceParameter('%ls'): foreign object %p is not an IAnimationObject - expressions using it cannot be evaluated\n", hs(key), (void *)u);
        return S_OK;
    }
    p->obj = OBJ_OF(u); comp_addref(p->obj); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetScalarParameter)(W(ICompositionAnimation) *This, HSTRING key, float v) { return par_floats(This, key, &v, 1); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetVector2Parameter)(W(ICompositionAnimation) *This, HSTRING key, Windows_Foundation_Numerics_Vector2 v) { float f[2] = { v.X, v.Y }; return par_floats(This, key, f, 2); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetVector3Parameter)(W(ICompositionAnimation) *This, HSTRING key, Windows_Foundation_Numerics_Vector3 v) { float f[3] = { v.X, v.Y, v.Z }; return par_floats(This, key, f, 3); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetVector4Parameter)(W(ICompositionAnimation) *This, HSTRING key, Windows_Foundation_Numerics_Vector4 v) { float f[4] = { v.X, v.Y, v.Z, v.W }; return par_floats(This, key, f, 4); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetColorParameter)(W(ICompositionAnimation) *This, HSTRING key, Windows_UI_Color v) { float f[4] = { v.A, v.R, v.G, v.B }; return par_floats(This, key, f, 4); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetQuaternionParameter)(W(ICompositionAnimation) *This, HSTRING key, Windows_Foundation_Numerics_Quaternion v) { float f[4] = { v.X, v.Y, v.Z, v.W }; return par_floats(This, key, f, 4); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetMatrix3x2Parameter)(W(ICompositionAnimation) *This, HSTRING key, M32 v) { float f[6] = { v.M11, v.M12, v.M21, v.M22, v.M31, v.M32 }; return par_floats(This, key, f, 6); }
/* XAML's DirectManipulation content: "transform*manipTransform.Matrix" with the element's own transform as this parameter */
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, SetMatrix4x4Parameter)(W(ICompositionAnimation) *This, HSTRING key, M44 v) { return par_floats(This, key, &v.M11, 16); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation2, SetBooleanParameter)(W(ICompositionAnimation2) *This, HSTRING key, boolean v) { float f = v ? 1.0f : 0.0f; return par_floats(This, key, &f, 1); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, ClearParameter)(W(ICompositionAnimation) *This, HSTRING key)
{
    Anim *a = (Anim *)OBJ_OF(This); const WCHAR *k = hs(key); int i;
    for (i = 0; i < a->npar; i++) if (!_wcsicmp(a->par[i].name, k)) { comp_anim_par_clear(&a->par[i]); a->par[i] = a->par[--a->npar]; break; }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionAnimation, ClearAllParameters)(W(ICompositionAnimation) *This)
{ Anim *a = (Anim *)OBJ_OF(This); while (a->npar) comp_anim_par_clear(&a->par[--a->npar]); return S_OK; }

/* ============================ IAnimationObject ============================ */
static void apinfo_destroy(CObj *o) { APInfo *i = (APInfo *)o; if (i->res) comp_release(i->res); }
HRESULT STDMETHODCALLTYPE CI_(IAnimationPropertyInfo, get_AccessMode)(W(IAnimationPropertyInfo) *This, Windows_UI_Composition_AnimationPropertyAccessMode *r) { if (!r) return E_POINTER; *r = ((APInfo *)OBJ_OF(This))->access; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IAnimationPropertyInfo, put_AccessMode)(W(IAnimationPropertyInfo) *This, Windows_UI_Composition_AnimationPropertyAccessMode v) { ((APInfo *)OBJ_OF(This))->access = (int)v; return S_OK; }
/* every composition object of ours: "the property is mine" (XAML forwards its elements' PopulatePropertyInfo to the property set that
   backs the element's facade properties, after writing the current value into it) */
HRESULT STDMETHODCALLTYPE CI_(IAnimationObject, PopulatePropertyInfo)(W(IAnimationObject) *This, HSTRING name, W(IAnimationPropertyInfo) *info)
{
    (void)name;
    if (info && !comp_is_foreign(info) && OBJ_OF(info)->cls == CLS_AnimationPropertyInfo) { APInfo *i = (APInfo *)OBJ_OF(info); comp_assign(&i->res, OBJ_OF(This)); i->access = 3; }
    return S_OK;
}
CObj *comp_resolve_animation_object(CObj *comp, IUnknown *ext, const WCHAR *name)
{
    APInfo *info; void *iv = NULL; HSTRING h = NULL; HRESULT hr; CObj *res = NULL; static LONG nlog;
    if (!ext) return NULL;
    info = (APInfo *)comp_new(CLS_AnimationPropertyInfo, comp); if (!info) return NULL;
    if (SUCCEEDED(comp_qi(&info->o, &IID_(IAnimationPropertyInfo), &iv)) && SUCCEEDED(WindowsCreateString(name, (UINT32)lstrlenW(name), &h))) {
        hr = ((W(IAnimationObject) *)ext)->lpVtbl->PopulatePropertyInfo((W(IAnimationObject) *)ext, h, (W(IAnimationPropertyInfo) *)iv);
        if (info->res) { res = info->res; comp_addref(res); }
        if (InterlockedIncrement(&nlog) <= 40) comp_log("animation object %p: property '%ls' -> %s %p (0x%08lX)\n", (void *)ext, name, res ? g_cd[res->cls].sname : "nothing", (void *)res, (unsigned long)hr);
    }
    if (h) WindowsDeleteString(h);
    if (iv) ((IUnknown *)iv)->lpVtbl->Release((IUnknown *)iv);
    comp_release(&info->o);
    return res;
}

/* ============================ expression evaluator ============================ */
typedef struct { int n; float v[16]; CObj *obj; AnimPar *xp; int any; } EV;      /* obj / xp: an object reference (ours / foreign); any: dry-run wildcard */
typedef struct { const WCHAR *p; Anim *a; CObj *target; const WCHAR *prop; int err; const float *start; int nstart; int dry, depth; } EP;
#define FAIL(e, k) do { if (!(e)->err) (e)->err = (k); } while (0)       /* 1 = not supported, 2 = not evaluable now */
static void ep_ws(EP *e) { while (*e->p == L' ' || *e->p == L'\t' || *e->p == L'\r' || *e->p == L'\n') e->p++; }
static int ep_eat(EP *e, const WCHAR *s) { int n = lstrlenW(s); ep_ws(e); if (!wcsncmp(e->p, s, n)) { e->p += n; return 1; } return 0; }
static int ep_ident(EP *e, WCHAR *out, int cap)
{
    int n = 0; ep_ws(e);
    if (!(iswalpha(*e->p) || *e->p == L'_')) return 0;
    while ((iswalnum(*e->p) || *e->p == L'_') && n < cap - 1) out[n++] = *e->p++;
    out[n] = 0; return n;
}
static void ev_scalar(EV *r, float f) { memset(r, 0, sizeof *r); r->n = 1; r->v[0] = f; }
static void ev_any(EV *r, int n) { memset(r, 0, sizeof *r); r->n = n > 0 ? n : 1; r->any = 1; }
static int ev_isval(const EV *x) { return !x->obj && !x->xp && x->n >= 1; }
/* matrices travel as their floats in System.Numerics order: Matrix3x2 = 6 (M11 M12 M21 M22 M31 M32), Matrix4x4 = 16 (row major) */
static void m32_mul(const float *a, const float *b, float *r)
{
    float t[6];
    t[0] = a[0] * b[0] + a[1] * b[2];        t[1] = a[0] * b[1] + a[1] * b[3];
    t[2] = a[2] * b[0] + a[3] * b[2];        t[3] = a[2] * b[1] + a[3] * b[3];
    t[4] = a[4] * b[0] + a[5] * b[2] + b[4]; t[5] = a[4] * b[1] + a[5] * b[3] + b[5];
    memcpy(r, t, sizeof t);
}
static void m44_mul(const float *a, const float *b, float *r)
{
    float t[16]; int i, j, k;
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) { float s = 0; for (k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j]; t[i * 4 + j] = s; }
    memcpy(r, t, sizeof t);
}
static void m32_to_m44(const float *m, float *r)
{
    memset(r, 0, 16 * sizeof(float));
    r[0] = m[0]; r[1] = m[1]; r[4] = m[2]; r[5] = m[3]; r[10] = 1; r[12] = m[4]; r[13] = m[5]; r[15] = 1;
}
static int m44_inv(const float *m, float *r)      /* Gauss-Jordan; 0 = singular */
{
    double a[4][8]; int i, j, k;
    for (i = 0; i < 4; i++) for (j = 0; j < 8; j++) a[i][j] = j < 4 ? m[i * 4 + j] : (j - 4 == i ? 1.0 : 0.0);
    for (i = 0; i < 4; i++) {
        int piv = i; double d;
        for (k = i + 1; k < 4; k++) if (fabs(a[k][i]) > fabs(a[piv][i])) piv = k;
        if (fabs(a[piv][i]) < 1e-12) return 0;
        if (piv != i) for (j = 0; j < 8; j++) { double t = a[i][j]; a[i][j] = a[piv][j]; a[piv][j] = t; }
        d = a[i][i]; for (j = 0; j < 8; j++) a[i][j] /= d;
        for (k = 0; k < 4; k++) if (k != i) { double f = a[k][i]; if (f != 0) for (j = 0; j < 8; j++) a[k][j] -= f * a[i][j]; }
    }
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) r[i * 4 + j] = (float)a[i][j + 4];
    return 1;
}
static void quat_mul(const float *a, const float *b, float *r)      /* System.Numerics Quaternion.Multiply(a, b) */
{
    float t[4];
    t[0] = a[0] * b[3] + b[0] * a[3] + (a[1] * b[2] - a[2] * b[1]);
    t[1] = a[1] * b[3] + b[1] * a[3] + (a[2] * b[0] - a[0] * b[2]);
    t[2] = a[2] * b[3] + b[2] * a[3] + (a[0] * b[1] - a[1] * b[0]);
    t[3] = a[3] * b[3] - (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
    memcpy(r, t, sizeof t);
}
static int ev_bin(EV *r, const EV *a, const EV *b, WCHAR op)
{
    int n = a->n > b->n ? a->n : b->n, i;
    if (!ev_isval(a) || !ev_isval(b)) return 0;
    if (a->any || b->any) { ev_any(r, n); return 1; }
    memset(r, 0, sizeof *r);
    if (op == L'*' && a->n == 6 && b->n == 6) { r->n = 6; m32_mul(a->v, b->v, r->v); return 1; }
    if (op == L'*' && (a->n == 16 || b->n == 16) && (a->n == 16 || a->n == 6) && (b->n == 16 || b->n == 6)) {
        float x[16], y[16]; if (a->n == 6) m32_to_m44(a->v, x); else memcpy(x, a->v, sizeof x); if (b->n == 6) m32_to_m44(b->v, y); else memcpy(y, b->v, sizeof y);
        r->n = 16; m44_mul(x, y, r->v); return 1; }
    if (a->n != b->n && a->n != 1 && b->n != 1) return 0;
    r->n = n;
    for (i = 0; i < n; i++) { float x = a->v[a->n == 1 ? 0 : i], y = b->v[b->n == 1 ? 0 : i];
        r->v[i] = op == L'+' ? x + y : op == L'-' ? x - y : op == L'*' ? x * y : op == L'%' ? (y != 0 ? fmodf(x, y) : 0) : (y != 0 ? x / y : 0); }
    return 1;
}
static void ep_expr(EP *e, EV *r);
/* a property of `o` by path ("Translation.Y"), also when it lives in o.Properties */
static int prop_get2(CObj *o, const WCHAR *path, int *dim, float *v) { return comp_prop_get(o, path, dim, v) || (o->props && comp_prop_get(o->props, path, dim, v)); }
/* member of an object: a property it animates (Offset, Color, Scale ...) or an entry of its property set */
static int obj_member(CObj *o, const WCHAR *name, EV *r)
{
    int dim = 0; memset(r, 0, sizeof *r);
    if (comp_prop_get(o, name, &dim, r->v)) { r->n = dim; return 1; }
    if (o->props && comp_prop_get(o->props, name, &dim, r->v)) { r->n = dim; return 1; }
    return 0;
}
/* the object behind `name` of a foreign reference parameter (asked once per name) */
static CObj *par_resolve(EP *e, AnimPar *p, const WCHAR *name)
{
    int i; CObj *o;
    for (i = 0; i < p->nxc; i++) if (!wcscmp(p->xc[i].name, name)) return p->xc[i].obj;
    if (lstrlenW(name) >= 28 || !e->a) return NULL;
    o = comp_resolve_animation_object(e->a->o.comp, p->ext, name);
    if (p->nxc < 4) { lstrcpynW(p->xc[p->nxc].name, name, 28); p->xc[p->nxc].obj = o; p->nxc++; return o; }
    if (p->xc[3].obj) comp_release(p->xc[3].obj);            /* more than four members of one element: the last slot is reused */
    lstrcpynW(p->xc[3].name, name, 28); p->xc[3].obj = o; return o;
}
static int swizzle(EV *r, const WCHAR *m)
{
    EV s; int i, n = lstrlenW(m);
    if (!ev_isval(r)) return 0;
    if ((r->n == 6 || r->n == 16) && (n == 3) && (m[0] == L'_' || m[0] == L'M' || m[0] == L'm') && iswdigit(m[1]) && iswdigit(m[2])) {   /* matrix element */
        int row = m[1] - L'1', col = m[2] - L'1', w = r->n == 6 ? 2 : 4, h = r->n == 6 ? 3 : 4;
        if (row < 0 || row >= h || col < 0 || col >= w) return 0;
        ev_scalar(&s, r->v[row * w + col]); *r = s; return 1;
    }
    if (n < 1 || n > 4) return 0;
    memset(&s, 0, sizeof s); s.n = n;
    for (i = 0; i < n; i++) { WCHAR c = (WCHAR)towupper(m[i]); int k = c == L'X' ? 0 : c == L'Y' ? 1 : c == L'Z' ? 2 : c == L'W' ? 3 : -1;
        if (k < 0 || k >= r->n) return 0; s.v[i] = r->v[k]; }
    *r = s; return 1;
}
#define IS(s) (!_wcsicmp(id, L##s))
/* a function call with evaluated arguments; 0 = unknown function / wrong arguments */
static int ev_call(const WCHAR *id, EV *a, int n, EV *r)
{
    int i, m, anyarg = 0;
    for (i = 0; i < n; i++) { if (!ev_isval(&a[i])) return 0; anyarg |= a[i].any; }
    memset(r, 0, sizeof *r);
    /* constructors */
    if (!_wcsnicmp(id, L"Vector", 6) && id[6] >= L'2' && id[6] <= L'4' && !id[7]) { int d = id[6] - L'0', k = 0;
        if (anyarg) { ev_any(r, d); return 1; }
        r->n = d; for (i = 0; i < n; i++) { int j; for (j = 0; j < a[i].n && k < d; j++) r->v[k++] = a[i].v[j]; }
        if (k == 1) for (; k < d; k++) r->v[k] = r->v[0];
        return k == d; }
    if (IS("Quaternion") && n == 4) { if (anyarg) { ev_any(r, 4); return 1; } r->n = 4; for (i = 0; i < 4; i++) r->v[i] = a[i].v[0]; return 1; }
    if ((IS("ColorRgb") || IS("ColorArgb")) && n == 4) { if (anyarg) { ev_any(r, 4); return 1; } r->n = 4; for (i = 0; i < 4; i++) r->v[i] = a[i].v[0]; return 1; }   /* (a, r, g, b) = the layout colours are stored in */
    if (IS("ColorHsl") && n == 3) {                                            /* hue in degrees, saturation and lightness 0..1 */
        float h, s, l, c, x, mm, rr = 0, gg = 0, bb = 0; if (anyarg) { ev_any(r, 4); return 1; }
        h = fmodf(a[0].v[0], 360.0f); if (h < 0) h += 360.0f; s = a[1].v[0]; l = a[2].v[0];
        c = (1 - fabsf(2 * l - 1)) * s; x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1)); mm = l - c / 2;
        if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; } else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
        r->n = 4; r->v[0] = 255; r->v[1] = (rr + mm) * 255; r->v[2] = (gg + mm) * 255; r->v[3] = (bb + mm) * 255; return 1; }
    if (IS("Matrix3x2") && n == 6) { if (anyarg) { ev_any(r, 6); return 1; } r->n = 6; for (i = 0; i < 6; i++) r->v[i] = a[i].v[0]; return 1; }
    if (IS("Matrix4x4") && n == 16) { if (anyarg) { ev_any(r, 16); return 1; } r->n = 16; for (i = 0; i < 16; i++) r->v[i] = a[i].v[0]; return 1; }
    if (IS("Matrix4x4") && n == 1) { if (anyarg) { ev_any(r, 16); return 1; }      /* Matrix4x4(matrix3x2) widens; a 4x4 passes through */
        if (a[0].n == 6) { r->n = 16; m32_to_m44(a[0].v, r->v); return 1; } if (a[0].n == 16) { *r = a[0]; return 1; } return 0; }
    if (anyarg) {                                                               /* dry run: the name must exist, the result is a wildcard */
        static const WCHAR *const known[] = { L"Abs", L"Acos", L"Asin", L"Atan", L"Ceil", L"Ceiling", L"Clamp", L"ColorLerp", L"ColorLerpHsl", L"ColorLerpRgb", L"Concatenate", L"Conjugate",
            L"Cos", L"Distance", L"DistanceSquared", L"Exp", L"Floor", L"Inverse", L"Length", L"LengthSquared", L"Lerp", L"Ln", L"Log10", L"Max", L"Min", L"Mod", L"Normalize", L"Pow",
            L"Round", L"Scale", L"Sin", L"Slerp", L"Sqrt", L"Square", L"Tan", L"ToDegrees", L"ToRadians", L"Transform", NULL };
        for (i = 0; known[i]; i++) if (!_wcsicmp(id, known[i])) { ev_any(r, n ? a[0].n : 1); return 1; }
        return 0; }
    /* one argument, per component */
    if (n == 1) { static const WCHAR *const f1[] = { L"Abs", L"Floor", L"Ceil", L"Ceiling", L"Round", L"Sqrt", L"Sin", L"Cos", L"Tan", L"Asin", L"Acos", L"Atan", L"Ln", L"Log10", L"Exp", L"Square", L"ToDegrees", L"ToRadians", NULL };
        int k = -1; for (i = 0; f1[i]; i++) if (!_wcsicmp(id, f1[i])) { k = i; break; }
        if (k >= 0) { *r = a[0]; for (i = 0; i < r->n; i++) { float x = r->v[i];
                r->v[i] = k == 0 ? fabsf(x) : k == 1 ? floorf(x) : (k == 2 || k == 3) ? ceilf(x) : k == 4 ? floorf(x + 0.5f) : k == 5 ? sqrtf(x) : k == 6 ? sinf(x) : k == 7 ? cosf(x) : k == 8 ? tanf(x) :
                          k == 9 ? asinf(x) : k == 10 ? acosf(x) : k == 11 ? atanf(x) : k == 12 ? (x > 0 ? logf(x) : 0) : k == 13 ? (x > 0 ? log10f(x) : 0) : k == 14 ? expf(x) : k == 15 ? x * x :
                          k == 16 ? x * 57.2957795f : x * 0.0174532925f; } return 1; }
        if (IS("Length") || IS("LengthSquared")) { float s = 0; for (i = 0; i < a[0].n; i++) s += a[0].v[i] * a[0].v[i]; ev_scalar(r, IS("Length") ? sqrtf(s) : s); return 1; }
        if (IS("Normalize")) { float s = 0; *r = a[0]; for (i = 0; i < r->n; i++) s += r->v[i] * r->v[i]; s = sqrtf(s); if (s > 0) for (i = 0; i < r->n; i++) r->v[i] /= s; return 1; }
        if (IS("Conjugate") && a[0].n == 4) { *r = a[0]; r->v[0] = -r->v[0]; r->v[1] = -r->v[1]; r->v[2] = -r->v[2]; return 1; }
        if (IS("Inverse")) {
            if (a[0].n == 16) { r->n = 16; return m44_inv(a[0].v, r->v); }
            if (a[0].n == 6) { float x[16], y[16]; m32_to_m44(a[0].v, x); if (!m44_inv(x, y)) return 0; r->n = 6; r->v[0] = y[0]; r->v[1] = y[1]; r->v[2] = y[4]; r->v[3] = y[5]; r->v[4] = y[12]; r->v[5] = y[13]; return 1; }
            if (a[0].n == 4) { float s = 0; *r = a[0]; for (i = 0; i < 4; i++) s += r->v[i] * r->v[i]; if (s <= 0) return 0; r->v[0] = -r->v[0] / s; r->v[1] = -r->v[1] / s; r->v[2] = -r->v[2] / s; r->v[3] = r->v[3] / s; return 1; }
            return 0; }
        return 0; }
    if (n == 2) {
        if (IS("Min") || IS("Max") || IS("Pow") || IS("Mod")) { m = a[0].n > a[1].n ? a[0].n : a[1].n; r->n = m;
            if (a[0].n != a[1].n && a[0].n != 1 && a[1].n != 1) return 0;
            for (i = 0; i < m; i++) { float x = a[0].v[a[0].n == 1 ? 0 : i], y = a[1].v[a[1].n == 1 ? 0 : i];
                r->v[i] = IS("Min") ? (x < y ? x : y) : IS("Max") ? (x > y ? x : y) : IS("Pow") ? powf(x, y) : (y != 0 ? fmodf(x, y) : 0); } return 1; }
        if (IS("Distance") || IS("DistanceSquared")) { float s = 0; if (a[0].n != a[1].n) return 0; for (i = 0; i < a[0].n; i++) { float d = a[0].v[i] - a[1].v[i]; s += d * d; } ev_scalar(r, IS("Distance") ? sqrtf(s) : s); return 1; }
        if (IS("Scale")) { if (a[1].n != 1) return 0; *r = a[0]; for (i = 0; i < r->n; i++) r->v[i] *= a[1].v[0]; return 1; }
        if (IS("Concatenate") && a[0].n == 4 && a[1].n == 4) { r->n = 4; quat_mul(a[1].v, a[0].v, r->v); return 1; }      /* value2 applied after value1 */
        if (IS("Transform")) {
            if (a[0].n == 2 && a[1].n == 6) { const float *v = a[0].v, *t = a[1].v; r->n = 2; r->v[0] = v[0] * t[0] + v[1] * t[2] + t[4]; r->v[1] = v[0] * t[1] + v[1] * t[3] + t[5]; return 1; }
            if (a[0].n == 4 && a[1].n == 16) { int j; r->n = 4; for (j = 0; j < 4; j++) { float s = 0; for (i = 0; i < 4; i++) s += a[0].v[i] * a[1].v[i * 4 + j]; r->v[j] = s; } return 1; }
            return 0; }
        return 0; }
    if (n == 3) {
        if (IS("Clamp") || IS("Lerp") || IS("ColorLerp") || IS("ColorLerpRgb") || IS("ColorLerpHsl") || IS("Slerp")) { int clamp = IS("Clamp"); m = a[0].n; r->n = m;
            if ((a[1].n != m && a[1].n != 1) || (a[2].n != m && a[2].n != 1)) return 0;
            for (i = 0; i < m; i++) { float x = a[0].v[i], y = a[1].v[a[1].n == 1 ? 0 : i], z = a[2].v[a[2].n == 1 ? 0 : i];
                r->v[i] = clamp ? (x < y ? y : x > z ? z : x) : x + (y - x) * z; }
            if (IS("Slerp") && m == 4) { float s = 0; for (i = 0; i < 4; i++) s += r->v[i] * r->v[i]; s = sqrtf(s); if (s > 0) for (i = 0; i < 4; i++) r->v[i] /= s; }   /* normalised lerp */
            return 1; }
        return 0; }
    return 0;
}
/* Matrix3x2.CreateRotation(...), Vector3.One, Quaternion.Identity ... (after the dot) */
static void ep_static(EP *e, const WCHAR *id, EV *r)
{
    WCHAR fn[64]; EV a[4]; int n = 0, i, m44 = !_wcsicmp(id, L"Matrix4x4"), anyarg = 0;
    if (!ep_ident(e, fn, 64)) { FAIL(e, 1); return; }
    memset(r, 0, sizeof *r);
    if (!_wcsnicmp(id, L"Vector", 6)) { int d = id[6] - L'0'; float f = !_wcsicmp(fn, L"One") ? 1.0f : 0.0f;
        if (_wcsicmp(fn, L"One") && _wcsicmp(fn, L"Zero")) { FAIL(e, 1); return; } r->n = d; for (i = 0; i < d; i++) r->v[i] = f; return; }
    if (!_wcsicmp(fn, L"Identity")) {
        if (!_wcsicmp(id, L"Quaternion")) { r->n = 4; r->v[3] = 1; return; }
        r->n = m44 ? 16 : 6; if (m44) { r->v[0] = r->v[5] = r->v[10] = r->v[15] = 1; } else { r->v[0] = r->v[3] = 1; } return; }
    if (!ep_eat(e, L"(")) { FAIL(e, 1); return; }
    if (!ep_eat(e, L")")) { do { if (n == 4) { FAIL(e, 1); return; } ep_expr(e, &a[n++]); if (e->err) return; } while (ep_eat(e, L",")); if (!ep_eat(e, L")")) { FAIL(e, 1); return; } }
    for (i = 0; i < n; i++) { if (!ev_isval(&a[i])) { FAIL(e, 1); return; } anyarg |= a[i].any; }
    if (!_wcsicmp(id, L"Quaternion")) {
        if (!_wcsicmp(fn, L"CreateFromAxisAngle") && n == 2) { float s, l = 0; if (anyarg) { ev_any(r, 4); return; } if (a[0].n != 3) { FAIL(e, 1); return; }
            for (i = 0; i < 3; i++) l += a[0].v[i] * a[0].v[i]; l = sqrtf(l); s = sinf(a[1].v[0] * 0.5f); r->n = 4;
            for (i = 0; i < 3; i++) r->v[i] = l > 0 ? a[0].v[i] / l * s : 0; r->v[3] = cosf(a[1].v[0] * 0.5f); return; }
        FAIL(e, 1); return; }
    if (anyarg) { ev_any(r, m44 ? 16 : 6); return; }
    r->n = 6; r->v[0] = r->v[3] = 1;                           /* start from identity (3x2) */
    if (!_wcsicmp(fn, L"CreateFromTranslation") || !_wcsicmp(fn, L"CreateTranslation")) {
        if (n == 1 && a[0].n >= 2) { r->v[4] = a[0].v[0]; r->v[5] = a[0].v[1]; } else if (n >= 2) { r->v[4] = a[0].v[0]; r->v[5] = a[1].v[0]; } else { FAIL(e, 1); return; }
        if (m44) { float t[6], z = (n == 1 && a[0].n >= 3) ? a[0].v[2] : n == 3 ? a[2].v[0] : 0; memcpy(t, r->v, sizeof t); m32_to_m44(t, r->v); r->v[14] = z; r->n = 16; return; } }
    else if (!_wcsicmp(fn, L"CreateFromScale") || !_wcsicmp(fn, L"CreateScale")) {
        float z = 1;
        if (n == 1 && a[0].n == 1) { r->v[0] = r->v[3] = z = a[0].v[0]; } else if (n == 1 && a[0].n >= 2) { r->v[0] = a[0].v[0]; r->v[3] = a[0].v[1]; if (a[0].n >= 3) z = a[0].v[2]; }
        else if (n >= 2) { r->v[0] = a[0].v[0]; r->v[3] = a[1].v[0]; if (n == 3) z = a[2].v[0]; } else { FAIL(e, 1); return; }
        if (m44) { float t[6]; memcpy(t, r->v, sizeof t); m32_to_m44(t, r->v); r->v[10] = z; r->n = 16; return; } }
    else if (!_wcsicmp(fn, L"CreateFromRotation") || !_wcsicmp(fn, L"CreateRotation") || !_wcsicmp(fn, L"CreateRotationZ")) {      /* radians */
        float c, s; if (n < 1) { FAIL(e, 1); return; } c = cosf(a[0].v[0]); s = sinf(a[0].v[0]); r->v[0] = c; r->v[1] = s; r->v[2] = -s; r->v[3] = c; }
    else if (!_wcsicmp(fn, L"CreateSkew")) {                                                   /* radians */
        if (n < 2) { FAIL(e, 1); return; } r->v[1] = tanf(a[1].v[0]); r->v[2] = tanf(a[0].v[0]); }
    else { FAIL(e, 1); return; }
    if (m44) { float t[6]; memcpy(t, r->v, sizeof t); m32_to_m44(t, r->v); r->n = 16; }
}
static void ep_primary(EP *e, EV *r)
{
    WCHAR id[96]; int i;
    memset(r, 0, sizeof *r); ep_ws(e);
    if (iswdigit(*e->p) || (*e->p == L'.' && iswdigit(e->p[1]))) { WCHAR *end; double d = wcstod(e->p, &end); if (end == e->p) { FAIL(e, 1); return; } e->p = end; if (*e->p == L'f' || *e->p == L'F') e->p++; ev_scalar(r, (float)d); return; }
    if (ep_eat(e, L"(")) { if (++e->depth > 200) { FAIL(e, 1); return; } ep_expr(e, r); e->depth--; if (!e->err && !ep_eat(e, L")")) FAIL(e, 1); return; }
    if (!ep_ident(e, id, 96)) { FAIL(e, 1); return; }
    if (*e->p == L'.' && (!_wcsicmp(id, L"Matrix3x2") || !_wcsicmp(id, L"Matrix4x4") || !_wcsicmp(id, L"Quaternion") ||
                          (!_wcsnicmp(id, L"Vector", 6) && id[6] >= L'2' && id[6] <= L'4' && !id[7]))) { e->p++; ep_static(e, id, r); return; }
    ep_ws(e);
    if (*e->p == L'(') {                                       /* function call */
        EV a[16]; int n = 0; e->p++;
        if (++e->depth > 200) { FAIL(e, 1); return; }
        if (!ep_eat(e, L")")) { do { if (n == 16) { FAIL(e, 1); return; } ep_expr(e, &a[n++]); if (e->err) return; } while (ep_eat(e, L",")); if (!ep_eat(e, L")")) { FAIL(e, 1); return; } }
        e->depth--;
        if (!ev_call(id, a, n, r)) FAIL(e, 1);
        return;
    }
    if (!_wcsicmp(id, L"this")) {                              /* this.Target / this.StartingValue / this.CurrentValue */
        WCHAR m[64]; int d = 0;
        if (!ep_eat(e, L".") || !ep_ident(e, m, 64)) { FAIL(e, 1); return; }
        if (!_wcsicmp(m, L"Target")) { if (e->target) r->obj = e->target; else if (e->dry) ev_any(r, 1); else FAIL(e, 2); return; }
        if (!_wcsicmp(m, L"StartingValue") || !_wcsicmp(m, L"CurrentValue") || !_wcsicmp(m, L"FinalValue")) {
            if (e->dry) { ev_any(r, 1); return; }
            if (!_wcsicmp(m, L"StartingValue") && e->start && e->nstart > 0) { r->n = e->nstart; memcpy(r->v, e->start, (size_t)e->nstart * sizeof(float)); return; }
            if (e->target && e->prop && prop_get2(e->target, e->prop, &d, r->v)) { r->n = d; return; }
            FAIL(e, 2); return; }
        /* "this.TargetValue" (the Store's carousel animates its tracker's Position to it): a property of the target written without ".Target" */
        if (e->dry) { ev_any(r, 1); return; }
        if (e->target && obj_member(e->target, m, r)) return;
        { int d = 0; memset(r, 0, sizeof *r);     /* ... or of the animation's own Properties (where the Store keeps TargetValue) */
          if (e->a && e->a->o.props && comp_prop_get(e->a->o.props, m, &d, r->v)) { r->n = d; return; } }
        memset(r, 0, sizeof *r);
        if (e->a) for (i = 0; i < e->a->npar; i++) if (!_wcsicmp(e->a->par[i].name, m)) { AnimPar *p = &e->a->par[i];
            if (p->obj) r->obj = p->obj; else if (p->ext) r->xp = p; else if (p->n > 0) { r->n = p->n; memcpy(r->v, p->v, sizeof p->v); } else FAIL(e, 1); return; }
        { static LONG nl; if (InterlockedIncrement(&nl) <= 6) { char buf[600]; int l = 0; PSet *ps = e->target ? (PSet *)e->target->props : NULL;
            l += _snprintf(buf + l, sizeof buf - l, "this.%ls: target %s, its properties:", m, e->target ? g_cd[e->target->cls].sname : "none");
            for (i = 0; ps && i < ps->n && l < 500; i++) l += _snprintf(buf + l, sizeof buf - l, " %ls", ps->e[i].name);
            l += _snprintf(buf + l, sizeof buf - l, "; parameters:");
            for (i = 0; e->a && i < e->a->npar && l < 560; i++) l += _snprintf(buf + l, sizeof buf - l, " %ls", e->a->par[i].name);
            buf[sizeof buf - 1] = 0; comp_log("%s\n", buf); } }
        FAIL(e, 2); return;
    }
    if (!_wcsicmp(id, L"true") || !_wcsicmp(id, L"false")) { ev_scalar(r, id[0] == L't' || id[0] == L'T' ? 1.0f : 0.0f); return; }
    if (!_wcsicmp(id, L"Pi")) { ev_scalar(r, 3.14159265f); return; }
    if (e->a) for (i = 0; i < e->a->npar; i++) if (!_wcsicmp(e->a->par[i].name, id)) {
        AnimPar *p = &e->a->par[i];
        if (p->obj) r->obj = p->obj; else if (p->ext) r->xp = p; else if (p->n > 0) { r->n = p->n; memcpy(r->v, p->v, sizeof p->v); } else FAIL(e, 1);   /* a reference that could not be taken */
        return; }
    if (e->dry) { ev_any(r, 1); return; }
    FAIL(e, 1);
}
static void ep_postfix(EP *e, EV *r)
{
    WCHAR m[64]; ep_primary(e, r);
    while (!e->err) {
        const WCHAR *save = e->p; ep_ws(e);
        if (*e->p != L'.' || iswdigit(e->p[1])) { e->p = save; return; }
        e->p++; if (!ep_ident(e, m, 64)) { FAIL(e, 1); return; }
        if (r->any) { int n = lstrlenW(m), k, sw = n <= 4; for (k = 0; k < n && sw; k++) if (!wcschr(L"XYZWxyzw", m[k])) sw = 0; ev_any(r, sw ? n : 1); }
        else if (r->xp) { CObj *o = par_resolve(e, r->xp, m); if (!o || !obj_member(o, m, r)) { FAIL(e, 2); return; } }
        else if (r->obj) { CObj *o = r->obj; if (!obj_member(o, m, r)) { FAIL(e, 2); return; } }
        else if (!swizzle(r, m)) { FAIL(e, 1); return; }
    }
}
static void ep_unary(EP *e, EV *r)
{
    int i;
    if (ep_eat(e, L"-")) { ep_unary(e, r); if (e->err) return; if (!ev_isval(r)) { FAIL(e, 1); return; } for (i = 0; i < r->n; i++) r->v[i] = -r->v[i]; return; }
    if (ep_eat(e, L"+")) { ep_unary(e, r); return; }
    if (ep_eat(e, L"!")) { int any; ep_unary(e, r); if (e->err) return; if (!ev_isval(r)) { FAIL(e, 1); return; } any = r->any; ev_scalar(r, r->v[0] == 0 ? 1.0f : 0.0f); r->any = any; return; }
    ep_postfix(e, r);
}
static void ep_mul(EP *e, EV *r)
{
    ep_unary(e, r);
    while (!e->err) { EV b, t; WCHAR op; ep_ws(e); op = *e->p; if (op != L'*' && op != L'/' && op != L'%') return; e->p++; ep_unary(e, &b); if (e->err) return; if (!ev_bin(&t, r, &b, op)) { FAIL(e, 1); return; } *r = t; }
}
static void ep_add(EP *e, EV *r)
{
    ep_mul(e, r);
    while (!e->err) { EV b, t; WCHAR op; ep_ws(e); op = *e->p; if (op != L'+' && op != L'-') return; e->p++; ep_mul(e, &b); if (e->err) return; if (!ev_bin(&t, r, &b, op)) { FAIL(e, 1); return; } *r = t; }
}
static void ep_cmp(EP *e, EV *r)
{
    static const WCHAR *const ops[] = { L"<=", L">=", L"==", L"!=", L"<", L">" }; int k, i;
    ep_add(e, r); if (e->err) return;
    for (k = 0; k < 6; k++) if (ep_eat(e, ops[k])) { EV b; float x, y, res; int any;
        ep_add(e, &b); if (e->err) return; if (!ev_isval(r) || !ev_isval(&b)) { FAIL(e, 1); return; }
        any = r->any || b.any; x = r->v[0]; y = b.v[0];
        if (k == 2 || k == 3) { int n = r->n > b.n ? r->n : b.n, eq = 1; for (i = 0; i < n; i++) if (r->v[r->n == 1 ? 0 : i] != b.v[b.n == 1 ? 0 : i]) eq = 0; res = (float)(k == 2 ? eq : !eq); }
        else res = k == 0 ? x <= y : k == 1 ? x >= y : k == 4 ? x < y : x > y;
        ev_scalar(r, res); r->any = any; return; }
}
static void ep_and(EP *e, EV *r) { ep_cmp(e, r); while (!e->err && ep_eat(e, L"&&")) { EV b; int any; ep_cmp(e, &b); if (e->err) return; if (!ev_isval(r) || !ev_isval(&b)) { FAIL(e, 1); return; } any = r->any || b.any; ev_scalar(r, (r->v[0] != 0 && b.v[0] != 0) ? 1.0f : 0.0f); r->any = any; } }
static void ep_or(EP *e, EV *r) { ep_and(e, r); while (!e->err && ep_eat(e, L"||")) { EV b; int any; ep_and(e, &b); if (e->err) return; if (!ev_isval(r) || !ev_isval(&b)) { FAIL(e, 1); return; } any = r->any || b.any; ev_scalar(r, (r->v[0] != 0 || b.v[0] != 0) ? 1.0f : 0.0f); r->any = any; } }
static void ep_expr(EP *e, EV *r)
{
    ep_or(e, r);
    if (!e->err && ep_eat(e, L"?")) { EV a, b; float c = r->v[0]; int any = r->any; if (!ev_isval(r)) { FAIL(e, 1); return; }
        ep_expr(e, &a); if (e->err) return; if (!ep_eat(e, L":")) { FAIL(e, 1); return; } ep_expr(e, &b); if (e->err) return;
        *r = c != 0 ? a : b; if (any) { int n = a.n > b.n ? a.n : b.n; if (ev_isval(&a) && ev_isval(&b)) ev_any(r, n); } }
}
static DWORD text_hash(const WCHAR *q, DWORD seed) { DWORD h = 2166136261u ^ seed; for (; *q; q++) h = (h ^ *q) * 16777619u; return h; }
/* evaluates `text` with the parameters of `anim` for target.prop */
int comp_expr_eval_text(const WCHAR *text, CObj *anim, CObj *target, const WCHAR *prop, const float *start, int nstart, float *out, int *dim)
{
    EP e; EV r; static LONG nlog, nbad;
    if (!text) return -1;
    memset(&e, 0, sizeof e); e.p = text; e.a = (Anim *)anim; e.target = target; e.prop = prop; e.start = start; e.nstart = nstart;
    ep_expr(&e, &r); ep_ws(&e);
    if (!e.err && (*e.p || !ev_isval(&r))) e.err = 1;
    if (e.err) {
        static DWORD seen[1024]; static LONG nseen; DWORD hsh = text_hash(text, (DWORD)e.err); LONG k, ns = nseen; int dup = 0;   /* each distinct expression once */
        for (k = 0; k < ns && k < 1024; k++) if (seen[k] == hsh) { dup = 1; break; }
        if (!dup) { if (ns < 1024) { seen[ns] = hsh; nseen = ns + 1; }
            if (InterlockedIncrement(&nbad) <= 600) comp_log("expression %s (stops at '%.24ls'): %.1100ls -> %ls\n", e.err == 1 ? "not supported" : "waits for a member", e.p, text, prop ? prop : L"?"); }
        return e.err == 1 ? -1 : 0;
    }
    if (r.n == 6 && target && prop) {   /* a Matrix3x2 result for a Matrix4x4 property (Visual.TransformMatrix) is widened */
        float cur[16]; int td = 0;
        if (prop_get2(target, prop, &td, cur) && td == 16) { float t[6]; memcpy(t, r.v, sizeof t); m32_to_m44(t, r.v); r.n = 16; } }
    memcpy(out, r.v, sizeof r.v); *dim = r.n;
    if (nlog < 20 && InterlockedIncrement(&nlog) <= 20) comp_log("expression %.160ls -> %ls = %.3f %.3f %.3f %.3f (n %d)\n", text, prop ? prop : L"?", r.v[0], r.v[1], r.v[2], r.v[3], r.n);
    return 1;
}
int comp_expr_eval(CObj *anim, CObj *target, const WCHAR *prop, float *out, int *dim)
{
    Anim *a = (Anim *)anim; if (!a->expr) return -1;
    return comp_expr_eval_text(a->expr, anim, target, prop, NULL, 0, out, dim);
}
/* syntax check without objects (tools\native\exprcheck): unknown names and all members are wildcards. 0 = the text parses; else the
   offset where parsing stopped + 1 */
__declspec(dllexport) int WINAPI WxrDcomp_ExprCheck(const WCHAR *text)
{
    EP e; EV r;
    memset(&e, 0, sizeof e); e.p = text; e.dry = 1;
    ep_expr(&e, &r); ep_ws(&e);
    if (!e.err && (*e.p || !ev_isval(&r))) e.err = 1;
    return e.err ? (int)(e.p - text) + 1 : 0;
}

void comp_register_impls_eval(void)
{
    g_impl[CLS_AnimationPropertyInfo].size = sizeof(APInfo); g_impl[CLS_AnimationPropertyInfo].destroy = apinfo_destroy;
}
