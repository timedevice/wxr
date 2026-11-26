/* dcomp_surface.c - drawing surfaces on a Direct3D 11 texture + Direct2D 1.1: IDCompositionSurface(+internal), WinRT CompositionDrawingSurface (+interop),
 * CompositionGraphicsDevice, CompositionSurfaceBrush, ICompositorInterop.  Replaces comp_surface.c in the dcomp.dll build. */
#include "dcomp_priv.h"
#include <stdio.h>
#define VT_(o, k, T) ((T)(*(void ***)(o))[k])
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n

static const GUID IID_ICompositionDrawingSurfaceInterop_ = {0xFD04E6E3, 0xFE0C, 0x4C3E, {0xAB, 0x19, 0xA0, 0x76, 0x01, 0xA5, 0x76, 0xEE}};
static const GUID IID_ICompositorInterop_ = {0x25297D5C, 0x3AD4, 0x4C9C, {0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30}};

typedef struct DSurf {
    CObj o; int w, h; DXGI_FORMAT fmt; DXGI_ALPHA_MODE am; int pixfmt; BYTE drawing, virt, valid; RECT upd; LONG version;
    ID3D11Texture2D *tex; ID2D1DeviceContext *dc; ID2D1Bitmap1 *bmp; CView surf, vsurf, sint, interop;
    ID3D11Texture2D *a8;   /* A8 surface backed by BGRA: the real A8 texture XAML writes through IDXGISurface (converted at EndDraw) */
    CObj *fwd; CView wpart;   /* surface wrapper (ICompositionSurfaceWrapperPartner): the surface shown in place of this one, see wp_Set */
    BYTE gdi;                 /* texture made GDI-compatible on first BeginDraw(IDXGISurface1) - see surf_begin */
    ID3D11Device *cdev;       /* the creating compositor's client device (dc_client_device), no reference */
    ID3D11Texture2D *ctex;    /* tex opened on cdev (shared handle): what BeginDraw(ID3D11Texture2D) hands a client device */
} DSurf;
/* the surface whose pixels a composition surface shows: itself, or the one XAML attached to it as a wrapper; NULL if o is no drawing surface */
static DSurf *ds_of(CObj *o)
{
    int k;
    if (!o || !comp_is_a(o->cls, CLS_CompositionDrawingSurface)) return NULL;
    for (k = 0; k < 4 && ((DSurf *)o)->fwd; k++) o = ((DSurf *)o)->fwd;
    return (DSurf *)o;
}

volatile LONG g_dc_lifted_drawn;   /* a client-device (WinUI 3 lifted compositor) surface has been drawn: dcomp_render.c's tree dump trigger */
static void ds_free_gpu(DSurf *s)
{
    if (s->dc) { ID2D1DeviceContext_SetTarget(s->dc, NULL); UREL(s->dc); s->dc = NULL; }
    if (s->bmp) { UREL(s->bmp); s->bmp = NULL; }
    if (s->tex) { ID3D11Texture2D_Release(s->tex); s->tex = NULL; }
    if (s->a8) { ID3D11Texture2D_Release(s->a8); s->a8 = NULL; }
    if (s->ctex) { ID3D11Texture2D_Release(s->ctex); s->ctex = NULL; }
}
/* A8 (alpha-only) surfaces are backed by BGRA textures: XAML renders text into A8 masks, and D2D drawing into an A8 target crashed the
   NVIDIA driver's worker thread (Clock). The alpha channel carries the coverage, which is all a mask reads. */
static DXGI_FORMAT ds_texfmt(DSurf *s)
{
    return s->fmt == DXGI_FORMAT_A8_UNORM ? DXGI_FORMAT_B8G8R8A8_UNORM : s->fmt;
}
static HRESULT ds_ensure(DSurf *s)
{
    DcDev *d = dc_dev(); D3D11_TEXTURE2D_DESC td; IDXGISurface *ds = NULL; D2D1_BITMAP_PROPERTIES1 bp; HRESULT hr;
    if (s->bmp && s->dc) return S_OK; if (!d) return E_FAIL; if (s->w <= 0 || s->h <= 0) return E_FAIL;
    ds_free_gpu(s);
    memset(&td, 0, sizeof td); td.Width = (UINT)s->w; td.Height = (UINT)s->h; td.MipLevels = 1; td.ArraySize = 1; td.Format = ds_texfmt(s); td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    /* GDI-compatible ONLY on request (s->gdi, set by BeginDraw(IDXGISurface1): WinUI 3's title bar draws caption text with GDI ->
       GetDC, which fails without the flag). Making every BGRA surface GDI-compatible turned Calculator black. */
    if (s->gdi && td.Format == DXGI_FORMAT_B8G8R8A8_UNORM) td.MiscFlags |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    if (!s->cdev) s->cdev = dc_client_device(s->o.comp);
    if (s->cdev && !(td.MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE)) td.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;   /* opened on the client device in BeginDraw */
    hr = ID3D11Device_CreateTexture2D(d->d3d, &td, NULL, &s->tex);
    if (FAILED(hr) && td.MiscFlags) { td.MiscFlags = 0; hr = ID3D11Device_CreateTexture2D(d->d3d, &td, NULL, &s->tex); } if (FAILED(hr)) { comp_log("surface texture %dx%d fmt %d failed 0x%08lX\n", s->w, s->h, (int)s->fmt, (unsigned long)hr); return hr; }
    hr = ID3D11Texture2D_QueryInterface(s->tex, &IID_IDXGISurface, (void **)&ds); if (FAILED(hr)) return hr;
    memset(&bp, 0, sizeof bp); bp.pixelFormat.format = ds_texfmt(s); bp.pixelFormat.alphaMode = s->am == DXGI_ALPHA_MODE_IGNORE ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED; bp.dpiX = 96; bp.dpiY = 96; bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    hr = ID2D1Device_CreateDeviceContext(d->d2d, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &s->dc);
    /* the CPU glyph path patches d2d1 itself: armed with the first context, not only at a D2D BeginDraw - WinUI 3's XAML draws its text with its
       own d2d1 contexts into its own compositor's surfaces and never draws through ours (no text, no FontIcons) */
    if (SUCCEEDED(hr) && s->dc) dc_text_hooks_install(s->dc);
    if (SUCCEEDED(hr)) hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(s->dc, ds, &bp, &s->bmp);
    IDXGISurface_Release(ds);
    /* the bitmap is bound as s->dc's target only between BeginDraw and EndDraw: d2d1 refuses to draw a bitmap bound as a target anywhere
       (DrawingContext::CheckTargetBoundAndFlushHazards, 0x88982F80), and the renderer draws these bitmaps */
    if (FAILED(hr)) { comp_log("surface D2D target failed 0x%08lX\n", (unsigned long)hr); ds_free_gpu(s); }
    return hr;
}
static void ds_destroy(CObj *o) { DSurf *s = (DSurf *)o; ds_free_gpu(s); if (s->fwd) { comp_release(s->fwd); s->fwd = NULL; } }
static void ds_init(CObj *o) { DSurf *s = (DSurf *)o; s->fmt = DXGI_FORMAT_B8G8R8A8_UNORM; s->am = DXGI_ALPHA_MODE_PREMULTIPLIED; s->pixfmt = 87; }

CObj *dc_create_surface(CObj *comp, int w, int h, DXGI_FORMAT fmt, DXGI_ALPHA_MODE am, int virt)
{
    DSurf *s; CObj *o;
    if (w < 0 || h < 0 || w > 16384 || h > 16384) return NULL;
    o = comp_new(virt ? CLS_CompositionVirtualDrawingSurface : CLS_CompositionDrawingSurface, comp); if (!o) return NULL;
    s = (DSurf *)o; s->w = w; s->h = h; s->virt = (BYTE)virt; s->fmt = fmt ? fmt : DXGI_FORMAT_B8G8R8A8_UNORM; s->am = am ? am : DXGI_ALPHA_MODE_PREMULTIPLIED; s->pixfmt = (int)s->fmt;
    if (w && h) { HRESULT hr = ds_ensure(s); if (FAILED(hr)) { comp_release(o); return NULL; } }
    return o;
}
CObj *dc_surface_from_unknown(IUnknown *u)
{
    void *p = NULL; CObj *o; if (!u) return NULL;
    if (FAILED(IUnknown_QueryInterface(u, &IID_IUnknown, &p)) || !p) return NULL;
    o = ((CView *)p)->obj; ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p);
    return (o && o->cls >= 0 && comp_is_a(o->cls, CLS_CompositionDrawingSurface)) ? o : NULL;
}

/* the update rect of the A8 mirror -> the BGRA texture, on the GPU: the A8 texture as an opacity mask painting white into the BGRA target
   (premultiplied white = alpha in every channel). A CPU readback (Map of a staging copy) deadlocked in the NVIDIA driver. */
static void a8_to_bgra(DSurf *s)
{
    IDXGISurface *ds = NULL; ID2D1Bitmap1 *ab = NULL; ID2D1SolidColorBrush *wb = NULL; D2D1_BITMAP_PROPERTIES1 bp; D2D1_COLOR_F white = { 1, 1, 1, 1 }, none = { 0, 0, 0, 0 };
    D2D1_RECT_F r; D2D1_MATRIX_3X2_F id = { 1, 0, 0, 1, 0, 0 }; HRESULT hr;
    if (!s->dc || !s->bmp || FAILED(ID3D11Texture2D_QueryInterface(s->a8, &IID_IDXGISurface, (void **)&ds))) return;
    memset(&bp, 0, sizeof bp); bp.pixelFormat.format = DXGI_FORMAT_A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED; bp.dpiX = 96; bp.dpiY = 96;
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(s->dc, ds, &bp, &ab); IDXGISurface_Release(ds);
    if (FAILED(hr)) { static LONG n; if (InterlockedIncrement(&n) <= 3) comp_log("surface A8 mirror: bitmap 0x%08lX\n", (unsigned long)hr); return; }
    r.left = (float)s->upd.left; r.top = (float)s->upd.top; r.right = (float)s->upd.right; r.bottom = (float)s->upd.bottom;
    ID2D1DeviceContext_SetTarget(s->dc, (void *)s->bmp); DCB_BeginDraw(s->dc); DCB_SetTransform(s->dc, &id);
    DCB_PushAxisAlignedClip(s->dc, &r, D2D1_ANTIALIAS_MODE_ALIASED); DCB_Clear(s->dc, &none);
    if (SUCCEEDED(DCB_CreateSolidColorBrush(s->dc, &white, NULL, &wb))) {
        D2D1_ANTIALIAS_MODE aa = DCB_(GetAntialiasMode, s->dc); DCB_SetAntialiasMode(s->dc, D2D1_ANTIALIAS_MODE_ALIASED);
        DCB_(FillOpacityMask, s->dc, (ID2D1Bitmap *)ab, (ID2D1Brush *)wb, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &r, &r);
        DCB_SetAntialiasMode(s->dc, aa); ID2D1SolidColorBrush_Release(wb); }
    DCB_PopAxisAlignedClip(s->dc); hr = DCB_EndDraw(s->dc, NULL, NULL); ID2D1DeviceContext_SetTarget(s->dc, NULL);
    if (FAILED(hr)) { static LONG n2; if (InterlockedIncrement(&n2) <= 3) comp_log("surface A8 mirror: EndDraw 0x%08lX\n", (unsigned long)hr); }
    UREL(ab);
}
/* ---- shared BeginDraw / EndDraw ---- */
static HRESULT surf_begin(DSurf *s, const RECT *upd, REFIID iid, void **obj, POINT *off, int clear, int gutter, void *caller)
{
    RECT u; HRESULT hr; D2D1_RECT_F clip;
    if (!obj || !off) return E_POINTER; *obj = NULL; off->x = off->y = 0;
    { static LONG nlog; char g[40]; if (InterlockedIncrement(&nlog) <= 400) comp_log("surface BeginDraw dc %p %dx%d fmt %d iid %s upd %s(%ld,%ld,%ld,%ld) clear %d\n", (void *)s->dc, s->w, s->h, (int)s->fmt, dc_guid(iid, g),
        upd ? "" : "null ", upd ? upd->left : 0, upd ? upd->top : 0, upd ? upd->right : 0, upd ? upd->bottom : 0, clear); }
    if (s->drawing) return E_FAIL;
    /* WinUI 3's lifted compositor (dwmcorei) draws on its own D3D device: hand it the texture opened on that device (shared). Recognised by
       the caller module; the device comes from wxrsys (WxrDx_GetLiftedDevice). The texture is recreated SHARED the first time. */
    if (!s->cdev && caller && (IsEqualGUID(iid, &IID_ID3D11Texture2D) || IsEqualGUID(iid, &IID_ID3D11Resource))) { HMODULE cm = NULL; WCHAR mp[MAX_PATH]; const WCHAR *b;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)caller, &cm) && GetModuleFileNameW(cm, mp, MAX_PATH)
            && (b = wcsrchr(mp, L'\\')) && !lstrcmpiW(b + 1, L"dwmcorei.dll")) {
            typedef HRESULT (WINAPI *PGD)(REFIID, void **); PGD gl = (PGD)GetProcAddress(GetModuleHandleW(L"wxrsys.dll"), "WxrDx_GetLiftedDevice"); ID3D11Device *ld = NULL; DcDev *d = dc_dev();
            if (gl && SUCCEEDED(gl(&IID_ID3D11Device, (void **)&ld)) && ld) {
                if (!d || ld != d->d3d) { s->cdev = ld; if (s->tex) ds_free_gpu(s); }   /* wxrsys keeps the device alive */
                ID3D11Device_Release(ld); } } }
    if (IsEqualGUID(iid, &IID_IDXGISurface1) && !s->gdi && s->fmt == DXGI_FORMAT_B8G8R8A8_UNORM) {   /* GDI drawing wanted: recreate the texture GDI-compatible */
        s->gdi = 1; if (s->tex) { ds_free_gpu(s); comp_log("surface %dx%d: recreated GDI-compatible for IDXGISurface1 (content restarts)\n", s->w, s->h); } }
    hr = ds_ensure(s); if (FAILED(hr)) return hr;
    if (upd) { u = *upd; if (gutter) { u.left -= gutter; u.top -= gutter; u.right += gutter; u.bottom += gutter; } if (u.left < 0) u.left = 0; if (u.top < 0) u.top = 0; if (u.right > s->w) u.right = s->w; if (u.bottom > s->h) u.bottom = s->h;
        if (u.left >= u.right || u.top >= u.bottom) return E_INVALIDARG; }
    else { u.left = u.top = 0; u.right = s->w; u.bottom = s->h; }
    /* texture-level access: IDXGISurface (XAML's CPU rasterizer) and the D3D11 resource itself - WinUI 3's title bar
       (Windowing.Core DCompUtil::UpdateSinglePixelSurfaceColor) asks for ID3D11Texture2D and writes it through the device context */
    if (IsEqualGUID(iid, &IID_IDXGISurface) || IsEqualGUID(iid, &IID_IDXGISurface1) || IsEqualGUID(iid, &IID_ID3D11Texture2D) || IsEqualGUID(iid, &IID_ID3D11Resource)) { ID3D11Texture2D *tx = s->tex;
        if (ds_texfmt(s) != s->fmt && s->fmt == DXGI_FORMAT_A8_UNORM) {   /* A8 backed by BGRA: XAML writes alpha bytes, so it gets a real A8 texture (kept: it mirrors the content) */
            if (!s->a8) { D3D11_TEXTURE2D_DESC td; DcDev *d = dc_dev(); ID3D11Texture2D_GetDesc(s->tex, &td); td.Format = DXGI_FORMAT_A8_UNORM; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;   /* no A8 render targets: this driver mishandles them */
                if (!d || FAILED(hr = ID3D11Device_CreateTexture2D(d->d3d, &td, NULL, &s->a8))) { comp_log("surface A8 mirror failed 0x%08lX\n", (unsigned long)hr); return E_FAIL; } }
            tx = s->a8; }
        else if (s->cdev) {   /* client device (WinUI 3 dwmcorei): the same texture opened on its device */
            if (!s->ctex) { IDXGIResource *r = NULL; HANDLE sh = NULL;
                if (SUCCEEDED(ID3D11Texture2D_QueryInterface(s->tex, &IID_IDXGIResource, (void **)&r)) && r) { IDXGIResource_GetSharedHandle(r, &sh); IDXGIResource_Release(r); }
                hr = sh ? ID3D11Device_OpenSharedResource(s->cdev, sh, &IID_ID3D11Texture2D, (void **)&s->ctex) : E_FAIL;
                { static LONG nl; if (InterlockedIncrement(&nl) <= 8) comp_log("surface %dx%d: opened on client device %p (shared %p) -> 0x%08lX\n", s->w, s->h, (void *)s->cdev, (void *)sh, (unsigned long)hr); } }
            if (s->ctex) tx = s->ctex; }
        s->upd = u; off->x = u.left; off->y = u.top; hr = ID3D11Texture2D_QueryInterface(tx, iid, obj); if (SUCCEEDED(hr)) { s->drawing = 2; } return hr; }
    hr = IUnknown_QueryInterface((IUnknown *)s->dc, iid, obj);
    if (FAILED(hr)) { char g[40]; comp_log("surface BeginDraw: interface %s not available on the context (0x%08lX)\n", dc_guid(iid, g), (unsigned long)hr); return hr; }
    dc_text_hooks_install(s->dc);                                                             /* d2d1 glyph runs -> CPU rasterizer (dcomp_textproxy.c) */
    ID2D1DeviceContext_SetTarget(s->dc, (void *)s->bmp); DCB_BeginDraw(s->dc);
    if (s->fmt == DXGI_FORMAT_A8_UNORM) DCB_(SetTextAntialiasMode, s->dc, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   /* ClearType cannot render into alpha-only targets (XAML's glyph masks) */
    clip.left = (float)u.left; clip.top = (float)u.top; clip.right = (float)u.right; clip.bottom = (float)u.bottom;
    DCB_PushAxisAlignedClip(s->dc, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    if (clear) { D2D1_COLOR_F c = { 0, 0, 0, 0 }; DCB_Clear(s->dc, &c); }
    s->drawing = 1; s->upd = u; off->x = u.left; off->y = u.top; return S_OK;
}
static HRESULT surf_end(DSurf *s)
{
    HRESULT hr = S_OK; if (!s->drawing) return E_FAIL;
    if (s->drawing == 1) { DCB_PopAxisAlignedClip(s->dc);
        hr = DCB_EndDraw(s->dc, NULL, NULL); ID2D1DeviceContext_SetTarget(s->dc, NULL); if (FAILED(hr)) comp_log("surface EndDraw failed 0x%08lX\n", (unsigned long)hr); }
    if (s->drawing == 2 && s->a8) a8_to_bgra(s);
    if (s->drawing == 2 && s->ctex && s->cdev) { ID3D11DeviceContext *cx = NULL;   /* the client's commands must reach the GPU before we compose */
        ID3D11Device_GetImmediateContext(s->cdev, &cx); if (cx) { ID3D11DeviceContext_Flush(cx); ID3D11DeviceContext_Release(cx); }
        g_dc_lifted_drawn = 1;
        { static LONG nl; if (InterlockedIncrement(&nl) <= 12) { SBrush fk; int v; LONG vv; memset(&fk, 0, sizeof fk); fk.surface = &s->o;
            comp_log("surface %dx%d (client device) EndDraw #%ld upd (%ld,%ld,%ld,%ld): alpha-px %d\n", s->w, s->h, s->version + 1, s->upd.left, s->upd.top, s->upd.right, s->upd.bottom, comp_surface_census(&fk, &v, &vv));
} } }
    s->drawing = 0; s->valid = 1; s->version++; INVALIDATE(&s->o);
    { static LONG nlog2; if (s->drawing == 0 && s->w == 78 && InterlockedIncrement(&nlog2) <= 3) { SBrush fake; int valid; LONG ver; memset(&fake, 0, sizeof fake); fake.surface = &s->o;
        comp_log("surface EndDraw (DXGI route) %dx%d tex %p: alpha-px now %d\n", s->w, s->h, (void *)s->tex, comp_surface_census(&fake, &valid, &ver)); } }
    { static LONG nlog; if (InterlockedIncrement(&nlog) <= 6) { SBrush fake; int valid; LONG ver; memset(&fake, 0, sizeof fake); fake.surface = &s->o;
        comp_log("surface EndDraw %dx%d -> 0x%08lX, alpha-px now %d\n", s->w, s->h, (unsigned long)hr, comp_surface_census(&fake, &valid, &ver)); } }
    return hr;
}
#define SU(T) ((DSurf *)OBJ_OF(T))
/* IDCompositionSurface (and the XAML-internal extension; one vtable, see DCVT_Surf) */
HRESULT STDMETHODCALLTYPE dcS_BeginDraw(void *This, const RECT *upd, REFIID iid, void **obj, POINT *off) { return surf_begin(SU(This), upd, iid, obj, off, 0, 0, __builtin_return_address(0)); }
HRESULT STDMETHODCALLTYPE dcS_BeginDrawWithClear(void *This, const RECT *upd, REFIID iid, void **obj, POINT *off) { return surf_begin(SU(This), upd, iid, obj, off, 1, 0, __builtin_return_address(0)); }
/* XAML 19041 DCompSurface::BeginDrawWithGutters: (rect, iid, obj, offset, gutters*); the gutters pointer is an int-sized local, never written (wine-uwp) */
HRESULT STDMETHODCALLTYPE dcS_BeginDrawWithGutters(void *This, const RECT *upd, REFIID iid, void **obj, POINT *off, void *gutters) { (void)gutters; return surf_begin(SU(This), upd, iid, obj, off, 0, 0, __builtin_return_address(0)); }
HRESULT STDMETHODCALLTYPE dcS_EndDraw(void *This) { return surf_end(SU(This)); }
HRESULT STDMETHODCALLTYPE dcS_SuspendDraw(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcS_ResumeDraw(void *This) { (void)This; return S_OK; }
HRESULT STDMETHODCALLTYPE dcS_HasValidPixels(void *This, BOOL *r) { if (r) *r = SU(This)->valid; return S_OK; }
HRESULT STDMETHODCALLTYPE dcS_Scroll(void *This, const RECT *scroll, const RECT *clip, int dx, int dy)
{
    DSurf *s = SU(This); RECT sc, dst; D3D11_BOX box; DcDev *d = dc_dev(); ID3D11DeviceContext *ctx = NULL;
    if (!s->tex || !d || s->drawing) return E_FAIL;
    sc.left = sc.top = 0; sc.right = s->w; sc.bottom = s->h; if (scroll) sc = *scroll;
    dst.left = sc.left + dx; dst.right = sc.right + dx; dst.top = sc.top + dy; dst.bottom = sc.bottom + dy;
    if (clip) { if (dst.left < clip->left) dst.left = clip->left; if (dst.top < clip->top) dst.top = clip->top; if (dst.right > clip->right) dst.right = clip->right; if (dst.bottom > clip->bottom) dst.bottom = clip->bottom; }
    if (dst.left < 0) dst.left = 0; if (dst.top < 0) dst.top = 0; if (dst.right > s->w) dst.right = s->w; if (dst.bottom > s->h) dst.bottom = s->h;
    if (dst.left >= dst.right || dst.top >= dst.bottom) return S_OK;
    /* overlapping copy through a temporary texture */
    { D3D11_TEXTURE2D_DESC td; ID3D11Texture2D *tmp = NULL; ID3D11Texture2D_GetDesc(s->tex, &td); td.Width = (UINT)(dst.right - dst.left); td.Height = (UINT)(dst.bottom - dst.top); td.BindFlags = 0; td.MiscFlags = 0;
      if (SUCCEEDED(ID3D11Device_CreateTexture2D(d->d3d, &td, NULL, &tmp))) {
        ID3D11Device_GetImmediateContext(d->d3d, &ctx);
        box.left = (UINT)(dst.left - dx); box.right = (UINT)(dst.right - dx); box.top = (UINT)(dst.top - dy); box.bottom = (UINT)(dst.bottom - dy); box.front = 0; box.back = 1;
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)tmp, 0, 0, 0, 0, (ID3D11Resource *)s->tex, 0, &box);
        ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)s->tex, 0, (UINT)dst.left, (UINT)dst.top, 0, (ID3D11Resource *)tmp, 0, NULL);
        ID3D11DeviceContext_Release(ctx); ID3D11Texture2D_Release(tmp); } }
    s->version++; INVALIDATE(&s->o); return S_OK;
}
HRESULT STDMETHODCALLTYPE dcS_Resize(void *This, UINT w, UINT h)
{
    DSurf *s = SU(This); ID3D11Texture2D *old = s->tex; int ow = s->w, oh = s->h; HRESULT hr; if (s->drawing) return E_FAIL;
    if (w > 16384 || h > 16384) return E_INVALIDARG;
    if (old) { ID3D11Texture2D_AddRef(old); } ds_free_gpu(s); s->w = (int)w; s->h = (int)h; hr = (w && h) ? ds_ensure(s) : S_OK;
    if (SUCCEEDED(hr) && old && s->tex) {                                                     /* DComp keeps the pixels */
        DcDev *d = dc_dev(); ID3D11DeviceContext *ctx = NULL; D3D11_BOX box; box.left = box.top = 0; box.front = 0; box.back = 1; box.right = (UINT)(ow < s->w ? ow : s->w); box.bottom = (UINT)(oh < s->h ? oh : s->h);
        if (d && box.right && box.bottom) { ID3D11Device_GetImmediateContext(d->d3d, &ctx); ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)s->tex, 0, 0, 0, 0, (ID3D11Resource *)old, 0, &box); ID3D11DeviceContext_Release(ctx); } }
    if (old) ID3D11Texture2D_Release(old); s->version++; INVALIDATE(&s->o); return hr;
}
HRESULT STDMETHODCALLTYPE dcS_Trim(void *This, const RECT *r, UINT n) { (void)This; (void)r; (void)n; return S_OK; }
HRESULT STDMETHODCALLTYPE dcS_CopySurface(void *This, void *src, const RECT *srcRect, int dx, int dy)
{
    DSurf *s = SU(This); CObj *so = dc_surface_from_unknown((IUnknown *)src); DSurf *ss = (DSurf *)so; DcDev *d = dc_dev(); ID3D11DeviceContext *ctx = NULL; D3D11_BOX box;
    if (!so || !d || !ss->tex || FAILED(ds_ensure(s))) return E_INVALIDARG;
    box.left = srcRect ? srcRect->left : 0; box.top = srcRect ? srcRect->top : 0; box.right = srcRect ? srcRect->right : ss->w; box.bottom = srcRect ? srcRect->bottom : ss->h; box.front = 0; box.back = 1;
    ID3D11Device_GetImmediateContext(d->d3d, &ctx); ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)s->tex, 0, (UINT)dx, (UINT)dy, 0, (ID3D11Resource *)ss->tex, 0, &box); ID3D11DeviceContext_Release(ctx);
    s->valid = 1; s->version++; INVALIDATE(&s->o); return S_OK;
}

/* WinRT ICompositionDrawingSurfaceInterop (IUnknown-derived): BeginDraw, EndDraw, Resize(SIZE), Scroll, ResumeDraw, SuspendDraw */
typedef struct DSInteropVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *BeginDraw)(void *, const RECT *, REFIID, void **, POINT *); HRESULT (STDMETHODCALLTYPE *EndDraw)(void *);
    HRESULT (STDMETHODCALLTYPE *Resize)(void *, SIZE); HRESULT (STDMETHODCALLTYPE *Scroll)(void *, const RECT *, const RECT *, int, int);
    HRESULT (STDMETHODCALLTYPE *ResumeDraw)(void *); HRESULT (STDMETHODCALLTYPE *SuspendDraw)(void *); } DSInteropVtbl;
static HRESULT STDMETHODCALLTYPE dsi_Resize(void *This, SIZE sz) { return dcS_Resize(This, (UINT)sz.cx, (UINT)sz.cy); }
static const DSInteropVtbl VT_DSInterop = { hv_QI, hv_AddRef, hv_Release, dcS_BeginDraw, dcS_EndDraw, dsi_Resize, dcS_Scroll, dcS_ResumeDraw, dcS_SuspendDraw };
/* Windows.UI.Composition.ICompositionSurfaceWrapperPartner {09AEF4AE-D045-4AF2-8F1F-320F928123F9} (IUnknown-based, slot 3 = set the
   IDCompositionSurface the wrapper shows). 19041 XAML gives a LoadedImageSurface whose image is not decoded yet a composition surface made
   from NO surface (DCompSurface::CreateWithNoHardware -> ICompositorPrivate5 slot 6 with NULL) and attaches the real one here once the
   pixels exist (DCompSurface::InitializeSurface); the same call replaces the surface after a resize or a lost device. Every drawing
   surface answers it: `fwd` is then drawn in its place (ds_of). */
static const GUID IID_SurfaceWrapperPartner_ = {0x09AEF4AE, 0xD045, 0x4AF2, {0x8F, 0x1F, 0x32, 0x0F, 0x92, 0x81, 0x23, 0xF9}};
static HRESULT STDMETHODCALLTYPE wp_Set(void *This, IUnknown *surf)
{
    DSurf *s = (DSurf *)((CView *)This)->obj; CObj *o = surf ? dc_surface_from_unknown(surf) : NULL; static LONG nlog;
    if (surf && !o) { comp_log("surface wrapper %p: SetSurface(%p) is not one of our surfaces\n", (void *)s, (void *)surf); return E_INVALIDARG; }
    if (o == &s->o) o = NULL;
    comp_assign(&s->fwd, o); INVALIDATE(&s->o);
    if (InterlockedIncrement(&nlog) <= 12) comp_log("surface wrapper %p now shows surface %p (%dx%d)\n", (void *)s, (void *)o, o ? ((DSurf *)o)->w : 0, o ? ((DSurf *)o)->h : 0);
    return S_OK;
}
static const struct { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *SetSurface)(void *, IUnknown *); } VT_WrapperPartner = { hv_QI, hv_AddRef, hv_Release, wp_Set };
HRESULT dc_surface_qi_extra(CObj *o, REFIID r, void **p)
{
    DSurf *s = (DSurf *)o;
    if (IsEqualGUID(r, &IID_SurfaceWrapperPartner_)) { s->wpart.vt = &VT_WrapperPartner; s->wpart.obj = o; comp_addref(o); *p = &s->wpart; return S_OK; }
    if (IsEqualGUID(r, &IID_ICompositionDrawingSurfaceInterop_)) { s->interop.vt = &VT_DSInterop; s->interop.obj = o; comp_addref(o); *p = &s->interop; return S_OK; }
    if (IsEqualGUID(r, &DCIID_Surface)) { s->surf.vt = DCVT_Surf; s->surf.obj = o; comp_addref(o); *p = &s->surf; return S_OK; }
    if (IsEqualGUID(r, &DCIID_VirtualSurface)) { s->vsurf.vt = DCVT_VSurf; s->vsurf.obj = o; comp_addref(o); *p = &s->vsurf; return S_OK; }
    if (r->Data1 == 0x5680c979) {                                                           /* IDCompositionSurfaceInternal: only the first 32 bits are known (wine-uwp); log the full IID */
        char g[40]; comp_log("QI IDCompositionSurfaceInternal %s answered (layout: wine-uwp)\n", dc_guid(r, g));
        s->sint.vt = DCVT_SurfInt; s->sint.obj = o; comp_addref(o); *p = &s->sint; return S_OK; }
    return E_NOINTERFACE;
}

/* ============================ WinRT: graphics device, drawing surface properties ============================ */
#define DS(T) ((DSurf *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface, get_AlphaMode)(W(ICompositionDrawingSurface) *This, Windows_Graphics_DirectX_DirectXAlphaMode *r) { *r = (int)DS(This)->am; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface, get_PixelFormat)(W(ICompositionDrawingSurface) *This, Windows_Graphics_DirectX_DirectXPixelFormat *r) { *r = DS(This)->pixfmt; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface, get_Size)(W(ICompositionDrawingSurface) *This, Windows_Foundation_Size *r) { r->Width = (float)DS(This)->w; r->Height = (float)DS(This)->h; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface2, get_SizeInt32)(W(ICompositionDrawingSurface2) *This, Windows_Graphics_SizeInt32 *r) { r->Width = DS(This)->w; r->Height = DS(This)->h; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface2, Resize)(W(ICompositionDrawingSurface2) *This, Windows_Graphics_SizeInt32 sz) { return dcS_Resize(&OBJ_OF(This)->views[0], (UINT)sz.Width, (UINT)sz.Height); }
static HRESULT ds_new(CObj *comp, int w, int h, int pf, int am, int virt, REFIID iid, void **out)
{
    CObj *o; HRESULT hr; if (!out) return E_POINTER; *out = NULL; o = dc_create_surface(comp, w, h, (DXGI_FORMAT)pf, (DXGI_ALPHA_MODE)am, virt); if (!o) return E_FAIL;
    hr = comp_qi(o, iid, out); comp_release(o); return hr;
}
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice, CreateDrawingSurface)(W(ICompositionGraphicsDevice) *This, Windows_Foundation_Size sz, Windows_Graphics_DirectX_DirectXPixelFormat pf, Windows_Graphics_DirectX_DirectXAlphaMode am, W(ICompositionDrawingSurface) **r)
{ return ds_new(OBJ_OF(This)->comp, (int)(sz.Width + 0.5f), (int)(sz.Height + 0.5f), (int)pf, (int)am, 0, &IID_(ICompositionDrawingSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice2, CreateDrawingSurface2)(W(ICompositionGraphicsDevice2) *This, Windows_Graphics_SizeInt32 sz, Windows_Graphics_DirectX_DirectXPixelFormat pf, Windows_Graphics_DirectX_DirectXAlphaMode am, W(ICompositionDrawingSurface) **r)
{ return ds_new(OBJ_OF(This)->comp, sz.Width, sz.Height, (int)pf, (int)am, 0, &IID_(ICompositionDrawingSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice2, CreateVirtualDrawingSurface)(W(ICompositionGraphicsDevice2) *This, Windows_Graphics_SizeInt32 sz, Windows_Graphics_DirectX_DirectXPixelFormat pf, Windows_Graphics_DirectX_DirectXAlphaMode am, W(ICompositionVirtualDrawingSurface) **r)
{ return ds_new(OBJ_OF(This)->comp, sz.Width, sz.Height, (int)pf, (int)am, 1, &IID_(ICompositionVirtualDrawingSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice, add_RenderingDeviceReplaced)(W(ICompositionGraphicsDevice) *This, Windows_Foundation_TypedEventHandler_2__Windows_UI_Composition_CompositionGraphicsDevice__Windows_UI_Composition_RenderingDeviceReplacedEventArgs *h, Windows_Foundation_EventRegistrationToken *t) { (void)This; (void)h; if (t) t->Value = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice, remove_RenderingDeviceReplaced)(W(ICompositionGraphicsDevice) *This, Windows_Foundation_EventRegistrationToken t) { (void)This; (void)t; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice3, Trim)(W(ICompositionGraphicsDevice3) *This) { (void)This; return S_OK; }

typedef struct CInteropVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *CreateCompositionSurfaceForHandle)(void *, HANDLE, W(ICompositionSurface) **);
    HRESULT (STDMETHODCALLTYPE *CreateCompositionSurfaceForSwapChain)(void *, IUnknown *, W(ICompositionSurface) **);
    HRESULT (STDMETHODCALLTYPE *CreateGraphicsDevice)(void *, IUnknown *, W(ICompositionGraphicsDevice) **); } CInteropVtbl;
static HRESULT STDMETHODCALLTYPE ci_Handle(void *This, HANDLE h, W(ICompositionSurface) **r) { (void)This; (void)h; if (r) *r = NULL; return comp_unimpl("ICompositorInterop.CreateCompositionSurfaceForHandle"); }
static HRESULT STDMETHODCALLTYPE ci_Swap(void *This, IUnknown *s, W(ICompositionSurface) **r) { (void)This; (void)s; if (r) *r = NULL; return comp_unimpl("ICompositorInterop.CreateCompositionSurfaceForSwapChain"); }
static HRESULT STDMETHODCALLTYPE ci_Dev(void *This, IUnknown *renderingDevice, W(ICompositionGraphicsDevice) **r)
{
    CObj *o; HRESULT hr; if (!r) return E_POINTER; *r = NULL;
    if (renderingDevice) { hr = dc_set_rendering_device(renderingDevice); if (FAILED(hr)) comp_log("CreateGraphicsDevice: rendering device not usable (0x%08lX); using the default device\n", (unsigned long)hr); }
    o = comp_new(CLS_CompositionGraphicsDevice, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionGraphicsDevice), (void **)r); comp_release(o); return hr;
}
static const CInteropVtbl VT_CInterop = { hv_QI, hv_AddRef, hv_Release, ci_Handle, ci_Swap, ci_Dev };
HRESULT dc_compositor_interop_qi(CObj *o, REFIID r, void **p)
{
    Compositor *c = (Compositor *)o;
    if (IsEqualGUID(r, &IID_ICompositorInterop_)) { c->compInterop.vt = &VT_CInterop; c->compInterop.obj = o; comp_addref(o); *p = &c->compInterop; return S_OK; }
    return E_NOINTERFACE;
}
/* ICompositorPrivate5 slot 6 */
HRESULT STDMETHODCALLTYPE dcX_CreateCompositionSurfaceForDCompositionSurface(void *This, IUnknown *surf, void **out)
{
    CObj *o; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    if (!surf) {   /* no surface yet: an empty wrapper, filled through ICompositionSurfaceWrapperPartner (wp_Set). E_INVALIDARG here was a fail-fast in XAML. */
        o = dc_create_surface(((CView *)This)->obj, 0, 0, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, 0); if (!o) return E_OUTOFMEMORY;
        hr = comp_qi(o, &IID_(ICompositionSurface), out); comp_release(o); return hr; }
    o = dc_surface_from_unknown(surf); if (!o) return E_INVALIDARG; return comp_qi(o, &IID_(ICompositionSurface), out);
}

/* ============================ surface brush ============================ */
static void sb_init(CObj *o) { SBrush *b = (SBrush *)o; b->stretch = 1; b->interp = 1; b->hr = b->vr = 0.5f; b->scale.X = b->scale.Y = 1.0f; b->tm.M11 = b->tm.M22 = 1.0f; }
static void sb_destroy(CObj *o) { SBrush *b = (SBrush *)o; if (b->surface) comp_release(b->surface); if (b->xsurf) b->xsurf->lpVtbl->Release(b->xsurf); }
/* a brush's surface: ours directly, or a XAML surface (LoadedImageSurface) whose real surface XAML creates only once the image is decoded.
   Such a surface is kept (xsurf) and resolved through its facade partner when the brush is drawn (sb_resolve). */
static void sb_set(SBrush *b, void *v, const char *what)
{
    CObj *o = comp_obj_of_any(v, what);
    if (b->xsurf) { b->xsurf->lpVtbl->Release(b->xsurf); b->xsurf = NULL; }
    comp_assign(&b->surface, o);
    /* a XAML surface is kept also when it named a real surface: XAML swaps the real surface behind it (sb_resolve) */
    if (v && comp_is_foreign(v)) { b->xsurf = (IUnknown *)v; b->xsurf->lpVtbl->AddRef(b->xsurf); b->xcheck = GetTickCount(); }
}
/* The real surface behind a XAML surface is not fixed: a LoadedImageSurface answers with an empty placeholder until its image is
   decoded and with another surface afterwards (also after a scale change or a lost device). Asking once, when the brush got the
   surface, kept the placeholder for ever - the Store's tile icons stayed empty squares. Now the brush asks again when it is drawn:
   every time while it has nothing to show, else every half second. */
static void sb_resolve(SBrush *b)
{
    static const GUID IID_FacadePartner = {0x3C1C3D30,0x7C6F,0x40C2,{0x89,0x43,0xB1,0x51,0xF3,0x6E,0xE0,0x8D}};
    IUnknown *fp = NULL; void *real = NULL; CObj *o = NULL; static LONG nlog; DWORD now;
    if (!b->xsurf) return;
    now = GetTickCount();
    if (b->surface && (!ds_of(b->surface) || ds_of(b->surface)->valid) && now - b->xcheck < 500) return;
    b->xcheck = now;
    if (FAILED(b->xsurf->lpVtbl->QueryInterface(b->xsurf, &IID_FacadePartner, (void **)&fp)) || !fp) return;
    if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)fp)[6])(fp, &real)) && real) {       /* slot 6 = GetRealSurface */
        if (!comp_is_foreign(real)) o = OBJ_OF(real);
        if (o && o != b->surface) { comp_assign(&b->surface, o); INVALIDATE(&b->o);
            if (InterlockedIncrement(&nlog) <= 20) comp_log("SurfaceBrush %p: XAML surface %p now shows real surface %p\n", (void *)b, (void *)b->xsurf, (void *)o); }
        ((IUnknown *)real)->lpVtbl->Release((IUnknown *)real);
    }
    fp->lpVtbl->Release(fp);
}
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateSurfaceBrush)(W(ICompositor) *This, W(ICompositionSurfaceBrush) **r)
{ CObj *o; HRESULT hr; if (!r) return E_POINTER; o = comp_new(CLS_CompositionSurfaceBrush, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionSurfaceBrush), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateSurfaceBrush_2)(W(ICompositor) *This, W(ICompositionSurface) *surf, W(ICompositionSurfaceBrush) **r)
{ HRESULT hr = CI_ICompositor_CreateSurfaceBrush(This, r); if (SUCCEEDED(hr) && surf) sb_set((SBrush *)OBJ_OF(*r), surf, "CreateSurfaceBrush"); return hr; }
#define SB(T) ((SBrush *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionSurfaceBrush, get_Surface)(W(ICompositionSurfaceBrush) *This, W(ICompositionSurface) **r)
{ return comp_return(SB(This)->surface, &IID_(ICompositionSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionSurfaceBrush, put_Surface)(W(ICompositionSurfaceBrush) *This, W(ICompositionSurface) *v) { sb_set(SB(This), v, "SurfaceBrush.put_Surface"); INVALIDATE(OBJ_OF(This)); return S_OK; }
#define SBP(Iface, name, type, field) \
 HRESULT STDMETHODCALLTYPE CI_(Iface, get_##name)(W(Iface) *This, type *r) { *r = (type)SB(This)->field; return S_OK; } \
 HRESULT STDMETHODCALLTYPE CI_(Iface, put_##name)(W(Iface) *This, type v) { SB(This)->field = v; INVALIDATE(OBJ_OF(This)); return S_OK; }
SBP(ICompositionSurfaceBrush, BitmapInterpolationMode, Windows_UI_Composition_CompositionBitmapInterpolationMode, interp)
SBP(ICompositionSurfaceBrush, HorizontalAlignmentRatio, float, hr) SBP(ICompositionSurfaceBrush, VerticalAlignmentRatio, float, vr)
SBP(ICompositionSurfaceBrush, Stretch, Windows_UI_Composition_CompositionStretch, stretch)
SBP(ICompositionSurfaceBrush2, AnchorPoint, V2, anchor) SBP(ICompositionSurfaceBrush2, CenterPoint, V2, center) SBP(ICompositionSurfaceBrush2, Offset, V2, offset)
SBP(ICompositionSurfaceBrush2, RotationAngle, float, rotation) SBP(ICompositionSurfaceBrush2, Scale, V2, scale) SBP(ICompositionSurfaceBrush2, TransformMatrix, M32, tm)
SBP(ICompositionSurfaceBrush3, SnapToPixels, boolean, snap)
HRESULT STDMETHODCALLTYPE CI_(ICompositionSurfaceBrush2, get_RotationAngleInDegrees)(W(ICompositionSurfaceBrush2) *This, float *r) { *r = SB(This)->rotation * (180.0f / 3.14159265358979f); return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionSurfaceBrush2, put_RotationAngleInDegrees)(W(ICompositionSurfaceBrush2) *This, float v) { SB(This)->rotation = v * (3.14159265358979f / 180.0f); INVALIDATE(OBJ_OF(This)); return S_OK; }

static void sb_dest(SBrush *b, float iw, float ih, float w, float h, float *dx, float *dy, float *dw, float *dh)
{
    float sx = 1, sy = 1;
    switch (b->stretch) {
    case 0: break;
    case 2: { float k = w / iw < h / ih ? w / iw : h / ih; sx = sy = k; break; }
    case 3: { float k = w / iw > h / ih ? w / iw : h / ih; sx = sy = k; break; }
    default: sx = w / iw; sy = h / ih; break;
    }
    *dw = iw * sx; *dh = ih * sy; *dx = (w - *dw) * b->hr; *dy = (h - *dh) * b->vr;
}
void comp_surface_draw(void *rtp, SBrush *b, float w, float h, float opacity)
{
    ID2D1DeviceContext *dc = (ID2D1DeviceContext *)rtp; DSurf *s; D2D1_RECT_F d, clip; float dx, dy, dw, dh; D2D1_INTERPOLATION_MODE im;
    sb_resolve(b);
    s = ds_of(b->surface); if (!s || !s->w || !s->h || FAILED(ds_ensure(s))) return;
    if (s->cdev && s->ctex) { ID3D11DeviceContext *cx = NULL;   /* the client's late work (WinUI 3: D2D fills submitted after EndDraw) must reach the GPU first */
        ID3D11Device_GetImmediateContext(s->cdev, &cx); if (cx) { ID3D11DeviceContext_Flush(cx); ID3D11DeviceContext_Release(cx); } }
    if (w <= 0 || h <= 0) { w = (float)s->w; h = (float)s->h; }                               /* content visual without a size: natural size */
    sb_dest(b, (float)s->w, (float)s->h, w, h, &dx, &dy, &dw, &dh);
    d.left = dx; d.top = dy; d.right = dx + dw; d.bottom = dy + dh; clip.left = 0; clip.top = 0; clip.right = w; clip.bottom = h;
    im = b->interp == 0 ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_LINEAR;
    DCB_PushAxisAlignedClip(dc, &clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DCB_DrawBitmap(dc, s->bmp, &d, opacity, (D2D1_BITMAP_INTERPOLATION_MODE)(im == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? 0 : 1), NULL);
    DCB_PopAxisAlignedClip(dc);
}
void *comp_surface_make_brush(void *rtp, SBrush *b, float x, float y, float w, float h, float opacity)
{
    ID2D1DeviceContext *dc = (ID2D1DeviceContext *)rtp; DSurf *s; ID2D1BitmapBrush *bb = NULL; D2D1_BITMAP_BRUSH_PROPERTIES pp; D2D1_BRUSH_PROPERTIES bp; float dx, dy, dw, dh;
    sb_resolve(b);
    s = ds_of(b->surface); if (!s || !s->w || !s->h || FAILED(ds_ensure(s))) return NULL;
    sb_dest(b, (float)s->w, (float)s->h, w, h, &dx, &dy, &dw, &dh);
    pp.extendModeX = D2D1_EXTEND_MODE_CLAMP; pp.extendModeY = D2D1_EXTEND_MODE_CLAMP; pp.interpolationMode = b->interp == 0 ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    bp.opacity = opacity; bp.transform._11 = dw / s->w; bp.transform._12 = 0; bp.transform._21 = 0; bp.transform._22 = dh / s->h; bp.transform._31 = x + dx; bp.transform._32 = y + dy;
    if (FAILED(dc->lpVtbl->Base.CreateBitmapBrush((ID2D1RenderTarget *)dc, (ID2D1Bitmap *)s->bmp, &pp, &bp, &bb))) return NULL;
    return bb;
}
/* the D2D bitmap and pixel size behind a surface brush (nine-grid slicing in dcomp_render.c); NULL when it has no drawable surface */
void *comp_surface_bitmap(SBrush *b, int *w, int *h)
{
    DSurf *s; if (b) sb_resolve(b);
    s = b ? ds_of(b->surface) : NULL; if (!s || !s->w || !s->h || FAILED(ds_ensure(s))) return NULL;
    *w = s->w; *h = s->h; return s->bmp;
}
/* diagnostics (tree dump): has the surface been drawn (valid, version), and how many pixels have non-zero alpha (GPU copy to a staging texture) */
int comp_surface_census(SBrush *b, int *valid, LONG *version)
{
    DSurf *s; DcDev *d = dc_dev(); D3D11_TEXTURE2D_DESC td; ID3D11Texture2D *st = NULL; ID3D11DeviceContext *ctx = NULL; D3D11_MAPPED_SUBRESOURCE m; int n = -1, x, y, bpp;
    s = b ? ds_of(b->surface) : NULL; if (!s || !d) return -1;
    *valid = s->valid; *version = s->version; if (!s->tex) return -1;
    ID3D11Texture2D_GetDesc(s->tex, &td); td.BindFlags = 0; td.MiscFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bpp = td.Format == DXGI_FORMAT_A8_UNORM ? 1 : 4;
    if (FAILED(ID3D11Device_CreateTexture2D(d->d3d, &td, NULL, &st))) return -1;
    ID3D11Device_GetImmediateContext(d->d3d, &ctx); ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)st, (ID3D11Resource *)s->tex);
    if (SUCCEEDED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m))) {
        n = 0; for (y = 0; y < (int)td.Height; y++) { const BYTE *r = (const BYTE *)m.pData + (size_t)y * m.RowPitch; for (x = 0; x < (int)td.Width; x++) if (r[x * bpp + bpp - 1]) n++; }
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)st, 0); }
    ID3D11DeviceContext_Release(ctx); ID3D11Texture2D_Release(st); return n;
}
/* content visuals: the surface of a brush (for IDCompositionVisual::SetContent) */
void dc_surface_register(void)
{
    g_impl[CLS_CompositionGraphicsDevice].size = sizeof(CObj);
    g_impl[CLS_CompositionDrawingSurface].size = sizeof(DSurf); g_impl[CLS_CompositionDrawingSurface].init = ds_init; g_impl[CLS_CompositionDrawingSurface].destroy = ds_destroy; g_impl[CLS_CompositionDrawingSurface].qi_extra = dc_surface_qi_extra;
    g_impl[CLS_CompositionVirtualDrawingSurface].size = sizeof(DSurf);
    g_impl[CLS_CompositionSurfaceBrush].size = sizeof(SBrush); g_impl[CLS_CompositionSurfaceBrush].init = sb_init; g_impl[CLS_CompositionSurfaceBrush].destroy = sb_destroy;
}
