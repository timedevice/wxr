/* comp_shapes.c - ShapeVisual, shapes, geometries, CompositionPath, geometric clip, shape / dash collections */
#include "comp_types.h"
#include <d2d1.h>
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

static HRESULT create(void *This, int cls, const GUID *iid, void **out)
{
    CObj *o; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    o = comp_new(cls, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY;
    hr = comp_qi(o, iid, out); comp_release(o); return hr;
}
#define MK(Iface, fn, cls, Ret) HRESULT STDMETHODCALLTYPE CI_(Iface, fn)(W(Iface) *This, W(Ret) **r) { return create(This, cls, &IID_(Ret), (void **)r); }
MK(ICompositor5, CreateShapeVisual, CLS_ShapeVisual, IShapeVisual)
MK(ICompositor5, CreateContainerShape, CLS_CompositionContainerShape, ICompositionContainerShape)
MK(ICompositor5, CreateSpriteShape, CLS_CompositionSpriteShape, ICompositionSpriteShape)
MK(ICompositor5, CreateRectangleGeometry, CLS_CompositionRectangleGeometry, ICompositionRectangleGeometry)
MK(ICompositor5, CreateRoundedRectangleGeometry, CLS_CompositionRoundedRectangleGeometry, ICompositionRoundedRectangleGeometry)
MK(ICompositor5, CreateEllipseGeometry, CLS_CompositionEllipseGeometry, ICompositionEllipseGeometry)
MK(ICompositor5, CreateLineGeometry, CLS_CompositionLineGeometry, ICompositionLineGeometry)
MK(ICompositor5, CreatePathGeometry, CLS_CompositionPathGeometry, ICompositionPathGeometry)
MK(ICompositor6, CreateGeometricClip, CLS_CompositionGeometricClip, ICompositionGeometricClip)
HRESULT STDMETHODCALLTYPE CI_(ICompositor5, CreateSpriteShape_2)(W(ICompositor5) *This, W(ICompositionGeometry) *g, W(ICompositionSpriteShape) **r)
{ HRESULT hr = create(This, CLS_CompositionSpriteShape, &IID_(ICompositionSpriteShape), (void **)r); if (SUCCEEDED(hr) && g) comp_assign(&((SpriteShape *)OBJ_OF(*r))->geometry, OBJ_OF(g)); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor5, CreatePathGeometry_2)(W(ICompositor5) *This, W(ICompositionPath) *p, W(ICompositionPathGeometry) **r)
{ HRESULT hr = create(This, CLS_CompositionPathGeometry, &IID_(ICompositionPathGeometry), (void **)r); if (SUCCEEDED(hr) && p) comp_assign(&((PathGeo *)OBJ_OF(*r))->path, OBJ_OF(p)); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor6, CreateGeometricClip_2)(W(ICompositor6) *This, W(ICompositionGeometry) *g, W(ICompositionGeometricClip) **r)
{ HRESULT hr = create(This, CLS_CompositionGeometricClip, &IID_(ICompositionGeometricClip), (void **)r); if (SUCCEEDED(hr) && g) comp_assign(&((GeoClip *)OBJ_OF(*r))->geo, OBJ_OF(g)); return hr; }

/* ---------------- collections ---------------- */
static void pcol_destroy(CObj *o) { PCol *c = (PCol *)o; UINT32 i; for (i = 0; i < c->n; i++) comp_release(c->s[i]); if (c->s) HeapFree(GetProcessHeap(), 0, c->s); }
static void fcol_destroy(CObj *o) { FCol *c = (FCol *)o; if (c->v) HeapFree(GetProcessHeap(), 0, c->v); }
static HRESULT pcol_insert(PCol *c, UINT32 i, CObj *item, int need_cls)
{
    if (!item) return E_INVALIDARG; if (i > c->n) return E_BOUNDS; if (!comp_is_a(item->cls, need_cls)) return E_INVALIDARG;
    if (c->n == c->cap) {
        UINT32 nc = c->cap ? c->cap * 2 : 4; CObj **n = (CObj **)HeapAlloc(GetProcessHeap(), 0, nc * sizeof(CObj *)); if (!n) return E_OUTOFMEMORY;
        if (c->s) { memcpy(n, c->s, c->n * sizeof(CObj *)); HeapFree(GetProcessHeap(), 0, c->s); } c->s = n; c->cap = nc;
    }
    comp_addref(item); memmove(&c->s[i + 1], &c->s[i], (c->n - i) * sizeof(CObj *)); c->s[i] = item; c->n++; INVALIDATE(&c->o); return S_OK;
}
static void pcol_remove(PCol *c, UINT32 i) { comp_release(c->s[i]); memmove(&c->s[i], &c->s[i + 1], (c->n - i - 1) * sizeof(CObj *)); c->n--; INVALIDATE(&c->o); }

#define VECS Windows_Foundation_Collections_IVector_1__Windows_UI_Composition_CompositionShape
#define CIS(m) CI_Windows_Foundation_Collections_IVector_1__Windows_UI_Composition_CompositionShape_##m
#define PC(T) ((PCol *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CIS(GetAt)(VECS *This, UINT32 i, W(ICompositionShape) **r) { PCol *c = PC(This); if (i >= c->n) return E_BOUNDS; return comp_qi(c->s[i], &IID_(ICompositionShape), (void **)r); }
HRESULT STDMETHODCALLTYPE CIS(get_Size)(VECS *This, UINT32 *r) { *r = PC(This)->n; return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(IndexOf)(VECS *This, W(ICompositionShape) *v, UINT32 *idx, boolean *r) { PCol *c = PC(This); UINT32 i; *r = 0; *idx = 0; for (i = 0; i < c->n; i++) if (v && c->s[i] == OBJ_OF(v)) { *idx = i; *r = 1; break; } return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(SetAt)(VECS *This, UINT32 i, W(ICompositionShape) *v) { PCol *c = PC(This); if (i >= c->n) return E_BOUNDS; if (!v) return E_INVALIDARG; comp_assign(&c->s[i], OBJ_OF(v)); INVALIDATE(&c->o); return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(InsertAt)(VECS *This, UINT32 i, W(ICompositionShape) *v) { return pcol_insert(PC(This), i, v ? OBJ_OF(v) : NULL, CLS_CompositionShape); }
HRESULT STDMETHODCALLTYPE CIS(RemoveAt)(VECS *This, UINT32 i) { PCol *c = PC(This); if (i >= c->n) return E_BOUNDS; pcol_remove(c, i); return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(Append)(VECS *This, W(ICompositionShape) *v) { return pcol_insert(PC(This), PC(This)->n, v ? OBJ_OF(v) : NULL, CLS_CompositionShape); }
HRESULT STDMETHODCALLTYPE CIS(RemoveAtEnd)(VECS *This) { PCol *c = PC(This); if (!c->n) return E_BOUNDS; pcol_remove(c, c->n - 1); return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(Clear)(VECS *This) { PCol *c = PC(This); while (c->n) pcol_remove(c, c->n - 1); return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(GetMany)(VECS *This, UINT32 start, UINT32 cnt, W(ICompositionShape) **items, UINT32 *r)
{ PCol *c = PC(This); UINT32 i, n = 0; for (i = start; i < c->n && n < cnt; i++, n++) comp_qi(c->s[i], &IID_(ICompositionShape), (void **)&items[n]); *r = n; return S_OK; }
HRESULT STDMETHODCALLTYPE CIS(ReplaceAll)(VECS *This, UINT32 n, W(ICompositionShape) **items)
{ PCol *c = PC(This); UINT32 i; while (c->n) pcol_remove(c, c->n - 1); for (i = 0; i < n; i++) { HRESULT hr = pcol_insert(c, i, OBJ_OF(items[i]), CLS_CompositionShape); if (FAILED(hr)) return hr; } return S_OK; }

#define VECF Windows_Foundation_Collections_IVector_1__float
#define CIF(m) CI_Windows_Foundation_Collections_IVector_1__float_##m
#define FC(T) ((FCol *)OBJ_OF(T))
static HRESULT fcol_insert(FCol *c, UINT32 i, float v)
{
    if (i > c->n) return E_BOUNDS;
    if (c->n == c->cap) { UINT32 nc = c->cap ? c->cap * 2 : 4; float *n = (float *)HeapAlloc(GetProcessHeap(), 0, nc * sizeof(float)); if (!n) return E_OUTOFMEMORY;
        if (c->v) { memcpy(n, c->v, c->n * sizeof(float)); HeapFree(GetProcessHeap(), 0, c->v); } c->v = n; c->cap = nc; }
    memmove(&c->v[i + 1], &c->v[i], (c->n - i) * sizeof(float)); c->v[i] = v; c->n++; INVALIDATE(&c->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE CIF(GetAt)(VECF *This, UINT32 i, float *r) { FCol *c = FC(This); if (i >= c->n) return E_BOUNDS; *r = c->v[i]; return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(get_Size)(VECF *This, UINT32 *r) { *r = FC(This)->n; return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(IndexOf)(VECF *This, float v, UINT32 *idx, boolean *r) { FCol *c = FC(This); UINT32 i; *r = 0; *idx = 0; for (i = 0; i < c->n; i++) if (c->v[i] == v) { *idx = i; *r = 1; break; } return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(SetAt)(VECF *This, UINT32 i, float v) { FCol *c = FC(This); if (i >= c->n) return E_BOUNDS; c->v[i] = v; INVALIDATE(&c->o); return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(InsertAt)(VECF *This, UINT32 i, float v) { return fcol_insert(FC(This), i, v); }
HRESULT STDMETHODCALLTYPE CIF(RemoveAt)(VECF *This, UINT32 i) { FCol *c = FC(This); if (i >= c->n) return E_BOUNDS; memmove(&c->v[i], &c->v[i + 1], (c->n - i - 1) * sizeof(float)); c->n--; INVALIDATE(&c->o); return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(Append)(VECF *This, float v) { return fcol_insert(FC(This), FC(This)->n, v); }
HRESULT STDMETHODCALLTYPE CIF(RemoveAtEnd)(VECF *This) { FCol *c = FC(This); if (!c->n) return E_BOUNDS; c->n--; INVALIDATE(&c->o); return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(Clear)(VECF *This) { FC(This)->n = 0; INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(GetMany)(VECF *This, UINT32 start, UINT32 cnt, float *items, UINT32 *r) { FCol *c = FC(This); UINT32 i, n = 0; for (i = start; i < c->n && n < cnt; i++, n++) items[n] = c->v[i]; *r = n; return S_OK; }
HRESULT STDMETHODCALLTYPE CIF(ReplaceAll)(VECF *This, UINT32 n, float *items) { FCol *c = FC(This); UINT32 i; c->n = 0; for (i = 0; i < n; i++) { HRESULT hr = fcol_insert(c, i, items[i]); if (FAILED(hr)) return hr; } return S_OK; }

/* ---------------- ShapeVisual ---------------- */
static void shapevisual_destroy(CObj *o) { ShapeVisual *s = (ShapeVisual *)o; if (s->shapes) comp_release(s->shapes); }
HRESULT STDMETHODCALLTYPE CI_(IShapeVisual, get_Shapes)(W(IShapeVisual) *This, VECS **r)
{
    ShapeVisual *s = (ShapeVisual *)OBJ_OF(This); if (!r) return E_POINTER;
    if (!s->shapes) { s->shapes = comp_new(CLS_CompositionShapeCollection, s->c.v.o.comp); if (!s->shapes) return E_OUTOFMEMORY; }
    return comp_qi(s->shapes, &IID_Windows_Foundation_Collections_IVector_1__Windows_UI_Composition_CompositionShape, (void **)r);
}
HRESULT STDMETHODCALLTYPE CI_(IShapeVisual, get_ViewBox)(W(IShapeVisual) *This, W(ICompositionViewBox) **r) { (void)This; *r = NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(IShapeVisual, put_ViewBox)(W(IShapeVisual) *This, W(ICompositionViewBox) *v) { (void)This; if (v) return comp_unimpl("IShapeVisual.put_ViewBox"); return S_OK; }

/* ---------------- shapes ---------------- */
static void shape_init(CObj *o) { Shape *s = (Shape *)o; s->scale.X = s->scale.Y = 1.0f; s->tm.M11 = s->tm.M22 = 1.0f; }
static void spriteshape_init(CObj *o) { SpriteShape *s = (SpriteShape *)o; s->thickness = 1.0f; s->miter = 1.0f; }
static void spriteshape_destroy(CObj *o) { SpriteShape *s = (SpriteShape *)o; if (s->fill) comp_release(s->fill); if (s->stroke) comp_release(s->stroke); if (s->geometry) comp_release(s->geometry); if (s->dashes) comp_release(s->dashes); }
static void containershape_destroy(CObj *o) { ContainerShape *s = (ContainerShape *)o; if (s->shapes) comp_release(s->shapes); }
#define SH(T) ((Shape *)OBJ_OF(T))
#define SPROP(name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionShape, get_##name)(W(ICompositionShape) *This, type *r) { *r = SH(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionShape, put_##name)(W(ICompositionShape) *This, type v) { SH(This)->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
SPROP(CenterPoint, V2, center) SPROP(Offset, V2, offset) SPROP(RotationAngle, float, rotation) SPROP(Scale, V2, scale) SPROP(TransformMatrix, M32, tm)
HRESULT STDMETHODCALLTYPE CI_(ICompositionShape, get_RotationAngleInDegrees)(W(ICompositionShape) *This, float *r) { *r = SH(This)->rotation * (180.0f / 3.14159265358979f); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionShape, put_RotationAngleInDegrees)(W(ICompositionShape) *This, float v) { SH(This)->rotation = v * (3.14159265358979f / 180.0f); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionContainerShape, get_Shapes)(W(ICompositionContainerShape) *This, VECS **r)
{
    ContainerShape *s = (ContainerShape *)OBJ_OF(This); if (!r) return E_POINTER;
    if (!s->shapes) { s->shapes = comp_new(CLS_CompositionShapeCollection, s->s.o.comp); if (!s->shapes) return E_OUTOFMEMORY; }
    return comp_qi(s->shapes, &IID_Windows_Foundation_Collections_IVector_1__Windows_UI_Composition_CompositionShape, (void **)r);
}
#define SS(T) ((SpriteShape *)OBJ_OF(T))
#define BRUSH_PROP(name, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, get_##name)(W(ICompositionSpriteShape) *This, W(ICompositionBrush) **r) { return comp_return(SS(This)->field, &IID_(ICompositionBrush), (void **)r); } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, put_##name)(W(ICompositionSpriteShape) *This, W(ICompositionBrush) *v) { comp_assign(&SS(This)->field, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
BRUSH_PROP(FillBrush, fill) BRUSH_PROP(StrokeBrush, stroke)
HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, get_Geometry)(W(ICompositionSpriteShape) *This, W(ICompositionGeometry) **r) { return comp_return(SS(This)->geometry, &IID_(ICompositionGeometry), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, put_Geometry)(W(ICompositionSpriteShape) *This, W(ICompositionGeometry) *v) { comp_assign(&SS(This)->geometry, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
#define SSP(name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, get_##name)(W(ICompositionSpriteShape) *This, type *r) { *r = (type)SS(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, put_##name)(W(ICompositionSpriteShape) *This, type v) { SS(This)->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
SSP(IsStrokeNonScaling, boolean, nonScaling) SSP(StrokeDashOffset, float, dashOffset) SSP(StrokeMiterLimit, float, miter) SSP(StrokeThickness, float, thickness)
SSP(StrokeDashCap, Windows_UI_Composition_CompositionStrokeCap, dashCap) SSP(StrokeStartCap, Windows_UI_Composition_CompositionStrokeCap, startCap)
SSP(StrokeEndCap, Windows_UI_Composition_CompositionStrokeCap, endCap) SSP(StrokeLineJoin, Windows_UI_Composition_CompositionStrokeLineJoin, lineJoin)
HRESULT STDMETHODCALLTYPE CI_(ICompositionSpriteShape, get_StrokeDashArray)(W(ICompositionSpriteShape) *This, VECF **r)
{
    SpriteShape *s = SS(This); if (!r) return E_POINTER;
    if (!s->dashes) { s->dashes = comp_new(CLS_CompositionStrokeDashArray, s->s.o.comp); if (!s->dashes) return E_OUTOFMEMORY; }
    return comp_qi(s->dashes, &IID_Windows_Foundation_Collections_IVector_1__float, (void **)r);
}

/* ---------------- geometries ---------------- */
static void geometry_init(CObj *o) { ((Geometry *)o)->trimEnd = 1.0f; }
#define GP(name, field) \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionGeometry, get_##name)(W(ICompositionGeometry) *This, float *r) { *r = ((Geometry *)OBJ_OF(This))->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(ICompositionGeometry, put_##name)(W(ICompositionGeometry) *This, float v) { ((Geometry *)OBJ_OF(This))->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
GP(TrimEnd, trimEnd) GP(TrimOffset, trimOffset) GP(TrimStart, trimStart)
#define GV2(Iface, Type, name, field) \
 HRESULT STDMETHODCALLTYPE CI_(Iface, get_##name)(W(Iface) *This, V2 *r) { *r = ((Type *)OBJ_OF(This))->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(Iface, put_##name)(W(Iface) *This, V2 v) { ((Type *)OBJ_OF(This))->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
GV2(ICompositionRectangleGeometry, RectGeo, Offset, offset) GV2(ICompositionRectangleGeometry, RectGeo, Size, size)
GV2(ICompositionRoundedRectangleGeometry, RRectGeo, Offset, offset) GV2(ICompositionRoundedRectangleGeometry, RRectGeo, Size, size) GV2(ICompositionRoundedRectangleGeometry, RRectGeo, CornerRadius, radius)
GV2(ICompositionEllipseGeometry, EllipseGeo, Center, center) GV2(ICompositionEllipseGeometry, EllipseGeo, Radius, radius)
GV2(ICompositionLineGeometry, LineGeo, Start, start) GV2(ICompositionLineGeometry, LineGeo, End, end)
HRESULT STDMETHODCALLTYPE CI_(ICompositionPathGeometry, get_Path)(W(ICompositionPathGeometry) *This, W(ICompositionPath) **r) { return comp_return(((PathGeo *)OBJ_OF(This))->path, &IID_(ICompositionPath), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionPathGeometry, put_Path)(W(ICompositionPathGeometry) *This, W(ICompositionPath) *v) { comp_assign(&((PathGeo *)OBJ_OF(This))->path, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
static void pathgeo_destroy(CObj *o) { PathGeo *p = (PathGeo *)o; if (p->path) comp_release(p->path); }

/* ---------------- CompositionPath: a private copy of the source ID2D1Geometry ---------------- */
ID2D1Factory *comp_d2d_factory(void);                       /* comp_render.c */
static const GUID IID_IGeometrySource2DInterop_ = {0x0657AF73, 0x53FD, 0x47CF, {0x84, 0xFF, 0xC8, 0x49, 0x2D, 0x2A, 0x80, 0xA3}};
typedef struct GSInteropVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *GetGeometry)(void *, ID2D1Geometry **); HRESULT (STDMETHODCALLTYPE *TryGetGeometryUsingFactory)(void *, ID2D1Factory *, ID2D1Geometry **); } GSInteropVtbl;
typedef struct GSInterop { const GSInteropVtbl *lpVtbl; } GSInterop;
static void path_destroy(CObj *o) { CPath *p = (CPath *)o; if (p->geo) ID2D1PathGeometry_Release((ID2D1PathGeometry *)p->geo); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionPathFactory, Create)(W(ICompositionPathFactory) *This, Windows_Graphics_IGeometrySource2D *src, W(ICompositionPath) **r)
{
    GSInterop *in = NULL; ID2D1Geometry *g = NULL; ID2D1Factory *f = comp_d2d_factory(); HRESULT hr; CPath *p; ID2D1PathGeometry *pg = NULL; ID2D1GeometrySink *sink = NULL;
    (void)This; if (!r) return E_POINTER; *r = NULL; if (!src) return E_INVALIDARG; if (!f) return E_FAIL;
    hr = ((IUnknown *)src)->lpVtbl->QueryInterface((IUnknown *)src, &IID_IGeometrySource2DInterop_, (void **)&in);
    if (FAILED(hr)) { comp_log("CompositionPath: source has no IGeometrySource2DInterop (0x%08lX)\n", (unsigned long)hr); return hr; }
    hr = in->lpVtbl->TryGetGeometryUsingFactory(in, f, &g); if (FAILED(hr) || !g) { g = NULL; hr = in->lpVtbl->GetGeometry(in, &g); }
    if (FAILED(hr) || !g) { in->lpVtbl->Release((IUnknown *)in); return FAILED(hr) ? hr : E_FAIL; }
    /* copy into our own factory so Direct2D can use it with our render target */
    hr = ID2D1Factory_CreatePathGeometry(f, &pg);
    if (SUCCEEDED(hr)) hr = ID2D1PathGeometry_Open(pg, &sink);
    if (SUCCEEDED(hr)) { hr = g->lpVtbl->Simplify(g, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES, NULL, 0.25f, (ID2D1SimplifiedGeometrySink *)sink); ID2D1GeometrySink_Close(sink); ID2D1GeometrySink_Release(sink); }
    ID2D1Geometry_Release(g); in->lpVtbl->Release((IUnknown *)in);
    if (FAILED(hr)) { if (pg) ID2D1PathGeometry_Release(pg); return hr; }
    { CObj *o = comp_new(CLS_CompositionPath, NULL); if (!o) { ID2D1PathGeometry_Release(pg); return E_OUTOFMEMORY; } p = (CPath *)o; p->geo = pg;
      hr = comp_qi(o, &IID_(ICompositionPath), (void **)r); comp_release(o); }
    return hr;
}

/* ---------------- geometric clip ---------------- */
static void geoclip_destroy(CObj *o) { GeoClip *g = (GeoClip *)o; if (g->geo) comp_release(g->geo); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGeometricClip, get_Geometry)(W(ICompositionGeometricClip) *This, W(ICompositionGeometry) **r) { return comp_return(((GeoClip *)OBJ_OF(This))->geo, &IID_(ICompositionGeometry), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGeometricClip, put_Geometry)(W(ICompositionGeometricClip) *This, W(ICompositionGeometry) *v) { comp_assign(&((GeoClip *)OBJ_OF(This))->geo, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGeometricClip, get_ViewBox)(W(ICompositionGeometricClip) *This, W(ICompositionViewBox) **r) { (void)This; *r = NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGeometricClip, put_ViewBox)(W(ICompositionGeometricClip) *This, W(ICompositionViewBox) *v) { (void)This; if (v) return comp_unimpl("ICompositionGeometricClip.put_ViewBox"); return S_OK; }

void comp_register_impls_shapes(void)
{
    g_impl[CLS_CompositionShape].init = shape_init;
    g_impl[CLS_CompositionSpriteShape].size = sizeof(SpriteShape); g_impl[CLS_CompositionSpriteShape].init = spriteshape_init; g_impl[CLS_CompositionSpriteShape].destroy = spriteshape_destroy;
    g_impl[CLS_CompositionContainerShape].size = sizeof(ContainerShape); g_impl[CLS_CompositionContainerShape].destroy = containershape_destroy;
    g_impl[CLS_CompositionShapeCollection].size = sizeof(PCol); g_impl[CLS_CompositionShapeCollection].destroy = pcol_destroy;
    g_impl[CLS_CompositionStrokeDashArray].size = sizeof(FCol); g_impl[CLS_CompositionStrokeDashArray].destroy = fcol_destroy;
    g_impl[CLS_ShapeVisual].size = sizeof(ShapeVisual); g_impl[CLS_ShapeVisual].destroy = shapevisual_destroy;
    g_impl[CLS_CompositionGeometry].init = geometry_init;
    g_impl[CLS_CompositionRectangleGeometry].size = sizeof(RectGeo);
    g_impl[CLS_CompositionRoundedRectangleGeometry].size = sizeof(RRectGeo);
    g_impl[CLS_CompositionEllipseGeometry].size = sizeof(EllipseGeo);
    g_impl[CLS_CompositionLineGeometry].size = sizeof(LineGeo);
    g_impl[CLS_CompositionPathGeometry].size = sizeof(PathGeo); g_impl[CLS_CompositionPathGeometry].destroy = pathgeo_destroy;
    g_impl[CLS_CompositionPath].size = sizeof(CPath); g_impl[CLS_CompositionPath].destroy = path_destroy;
    g_impl[CLS_CompositionGeometricClip].size = sizeof(GeoClip); g_impl[CLS_CompositionGeometricClip].destroy = geoclip_destroy;
    g_impl[CLS_InsetClip].size = sizeof(InsetClip);
}
