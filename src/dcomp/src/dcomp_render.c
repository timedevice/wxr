/* comp_render.c - DesktopWindowTarget, ICompositorDesktopInterop, Direct2D 1.0 renderer, hit testing, hosting API */
#include "dcomp_priv.h"

#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n
#define TIMER_ID 0x7C01
#define FRAME_MS 16

struct DTarget {
    CObj o; HWND hwnd; BOOL topmost; Visual *root; WNDPROC oldproc; BOOL subclassed; BOOL root_explicit;
    IDXGISwapChain1 *sc; ID2D1DeviceContext *rt; ID2D1Bitmap1 *back; ID2D1SolidColorBrush *solid; UINT w, h; BOOL timer; BOOL in_paint; CView dct; double last_present_ms;
    D2D1_RECT_F cull;                  /* while drawing: the device rectangle anything visible must touch (target bounds narrowed by the clips above) */
};
static const WCHAR PROP_TARGET[] = L"wxr.comptarget";

static ID2D1Factory *get_factory(void) { return comp_d2d_factory(); }
HRESULT dc_snapshot_png(HWND hwnd, const WCHAR *path);

/* ============================ renderer ============================ */
static void release_rt(DTarget *t)
{
    if (t->solid) { ID2D1SolidColorBrush_Release(t->solid); t->solid = NULL; }
    if (t->rt) { ID2D1DeviceContext_SetTarget(t->rt, NULL); UREL(t->rt); t->rt = NULL; }
    if (t->back) { UREL(t->back); t->back = NULL; }
    if (t->sc) { IDXGISwapChain1_Release(t->sc); t->sc = NULL; }
}
static HRESULT bind_backbuffer(DTarget *t)
{
    IDXGISurface *surf = NULL; D2D1_BITMAP_PROPERTIES1 bp; HRESULT hr; D2D1_COLOR_F white = { 1, 1, 1, 1 };
    hr = IDXGISwapChain1_GetBuffer(t->sc, 0, &IID_IDXGISurface, (void **)&surf); if (FAILED(hr)) return hr;
    memset(&bp, 0, sizeof bp); bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE; bp.dpiX = 96; bp.dpiY = 96; bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(t->rt, surf, &bp, &t->back); IDXGISurface_Release(surf); if (FAILED(hr)) return hr;
    ID2D1DeviceContext_SetTarget(t->rt, (void *)t->back);
    if (!t->solid) hr = DCB_CreateSolidColorBrush(t->rt, &white, NULL, &t->solid);
    return hr;
}
/* frame cost counters (WXR_PERFLOG=1: one line per 30 frames): what a frame drew and where its time went */
static struct { LONG visuals, culled, sprites, masks, offscreens, layers, shapes, vissurfs, composites, surfdraws; double ms_mask, ms_vissurf, ms_comp, ms_surf, ms_shape; int depth; } g_pf;
#define PF_T0 LARGE_INTEGER pf0_; int pfd_ = g_pf.depth++; QueryPerformanceCounter(&pf0_)
#define PF_T1(field) do { LARGE_INTEGER pf1_; g_pf.depth--; if (!pfd_) { QueryPerformanceCounter(&pf1_); g_pf.field += pf_ms(pf0_, pf1_); } } while (0)   /* outermost timed section only: nested ones are part of it */
static double pf_ms(LARGE_INTEGER a, LARGE_INTEGER b) { static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f); return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart; }
extern char g_tick_stats[160];
static int perflog(void) { static int v = -1; if (v < 0) v = GetEnvironmentVariableA("WXR_PERFLOG", NULL, 0) ? 1 : 0; return v; }
static void draw_brush_rect_(DTarget *t, CObj *b, float w, float h, float opacity);
static void brush_desc(CObj *b, char *out, size_t n);
static BOOL ensure_rt(DTarget *t)
{
    RECT rc; DcDev *d; DXGI_SWAP_CHAIN_DESC1 sd; HRESULT hr;
    if (t->rt) return TRUE;
    d = dc_dev(); if (!d || !d->dxf || !IsWindow(t->hwnd)) return FALSE;
    GetClientRect(t->hwnd, &rc); t->w = (UINT)(rc.right > 0 ? rc.right : 1); t->h = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
    memset(&sd, 0, sizeof sd); sd.Width = t->w; sd.Height = t->h; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 1;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;      /* Win7 platform update: bitblt model only (no flip) */
    hr = IDXGIFactory2_CreateSwapChainForHwnd(d->dxf, (IUnknown *)d->d3d, t->hwnd, &sd, NULL, NULL, &t->sc);
    if (FAILED(hr)) { comp_log("CreateSwapChainForHwnd failed 0x%08lX\n", (unsigned long)hr); t->sc = NULL; return FALSE; }
    IDXGIFactory2_MakeWindowAssociation(d->dxf, t->hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    hr = ID2D1Device_CreateDeviceContext(d->d2d, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &t->rt);
    if (SUCCEEDED(hr)) hr = bind_backbuffer(t);
    if (FAILED(hr)) { comp_log("D2D device context / back buffer failed 0x%08lX\n", (unsigned long)hr); release_rt(t); return FALSE; }
    return TRUE;
}
static void resize_rt(DTarget *t, UINT w, UINT h)
{
    HRESULT hr; if (!t->sc || !t->rt) { t->w = w; t->h = h; return; }
    ID2D1DeviceContext_SetTarget(t->rt, NULL); if (t->back) { UREL(t->back); t->back = NULL; }
    hr = IDXGISwapChain1_ResizeBuffers(t->sc, 0, w, h, DXGI_FORMAT_UNKNOWN, 0); t->w = w; t->h = h;
    if (SUCCEEDED(hr)) hr = bind_backbuffer(t);
    if (FAILED(hr)) { comp_log("resize failed 0x%08lX, recreating\\n", (unsigned long)hr); release_rt(t); }
}
static D2D1_MATRIX_3X2_F to_d2d(Mat m) { D2D1_MATRIX_3X2_F r; r._11 = m.a; r._12 = m.b; r._21 = m.c; r._22 = m.d; r._31 = m.e; r._32 = m.f; return r; }
static D2D1_COLOR_F to_color(Col c, float opacity) { D2D1_COLOR_F r; r.r = c.R / 255.0f; r.g = c.G / 255.0f; r.b = c.B / 255.0f; r.a = c.A / 255.0f * opacity; return r; }
static int stop_cmp(const void *a, const void *b) { float x = (*(Stop *const *)a)->offset, y = (*(Stop *const *)b)->offset; return x < y ? -1 : x > y; }

/* Build a D2D brush for composition brush `b` covering the box (x,y,w,h) in the current drawing space.  *tmp = 1 when the caller must Release it. */
static ID2D1Brush *make_brush(DTarget *t, CObj *b, float x, float y, float w, float h, float opacity, int *tmp)
{
    ID2D1DeviceContext *rt = t->rt; *tmp = 0;
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
        if (SUCCEEDED(DCB_CreateLinearGradientBrush(rt, &lp, &bp, coll, &lb))) *tmp = 1;
        ID2D1GradientStopCollection_Release(coll); return (ID2D1Brush *)lb;
    }
    if (b->cls == CLS_CompositionSurfaceBrush) { void *r = comp_surface_make_brush(rt, (SBrush *)b, x, y, w, h, opacity); if (r) *tmp = 1; return (ID2D1Brush *)r; }
    {   Col k;   /* effect brushes (acrylic): their colour layers as one solid colour, no blur (comp_effect.c) */
        if (b->cls == CLS_CompositionEffectBrush && comp_effect_brush_color(b, &k)) { D2D1_COLOR_F c = to_color(k, opacity); ID2D1SolidColorBrush_SetColor(t->solid, &c); return (ID2D1Brush *)t->solid; } }
    { static LONG warned[CLS__COUNT]; if (!InterlockedExchange(&warned[b->cls], 1)) comp_log("render: brush class %s not drawn yet\n", g_cd[b->cls].sname); }
    return NULL;
}
static D2D1_MATRIX_3X2_F id_matrix(void);
static void draw_brush_rect(DTarget *t, CObj *b, float w, float h, float opacity);
/* NineGridBrush over (0,0,w,h): a surface source is drawn as nine bitmap slices (insets in source pixels, scaled by the inset scales in the
   destination); any other source fills the box, or only the frame between the insets when the centre is hollow */
static void draw_ninegrid(DTarget *t, NineG *n, float w, float h, float opacity)
{
    ID2D1DeviceContext *rt = t->rt; float L = n->l * n->ls, T = n->t * n->ts, R = n->r * n->rs, B = n->b * n->bs; int iw, ih, i, j; void *bmp;
    if (w <= 0 || h <= 0 || !n->src) return;
    if (L + R > w && L + R > 0) { float k = w / (L + R); L *= k; R *= k; }
    if (T + B > h && T + B > 0) { float k = h / (T + B); T *= k; B *= k; }
    if (n->src->cls == CLS_CompositionSurfaceBrush && (bmp = comp_surface_bitmap((SBrush *)n->src, &iw, &ih)) != NULL) {
        float sx[4], sy[4], dx[4], dy[4];
        sx[0] = 0; sx[1] = n->l; sx[2] = iw - n->r; sx[3] = (float)iw; sy[0] = 0; sy[1] = n->t; sy[2] = ih - n->b; sy[3] = (float)ih;
        dx[0] = 0; dx[1] = L; dx[2] = w - R; dx[3] = w; dy[0] = 0; dy[1] = T; dy[2] = h - B; dy[3] = h;
        for (j = 0; j < 3; j++) for (i = 0; i < 3; i++) {
            D2D1_RECT_F s, d; if (i == 1 && j == 1 && n->hollow) continue;
            s.left = sx[i]; s.right = sx[i + 1]; s.top = sy[j]; s.bottom = sy[j + 1]; d.left = dx[i]; d.right = dx[i + 1]; d.top = dy[j]; d.bottom = dy[j + 1];
            if (s.right <= s.left || s.bottom <= s.top || d.right <= d.left || d.bottom <= d.top) continue;
            DCB_DrawBitmap(rt, bmp, &d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &s);
        }
        return;
    }
    if (!n->hollow) { draw_brush_rect(t, n->src, w, h, opacity); return; }
    {   int tmp, k; ID2D1Brush *br = make_brush(t, n->src, 0, 0, w, h, opacity, &tmp); D2D1_RECT_F r[4];
        if (!br) return;
        r[0].left = 0; r[0].top = 0; r[0].right = w; r[0].bottom = T;         r[1].left = 0; r[1].top = h - B; r[1].right = w; r[1].bottom = h;
        r[2].left = 0; r[2].top = T; r[2].right = L; r[2].bottom = h - B;     r[3].left = w - R; r[3].top = T; r[3].right = w; r[3].bottom = h - B;
        for (k = 0; k < 4; k++) if (r[k].right > r[k].left && r[k].bottom > r[k].top) DCB_FillRectangle(rt, &r[k], br);
        if (tmp) ID2D1Brush_Release(br); }
}
/* MaskBrush over (0,0,w,h): the mask brush is rendered into an offscreen target whose alpha becomes the layer's opacity brush, then the source
   brush fills the box inside that layer. XAML 19041 uses it for glyph runs (Mask = A8 surface) and rounded button faces/borders (Mask = NineGrid) */
static void draw_masked(DTarget *t, MaskB *m, float w, float h, float opacity)
{
    ID2D1DeviceContext *rt = t->rt, *mdc = NULL; ID2D1BitmapRenderTarget *brt = NULL; ID2D1Bitmap *mb = NULL; ID2D1BitmapBrush *ob = NULL; ID2D1Layer *layer = NULL;
    D2D1_SIZE_U px; D2D1_SIZE_F sz; D2D1_LAYER_PARAMETERS lp; D2D1_COLOR_F clear = { 0, 0, 0, 0 }; D2D1_MATRIX_3X2_F id = id_matrix(); D2D1_BITMAP_BRUSH_PROPERTIES bbp;
    if (!m->src || w < 1 || h < 1) return;
    g_pf.masks++;
    if (!m->mask) { draw_brush_rect(t, m->src, w, h, opacity); return; }
    bbp.extendModeX = D2D1_EXTEND_MODE_CLAMP; bbp.extendModeY = D2D1_EXTEND_MODE_CLAMP; bbp.interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    if (m->mask->cls == CLS_CompositionSurfaceBrush) {
        /* surface mask (A8 glyph runs): its bitmap brush is the opacity brush directly (d2d1 cannot DrawBitmap an A8 bitmap: 0x88982F80) */
        ob = (ID2D1BitmapBrush *)comp_surface_make_brush(rt, (SBrush *)m->mask, 0, 0, w, h, 1.0f); if (!ob) return;
        goto layer;
    }
    px.width = (UINT32)ceilf(w); px.height = (UINT32)ceilf(h); sz.width = (float)px.width; sz.height = (float)px.height;
    if (FAILED(DCB_(CreateCompatibleRenderTarget, rt, &sz, &px, NULL, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &brt))) return;
    if (SUCCEEDED(((IUnknown *)brt)->lpVtbl->QueryInterface((IUnknown *)brt, &IID_ID2D1DeviceContext, (void **)&mdc))) {
        t->rt = mdc; DCB_BeginDraw(mdc); DCB_SetTransform(mdc, &id); DCB_Clear(mdc, &clear);
        draw_brush_rect(t, m->mask, w, h, 1.0f);
        DCB_EndDraw(mdc, NULL, NULL); t->rt = rt;
        brt->lpVtbl->GetBitmap(brt, &mb);
    }
    if (mb) DCB_(CreateBitmapBrush, rt, mb, &bbp, NULL, &ob);
layer:
    if (ob && SUCCEEDED(DCB_CreateLayer(rt, NULL, &layer))) {
        lp.contentBounds.left = 0; lp.contentBounds.top = 0; lp.contentBounds.right = w; lp.contentBounds.bottom = h; lp.geometricMask = NULL;
        lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE; lp.maskTransform = id; lp.opacity = 1.0f; lp.opacityBrush = (ID2D1Brush *)ob; lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
        DCB_PushLayer(rt, &lp, layer); draw_brush_rect(t, m->src, w, h, opacity); DCB_PopLayer(rt);
    }
    if (layer) ID2D1Layer_Release(layer); if (ob) ((IUnknown *)ob)->lpVtbl->Release((IUnknown *)ob); if (mb) ((IUnknown *)mb)->lpVtbl->Release((IUnknown *)mb);
    if (mdc) ((IUnknown *)mdc)->lpVtbl->Release((IUnknown *)mdc); if (brt) ((IUnknown *)brt)->lpVtbl->Release((IUnknown *)brt);
}
/* ---- offscreen pictures: CompositionVisualSurface and Composite effect brushes (WinUI animated icons' mattes/masks) ---- */
static void draw_visual(DTarget *t, Visual *v, float pw, float ph, Mat pm, float popacity);
typedef struct OffArg { CObj *what; float w, h; V2 off; } OffArg;
/* run the drawing of `a` (a brush filling w x h, or a visual tree shifted by -off) into a transparent offscreen bitmap; NULL on failure */
static ID2D1Bitmap *offscreen(DTarget *t, OffArg *a, int visual)
{
    ID2D1DeviceContext *rt = t->rt, *mdc = NULL; ID2D1BitmapRenderTarget *brt = NULL; ID2D1Bitmap *mb = NULL; D2D1_SIZE_U px; D2D1_SIZE_F sz;
    D2D1_COLOR_F clear = { 0, 0, 0, 0 }; D2D1_MATRIX_3X2_F id = id_matrix();
    g_pf.offscreens++;
    px.width = (UINT32)ceilf(a->w); px.height = (UINT32)ceilf(a->h); if (!px.width || !px.height || px.width > 4096 || px.height > 4096) return NULL;
    sz.width = (float)px.width; sz.height = (float)px.height;
    if (FAILED(DCB_(CreateCompatibleRenderTarget, rt, &sz, &px, NULL, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &brt))) return NULL;
    if (SUCCEEDED(((IUnknown *)brt)->lpVtbl->QueryInterface((IUnknown *)brt, &IID_ID2D1DeviceContext, (void **)&mdc))) {
        t->rt = mdc; DCB_BeginDraw(mdc); DCB_SetTransform(mdc, &id); DCB_Clear(mdc, &clear);
        if (visual) { D2D1_RECT_F c0 = t->cull; t->cull.left = 0; t->cull.top = 0; t->cull.right = sz.width; t->cull.bottom = sz.height;
            draw_visual(t, (Visual *)a->what, a->w, a->h, mat_translate(-a->off.X, -a->off.Y), 1.0f); t->cull = c0; }
        else draw_brush_rect(t, a->what, a->w, a->h, 1.0f);
        DCB_EndDraw(mdc, NULL, NULL); t->rt = rt;
        brt->lpVtbl->GetBitmap(brt, &mb);
        ((IUnknown *)mdc)->lpVtbl->Release((IUnknown *)mdc);
    }
    ((IUnknown *)brt)->lpVtbl->Release((IUnknown *)brt); return mb;
}
/* surface brush over a CompositionVisualSurface: the source tree rendered at SourceSize (0 = the box), placed by the brush's stretch/alignment */
static void draw_vissurf(DTarget *t, SBrush *b, float w, float h, float opacity)
{
    VisSurf *vs = (VisSurf *)b->surface; OffArg a; ID2D1Bitmap *bm; float iw, ih, sx = 1, sy = 1, dw, dh, dx, dy; D2D1_RECT_F d, clip;
    if (!vs->src || !comp_is_a(vs->src->cls, CLS_Visual) || w <= 0 || h <= 0) return;
    iw = vs->size.X > 0 ? vs->size.X : w; ih = vs->size.Y > 0 ? vs->size.Y : h;
    a.what = vs->src; a.w = iw; a.h = ih; a.off = vs->offset; if (!(bm = offscreen(t, &a, 1))) return;
    switch (b->stretch) {
    case 0: break;
    case 2: { float k = w / iw < h / ih ? w / iw : h / ih; sx = sy = k; break; }
    case 3: { float k = w / iw > h / ih ? w / iw : h / ih; sx = sy = k; break; }
    default: sx = w / iw; sy = h / ih; break;
    }
    dw = iw * sx; dh = ih * sy; dx = (w - dw) * b->hr; dy = (h - dh) * b->vr;
    d.left = dx; d.top = dy; d.right = dx + dw * ceilf(iw) / iw; d.bottom = dy + dh * ceilf(ih) / ih; clip.left = 0; clip.top = 0; clip.right = w; clip.bottom = h;
    DCB_PushAxisAlignedClip(t->rt, &clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DCB_DrawBitmap(t->rt, bm, &d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
    DCB_PopAxisAlignedClip(t->rt);
    ((IUnknown *)bm)->lpVtbl->Release((IUnknown *)bm);
}
/* Composite effect brush: every source parameter rendered offscreen (w x h), combined by the D2D Composite effect (input 0 = destination,
   1 = source, as CompositeEffect.Sources), drawn into the box. Without effects (no D2D 1.1 effect support): DestinationIn via an opacity layer */
static const GUID CLSID_D2D1Composite_r = {0x48FC9F51, 0xF6AC, 0x48F1, {0x8B, 0x58, 0x3B, 0x28, 0xAC, 0x46, 0xF7, 0x6D}};
typedef struct ID2D1Image ID2D1Image;
#define D2D1_COMPOSITE_PROP_MODE_ 0u
#define FX(o, k, T) ((T)(*(void ***)(o))[k])          /* ID2D1Effect (C++-only in mingw): 9 SetValue, 14 SetInput, 15 SetInputCount, 18 GetOutput */
static void draw_composite(DTarget *t, CObj *b, float w, float h, float opacity)
{
    ID2D1DeviceContext *rt = t->rt; UINT32 mode = 0; CObj *src[8]; ID2D1Bitmap *bm[8] = { 0 }; int n = comp_effect_composite(b, &mode, src, 8), i, ok = 1; IUnknown *fx = NULL;
    ID2D1Layer *layer = NULL; D2D1_LAYER_PARAMETERS lp; D2D1_MATRIX_3X2_F id = id_matrix();
    if (n <= 0 || w < 1 || h < 1) return;
    for (i = 0; i < n; i++) { OffArg a; a.what = src[i]; a.w = w; a.h = h; a.off.X = a.off.Y = 0;
        bm[i] = offscreen(t, &a, 0); if (!bm[i]) ok = 0; }   /* an unset parameter renders as a transparent picture */
    lp.contentBounds.left = 0; lp.contentBounds.top = 0; lp.contentBounds.right = w; lp.contentBounds.bottom = h; lp.geometricMask = NULL;
    lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE; lp.maskTransform = id; lp.opacity = opacity; lp.opacityBrush = NULL; lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
    if (ok && SUCCEEDED(rt->lpVtbl->CreateEffect(rt, &CLSID_D2D1Composite_r, (ID2D1Effect **)&fx)) && fx) {
        ID2D1Image *img = NULL;
        FX(fx, 15, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32))(fx, (UINT32)n);
        for (i = 0; i < n; i++) FX(fx, 14, void (STDMETHODCALLTYPE *)(void *, UINT32, ID2D1Image *, BOOL))(fx, (UINT32)i, (ID2D1Image *)bm[i], TRUE);
        FX(fx, 9, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32, D2D1_PROPERTY_TYPE, const BYTE *, UINT32))(fx, D2D1_COMPOSITE_PROP_MODE_, D2D1_PROPERTY_TYPE_ENUM, (const BYTE *)&mode, sizeof mode);
        FX(fx, 18, void (STDMETHODCALLTYPE *)(void *, ID2D1Image **))(fx, &img);
        if (img) {
            int lay = opacity < 0.999f && SUCCEEDED(DCB_CreateLayer(rt, NULL, &layer));
            if (lay) DCB_PushLayer(rt, &lp, layer);
            rt->lpVtbl->DrawImage(rt, img, NULL, NULL, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
            if (lay) DCB_PopLayer(rt);
            ((IUnknown *)img)->lpVtbl->Release((IUnknown *)img);
        }
        fx->lpVtbl->Release(fx);
    } else if (ok && n == 2 && (mode == 3 || mode == 2)) {   /* DestinationIn (SourceIn: the other way round) without effects */
        ID2D1BitmapBrush *ob = NULL; D2D1_BITMAP_BRUSH_PROPERTIES bbp; ID2D1Bitmap *pic = mode == 3 ? bm[0] : bm[1], *msk = mode == 3 ? bm[1] : bm[0]; D2D1_RECT_F d;
        static LONG warned; if (!InterlockedExchange(&warned, 1)) comp_log("render: no D2D effects - Composite drawn with an opacity layer (DestinationIn/SourceIn only)\n");
        bbp.extendModeX = D2D1_EXTEND_MODE_CLAMP; bbp.extendModeY = D2D1_EXTEND_MODE_CLAMP; bbp.interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
        if (SUCCEEDED(DCB_(CreateBitmapBrush, rt, msk, &bbp, NULL, &ob)) && SUCCEEDED(DCB_CreateLayer(rt, NULL, &layer))) {
            lp.opacityBrush = (ID2D1Brush *)ob; DCB_PushLayer(rt, &lp, layer);
            d.left = 0; d.top = 0; d.right = ceilf(w); d.bottom = ceilf(h); DCB_DrawBitmap(rt, pic, &d, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
            DCB_PopLayer(rt); }
        if (ob) ((IUnknown *)ob)->lpVtbl->Release((IUnknown *)ob);
    }
    if (layer) ID2D1Layer_Release(layer);
    for (i = 0; i < n; i++) if (bm[i]) ((IUnknown *)bm[i])->lpVtbl->Release((IUnknown *)bm[i]);
}

/* fill (0,0,w,h) of the current drawing space with any composition brush */
static void draw_brush_rect(DTarget *t, CObj *b, float w, float h, float opacity)
{
    if (!b) return;
    { PF_T0; draw_brush_rect_(t, b, w, h, opacity);
      if (b->cls == CLS_CompositionMaskBrush) PF_T1(ms_mask); else if (b->cls == CLS_CompositionEffectBrush) PF_T1(ms_comp);
      else if (b->cls == CLS_CompositionSurfaceBrush && ((SBrush *)b)->surface && ((SBrush *)b)->surface->cls == CLS_CompositionVisualSurface) PF_T1(ms_vissurf);
      else if (b->cls == CLS_CompositionSurfaceBrush) PF_T1(ms_surf); else g_pf.depth--; }
}
static void draw_brush_rect_(DTarget *t, CObj *b, float w, float h, float opacity)
{
    D2D1_RECT_F rc; int tmp; ID2D1Brush *br;
    if (!b) return;
    if (b->cls == CLS_CompositionSurfaceBrush) {
        SBrush *sb = (SBrush *)b;
        if (sb->surface && sb->surface->cls == CLS_CompositionVisualSurface) { g_pf.vissurfs++; draw_vissurf(t, sb, w, h, opacity); return; }
        g_pf.surfdraws++; comp_surface_draw(t->rt, sb, w, h, opacity); return; }
    if (w <= 0 || h <= 0) return;
    { UINT32 m; CObj *s[1]; if (b->cls == CLS_CompositionEffectBrush && comp_effect_composite(b, &m, s, 1) > 0) { g_pf.composites++; draw_composite(t, b, w, h, opacity); return; } }
    { CObj *ls[8]; float lo[8]; int k, nl = b->cls == CLS_CompositionEffectBrush ? comp_effect_layers(b, ls, lo, 8) : 0;   /* layered effect brush: its sources, bottom to top */
      if (nl > 0) {
        for (k = 0; k < nl; k++) if (ls[k] && lo[k] * opacity > 0.0f) draw_brush_rect(t, ls[k], w, h, opacity * lo[k]); return; } }
    if (b->cls == CLS_CompositionNineGridBrush) { draw_ninegrid(t, (NineG *)b, w, h, opacity); return; }
    if (b->cls == CLS_CompositionMaskBrush) { draw_masked(t, (MaskB *)b, w, h, opacity); return; }
    br = make_brush(t, b, 0, 0, w, h, opacity, &tmp); if (!br) return;
    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h; DCB_FillRectangle(t->rt, &rc, br); if (tmp) ID2D1Brush_Release(br);
}
static void fill_sprite(DTarget *t, Sprite *s, float w, float h, float opacity) { draw_brush_rect(t, s->brush, w, h, opacity); }

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
    ID2D1DeviceContext *rt = t->rt; Mat world; D2D1_MATRIX_3X2_F wm;
    if (!sh) return;
    world = mat_mul(shape_local_matrix((Shape *)sh), pm); wm = to_d2d(world);
    if (sh->cls == CLS_CompositionContainerShape) {
        ContainerShape *c = (ContainerShape *)sh; PCol *pc = (PCol *)c->shapes; UINT32 i; if (!pc) return;
        for (i = 0; i < pc->n; i++) draw_shape(t, pc->s[i], world, opacity); return;
    }
    if (sh->cls == CLS_CompositionSpriteShape) {
        SpriteShape *s = (SpriteShape *)sh; ID2D1Geometry *g; D2D1_RECT_F bb; int tmp; ID2D1Brush *b;
        if (!s->geometry) return;
        { Geometry *gg = (Geometry *)s->geometry; if (fabsf(gg->trimEnd - gg->trimStart) < 1e-5f) return; }   /* trimmed to nothing (partial trims are drawn whole) */
        g = geo_create(s->geometry); if (!g) return;
        g_pf.shapes++;
        DCB_SetTransform(rt, &wm);
        DCB_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (FAILED(ID2D1Geometry_GetBounds(g, NULL, &bb))) { bb.left = bb.top = 0; bb.right = bb.bottom = 1; }
        if (s->fill && (b = make_brush(t, s->fill, bb.left, bb.top, bb.right - bb.left, bb.bottom - bb.top, opacity, &tmp)) != NULL) {
            DCB_FillGeometry(rt, g, b, NULL); if (tmp) ID2D1Brush_Release(b); }
        if (s->stroke && s->thickness > 0.0f && (b = make_brush(t, s->stroke, bb.left, bb.top, bb.right - bb.left, bb.bottom - bb.top, opacity, &tmp)) != NULL) {
            ID2D1StrokeStyle *ss = NULL; ID2D1Factory *f = get_factory(); D2D1_STROKE_STYLE_PROPERTIES sp; FCol *da = (FCol *)s->dashes; float w = s->thickness;
            sp.startCap = (D2D1_CAP_STYLE)s->startCap; sp.endCap = (D2D1_CAP_STYLE)s->endCap; sp.dashCap = (D2D1_CAP_STYLE)s->dashCap; sp.lineJoin = (D2D1_LINE_JOIN)s->lineJoin;
            sp.miterLimit = s->miter < 1.0f ? 1.0f : s->miter; sp.dashStyle = (da && da->n) ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID; sp.dashOffset = s->dashOffset;
            if (s->nonScaling) { Mat inv; (void)inv; w = s->thickness / sqrtf(fabsf(world.a * world.d - world.b * world.c) > 1e-12f ? fabsf(world.a * world.d - world.b * world.c) : 1.0f); }
            if (f) ID2D1Factory_CreateStrokeStyle(f, &sp, (da && da->n) ? da->v : NULL, (da && da->n) ? da->n : 0, &ss);
            DCB_DrawGeometry(rt, g, b, w, ss); if (ss) ID2D1StrokeStyle_Release(ss); if (tmp) ID2D1Brush_Release(b);
        }
        ID2D1Geometry_Release(g);
    }
}

/* clip a visual: returns 0 none, 1 axis-aligned clip pushed, 2 layer pushed (caller pops; *layer / *geo to release) */
static int push_clip(DTarget *t, Visual *v, Mat world, float ew, float eh, ID2D1Layer **layer, ID2D1Geometry **geo)
{
    ID2D1DeviceContext *rt = t->rt; ID2D1Factory *f = get_factory(); D2D1_MATRIX_3X2_F id = id_matrix(), wm = to_d2d(world); ID2D1Geometry *g = NULL;
    if (!v->clip) return 0;
    if (v->clip->cls == CLS_InsetClip) {
        InsetClip *c = (InsetClip *)v->clip; D2D1_RECT_F r; float rx = 0, ry = 0; int k;
        if (c->rect) { r.left = c->L; r.top = c->T; r.right = c->R; r.bottom = c->B; for (k = 0; k < 4; k++) { if (c->rad[k].X > rx) rx = c->rad[k].X; if (c->rad[k].Y > ry) ry = c->rad[k].Y; } }   /* RectangleClip: absolute; one radius for all corners */
        else { r.left = c->l; r.top = c->t; r.right = ew - c->r; r.bottom = eh - c->b; }
        if (r.right < r.left) r.right = r.left; if (r.bottom < r.top) r.bottom = r.top;
        if ((rx > 0 || ry > 0) && f) { D2D1_ROUNDED_RECT rr; ID2D1RoundedRectangleGeometry *rg = NULL; rr.rect = r; rr.radiusX = rx; rr.radiusY = ry > 0 ? ry : rx;
            if (SUCCEEDED(ID2D1Factory_CreateRoundedRectangleGeometry(f, &rr, &rg))) g = (ID2D1Geometry *)rg; }
        else if (fabsf(world.b) < 1e-6f && fabsf(world.c) < 1e-6f) {
            D2D1_RECT_F dev; float ax, ay, bx, by; mat_apply(&world, r.left, r.top, &ax, &ay); mat_apply(&world, r.right, r.bottom, &bx, &by);
            dev.left = ax < bx ? ax : bx; dev.right = ax < bx ? bx : ax; dev.top = ay < by ? ay : by; dev.bottom = ay < by ? by : ay;
            DCB_SetTransform(rt, &id); DCB_PushAxisAlignedClip(rt, &dev, D2D1_ANTIALIAS_MODE_ALIASED); return 1;
        }
        if (!g && f) { ID2D1RectangleGeometry *rg = NULL; if (SUCCEEDED(ID2D1Factory_CreateRectangleGeometry(f, &r, &rg))) g = (ID2D1Geometry *)rg; }
    } else if (v->clip->cls == CLS_CompositionGeometricClip) {
        /* DComp's "no clip" (a rectangle clip from -FLT_MAX to +FLT_MAX) arrives as a rectangle geometry with infinite size: as a D2D layer
           it clipped away everything below (WinUI 3's whole system backdrop - the Mica fallback colour - stayed black) */
        CObj *cg = ((GeoClip *)v->clip)->geo;
        if (cg && (cg->cls == CLS_CompositionRectangleGeometry || cg->cls == CLS_CompositionRoundedRectangleGeometry)) {
            V2 o = cg->cls == CLS_CompositionRectangleGeometry ? ((RectGeo *)cg)->offset : ((RRectGeo *)cg)->offset;
            V2 s = cg->cls == CLS_CompositionRectangleGeometry ? ((RectGeo *)cg)->size : ((RRectGeo *)cg)->size;
            if (!(fabsf(o.X) < 1e30f) || !(fabsf(o.Y) < 1e30f) || !(fabsf(s.X) < 1e30f) || !(fabsf(s.Y) < 1e30f)) return 0; }
        g = geo_create(cg);
    } else { static LONG warned; if (!InterlockedExchange(&warned, 1)) comp_log("render: clip class %s not applied\n", g_cd[v->clip->cls].sname); return 0; }
    if (!g || !f) { if (g) ID2D1Geometry_Release(g); return 0; }
    { ID2D1TransformedGeometry *tg = NULL; D2D1_LAYER_PARAMETERS lp;
      /* pre-transform the mask into device space: unambiguous whatever the render target's transform semantics are */
      if (FAILED(ID2D1Factory_CreateTransformedGeometry(f, g, &wm, &tg)) || FAILED(DCB_CreateLayer(rt, NULL, layer))) { ID2D1Geometry_Release(g); if (tg) ID2D1TransformedGeometry_Release(tg); return 0; }
      ID2D1Geometry_Release(g); *geo = (ID2D1Geometry *)tg; DCB_SetTransform(rt, &id);
      lp.contentBounds.left = -FLT_MAX; lp.contentBounds.top = -FLT_MAX; lp.contentBounds.right = FLT_MAX; lp.contentBounds.bottom = FLT_MAX;
      lp.geometricMask = *geo; lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE; lp.maskTransform = id; lp.opacity = 1.0f; lp.opacityBrush = NULL; lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;
      DCB_PushLayer(rt, &lp, *layer); g_pf.layers++; }
    return 2;
}

/* the world matrix of a visual computed from the root (used for transform parents); *ew/*eh = its effective size */
static Mat world_of(DTarget *t, Visual *x, int depth, float *ew, float *eh)
{
    Mat pm = mat_id(); float pw = (float)t->w, ph = (float)t->h, dw, dh; Visual *par;
    if (!x || depth > 64) { *ew = pw; *eh = ph; return pm; }
    par = x->parentForTransform ? x->parentForTransform : x->parent;
    if (par) pm = world_of(t, par, depth + 1, &pw, &ph);
    pm = mat_mul(visual_local_matrix(x, 0, 0, pw, ph, &dw, &dh), pm); *ew = dw; *eh = dh; return pm;
}
/* culling: a frame used to draw every visual of the tree, also the ones scrolled out of view (the Store: 1500 visuals, 190
   text masks per frame). t->cull is the device rectangle that can still show something; an InsetClip narrows it for its subtree, a
   subtree whose clip misses it is skipped, a sprite / shape visual whose own box misses it is not filled. */
static D2D1_RECT_F dev_bbox(const Mat *m, float l, float tp, float r, float b)
{
    float x[4], y[4]; D2D1_RECT_F o; int i;
    mat_apply(m, l, tp, &x[0], &y[0]); mat_apply(m, r, tp, &x[1], &y[1]); mat_apply(m, l, b, &x[2], &y[2]); mat_apply(m, r, b, &x[3], &y[3]);
    o.left = o.right = x[0]; o.top = o.bottom = y[0];
    for (i = 1; i < 4; i++) { if (x[i] < o.left) o.left = x[i]; if (x[i] > o.right) o.right = x[i]; if (y[i] < o.top) o.top = y[i]; if (y[i] > o.bottom) o.bottom = y[i]; }
    return o;
}
static int rect_hits(const D2D1_RECT_F *a, const D2D1_RECT_F *b) { return a->left < b->right && b->left < a->right && a->top < b->bottom && b->top < a->bottom; }
static void draw_visual(DTarget *t, Visual *v, float pw, float ph, Mat pm, float popacity)
{
    ID2D1DeviceContext *rt = t->rt; Mat lm, world; float ew, eh, op; int i, pushed, self = 1; ID2D1Layer *layer = NULL; ID2D1Geometry *geo = NULL; D2D1_RECT_F cull0 = t->cull;
    if (!v->visible) return;
    g_pf.visuals++;
    /* ParentForTransform (dcomp SetTransformParent/2): the visual is positioned relative to that visual instead of its tree parent. XAML hangs
       scrolled content off a visual that carries the scroll offset this way (ScrollViewer contents stayed put while the thumb moved) */
    if (v->parentForTransform && v->parentForTransform != v->parent) { float tw, th; pm = world_of(t, v->parentForTransform, 0, &tw, &th); pw = tw; ph = th; }
    lm = visual_local_matrix(v, 0, 0, pw, ph, &ew, &eh); world = mat_mul(lm, pm); op = popacity * v->opacity;
    if (op <= 0.0f) return;
    /* a dcomp content visual (IDCompositionVisual::SetContent) has no size of its own: its box is the content's. Without this the cull below
       took it for an empty box and never drew it (WinUI 3: the lifted compositor's whole frame is one such visual - the window stayed black) */
    if ((ew <= 0 || eh <= 0) && v->dcx && v->dcx->surfbrush && comp_is_a(v->o.cls, CLS_SpriteVisual) && ((Sprite *)v)->brush == v->dcx->surfbrush) {
        int sw = 0, sh = 0; if (comp_surface_bitmap((SBrush *)v->dcx->surfbrush, &sw, &sh) && sw > 0 && sh > 0) { if (ew <= 0) ew = (float)sw; if (eh <= 0) eh = (float)sh; } }
    {
        D2D1_RECT_F bb;
        if (v->clip && v->clip->cls == CLS_InsetClip) {
            InsetClip *c = (InsetClip *)v->clip; float l, tp, r, b;
            if (c->rect) { l = c->L; tp = c->T; r = c->R; b = c->B; } else { l = c->l; tp = c->t; r = ew - c->r; b = eh - c->b; }
            if (r < l) r = l; if (b < tp) b = tp;
            bb = dev_bbox(&world, l, tp, r, b);
            if (!rect_hits(&bb, &t->cull)) { g_pf.culled++; return; }                 /* the whole subtree is clipped away */
            if (bb.left > t->cull.left) t->cull.left = bb.left; if (bb.top > t->cull.top) t->cull.top = bb.top;
            if (bb.right < t->cull.right) t->cull.right = bb.right; if (bb.bottom < t->cull.bottom) t->cull.bottom = bb.bottom;
        }
        if (ew > 0 && eh > 0) { bb = dev_bbox(&world, -1, -1, ew + 1, eh + 1); self = rect_hits(&bb, &t->cull); } else self = 0;   /* an empty box draws nothing itself */
        if (!self) g_pf.culled++;
    }
    /* WinUI 3 caption-buttons window (a small child target): Windowing's title-bar background and title text arrive in its tree at title-bar
       coordinates (1358 / 1190 px wide) and, opaque here, covered the 144-px window in black with a ghost title over the buttons. A sprite in a
       small child target that is several times wider than the window is not this window's content: not drawn. */
    if (self && t->hwnd && t->w > 0 && t->w < 400 && comp_is_a(v->o.cls, CLS_SpriteVisual) && (GetWindowLongW(t->hwnd, GWL_STYLE) & WS_CHILD)) {
        D2D1_RECT_F bw = dev_bbox(&world, 0, 0, ew, eh);
        if (bw.right - bw.left > 3.0f * (float)t->w) self = 0; }
    pushed = push_clip(t, v, world, ew, eh, &layer, &geo);
    if (self && comp_is_a(v->o.cls, CLS_SpriteVisual)) {
        D2D1_MATRIX_3X2_F wm = to_d2d(world);
        DCB_SetTransform(rt, &wm);
        DCB_SetAntialiasMode(rt, v->borderMode == Windows_UI_Composition_CompositionBorderMode_Hard ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (((Sprite *)v)->brush) g_pf.sprites++;
        fill_sprite(t, (Sprite *)v, ew, eh, op);
    }
    if (self && v->o.cls == CLS_ShapeVisual) {
        PCol *pc = (PCol *)((ShapeVisual *)v)->shapes; UINT32 k; PF_T0; if (pc) for (k = 0; k < pc->n; k++) draw_shape(t, pc->s[k], world, op); PF_T1(ms_shape);
    }
    if (comp_is_a(v->o.cls, CLS_ContainerVisual)) {
        Container *c = (Container *)v;
        for (i = 0; i < c->nkids; i++) draw_visual(t, c->kids[i], ew, eh, world, op);
    }
    if (pushed == 1) DCB_PopAxisAlignedClip(rt);
    else if (pushed == 2) DCB_PopLayer(rt);
    if (layer) ID2D1Layer_Release(layer);
    if (geo) ID2D1Geometry_Release(geo);
    t->cull = cull0;
}

static void ensure_timer(DTarget *t, BOOL want)
{
    if (!t->hwnd) return;
    if (want && !t->timer) { SetTimer(t->hwnd, TIMER_ID, FRAME_MS, NULL); t->timer = TRUE; }
    else if (!want && t->timer) { KillTimer(t->hwnd, TIMER_ID); t->timer = FALSE; }
}

/* ---- registry of interop visuals: used to pick a root when the client never calls SetRoot ---- */
static Visual *g_vis[4096]; static int g_nvis;
void dc_note_visual(Visual *v, int created)
{
    int i; EnterCriticalSection(&g_dc_lock);
    if (created) { if (g_nvis < 4096) g_vis[g_nvis++] = v; }
    else for (i = 0; i < g_nvis; i++) if (g_vis[i] == v) { g_vis[i] = g_vis[--g_nvis]; break; }
    LeaveCriticalSection(&g_dc_lock);
}
static int subtree(Visual *v) { int n = 1, i; if (comp_is_a(v->o.cls, CLS_ContainerVisual)) { Container *c = (Container *)v; for (i = 0; i < c->nkids; i++) n += subtree(c->kids[i]); } return n; }
static Visual *pick_root(DTarget *t)
{
    int i, best = 0; Visual *b = NULL; EnterCriticalSection(&g_dc_lock);
    for (i = 0; i < g_nvis; i++) { Visual *v = g_vis[i]; int n; if (v->parent) continue; n = subtree(v); if (n > best) { best = n; b = v; } }
    LeaveCriticalSection(&g_dc_lock);
    if (b != t->root) { comp_log("dcomp: no SetRoot yet: drawing the largest parentless visual %p (%d visuals) as root (heuristic)\n", (void *)b, best); }
    return b;
}

/* draw the whole scene into any D2D 1.1 device context whose target is w x h pixels */
/* Child-window targets that overlap a sibling target BELOW them (WinUI 3's caption buttons over the content window): Win10 composes
   them translucently (WS_EX_LAYERED / NOREDIRECTIONBITMAP children); Win7 child windows are opaque, so the buttons sat on black. The
   siblings' scenes are drawn first, offset into this window - the same pixels the window covers. */
static void draw_scene_under(DTarget *t, ID2D1DeviceContext *dc)
{
    RECT me, r; HWND s; int n = 0;
    if (!t->hwnd || !(GetWindowLongW(t->hwnd, GWL_STYLE) & WS_CHILD) || !GetWindowRect(t->hwnd, &me)) return;
    for (s = GetWindow(t->hwnd, GW_HWNDLAST); s && s != t->hwnd && n < 4; s = GetWindow(s, GW_HWNDPREV)) {   /* bottom-most first, up to this window */
        DTarget *u = (DTarget *)GetPropW(s, PROP_TARGET); Visual *root; Mat m = mat_id(); ID2D1DeviceContext *save; D2D1_RECT_F cull;
        if (!u || u == t || !IsWindowVisible(s) || !GetWindowRect(s, &r) || r.right <= me.left || r.left >= me.right || r.bottom <= me.top || r.top >= me.bottom) continue;
        root = u->root; if (!root && !u->root_explicit) root = pick_root(u); if (!root) continue;
        m.e = (float)(r.left - me.left); m.f = (float)(r.top - me.top);
        save = u->rt; cull = u->cull; u->rt = dc;
        u->cull.left = 0; u->cull.top = 0; u->cull.right = (float)(me.right - me.left); u->cull.bottom = (float)(me.bottom - me.top);   /* device space = this window */
        draw_visual(u, root, (float)u->w, (float)u->h, m, 1.0f);
        u->rt = save; u->cull = cull; n++;
        { static LONG nl; if (InterlockedIncrement(&nl) <= 4) comp_log("target %p (hwnd %p): drew sibling target %p (hwnd %p) underneath at %.0f,%.0f\n", (void *)t, (void *)t->hwnd, (void *)u, (void *)s, m.e, m.f); } }
}
/* after a child target presents: overlapping child targets ABOVE it repaint, so they stay on top (no WS_CLIPSIBLINGS: the content window
   must keep drawing under the transparent title-bar windows) */
static void repaint_children_above(DTarget *t)
{
    RECT me, r; HWND s;
    if (!t->hwnd || !(GetWindowLongW(t->hwnd, GWL_STYLE) & WS_CHILD) || !GetWindowRect(t->hwnd, &me)) return;
    for (s = GetWindow(t->hwnd, GW_HWNDPREV); s; s = GetWindow(s, GW_HWNDPREV)) {
        if (!GetPropW(s, PROP_TARGET) || !IsWindowVisible(s) || !GetWindowRect(s, &r) || r.right <= me.left || r.left >= me.right || r.bottom <= me.top || r.top >= me.bottom) continue;
        InvalidateRect(s, NULL, FALSE); }
}
static void draw_scene(DTarget *t, ID2D1DeviceContext *dc, UINT w, UINT h)
{
    D2D1_COLOR_F clear = { 0, 0, 0, 1 }; D2D1_MATRIX_3X2_F id = id_matrix(); Mat m = mat_id(); Visual *root = t->root;
    ID2D1DeviceContext *save = t->rt; t->rt = dc;                                         /* the draw helpers use t->rt */
    t->cull.left = 0; t->cull.top = 0; t->cull.right = (float)w; t->cull.bottom = (float)h;
    DCB_SetTransform(dc, &id); DCB_Clear(dc, &clear);
    draw_scene_under(t, dc); DCB_SetTransform(dc, &id);
    if (!root && !t->root_explicit) root = pick_root(t);
    if (root) draw_visual(t, root, (float)w, (float)h, m, 1.0f);
    t->rt = save;
}
/* render diagnostics: tree statistics (visuals, sprites with a brush, brush classes) */
static void tree_stats(Visual *v, int *nv, int *nspr, int *nbrush, int *nsurf, int *nhidden)
{
    int i; (*nv)++; if (!v->visible || v->opacity <= 0.0f) (*nhidden)++;
    if (comp_is_a(v->o.cls, CLS_SpriteVisual)) { Sprite *s = (Sprite *)v; (*nspr)++; if (s->brush) { (*nbrush)++; if (s->brush->cls == CLS_CompositionSurfaceBrush) (*nsurf)++; } }
    if (comp_is_a(v->o.cls, CLS_ContainerVisual)) { Container *c = (Container *)v; for (i = 0; i < c->nkids; i++) tree_stats(c->kids[i], nv, nspr, nbrush, nsurf, nhidden); }
}
/* WXR_DCOMP_TREE=path: one-time text dump of the visual tree (class, visible/opacity, offset, size, relSize, clip, brush class/colour) */
static void brush_desc(CObj *b, char *out, size_t n)
{
    char a[160] = "", c[160] = "";
    if (!b) { _snprintf(out, n, "null"); return; }
    if (b->cls == CLS_CompositionColorBrush) { Col k = ((ColorBrush *)b)->color; int l = _snprintf(out, n, "Color#%02X%02X%02X%02X@%p", k.A, k.R, k.G, k.B, (void *)b);
        if (b->props && l > 0) { PSet *p = (PSet *)b->props; int i; for (i = 0; i < p->n && i < 4 && (size_t)l + 40 < n; i++) l += _snprintf(out + l, n - l, " %ls=%.2f", p->e[i].name, p->e[i].v[0]); }
        return; }
    { UINT32 m; CObj *s[2] = { 0 }; int k = comp_effect_composite(b, &m, s, 2);
      if (k > 0) { brush_desc(s[0], a, sizeof a); if (k > 1) brush_desc(s[1], c, sizeof c); _snprintf(out, n, "Composite%u[%s | %s]", m, a, c); return; } }
    if (b->cls == CLS_CompositionSurfaceBrush && ((SBrush *)b)->surface && ((SBrush *)b)->surface->cls == CLS_CompositionVisualSurface) {
        VisSurf *vs = (VisSurf *)((SBrush *)b)->surface; _snprintf(out, n, "VisualSurface(%s %.0fx%.0f)", vs->src ? g_cd[vs->src->cls].sname : "none", vs->size.X, vs->size.Y); return; }
    if (b->cls == CLS_CompositionSurfaceBrush) { int w = 0, h = 0, valid = 0, px; LONG ver = 0; void *bm = comp_surface_bitmap((SBrush *)b, &w, &h); px = comp_surface_census((SBrush *)b, &valid, &ver);
        _snprintf(out, n, "Surface(%dx%d%s st=%d valid=%d ver=%ld alpha-px=%d)", w, h, bm ? "" : " none", ((SBrush *)b)->stretch, valid, ver, px); return; }
    if (b->cls == CLS_CompositionMaskBrush) { brush_desc(((MaskB *)b)->src, a, sizeof a); brush_desc(((MaskB *)b)->mask, c, sizeof c); _snprintf(out, n, "Mask[src=%s mask=%s]", a, c); return; }
    if (b->cls == CLS_CompositionNineGridBrush) { NineG *g = (NineG *)b; brush_desc(g->src, a, sizeof a); _snprintf(out, n, "NineGrid[%s ins=%.0f,%.0f,%.0f,%.0f sc=%.2f hollow=%d]", a, g->l, g->t, g->r, g->b, g->ls, g->hollow); return; }
    _snprintf(out, n, "%s", g_cd[b->cls].sname);
}
static void shape_dump(FILE *f, CObj *sh, int depth)
{
    Shape *s = (Shape *)sh; char a[200] = "", b[200] = "";
    if (!sh) return;
    if (sh->cls == CLS_CompositionContainerShape) { PCol *pc = (PCol *)((ContainerShape *)sh)->shapes; UINT32 i;
        fprintf(f, "%*sContainerShape off=(%.1f,%.1f) scale=(%.2f,%.2f) rot=%.1f n=%u\n", depth * 2, "", s->offset.X, s->offset.Y, s->scale.X, s->scale.Y, s->rotation, pc ? pc->n : 0);
        if (pc) for (i = 0; i < pc->n && depth < 60; i++) shape_dump(f, pc->s[i], depth + 1); return; }
    if (sh->cls == CLS_CompositionSpriteShape) { SpriteShape *p = (SpriteShape *)sh; Geometry *g = (Geometry *)p->geometry;
        if (p->fill) brush_desc(p->fill, a, sizeof a); if (p->stroke) brush_desc(p->stroke, b, sizeof b);
        fprintf(f, "%*sSpriteShape off=(%.1f,%.1f) scale=(%.2f,%.2f) geo=%s%s trim=%.2f..%.2f fill=%s stroke=%s w=%.2f\n", depth * 2, "", s->offset.X, s->offset.Y, s->scale.X, s->scale.Y,
                g ? g_cd[g->o.cls].sname : "-", (g && g->o.cls == CLS_CompositionPathGeometry) ? (((PathGeo *)g)->path && ((CPath *)((PathGeo *)g)->path)->geo ? "(d2d)" : "(no path)") : "",
                g ? g->trimStart : 0, g ? g->trimEnd : 0, a[0] ? a : "-", b[0] ? b : "-", p->thickness); return; }
    fprintf(f, "%*s%s\n", depth * 2, "", g_cd[sh->cls].sname);
}
static void tree_dump(FILE *f, Visual *v, int depth)
{
    int i; char bd[400] = "";
    if (comp_is_a(v->o.cls, CLS_SpriteVisual) && ((Sprite *)v)->brush) brush_desc(((Sprite *)v)->brush, bd, sizeof bd);
    fprintf(f, "%*s%s vis=%d op=%.2f off=(%.1f,%.1f) size=(%.1f,%.1f) tm=(%.2f %.2f / %.1f,%.1f) clip=%s %s\n", depth * 2, "", g_cd[v->o.cls].sname, v->visible, v->opacity, v->offset.X, v->offset.Y,
            v->size.X, v->size.Y, v->tm.M11, v->tm.M22, v->tm.M41, v->tm.M42, v->clip ? g_cd[v->clip->cls].sname : "-", bd);
    if (v->parentForTransform) fprintf(f, "%*s<visual %p: transform parent %p>\n", depth * 2 + 2, "", (void *)v, (void *)v->parentForTransform);
    else if (comp_is_a(v->o.cls, CLS_ContainerVisual) && !((Container *)v)->nkids) fprintf(f, "%*s<visual %p>\n", depth * 2 + 2, "", (void *)v);
    if (v->clip && v->clip->cls == CLS_CompositionGeometricClip) { CObj *g = ((GeoClip *)v->clip)->geo;   /* the clip's geometry (dcomp rectangle clips arrive as these) */
        if (!g) fprintf(f, "%*s<clip geometry: none>\n", depth * 2 + 2, "");
        else if (g->cls == CLS_CompositionRectangleGeometry) { RectGeo *r = (RectGeo *)g; fprintf(f, "%*s<clip rect off (%.1f,%.1f) size (%.1f,%.1f)>\n", depth * 2 + 2, "", r->offset.X, r->offset.Y, r->size.X, r->size.Y); }
        else if (g->cls == CLS_CompositionRoundedRectangleGeometry) { RRectGeo *r = (RRectGeo *)g; fprintf(f, "%*s<clip rrect off (%.1f,%.1f) size (%.1f,%.1f) radius %.1f>\n", depth * 2 + 2, "", r->offset.X, r->offset.Y, r->size.X, r->size.Y, r->radius.X); }
        else fprintf(f, "%*s<clip geometry %s>\n", depth * 2 + 2, "", g_cd[g->cls].sname); }
    if (v->clip && v->clip->cls == CLS_InsetClip) { InsetClip *c = (InsetClip *)v->clip; fprintf(f, "%*s<inset clip rect=%d LTRB (%.1f,%.1f,%.1f,%.1f) ltrb (%.1f,%.1f,%.1f,%.1f)>\n", depth * 2 + 2, "", c->rect, c->L, c->T, c->R, c->B, c->l, c->t, c->r, c->b); }
    if (comp_is_a(v->o.cls, CLS_SpriteVisual) && ((Sprite *)v)->brush && depth < 50) {   /* effect inputs over visual surfaces: their source trees */
        UINT32 m; CObj *s[4] = { 0 }; int k, ns = comp_effect_composite(((Sprite *)v)->brush, &m, s, 4);
        for (k = 0; k < ns; k++) if (s[k] && s[k]->cls == CLS_CompositionSurfaceBrush && ((SBrush *)s[k])->surface && ((SBrush *)s[k])->surface->cls == CLS_CompositionVisualSurface) {
            VisSurf *vs = (VisSurf *)((SBrush *)s[k])->surface; fprintf(f, "%*s<input %d: visual surface %.0fx%.0f off %.1f,%.1f>\n", (depth + 1) * 2, "", k, vs->size.X, vs->size.Y, vs->offset.X, vs->offset.Y);
            if (vs->src && comp_is_a(vs->src->cls, CLS_Visual)) tree_dump(f, (Visual *)vs->src, depth + 2); } }
    if (v->o.cls == CLS_ShapeVisual) { PCol *pc = (PCol *)((ShapeVisual *)v)->shapes; UINT32 k; if (pc) for (k = 0; k < pc->n; k++) shape_dump(f, pc->s[k], depth + 1); }
    if (comp_is_a(v->o.cls, CLS_ContainerVisual)) { Container *c = (Container *)v; for (i = 0; i < c->nkids && depth < 60; i++) tree_dump(f, c->kids[i], depth + 1); }
}
/* a render request (renders are driven by WM_PAINT) */
static void dc_request_paint(HWND h) { InvalidateRect(h, NULL, FALSE); }
static void target_render(DTarget *t)
{
    Compositor *c = (Compositor *)t->o.comp; HRESULT hr; static LONG nrender;
    LONG k = InterlockedIncrement(&nrender);
    if (k <= 3 || k % 200 == 0) {
        int nv = 0, nspr = 0, nb = 0, ns = 0, nh = 0; if (t->root) tree_stats(t->root, &nv, &nspr, &nb, &ns, &nh);
        comp_log("render #%ld: %ux%u sc=%p root=%p (%.0fx%.0f) visuals=%d hidden=%d sprites=%d brushed=%d surface-brushed=%d\n", k, t->w, t->h, (void *)t->sc, (void *)t->root,
                 t->root ? t->root->size.X : 0.0f, t->root ? t->root->size.Y : 0.0f, nv, nh, nspr, nb, ns);
    }
    { static DWORD first, at; static LONG snapped; char b[16];
      if (k == 1) { first = GetTickCount(); at = GetEnvironmentVariableA("WXR_DCOMP_SNAP_AT", b, sizeof b) ? (DWORD)atoi(b) : 8000; }   /* ms after the first frame */
      { extern volatile LONG g_dc_lifted_drawn;   /* WinUI 3: dump once the lifted compositor's surface has been drawn (dcomp_surface.c) */
        if (k >= 3 && (GetTickCount() - first >= at || g_dc_lifted_drawn) && t->w >= 400 && !InterlockedExchange(&snapped, 1)) k = 60; else if (k == 60) k = 61; } }   /* not the small caption targets */
    if (k == 60) { WCHAR p[MAX_PATH]; if (GetEnvironmentVariableW(L"WXR_DCOMP_SNAP", p, MAX_PATH)) comp_log("snapshot -> 0x%08lX\n", (unsigned long)dc_snapshot_png(t->hwnd, p));
        if (t->root && GetEnvironmentVariableW(L"WXR_DCOMP_TREE", p, MAX_PATH)) { FILE *f = _wfopen(p, L"w"); if (f) { int i, n = 0; tree_dump(f, t->root, 0);
            /* orphans: visuals with children but no parent, other than the root (a subtree built but never attached, or attached through
               something this compositor does not follow) */
            {   extern Visual *g_all_vis[]; extern volatile LONG g_all_nvis; void comp_all_visuals_lock(int on); int total = 0, parentless = 0;
                comp_all_visuals_lock(1); total = g_all_nvis;
                for (i = 0; i < g_all_nvis; i++) { Visual *v = g_all_vis[i]; int nv = 0, ns = 0, nb = 0, nsf = 0, nh = 0;
                    if (v == t->root || v->parent) continue; parentless++;
                    if (!comp_is_a(v->o.cls, CLS_ContainerVisual) || !((Container *)v)->nkids) continue;
                    tree_stats(v, &nv, &ns, &nb, &nsf, &nh); fprintf(f, "\n==== orphan %p %s (%d visuals%s) ====\n", (void *)v, g_cd[v->o.cls].sname, nv, v->parentForTransform ? ", has transform parent" : "");
                    if (n++ < 8 && nv < 600) tree_dump(f, v, 1); }
                comp_all_visuals_lock(0);
                fprintf(f, "\n==== live visuals %d, parentless (besides root) %d ====\n", total, parentless); }
            fclose(f); } } }
#ifndef WXR_RETAIL   /* a diagnostic: not even its environment lookup in a retail build */
    { WCHAR p2[MAX_PATH]; static LONG small; if (t->w < 400 && t->root && t->w && GetEnvironmentVariableW(L"WXR_DCOMP_TREE_SMALL", p2, MAX_PATH) && InterlockedIncrement(&small) == 20) { FILE *f2 = _wfopen(p2, L"w"); if (f2) { tree_dump(f2, t->root, 0); fclose(f2); } } }
#endif
    if (!t->hwnd || !ensure_rt(t)) { if (k <= 3) comp_log("render #%ld: no render target (hwnd %p)\n", k, (void *)t->hwnd); return; }
    EnterCriticalSection(&g_dc_lock);
    { LARGE_INTEGER q0, q1, q2, q3, q4; memset(&g_pf, 0, sizeof g_pf); QueryPerformanceCounter(&q0);
    c->rendering++; c->dc_pending = 0; comp_tick(&c->o);
    QueryPerformanceCounter(&q1);
    DCB_BeginDraw(t->rt);
    draw_scene(t, t->rt, t->w, t->h);
    QueryPerformanceCounter(&q2);
    hr = DCB_EndDraw(t->rt, NULL, NULL);
    QueryPerformanceCounter(&q3);
    if (perflog()) { static double st, sd, se, sp; static LONG nf; static LARGE_INTEGER last, win0; double gap = last.QuadPart ? pf_ms(last, q0) : 0; (void)gap; (void)q4;
        if (!nf) win0 = q0; st += pf_ms(q0, q1); sd += pf_ms(q1, q2); se += pf_ms(q2, q3); sp += t->last_present_ms; last = q0;
        if (++nf == 30) { comp_log("perf: 30 frames in %.0f ms; per frame tick %.1f draw %.1f enddraw %.1f present %.1f ms; last frame: visuals %ld sprites %ld masks %ld (%.1f ms) vissurf %ld (%.1f) composite %ld (%.1f) surfaces %ld (%.1f) shapes %ld (%.1f) offscreens %ld layers %ld culled %ld; tick: %s\n",
            pf_ms(win0, q3), st / 30, sd / 30, se / 30, sp / 30, g_pf.visuals, g_pf.sprites, g_pf.masks, g_pf.ms_mask, g_pf.vissurfs, g_pf.ms_vissurf, g_pf.composites, g_pf.ms_comp, g_pf.surfdraws, g_pf.ms_surf, g_pf.shapes, g_pf.ms_shape, g_pf.offscreens, g_pf.layers, g_pf.culled, g_tick_stats); nf = 0; st = sd = se = sp = 0; } }
    }
    if (hr == (HRESULT)0x8899000C) release_rt(t); else if (FAILED(hr)) comp_log("EndDraw failed 0x%08lX\n", (unsigned long)hr);
    c->rendering--;
    LeaveCriticalSection(&g_dc_lock);
    if (t->sc) { LARGE_INTEGER p0, p1; QueryPerformanceCounter(&p0); hr = IDXGISwapChain1_Present(t->sc, 1, 0); QueryPerformanceCounter(&p1); t->last_present_ms = pf_ms(p0, p1); if (FAILED(hr) && k <= 200) comp_log("Present failed 0x%08lX\n", (unsigned long)hr); if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { comp_log("device removed/reset (0x%08lX)\n", (unsigned long)hr); release_rt(t); }
      else if (SUCCEEDED(hr)) {   /* the WXR shell's splash screen leaves once the app's frames arrive (wxrsys WxrFramePresented) */
        static void (WINAPI *fp)(HWND); static LONG looked;
        if (!InterlockedExchange(&looked, 1)) { HMODULE u = GetModuleHandleA("wxrsys.dll"); fp = u ? (void (WINAPI *)(HWND))GetProcAddress(u, "WxrFramePresented") : NULL; }
        if (fp) fp(t->hwnd); repaint_children_above(t); } }
    comp_batch_fire(&c->o);
    if (c->dirty) { c->dirty = 0; if (!comp_has_active(&c->o) && IsWindow(t->hwnd)) dc_request_paint(t->hwnd); }
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
        if (wp != SIZE_MINIMIZED) resize_rt(t, LOWORD(lp) ? LOWORD(lp) : 1, HIWORD(lp) ? HIWORD(lp) : 1);
        dc_request_paint(hwnd); return r;
    case WM_NCDESTROY:
        ensure_timer(t, FALSE); SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)old); RemovePropW(hwnd, PROP_TARGET); t->subclassed = FALSE; release_rt(t); t->hwnd = NULL;
        r = CallWindowProcW(old, hwnd, msg, wp, lp); return r;
    }
    return CallWindowProcW(old, hwnd, msg, wp, lp);
}

/* ============================ targets / invalidation ============================ */
static void comp_add_target(Compositor *c, DTarget *t)
{
    if (c->ntargets == c->captargets) {
        int nc = c->captargets ? c->captargets * 2 : 4; DTarget **n = (DTarget **)HeapAlloc(GetProcessHeap(), 0, (size_t)nc * sizeof(DTarget *)); if (!n) return;
        if (c->targets) { memcpy(n, c->targets, (size_t)c->ntargets * sizeof(DTarget *)); HeapFree(GetProcessHeap(), 0, c->targets); } c->targets = n; c->captargets = nc;
    }
    c->targets[c->ntargets++] = t;
}
static void comp_remove_target(Compositor *c, DTarget *t) { int i; for (i = 0; i < c->ntargets; i++) if (c->targets[i] == t) { c->targets[i] = c->targets[--c->ntargets]; return; } }
/* every window a target was bound to (any compositor): WinUI 3's lifted compositor commits on a device without targets of its own - its
   content reaches the windows through shared visuals - so its commits repaint all of them */
static HWND g_dc_all_hwnd[32]; static volatile LONG g_dc_nall;
static void dc_note_target_hwnd(HWND h) { LONG i, n = g_dc_nall; for (i = 0; i < n && i < 32; i++) if (g_dc_all_hwnd[i] == h) return; i = InterlockedIncrement(&g_dc_nall) - 1; if (i < 32) g_dc_all_hwnd[i] = h; else InterlockedDecrement(&g_dc_nall); }
static void repaint_targets(Compositor *c)
{
    int i; for (i = 0; i < c->ntargets; i++) { DTarget *t = c->targets[i]; if (t->hwnd && IsWindow(t->hwnd)) { dc_request_paint(t->hwnd); if (comp_has_active(&c->o)) ensure_timer(t, TRUE); } }
    if (!c->ntargets) { LONG n = g_dc_nall; for (i = 0; i < n && i < 32; i++) if (g_dc_all_hwnd[i] && IsWindow(g_dc_all_hwnd[i])) dc_request_paint(g_dc_all_hwnd[i]); }
}
/* scene changes made through the dcomp API only repaint at Commit (DComp transaction semantics) */
static volatile DWORD g_dc_last_change;   /* tick of the last scene change (any invalidation) */
void comp_invalidate(CObj *co)
{
    Compositor *c = (Compositor *)co; extern volatile LONG g_comp_gen; InterlockedIncrement(&g_comp_gen); if (!c) return; g_dc_last_change = GetTickCount() | 1;
    if (c->rendering) { c->dirty = 1; return; }
    c->dc_pending = 1;
    if (comp_has_active(co)) repaint_targets(c);                                          /* running animations keep the timer alive */
}
void dc_commit(CObj *co)
{
    Compositor *c = (Compositor *)co; static LONG n; static DWORD t0; if (!c) return;
    if (!t0) t0 = GetTickCount(); { LONG k = InterlockedIncrement(&n); if (k <= 40 || k % 50 == 0) comp_log("commit #%ld at +%lu ms (targets %d)\n", k, GetTickCount() - t0, c->ntargets); }
    {   /* commit storm detector (DirectManipulation once committed thousands of times a second) */
        static DWORD sec; static LONG cnt, warned; DWORD now = GetTickCount() / 1000;
        if (now != sec) { sec = now; cnt = 0; } else if (InterlockedIncrement(&cnt) == 300 && warned++ < 5) comp_log("commit storm: 300 commits within one second (targets %d)\n", c->ntargets); }
    c->dc_pending = 0; repaint_targets(c); dcomp_commit_done(co);
    /* XAML schedules its next frame when the compositor reports itself dirty (IInteropCompositorPartnerCallback slot 3 NotifyDirty). Only while
       the scene is changing (or did within 2 s, for time-driven animations), else the two tick each other forever (wine-uwp "Idle loop") */
    /* DirectManipulation committed its own device after moving content (dcomp_manip.c): XAML must tick to apply it */
    if (dcomp_manip_take_dirty() || (g_dc_last_change && GetTickCount() - g_dc_last_change < 2000)) {
        extern IUnknown *g_dc_partner_cbs[4]; extern LONG g_dc_npartner; LONG i, m = g_dc_npartner < 4 ? g_dc_npartner : 4;
        for (i = 0; i < m; i++) if (g_dc_partner_cbs[i]) ((HRESULT (STDMETHODCALLTYPE *)(void *))(*(void ***)g_dc_partner_cbs[i])[3])(g_dc_partner_cbs[i]);
    }
}

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
    comp_log("dcomp: target bound to hwnd %p\n", (void *)hwnd);
    dc_note_target_hwnd(hwnd); dc_request_paint(hwnd); return S_OK;
}
static DTarget *g_unbound[16]; static int g_nunbound;
CObj *dc_create_target(CObj *comp, HWND hwnd, BOOL topmost)
{
    DTarget *t = (DTarget *)comp_new(CLS_DesktopWindowTarget, comp); if (!t) return NULL; t->topmost = topmost;
    comp_add_target((Compositor *)comp, t);
    if (!hwnd) hwnd = g_dc_target_hwnd;
    if (hwnd) { HRESULT hr = target_hook(t, hwnd); if (FAILED(hr)) { comp_log("dcomp: target bind to %p failed 0x%08lX\n", (void *)hwnd, (unsigned long)hr); } }
    else { comp_log("dcomp: target created without a window; waiting for WxrDcomp_SetTargetWindow\n"); if (g_nunbound < 16) { g_unbound[g_nunbound++] = t; comp_addref(&t->o); } }
    return &t->o;
}
__declspec(dllexport) void WINAPI WxrDcomp_SetTargetWindow(HWND hwnd)
{
    int i; g_dc_target_hwnd = hwnd; comp_log("WxrDcomp_SetTargetWindow(%p)\n", (void *)hwnd);
    for (i = 0; i < g_nunbound; i++) { DTarget *t = g_unbound[i]; if (!t->hwnd && hwnd) target_hook(t, hwnd); comp_release(&t->o); }
    g_nunbound = 0;
}
void dc_target_set_root(CObj *target, Visual *root)
{
    DTarget *t = (DTarget *)target; if (root) comp_addref(&root->o); if (t->root) comp_release(&t->root->o); t->root = root; t->root_explicit = root != NULL;
    comp_log("dcomp: SetRoot(%p)\n", (void *)root); INVALIDATE(&t->o);
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionTarget, get_Root)(W(ICompositionTarget) *This, W(IVisual) **r) { DTarget *t = (DTarget *)OBJ_OF(This); return comp_return(t->root ? &t->root->o : NULL, &IID_(IVisual), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionTarget, put_Root)(W(ICompositionTarget) *This, W(IVisual) *v) { dc_target_set_root(OBJ_OF(This), v ? (Visual *)OBJ_OF(v) : NULL); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(Desktop_IDesktopWindowTarget, get_IsTopmost)(Windows_UI_Composition_Desktop_IDesktopWindowTarget *This, boolean *r) { *r = (boolean)((DTarget *)OBJ_OF(This))->topmost; return S_OK; }

/* IDCompositionTarget view (SetRoot) */
HRESULT dc_target_qi_extra(CObj *o, REFIID r, void **p)
{
    DTarget *t = (DTarget *)o;
    if (IsEqualGUID(r, &DCIID_Target)) { t->dct.vt = DCVT_Target; t->dct.obj = o; comp_addref(o); *p = &t->dct; return S_OK; }
    return E_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE dcT_SetRoot(void *This, void *visual)
{
    Visual *v = visual ? dc_visual_from(visual) : NULL; if (visual && !v) return E_INVALIDARG; dc_target_set_root(OBJ_OF(This), v); return S_OK;
}

/* ICompositorDesktopInterop (WinRT side of CreateTargetForHwnd) */
static const GUID IID_ICompositorDesktopInterop_ = {0x29E691FA, 0x4567, 0x4DCA, {0xB3, 0x19, 0xD0, 0xF2, 0x07, 0xEB, 0x68, 0x07}};
typedef struct DesktopInteropVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *CreateDesktopWindowTarget)(void *, HWND, BOOL, Windows_UI_Composition_Desktop_IDesktopWindowTarget **);
    HRESULT (STDMETHODCALLTYPE *EnsureOnThread)(void *, DWORD);
} DesktopInteropVtbl;
static HRESULT STDMETHODCALLTYPE di_Create(void *This, HWND hwnd, BOOL topmost, Windows_UI_Composition_Desktop_IDesktopWindowTarget **result)
{
    CObj *t, *comp = OBJ_OF(This); HRESULT hr; if (!result) return E_POINTER; *result = NULL;
    t = dc_create_target(comp, hwnd, topmost); if (!t) return E_OUTOFMEMORY;
    hr = comp_qi(t, &IID_Windows_UI_Composition_Desktop_IDesktopWindowTarget, (void **)result); comp_release(t); return hr;
}
static HRESULT STDMETHODCALLTYPE di_Ensure(void *This, DWORD tid) { (void)This; (void)tid; return S_OK; }
static const DesktopInteropVtbl VT_DesktopInterop = { cv_QI, cv_AddRef, cv_Release, di_Create, di_Ensure };
HRESULT dc_desktop_interop_qi(CObj *o, REFIID r, void **p)
{
    Compositor *c = (Compositor *)o;
    if (IsEqualGUID(r, &IID_ICompositorDesktopInterop_)) { c->desktopInterop.vt = &VT_DesktopInterop; c->desktopInterop.obj = o; comp_addref(o); *p = &c->desktopInterop; return S_OK; }
    return E_NOINTERFACE;
}

/* ---- snapshot: render the tree of the bound window into an offscreen bitmap and save a PNG (WIC) ---- */
#include <wincodec.h>
HRESULT dc_snapshot_png(HWND hwnd, const WCHAR *path)
{
    DTarget *t = IsWindow(hwnd) ? (DTarget *)GetPropW(hwnd, PROP_TARGET) : NULL; DcDev *d = dc_dev(); ID2D1DeviceContext *dc = NULL; ID2D1Bitmap1 *bmp = NULL, *stage = NULL; D2D1_BITMAP_PROPERTIES1 bp;
    D2D1_MAPPED_RECT mr; HRESULT hr; UINT w, h; Compositor *c; IWICImagingFactory *wic = NULL; IWICBitmapEncoder *enc = NULL; IWICBitmapFrameEncode *fr = NULL; IWICStream *st = NULL; IPropertyBag2 *pb = NULL; D2D1_SIZE_U sz;
    if (!t || !d) return E_INVALIDARG; c = (Compositor *)t->o.comp; w = t->w ? t->w : 1; h = t->h ? t->h : 1; if (!t->w) { RECT rc; GetClientRect(hwnd, &rc); w = rc.right > 0 ? rc.right : 1; h = rc.bottom > 0 ? rc.bottom : 1; }
    sz.width = w; sz.height = h;
    hr = ID2D1Device_CreateDeviceContext(d->d2d, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc); if (FAILED(hr)) return hr;
    memset(&bp, 0, sizeof bp); bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED; bp.dpiX = 96; bp.dpiY = 96; bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    hr = dc->lpVtbl->CreateBitmap(dc, sz, NULL, 0, &bp, &bmp);
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;
    if (SUCCEEDED(hr)) hr = dc->lpVtbl->CreateBitmap(dc, sz, NULL, 0, &bp, &stage);
    if (SUCCEEDED(hr)) {
        ID2D1SolidColorBrush *saved = t->solid; D2D1_COLOR_F white = { 1, 1, 1, 1 };
        t->solid = NULL; DCB_CreateSolidColorBrush(dc, &white, NULL, &t->solid);
        EnterCriticalSection(&g_dc_lock); c->rendering++; comp_tick(&c->o);
        ID2D1DeviceContext_SetTarget(dc, (void *)bmp); DCB_BeginDraw(dc); draw_scene(t, dc, w, h); hr = DCB_EndDraw(dc, NULL, NULL);
        c->rendering--; LeaveCriticalSection(&g_dc_lock);
        if (t->solid) ID2D1SolidColorBrush_Release(t->solid); t->solid = saved;
        if (SUCCEEDED(hr)) hr = ((ID2D1Bitmap *)stage)->lpVtbl->CopyFromBitmap((ID2D1Bitmap *)stage, NULL, (ID2D1Bitmap *)bmp, NULL);
        if (SUCCEEDED(hr)) hr = ID2D1Bitmap1_Map(stage, D2D1_MAP_OPTIONS_READ, &mr);
        if (SUCCEEDED(hr)) {
            hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wic);
            if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateStream(wic, &st);
            if (SUCCEEDED(hr)) hr = IWICStream_InitializeFromFilename(st, path, GENERIC_WRITE);
            if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(wic, &GUID_ContainerFormatPng, NULL, &enc);
            if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, (IStream *)st, WICBitmapEncoderNoCache);
            if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &fr, &pb);
            if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(fr, pb);
            if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(fr, w, h);
            if (SUCCEEDED(hr)) { WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA; hr = IWICBitmapFrameEncode_SetPixelFormat(fr, &pf); }
            if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WritePixels(fr, h, mr.pitch, mr.pitch * h, mr.bits);
            if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(fr);
            if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);
            ID2D1Bitmap1_Unmap(stage);
        }
    }
    if (pb) IPropertyBag2_Release(pb); if (fr) IWICBitmapFrameEncode_Release(fr); if (enc) IWICBitmapEncoder_Release(enc); if (st) IWICStream_Release(st); if (wic) IWICImagingFactory_Release(wic);
    if (stage) UREL(stage); if (bmp) UREL(bmp); ID2D1DeviceContext_SetTarget(dc, NULL); UREL(dc);
    return hr;
}
__declspec(dllexport) HRESULT WINAPI WxrDcomp_SnapshotToPng(HWND hwnd, const WCHAR *path) { return dc_snapshot_png(hwnd, path); }
__declspec(dllexport) HRESULT WINAPI WxrDcomp_RenderNow(HWND hwnd)
{
    DTarget *t = IsWindow(hwnd) ? (DTarget *)GetPropW(hwnd, PROP_TARGET) : NULL; if (!t) return E_INVALIDARG; if (!t->in_paint) { t->in_paint = TRUE; target_render(t); t->in_paint = FALSE; } return S_OK;
}

/* ---- registration (engine classes get the dcomp views) ---- */
void dc_register_render(void)
{
    g_impl[CLS_CompositionTarget].size = sizeof(DTarget);
    g_impl[CLS_DesktopWindowTarget].size = sizeof(DTarget); g_impl[CLS_DesktopWindowTarget].destroy = target_destroy; g_impl[CLS_DesktopWindowTarget].qi_extra = dc_target_qi_extra;
}
