#pragma once
/* comp_types.h - object model of the Windows.UI.Composition engine (hand-written).  Everything here is plain C. */
#include "comp_gen.h"
#include <math.h>
#include <stddef.h>

#define W(n) Windows_UI_Composition_##n
typedef Windows_Foundation_Numerics_Vector2 V2;
typedef Windows_Foundation_Numerics_Vector3 V3;
typedef Windows_Foundation_Numerics_Vector4 V4;
typedef Windows_Foundation_Numerics_Matrix3x2 M32;
typedef Windows_Foundation_Numerics_Matrix4x4 M44;
typedef Windows_Foundation_Numerics_Quaternion Quat;
typedef Windows_UI_Color Col;
typedef Windows_Foundation_TimeSpan TimeSpan;

#ifndef E_BOUNDS
#define E_BOUNDS ((HRESULT)0x8000000BL)
#endif
#define rt_log comp_log
#define OBJ_STATIC  1u      /* statically allocated / never freed (factories) */
#define OBJ_FACTORY 2u

struct Compositor;
struct ActiveAnim;
struct CObj {
    LONG ref; int cls; DWORD flags;
    struct CObj *comp;                 /* owning Compositor (strong ref), NULL for the Compositor itself */
    CView *views; int nviews; const IfaceEntry *ents;
    struct CObj *props;                /* lazily created CompositionPropertySet exposed as .Properties */
    struct CObj *implicit;             /* ImplicitAnimationCollection */
    WCHAR *comment;
};

typedef struct ClassImpl {
    size_t size;                       /* sizeof the leaf struct (only read for the class actually instantiated) */
    void (*init)(CObj *);              /* called base-first */
    void (*destroy)(CObj *);           /* called leaf-first, frees only this level's fields */
    HRESULT (*qi_extra)(CObj *, REFIID, void **);   /* non-metadata interfaces (interop) */
} ClassImpl;
extern ClassImpl g_impl[CLS__COUNT];
void comp_register_impls(void);

/* ---- core ---- */
CObj *comp_new(int cls, CObj *comp);                  /* ref=1, returns NULL on OOM */
HRESULT comp_qi(CObj *o, REFIID r, void **p);
int comp_is_a(int cls, int ancestor);
static __inline void *comp_view(CObj *o, REFIID iid) { void *p = NULL; if (SUCCEEDED(comp_qi(o, iid, &p))) { ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p); } return p; }
static __inline void comp_addref(CObj *o) { if (o) InterlockedIncrement(&o->ref); }
void comp_release(CObj *o);
#define OBJ_OF(p) (((CView *)(p))->obj)
#define CAST_OBJ(T, p) ((T *)OBJ_OF(p))
/* store the object behind interface pointer `iface` (which may be NULL) in *slot, AddRef/Release correctly */
void comp_assign(CObj **slot, CObj *newobj);
/* OBJ_OF for interfaces a caller passes in: NULL (logged) when the object is not one of ours */
CObj *comp_obj_of_any(void *iface, const char *what);
/* give back an interface of `o` (the one with this IID), AddRef'd */
HRESULT comp_return(CObj *o, REFIID iid, void **out);
HRESULT comp_return_default(CObj *o, void **out);

/* ---- compositor ---- */
typedef struct ActiveAnim {
    struct ActiveAnim *next; CObj *target; CObj *ctx; CObj *anim; WCHAR prop[64];
    LONGLONG t0; float start[16]; int have_start; CObj *batches[4]; int nb;
    CObj *ctl;                         /* AnimationController from TryGetAnimationController: paused = evaluated at its Progress */
    float last[16]; int have_last;     /* last value written: an unchanged value is not written again (a write invalidates = another frame) */
} ActiveAnim;
typedef struct AnimCtl { CObj o; float progress, rate; int paused; } AnimCtl;
typedef struct Compositor {
    CObj o; ActiveAnim *active; LONGLONG now; float rate; struct ExprBind *exprs; struct Tracker *trackers; LONG expr_gen;
    struct DTarget **targets; int ntargets, captargets;
    CObj *scoped[64]; int nscoped; int rendering; int dirty; CObj *commit; CObj *fireq[1024]; int nfire;
    CView desktopInterop, compInterop;
    CView dcview[48]; int dc_pending;                                    /* dcomp.dll only */
} Compositor;
LONGLONG comp_clock(void);                        /* 100ns units */
void comp_invalidate(CObj *comp);                 /* something changed: schedule a frame */
#define INVALIDATE(o) comp_invalidate((o)->comp)

/* ---- visuals ---- */
typedef struct Visual {
    CObj o;
    V2 anchor; int borderMode; V3 center; CObj *clip; int composite, backface; BYTE visible, hittest, snap;
    V3 offset; float opacity; Quat orient; struct Visual *parent; float rotation /*radians*/; V3 axis; V3 scale; V2 size; M44 tm;
    V3 relOffset; V2 relSize; struct Visual *parentForTransform;
    CView dcv, dcv3, dcvpl, dcvr; struct DcVis *dcx;                                /* dcomp.dll only: IDCompositionVisual views + private data (unused in own mode) */
} Visual;
typedef struct Container { Visual v; struct Visual **kids; int nkids, cap; CObj *coll; } Container;
typedef struct Sprite { Container c; CObj *brush; CObj *shadow; } Sprite;
typedef struct DcVis { CObj *rectgeo, *geoclip, *content, *surfbrush; void *xform /* HObj* transform object */; Visual *tparent; BYTE hasclip; } DcVis;
int dc_visual_free(Visual *v);
typedef struct VisualColl { CObj o; Container *owner; } VisualColl;

/* ---- brushes / clips / shapes ---- */
typedef struct ColorBrush { CObj o; Col color; } ColorBrush;
/* comp_brush2.c: XAML 19041 draws glyph runs and button faces/borders as MaskBrush(Source, Mask = surface or NineGrid) */
typedef struct MaskB { CObj o; CObj *src, *mask; } MaskB;
typedef struct NineG { CObj o; CObj *src; float l, t, r, b, ls, ts, rs, bs; boolean hollow; } NineG;
void comp_register_impls_brush2(void);
void comp_register_impls_effect(void);
/* comp_effect.c: the solid colour an effect brush (acrylic) is drawn with; 0 = draws nothing */
int comp_effect_brush_color(CObj *b, Col *out);
int comp_effect_layers(CObj *b, CObj **srcs, float *op, int max);          /* layered effect brush: sources bottom to top + opacities */
int comp_effect_prop(CObj *b, const WCHAR *path, int set, float *v);       /* 'OpacityEffect.Opacity' of a layered effect brush */
typedef struct Stop { CObj o; float offset; Col color; } Stop;
typedef struct StopColl { CObj o; Stop **s; UINT32 n, cap; } StopColl;
typedef struct GradBrush { CObj o; V2 anchor, center, offset, scale, start, end; float rotation; int extend, space, mapping; StopColl *stops; CObj *stopsObj; M32 tm; } GradBrush;
typedef struct Clip { CObj o; V2 anchor, center, offset, scale; float rotation; M32 tm; } Clip;      /* CompositionClip (ICompositionClip2 props) */
/* rect != 0: the clip was made by XAML 19041's private CreateRectangleClip ({D57563C7} slot 11) and is driven through Private::IRectangleClip:
   an absolute rectangle L,T,R,B in the visual's space with corner radii rad[0..3] = top-left, top-right, bottom-right, bottom-left */
typedef struct InsetClip { Clip c; float l, t, r, b; BYTE rect; float L, T, R, B; V2 rad[4]; } InsetClip;
typedef struct GeoClip { Clip c; CObj *geo; } GeoClip;

/* ---- shapes / geometry ---- */
typedef struct Shape { CObj o; V2 center, offset, scale; float rotation; M32 tm; } Shape;
typedef struct PCol { CObj o; CObj **s; UINT32 n, cap; } PCol;        /* pointer collection: CompositionShapeCollection */
typedef struct FCol { CObj o; float *v; UINT32 n, cap; } FCol;        /* CompositionStrokeDashArray */
typedef struct SpriteShape { Shape s; CObj *fill, *stroke, *geometry; float thickness, dashOffset, miter; int startCap, endCap, dashCap, lineJoin; BYTE nonScaling; CObj *dashes; } SpriteShape;
typedef struct ContainerShape { Shape s; CObj *shapes; } ContainerShape;
typedef struct Geometry { CObj o; float trimStart, trimEnd, trimOffset; } Geometry;
typedef struct RectGeo { Geometry g; V2 offset, size; } RectGeo;
typedef struct RRectGeo { Geometry g; V2 offset, size, radius; } RRectGeo;
typedef struct EllipseGeo { Geometry g; V2 center, radius; } EllipseGeo;
typedef struct LineGeo { Geometry g; V2 start, end; } LineGeo;
typedef struct PathGeo { Geometry g; CObj *path; } PathGeo;
typedef struct CPath { CObj o; void *geo; } CPath;                    /* geo: ID2D1PathGeometry (private copy in our D2D factory) */
typedef struct ShapeVisual { Container c; CObj *shapes; } ShapeVisual;

/* ---- surfaces ---- */
typedef struct DSurface {
    CObj o; int w, h; int pixfmt, alphamode; HDC dc; HBITMAP dib, olddib; void *bits; void *rt /* ID2D1DCRenderTarget */; BYTE drawing; RECT upd; LONG version;
    void *bmp /* ID2D1Bitmap */; void *bmp_owner; LONG bmp_ver; CView interop;
} DSurface;
typedef struct SBrush { CObj o; CObj *surface; int stretch, interp; float hr, vr; BYTE snap; V2 anchor, center, offset, scale; float rotation; M32 tm;
    IUnknown *xsurf; DWORD xcheck; } SBrush;   /* xsurf: a XAML surface (LoadedImageSurface) whose real surface did not exist yet; resolved at draw time */
int comp_is_foreign(void *iface);
void comp_log_stack(const char *what, void *obj);   /* diagnostics */
/* CompositionVisualSurface: a live picture of SourceVisual's subtree (SourceOffset, SourceSize; size 0 = the brush's box), rendered offscreen at draw time */
typedef struct VisSurf { CObj o; CObj *src; V2 offset, size; CView partner; V2 realization; int frozen; } VisSurf;
/* comp_effect.c: effect brushes whose graph is one Composite effect over named source parameters (WinUI animated icons' mattes/masks) */
int comp_effect_composite(CObj *b, UINT32 *mode, CObj **srcs, int max);   /* number of sources (0 = not such a brush) */
void comp_surface_draw(void *rt, SBrush *b, float w, float h, float opacity);                                   /* DrawBitmap into the sprite rect (0,0,w,h) */
void *comp_surface_make_brush(void *rt, SBrush *b, float x, float y, float w, float h, float opacity);         /* ID2D1BitmapBrush (Release it) or NULL */
void *comp_surface_bitmap(SBrush *b, int *w, int *h);                                                       /* ID2D1Bitmap1 (not AddRef'd) or NULL */
int comp_surface_census(SBrush *b, int *valid, LONG *version);                                              /* diagnostics: pixels with alpha != 0, -1 if unknown */
HRESULT comp_compositor_interop_qi(CObj *o, REFIID r, void **p);

/* ---- property sets / animation groups (comp_expr.c) ---- */
typedef struct PEntry { WCHAR name[48]; int kind; float v[16]; } PEntry;                 /* kind: 1 scalar 2 vec2 3 vec3 4 vec4 5 quat 6 colour(A,R,G,B) 7 m3x2 8 m4x4 9 bool */
typedef struct PSet { CObj o; PEntry *e; int n, cap; } PSet;
typedef struct AGroup { CObj o; CObj **a; int n, cap; } AGroup;
PEntry *comp_pset_find(CObj *pset, const WCHAR *name);
void comp_pset_put(CObj *pset, const WCHAR *name, int kind, const float *v, int n);   /* internal writer, invalidates on change */
void dcomp_manip_tick(CObj *comp);                 /* dcomp_manip.c: DirectManipulation transforms -> property sets */
int dcomp_manip_take_dirty(void);
void dcomp_set_commit_event(CObj *comp, HANDLE ev);  /* IDCompositionDeviceInternal::SetCommitCompletionEvent */
void dcomp_commit_done(CObj *comp);                /* dc_commit: signal that event one frame later */
int comp_pset_dim(int kind);

/* ---- animations ---- */
typedef struct KFrame { float t; float v[4]; CObj *ease; int expr; WCHAR *xs; } KFrame;   /* expr: expression key frame; xs = its text (evaluated while the animation runs; not evaluable -> the start value) */
/* Set*Parameter: obj = a composition object of ours; ext = a foreign IAnimationObject (a XAML UIElement), whose members are resolved to
   the composition object backing them with IAnimationObject::PopulatePropertyInfo (xc = the answers so far, obj NULL = "has no such
   property"); otherwise n floats */
typedef struct AnimPar { WCHAR name[64]; CObj *obj; IUnknown *ext; struct { WCHAR name[28]; CObj *obj; } xc[4]; int nxc; float v[16]; int n; } AnimPar;
typedef struct Anim {
    CObj o; int dim; int isColor; int isBool; KFrame *kf; int nkf, cap;
    INT64 duration, delay; int iterBehavior, iterCount, stopBehavior, direction, delayBehavior;
    WCHAR *expr; WCHAR *target;
    CView priv;                        /* {DDAABD43-...} keyframe-animation partner view (dcomp_visual.c) */
    AnimPar *par; int npar, cappar;
} Anim;
void comp_anim_par_clear(AnimPar *p);  /* drops what a parameter slot references */
/* a running ExpressionAnimation: re-evaluated every tick (comp_expr.c), the target written only when the value changes.
   bad: the text cannot be parsed (never retried). batches: scoped batches of type InfiniteAnimation it keeps open until it is stopped. */
typedef struct ExprBind { struct ExprBind *next; CObj *target; CObj *ctx; CObj *anim; WCHAR prop[64]; float last[16]; int have, bad; CObj *batches[4]; int nb; } ExprBind;
/* 1 = *out holds *dim floats; 0 = not evaluable now (a referenced property does not exist yet); -1 = the text is not supported */
int comp_expr_eval(CObj *anim, CObj *target, const WCHAR *prop, float *out, int *dim);
int comp_expr_eval_text(const WCHAR *text, CObj *anim, CObj *target, const WCHAR *prop, const float *start, int nstart, float *out, int *dim);
/* IAnimationObject: the composition object (ours) that backs `name` on a foreign animation object, AddRef'd, or NULL */
CObj *comp_resolve_animation_object(CObj *comp, IUnknown *ext, const WCHAR *name);
typedef struct APInfo { CObj o; int access; CObj *res; } APInfo;      /* AnimationPropertyInfo: res = the object that answered PopulatePropertyInfo */
void comp_batch_attach_list(CObj *comp, CObj **batches, int *nb, int typemask);
void comp_batch_detach_list(CObj *comp, CObj **batches, int *nb);
void comp_exprbind_free(CObj *comp, ExprBind *b);
void comp_register_impls_interact(void);
void comp_interact_tick(CObj *comp);              /* comp_interact.c: InteractionTracker state changes -> owner callbacks */
int comp_tracker_prop(CObj *o, const WCHAR *name, int set, int *dim, float *v);   /* InteractionTracker properties by name; 0 = not one */
typedef struct Easing { CObj o; float c1x, c1y, c2x, c2y; int steps; int initialStepSingle, finalStepSingle; } Easing;
float comp_ease(CObj *e, float t);

/* ---- animation engine (comp_anim.c) ---- */
HRESULT comp_start_animation(CObj *target, const WCHAR *name, CObj *anim);
HRESULT comp_stop_animation(CObj *target, const WCHAR *name);
void comp_tick(CObj *comp);                       /* advance all animations to now */
int comp_has_active(CObj *comp);
int comp_prop_set(CObj *target, const WCHAR *path, int dim, const float *v);
int comp_prop_get(CObj *target, const WCHAR *path, int *dim, float *v);

/* ---- batches (comp_batch.c) ---- */
void comp_batch_attach(CObj *comp, ActiveAnim *a);           /* a new animation joins the open scoped batches + the commit batch */
void comp_batch_detach(CObj *comp, ActiveAnim *a);           /* an animation finished / was stopped */
void comp_batch_frame(CObj *comp);                           /* once per frame: close the commit batch */
void comp_batch_fire(CObj *comp);                            /* deliver queued Completed events (end of tick) */
void comp_aa_free(CObj *comp, ActiveAnim *a);

/* ---- helpers ---- */
static __inline Col col_make(UINT8 a, UINT8 r, UINT8 g, UINT8 b) { Col c; c.A = a; c.R = r; c.G = g; c.B = b; return c; }
static __inline const WCHAR *hs(HSTRING s) { UINT32 n; const WCHAR *w = s ? WindowsGetStringRawBuffer(s, &n) : NULL; return w ? w : L""; }
HRESULT comp_dup_hstring(HSTRING s, WCHAR **out);

/* 2D affine, row-vector convention (same as D2D1_MATRIX_3X2_F): x' = a x + c y + e ; y' = b x + d y + f */
typedef struct Mat { float a, b, c, d, e, f; } Mat;
static __inline Mat mat_id(void) { Mat m = { 1, 0, 0, 1, 0, 0 }; return m; }
static __inline Mat mat_mul(Mat p, Mat q) {          /* apply p first, then q */
    Mat r; r.a = p.a * q.a + p.b * q.c; r.b = p.a * q.b + p.b * q.d; r.c = p.c * q.a + p.d * q.c; r.d = p.c * q.b + p.d * q.d;
    r.e = p.e * q.a + p.f * q.c + q.e; r.f = p.e * q.b + p.f * q.d + q.f; return r; }
static __inline Mat mat_translate(float x, float y) { Mat m = { 1, 0, 0, 1, x, y }; return m; }
static __inline Mat mat_scale(float x, float y) { Mat m = { x, 0, 0, y, 0, 0 }; return m; }
static __inline Mat mat_rotate(float rad) { float s = sinf(rad), c = cosf(rad); Mat m = { c, s, -s, c, 0, 0 }; return m; }
static __inline void mat_apply(const Mat *m, float x, float y, float *ox, float *oy) { *ox = m->a * x + m->c * y + m->e; *oy = m->b * x + m->d * y + m->f; }
int mat_invert(const Mat *m, Mat *out);
Mat visual_local_matrix(const Visual *v, float w, float h, float parentW, float parentH, float *effW, float *effH);
void visual_effective(const Visual *v, const Visual *parent, float *offx, float *offy, float *w, float *h);

/* ---- rendering / hosting (comp_render.c) ---- */
typedef struct DTarget DTarget;
void comp_targets_invalidate(CObj *comp);
