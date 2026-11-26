/* comp_render.c - DesktopWindowTarget, ICompositorDesktopInterop, Direct2D 1.0 renderer */
#include "comp_types.h"
#include <d2d1.h>
#include <float.h>

#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n
#define TIMER_ID 0x7C01
#define FRAME_MS 16

struct DTarget {
    CObj o; HWND hwnd; BOOL topmost; Visual *root; WNDPROC oldproc; BOOL subclassed;
    ID2D1HwndRenderTarget *rt; ID2D1SolidColorBrush *solid; UINT w, h; BOOL timer; BOOL in_paint;
};
static const GUID IID_ID2D1Factory_ = {0x06152247,0x6f50,0x465a,{0x92,0x45,0x11,0x8b,0xfd,0x3b,0x60,0x07}};
#define IID_ID2D1Factory IID_ID2D1Factory_
static ID2D1Factory *g_d2d; static BOOL g_d2d_tried;
static const WCHAR PROP_TARGET[] = L"wxr.comptarget";

static ID2D1Factory *get_factory(void)
{
    if (!g_d2d && !g_d2d_tried) {
        HRESULT (WINAPI *create)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS *, void **); HMODULE m = LoadLibraryW(L"d2d1.dll"); HRESULT hr = E_FAIL;
        g_d2d_tried = TRUE;
        create = m ? (HRESULT (WINAPI *)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS *, void **))GetProcAddress(m, "D2D1CreateFactory") : NULL;
        if (create) hr = create(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&g_d2d);
        comp_log("Direct2D factory: %s (0x%08lX)\n", g_d2d ? "ok" : "unavailable", (unsigned long)hr);
    }
    return g_d2d;
}

/* ============================ renderer ============================ */
static void release_rt(DTarget *t)
{
    if (t->solid) { ID2D1SolidColorBrush_Release(t->solid); t->solid = NULL; }
    if (t->rt) { ID2D1HwndRenderTarget_Release(t->rt); t->rt = NULL; }
}
static BOOL ensure_rt(DTarget *t)
{
    RECT rc; ID2D1Factory *f; D2D1_RENDER_TARGET_PROPERTIES rp; D2D1_HWND_RENDER_TARGET_PROPERTIES hp; D2D1_COLOR_F white = { 1, 1, 1, 1 }; HRESULT hr;
    if (t->rt) return TRUE;
    f = get_factory(); if (!f || !IsWindow(t->hwnd)) return FALSE;
    GetClientRect(t->hwnd, &rc); t->w = (UINT)(rc.right > 0 ? rc.right : 1); t->h = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
    memset(&rp, 0, sizeof rp); rp.type = D2D1_RENDER_TARGET_TYPE_DEFAULT; rp.pixelFormat.format = DXGI_FORMAT_UNKNOWN; rp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
    rp.usage = D2D1_RENDER_TARGET_USAGE_NONE; rp.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    hp.hwnd = t->hwnd; hp.pixelSize.width = t->w; hp.pixelSize.height = t->h; hp.presentOptions = D2D1_PRESENT_OPTIONS_NONE;
    hr = ID2D1Factory_CreateHwndRenderTarget(f, &rp, &hp, &t->rt);
    if (FAILED(hr)) { comp_log("CreateHwndRenderTarget failed 0x%08lX\n", (unsigned long)hr); t->rt = NULL; return FALSE; }
    hr = ID2D1HwndRenderTarget_CreateSolidColorBrush(t->rt, &white, NULL, &t->solid);
    if (FAILED(hr)) { release_rt(t); return FALSE; }
    return TRUE;
}
static D2D1_MATRIX_3X2_F to_d2d(Mat m) { D2D1_MATRIX_3X2_F r; r._11 = m.a; r._12 = m.b; r._21 = m.c; r._22 = m.d; r._31 = m.e; r._32 = m.f; return r; }
static D2D1_COLOR_F to_color(Col c, float opacity) { D2D1_COLOR_F r; r.r = c.R / 255.0f; r.g = c.G / 255.0f; r.b = c.B / 255.0f; r.a = c.A / 255.0f * opacity; return r; }
static int stop_cmp(const void *a, const void *b) { float x = (*(Stop *const *)a)->offset, y = (*(Stop *const *)b)->offset; return x < y ? -1 : x > y; }
ID2D1Factory *comp_d2d_factory(void) { return get_factory(); }

/* Build a D2D brush for composition brush `b` covering the box (x,y,w,h) in the current drawing space.  *tmp = 1 when the caller must Release it. */
static ID2D1Brush *make_brush(DTarget *t, CObj *b, float x, float y, float w, float h, float opacity, int *tmp)
{
    ID2D1HwndRenderTarget *rt = t->rt; *tmp = 0;
    if (!b) return NULL;
    if (b->cls == CLS_CompositionColorBrush) {
        D2D1_COLOR_F c = to_color(((ColorBrush *)b)->color, opacity); ID2D1SolidColorBrush_SetColor(t->solid, &c); return (ID2D1Brush *)t->solid;
    }
    if (b->cls == CLS_CompositionLinearGradientBrush) {
        GradBrush *g = (GradBrush *)b; StopColl *sc = g->stops; D2D1_GRADIENT_STOP gs[32]; Stop *sorted[32]; UINT32 i, n;
        ID2D1GradientStopCollection *coll = NULL; ID2D1LinearGradientBrush *lb = NULL; D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp; D2D1_BRUSH_PROPERTIES bp;
        if (!sc || sc->n == 0) return NULL;
        n = sc->n > 32 ? 32 : sc->n; for (i = 0; i < n; i++) sorted[i] = sc->s[i]; qsort(sorted, n, sizeof(Stop *), stop_cmp);
        for (i = 0; i < n; i++) { gs[i].position = sorted[i]->offset; gs[i].color = to_color(sorted[i]->color, opacity); }
        if (FAILED(rt->lpVtbl->Base.CreateGradientStopCollection((ID2D1RenderTarget *)rt, gs, n, D2D1_GAMMA_2_2, g->extend == 1 ? D2D1_EXTEND_MODE_WRAP : g->extend == 2 ? D2D1_EXTEND_MODE_MIRROR : D2D1_EXTEND_MODE_CLAMP, &coll))) return NULL;
        if (g->mapping == 1) { lp.startPoint.x = g->start.X; lp.startPoint.y = g->start.Y; lp.endPoint.x = g->end.X; lp.endPoint.y = g->end.Y; }     /* Absolute */
        else { lp.startPoint.x = x + g->start.X * w; lp.startPoint.y = y + g->start.Y * h; lp.endPoint.x = x + g->end.X * w; lp.endPoint.y = y + g->end.Y * h; } /* Relative */
        bp.opacity = 1.0f; bp.transform._11 = 1; bp.transform._12 = 0; bp.transform._21 = 0; bp.transform._22 = 1; bp.transform._31 = 0; bp.transform._32 = 0;
        if (SUCCEEDED(ID2D1HwndRenderTarget_CreateLinearGradientBrush(rt, &lp, &bp, coll, &lb))) *tmp = 1;
        ID2D1GradientStopCollection_Release(coll); return (ID2D1Brush *)lb;
    }
    if (b->cls == CLS_CompositionSurfaceBrush) { void *r = comp_surface_make_brush(rt, (SBrush *)b, x, y, w, h, opacity); if (r) *tmp = 1; return (ID2D1Brush *)r; }
    { static LONG warned[CLS__COUNT]; if (!InterlockedExchange(&warned[b->cls], 1)) comp_log("render: brush class %s not drawn yet\n", g_cd[b->cls].sname); }
    return NULL;
}
static void fill_sprite(DTarget *t, Sprite *s, float w, float h, float opacity)
{
    D2D1_RECT_F rc; int tmp; ID2D1Brush *b; rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    if (!s->brush || w <= 0 || h <= 0) return;
    if (s->brush->cls == CLS_CompositionSurfaceBrush) { comp_surface_draw(t->rt, (SBrush *)s->brush, w, h, opacity); return; }
    b = make_brush(t, s->brush, 0, 0, w, h, opacity, &tmp); if (!b) return;
    ID2D1HwndRenderTarget_FillRectangle(t->rt, &rc, b); if (tmp) ID2D1Brush_Release(b);
}

/* ---- geometry (in the geometry's own space) ---- */
static ID2D1Geometry *geo_create(CObj *g)
{
    ID2D1Factory *f = get_factory(); ID2D1Geometry *r = NULL; if (!f || !g) return NULL;
    switch (g->cls) {
    case CLS_CompositionRectangleGeometry: { RectGeo *x = (RectGeo *)g; D2D1_RECT_F rc; ID2D1RectangleGeometry *o = NULL;
        rc.left = x->offset.X; rc.top = x->offset.Y; rc.right = x->offset.X + x->size.X; rc.bottom = x->offset.Y + x->size.Y;
        if (SUCCEEDED(ID2D1Factory_CreateRectangleGeometry(f, &rc, &o))) r = (ID2D1Geometry *)o; break; }
    case CLS_CompositionRoundedRectangleGeometry: { RRectGeo *x = (RRectGeo *)g; D2D1_ROUNDED_RECT rr; ID2D1RoundedRectangleGeometry *o = NULL;
        rr.rect.left = x->offset.X; rr.rect.top = x->offset.Y; rr.rect.right = x->offset.X + x->size.X; rr.rect.bottom = x->offset.Y + x->size.Y; rr.radiusX = x->radius.X; rr.radiusY = x->radius.Y;
        if (SUCCEEDED(ID2D1Factory_CreateRoundedRectangleGeometry(f, &rr, &o))) r = (ID2D1Geometry *)o; break; }
    case CLS_CompositionEllipseGeometry: { EllipseGeo *x = (EllipseGeo *)g; D2D1_ELLIPSE e; ID2D1EllipseGeometry *o = NULL;
        e.point.x = x->center.X; e.point.y = x->center.Y; e.radiusX = x->radius.X; e.radiusY = x->radius.Y;
        if (SUCCEEDED(ID2D1Factory_CreateEllipseGeometry(f, &e, &o))) r = (ID2D1Geometry *)o; break; }
    case CLS_CompositionLineGeometry: { LineGeo *x = (LineGeo *)g; ID2D1PathGeometry *pg = NULL; ID2D1GeometrySink *sink = NULL; D2D1_POINT_2F p0, p1;
        p0.x = x->start.X; p0.y = x->start.Y; p1.x = x->end.X; p1.y = x->end.Y;
        if (SUCCEEDED(ID2D1Factory_CreatePathGeometry(f, &pg)) && SUCCEEDED(ID2D1PathGeometry_Open(pg, &sink))) {
            ID2D1GeometrySink_BeginFigure(sink, p0, D2D1_FIGURE_BEGIN_HOLLOW); ID2D1GeometrySink_AddLine(sink, p1); ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
            ID2D1GeometrySink_Close(sink); ID2D1GeometrySink_Release(sink); r = (ID2D1Geometry *)pg; }
        else if (pg) ID2D1PathGeometry_Release(pg); break; }
    case CLS_CompositionPathGeometry: { PathGeo *x = (PathGeo *)g; if (x->path && ((CPath *)x->path)->geo) { r = (ID2D1Geometry *)((CPath *)x->path)->geo; ID2D1Geometry_AddRef(r); } break; }
    default: break;
    }
    if (g->cls != CLS_CompositionPathGeometry && r && (((Geometry *)g)->trimStart != 0.0f || ((Geometry *)g)->trimEnd != 1.0f || ((Geometry *)g)->trimOffset != 0.0f)) {
        static LONG warned; if (!InterlockedExchange(&warned, 1)) comp_log("render: geometry Trim* ignored (not implemented)\n"); }
    return r;
}
static D2D1_MATRIX_3X2_F id_matrix(void) { D2D1_MATRIX_3X2_F m; m._11 = 1; m._12 = 0; m._21 = 0; m._22 = 1; m._31 = 0; m._32 = 0; return m; }
static Mat shape_local_matrix(const Shape *s)
{
    Mat tm, m; tm.a = s->tm.M11; tm.b = s->tm.M12; tm.c = s->tm.M21; tm.d = s->tm.M22; tm.e = s->tm.M31; tm.f = s->tm.M32; m = tm;
    m = mat_mul(m, mat_translate(-s->center.X, -s->center.Y)); m = mat_mul(m, mat_scale(s->scale.X, s->scale.Y));
    if (s->rotation != 0.0f) m = mat_mul(m, mat_rotate(s->rotation));
    m = mat_mul(m, mat_translate(s->center.X, s->center.Y)); m = mat_mul(m, mat_translate(s->offset.X, s->offset.Y)); return m;
}
static void draw_shape(DTarget *t, CObj *sh, Mat pm, float opacity)
{
    ID2D1HwndRenderTarget *rt = t->rt; Mat world; D2D1_MATRIX_3X2_F wm;
    if (!sh) return;
    world = mat_mul(shape_local_matrix((Shape *)sh), pm); wm = to_d2d(world);
    if (sh->cls == CLS_CompositionContainerShape) {
        ContainerShape *c = (ContainerShape *)sh; PCol *pc = (PCol *)c->shapes; UINT32 i; if (!pc) return;
        for (i = 0; i < pc->n; i++) draw_shape(t, pc->s[i], world, opacity); return;
    }
    if (sh->cls == CLS_CompositionSpriteShape) {
        SpriteShape *s = (SpriteShape *)sh; ID2D1Geometry *g; D2D1_RECT_F bb; int tmp; ID2D1Brush *b;
        if (!s->geometry) return; g = geo_create(s->geometry); if (!g) return;
        ID2D1HwndRenderTarget_SetTransform(rt, &wm);
        ID2D1HwndRenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (FAILED(ID2D1Geometry_GetBounds(g, NULL, &bb))) { bb.left = bb.top = 0; bb.right = bb.bottom = 1; }
        if (s->fill && (b = make_brush(t, s->fill, bb.left, bb.top, bb.right - bb.left, bb.bottom - bb.top, opacity, &tmp)) != NULL) {
            ID2D1HwndRenderTarget_FillGeometry(rt, g, b, NULL); if (tmp) ID2D1Brush_Release(b); }
        if (s->stroke && s->thickness > 0.0f && (b = make_brush(t, s->stroke, bb.left, bb.top, bb.right - bb.left, bb.bottom - bb.top, opacity, &tmp)) != NULL) {
            ID2D1StrokeStyle *ss = NULL; ID2D1Factory *f = get_factory(); D2D1_STROKE_STYLE_PROPERTIES sp; FCol *da = (FCol *)s->dashes; float w = s->thickness;
            sp.startCap = (D2D1_CAP_STYLE)s->startCap; sp.endCap = (D2D1_CAP_STYLE)s->endCap; sp.dashCap = (D2D1_CAP_STYLE)s->dashCap; sp.lineJoin = (D2D1_LINE_JOIN)s->lineJoin;
            sp.miterLimit = s->miter < 1.0f ? 1.0f : s->miter; sp.dashStyle = (da && da->n) ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID; sp.dashOffset = s->dashOffset;
            if (s->nonScaling) { Mat inv; (void)inv; w = s->thickness / sqrtf(fabsf(world.a * world.d - world.b * world.c) > 1e-12f ? fabsf(world.a * world.d - world.b * world.c) : 1.0f); }
            if (f) ID2D1Factory_CreateStrokeStyle(f, &sp, (da && da->n) ? da->v : NULL, (da && da->n) ? da->n : 0, &ss);
            ID2D1HwndRenderTarget_DrawGeometry(rt, g, b, w, ss); if (ss) ID2D1StrokeStyle_Release(ss); if (tmp) ID2D1Brush_Release(b);
        }
        ID2D1Geometry_Release(g);
    }
}

/* clip a visual: returns 0 none, 1 axis-aligned clip pushed, 2 layer pushed (caller pops; *layer / *geo to release) */
static int push_clip(DTarget *t, Visual *v, Mat world, float ew, float eh, ID2D1Layer **layer, ID2D1Geometry **geo)
{
    ID2D1HwndRenderTarget *rt = t->rt; ID2D1Factory *f = get_factory(); D2D1_MATRIX_3X2_F id = id_matrix(), wm = to_d2d(world); ID2D1Geometry *g = NULL;
    if (!v->clip) return 0;
    if (v->clip->cls == CLS_InsetClip) {
        InsetClip *c = (InsetClip *)v->clip; D2D1_RECT_F r;
        r.left = c->l; r.top = c->t; r.right = ew - c->r; r.bottom = eh - c->b; if (r.right < r.left) r.right = r.left; if (r.bottom < r.top) r.bottom = r.top;
        if (fabsf(world.b) < 1e-6f && fabsf(world.c) < 1e-6f) {
            D2D1_RECT_F dev; float ax, ay, bx, by; mat_apply(&world, r.left, r.top, &ax, &ay); mat_apply(&world, r.right, r.bottom, &bx, &by);
            dev.left = ax < bx ? ax : bx; dev.right = ax < bx ? bx : ax; dev.top = ay < by ? ay : by; dev.bottom = ay < by ? by : ay;
            ID2D1HwndRenderTarget_SetTransform(rt, &id); ID2D1HwndRenderTarget_PushAxisAlignedClip(rt, &dev, D2D1_ANTIALIAS_MODE_ALIASED); return 1;
        }
        if (f) { ID2D1RectangleGeometry *rg = NULL; if (SUCCEEDED(ID2D1Factory_CreateRectangleGeometry(f, &r, &rg))) g = (ID2D1Geometry *)rg; }
    } else if (v->clip->cls == CLS_CompositionGeometricClip) {
        g = geo_create(((GeoClip *)v->clip)->geo);
    } else { static LONG warned; if (!InterlockedExchange(&warned, 1)) comp_log("render: clip class %s not applied\n", g_cd[v->clip->cls].sname); return 0; }
    if (!g || !f) { if (g) ID2D1Geometry_Release(g); return 0; }
    { ID2D1TransformedGeometry *tg = NULL; D2D1_LAYER_PARAMETERS lp;
      /* pre-transform the mask into device space: unambiguous whatever the render target's transform semantics are */
      if (FAILED(ID2D1Factory_CreateTransformedGeometry(f, g, &wm, &tg)) || FAILED(ID2D1HwndRenderTarget_CreateLayer(rt, NULL, layer))) { ID2D1Geometry_Release(g); if (tg) ID2D1TransformedGeometry_Release(tg); return 0; }
      ID2D1Geometry_Release(g); *geo = (ID2D1Geometry *)tg; ID2D1HwndRenderTarget_SetTransform(rt, &id);
      lp.contentBounds.left = -FLT_MAX; lp.contentBounds.top = -FLT_MAX; lp.contentBounds.right = FLT_MAX; lp.contentBounds.bottom = FLT_MAX;
      lp.geometricMask = *geo; lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE; lp.maskTransform = id; lp.opacity = 1.0f; lp.opacityBrush = NULL; lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
      ID2D1HwndRenderTarget_PushLayer(rt, &lp, *layer); }
    return 2;
}

static void draw_visual(DTarget *t, Visual *v, float pw, float ph, Mat pm, float popacity)
{
    ID2D1HwndRenderTarget *rt = t->rt; Mat lm, world; float ew, eh, op; int i, pushed; ID2D1Layer *layer = NULL; ID2D1Geometry *geo = NULL;
    if (!v->visible) return;
    lm = visual_local_matrix(v, 0, 0, pw, ph, &ew, &eh); world = mat_mul(lm, pm); op = popacity * v->opacity;
    if (op <= 0.0f) return;
    pushed = push_clip(t, v, world, ew, eh, &layer, &geo);
    if (comp_is_a(v->o.cls, CLS_SpriteVisual)) {
        D2D1_MATRIX_3X2_F wm = to_d2d(world);
        ID2D1HwndRenderTarget_SetTransform(rt, &wm);
        ID2D1HwndRenderTarget_SetAntialiasMode(rt, v->borderMode == Windows_UI_Composition_CompositionBorderMode_Hard ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        fill_sprite(t, (Sprite *)v, ew, eh, op);
    }
    if (v->o.cls == CLS_ShapeVisual) {
        PCol *pc = (PCol *)((ShapeVisual *)v)->shapes; UINT32 k; if (pc) for (k = 0; k < pc->n; k++) draw_shape(t, pc->s[k], world, op);
    }
    if (comp_is_a(v->o.cls, CLS_ContainerVisual)) {
        Container *c = (Container *)v;
        for (i = 0; i < c->nkids; i++) draw_visual(t, c->kids[i], ew, eh, world, op);
    }
    if (pushed == 1) ID2D1HwndRenderTarget_PopAxisAlignedClip(rt);
    else if (pushed == 2) ID2D1HwndRenderTarget_PopLayer(rt);
    if (layer) ID2D1Layer_Release(layer);
    if (geo) ID2D1Geometry_Release(geo);
}

static void ensure_timer(DTarget *t, BOOL want)
{
    if (want && !t->timer) { SetTimer(t->hwnd, TIMER_ID, FRAME_MS, NULL); t->timer = TRUE; }
    else if (!want && t->timer) { KillTimer(t->hwnd, TIMER_ID); t->timer = FALSE; }
}
static void target_render(DTarget *t)
{
    Compositor *c = (Compositor *)t->o.comp; HRESULT hr; D2D1_COLOR_F clear = { 0, 0, 0, 1 }; D2D1_MATRIX_3X2_F id; Mat m = mat_id();
    if (!ensure_rt(t)) return;
    c->rendering++; comp_tick(&c->o);
    id._11 = 1; id._12 = 0; id._21 = 0; id._22 = 1; id._31 = 0; id._32 = 0;
    ID2D1HwndRenderTarget_BeginDraw(t->rt); ID2D1HwndRenderTarget_SetTransform(t->rt, &id); ID2D1HwndRenderTarget_Clear(t->rt, &clear);
    if (t->root) draw_visual(t, t->root, (float)t->w, (float)t->h, m, 1.0f);
    hr = ID2D1HwndRenderTarget_EndDraw(t->rt, NULL, NULL);
    if (hr == (HRESULT)0x8899000C) { release_rt(t); } else if (FAILED(hr)) comp_log("EndDraw failed 0x%08lX\n", (unsigned long)hr);
    c->rendering--;
    comp_batch_fire(&c->o);                                  /* Completed events run outside the frame so handlers can change the scene and schedule a repaint */
    if (c->dirty) { c->dirty = 0; if (!comp_has_active(&c->o) && IsWindow(t->hwnd)) InvalidateRect(t->hwnd, NULL, FALSE); }
    ensure_timer(t, comp_has_active(&c->o));
}

static LRESULT CALLBACK target_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DTarget *t = (DTarget *)GetPropW(hwnd, PROP_TARGET); WNDPROC old; LRESULT r;
    if (!t) return DefWindowProcW(hwnd, msg, wp, lp);
    old = t->oldproc;
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); if (!t->in_paint) { t->in_paint = TRUE; target_render(t); t->in_paint = FALSE; } EndPaint(hwnd, &ps); return 0; }
    case WM_TIMER: if (wp == TIMER_ID) { if (!t->in_paint) { t->in_paint = TRUE; target_render(t); t->in_paint = FALSE; } return 0; } break;
    case WM_SIZE:
        r = CallWindowProcW(old, hwnd, msg, wp, lp);
        if (t->rt && wp != SIZE_MINIMIZED) { D2D1_SIZE_U s; s.width = LOWORD(lp) ? LOWORD(lp) : 1; s.height = HIWORD(lp) ? HIWORD(lp) : 1; t->w = s.width; t->h = s.height; ID2D1HwndRenderTarget_Resize(t->rt, &s); }
        InvalidateRect(hwnd, NULL, FALSE); return r;
    case WM_NCDESTROY:
        ensure_timer(t, FALSE); SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)old); RemovePropW(hwnd, PROP_TARGET); t->subclassed = FALSE; release_rt(t);
        r = CallWindowProcW(old, hwnd, msg, wp, lp); return r;
    }
    return CallWindowProcW(old, hwnd, msg, wp, lp);
}

/* ============================ targets list / invalidation ============================ */
static void comp_add_target(Compositor *c, DTarget *t)
{
    if (c->ntargets == c->captargets) {
        int nc = c->captargets ? c->captargets * 2 : 4; DTarget **n = (DTarget **)HeapAlloc(GetProcessHeap(), 0, (size_t)nc * sizeof(DTarget *)); if (!n) return;
        if (c->targets) { memcpy(n, c->targets, (size_t)c->ntargets * sizeof(DTarget *)); HeapFree(GetProcessHeap(), 0, c->targets); } c->targets = n; c->captargets = nc;
    }
    c->targets[c->ntargets++] = t;
}
static void comp_remove_target(Compositor *c, DTarget *t) { int i; for (i = 0; i < c->ntargets; i++) if (c->targets[i] == t) { c->targets[i] = c->targets[--c->ntargets]; return; } }
void comp_invalidate(CObj *co)
{
    Compositor *c = (Compositor *)co; int i;
    if (!c) return;
    if (c->rendering) { c->dirty = 1; return; }
    for (i = 0; i < c->ntargets; i++) { DTarget *t = c->targets[i]; if (t->hwnd && IsWindow(t->hwnd)) { InvalidateRect(t->hwnd, NULL, FALSE); if (comp_has_active(co)) ensure_timer(t, TRUE); } }
}

/* ============================ DesktopWindowTarget ============================ */
static void target_destroy(CObj *o)
{
    DTarget *t = (DTarget *)o;
    if (o->comp) comp_remove_target((Compositor *)o->comp, t);
    if (t->subclassed && IsWindow(t->hwnd)) { ensure_timer(t, FALSE); SetWindowLongPtrW(t->hwnd, GWLP_WNDPROC, (LONG_PTR)t->oldproc); RemovePropW(t->hwnd, PROP_TARGET); }
    release_rt(t); if (t->root) comp_release(&t->root->o);
}
static HRESULT target_hook(DTarget *t, HWND hwnd)
{
    DTarget *prev;
    if (!IsWindow(hwnd)) return E_INVALIDARG;
    if (GetWindowThreadProcessId(hwnd, NULL) != GetCurrentThreadId()) return E_ACCESSDENIED;
    prev = (DTarget *)GetPropW(hwnd, PROP_TARGET);
    if (prev) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    t->hwnd = hwnd; SetPropW(hwnd, PROP_TARGET, t);
    t->oldproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)target_wndproc); t->subclassed = TRUE;
    comp_add_target((Compositor *)t->o.comp, t);
    InvalidateRect(hwnd, NULL, FALSE); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionTarget, get_Root)(W(ICompositionTarget) *This, W(IVisual) **r) { DTarget *t = (DTarget *)OBJ_OF(This); return comp_return(t->root ? &t->root->o : NULL, &IID_(IVisual), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionTarget, put_Root)(W(ICompositionTarget) *This, W(IVisual) *v)
{
    DTarget *t = (DTarget *)OBJ_OF(This); Visual *nv = v ? (Visual *)OBJ_OF(v) : NULL;
    if (nv) comp_addref(&nv->o); if (t->root) comp_release(&t->root->o); t->root = nv; INVALIDATE(&t->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE CI_(Desktop_IDesktopWindowTarget, get_IsTopmost)(Windows_UI_Composition_Desktop_IDesktopWindowTarget *This, boolean *r) { *r = (boolean)((DTarget *)OBJ_OF(This))->topmost; return S_OK; }

static HRESULT create_target(CObj *comp, HWND hwnd, BOOL topmost, CObj **out)
{
    DTarget *t = (DTarget *)comp_new(CLS_DesktopWindowTarget, comp); HRESULT hr;
    if (!t) return E_OUTOFMEMORY; t->topmost = topmost;
    hr = target_hook(t, hwnd); if (FAILED(hr)) { comp_release(&t->o); return hr; }
    *out = &t->o; return S_OK;
}

/* ICompositorDesktopInterop {29E691FA-4567-4DCA-B319-D0F207EB6807} : IUnknown */
static const GUID IID_ICompositorDesktopInterop_ = {0x29E691FA, 0x4567, 0x4DCA, {0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07}};
typedef struct DesktopInteropVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *CreateDesktopWindowTarget)(void *, HWND, BOOL, Windows_UI_Composition_Desktop_IDesktopWindowTarget **);
    HRESULT (STDMETHODCALLTYPE *EnsureOnThread)(void *, DWORD);
} DesktopInteropVtbl;
static HRESULT STDMETHODCALLTYPE di_Create(void *This, HWND hwnd, BOOL topmost, Windows_UI_Composition_Desktop_IDesktopWindowTarget **result)
{
    CObj *t = NULL, *comp = OBJ_OF(This); HRESULT hr; if (!result) return E_POINTER; *result = NULL;
    hr = create_target(comp, hwnd, topmost, &t); if (FAILED(hr)) { comp_log("CreateDesktopWindowTarget(hwnd=%p) -> 0x%08lX\n", (void *)hwnd, (unsigned long)hr); return hr; }
    hr = comp_qi(t, &IID_Windows_UI_Composition_Desktop_IDesktopWindowTarget, (void **)result); comp_release(t); return hr;
}
static HRESULT STDMETHODCALLTYPE di_Ensure(void *This, DWORD tid) { (void)This; (void)tid; return S_OK; }
static const DesktopInteropVtbl VT_DesktopInterop = { cv_QI, cv_AddRef, cv_Release, di_Create, di_Ensure };
static HRESULT compositor_qi_extra(CObj *o, REFIID r, void **p)
{
    Compositor *c = (Compositor *)o;
    if (IsEqualGUID(r, &IID_ICompositorDesktopInterop_)) { c->desktopInterop.vt = &VT_DesktopInterop; c->desktopInterop.obj = o; comp_addref(o); *p = &c->desktopInterop; return S_OK; }
    return comp_compositor_interop_qi(o, r, p);
}

/* ============================ registration ============================ */
void comp_register_impls_visual(void); void comp_register_impls_anim(void); void comp_register_impls_shapes(void); void comp_register_impls_surface(void); void comp_register_impls_batch(void); void comp_register_impls_expr(void);
void comp_register_impls(void)
{
    static LONG done;
    if (InterlockedExchange(&done, 1)) return;
    comp_register_impls_visual(); comp_register_impls_anim(); comp_register_impls_shapes(); comp_register_impls_surface(); comp_register_impls_batch(); comp_register_impls_expr();
    g_impl[CLS_CompositionTarget].size = sizeof(DTarget);
    g_impl[CLS_DesktopWindowTarget].size = sizeof(DTarget); g_impl[CLS_DesktopWindowTarget].destroy = target_destroy;
    g_impl[CLS_Compositor].qi_extra = compositor_qi_extra;
}
