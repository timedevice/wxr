/* comp_surface.c - CompositionGraphicsDevice, CompositionDrawingSurface (+ICompositionDrawingSurfaceInterop), CompositionSurfaceBrush, ICompositorInterop
 *
 * A drawing surface is a premultiplied BGRA DIB section kept in system memory.  ICompositionDrawingSurfaceInterop::BeginDraw hands out an
 * ID2D1RenderTarget (Direct2D 1.0 ID2D1DCRenderTarget bound to the surface's DC) because ID2D1DeviceContext / IDXGISurface do not exist on stock
 * Windows 7.  The renderer uploads the bits to an ID2D1Bitmap of the window's render target whenever EndDraw bumped the surface's version. */
#include "comp_types.h"
#include <d2d1.h>
#define CI_(i, m) CI_##i##_##m
#define IID_(n) IID_Windows_UI_Composition_##n
ID2D1Factory *comp_d2d_factory(void);

static const GUID IID_IUnknown_s = {0x00000000, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_ID2D1RenderTarget_ = {0x2cd90694, 0x12e2, 0x11dc, {0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9}};
static const GUID IID_ID2D1DCRenderTarget_ = {0x1c51bc64, 0xde61, 0x46fd, {0x98, 0x99, 0x63, 0xa5, 0xd8, 0xf0, 0x39, 0x50}};
static const GUID IID_ICompositionDrawingSurfaceInterop_ = {0xFD04E6E3, 0xFE0C, 0x4C3E, {0xAB, 0x19, 0xA0, 0x76, 0x01, 0xA5, 0x76, 0xEE}};
static const GUID IID_ICompositorInterop_ = {0x25297D5C, 0x3AD4, 0x4C9C, {0xB5, 0xCF, 0xE3, 0x6A, 0x38, 0x51, 0x23, 0x30}};

/* ============================ drawing surface ============================ */
static void ds_free_gdi(DSurface *s)
{
    if (s->bmp) { ID2D1Bitmap_Release((ID2D1Bitmap *)s->bmp); s->bmp = NULL; }
    if (s->rt) { ID2D1DCRenderTarget_Release((ID2D1DCRenderTarget *)s->rt); s->rt = NULL; }
    if (s->dc) { if (s->olddib) SelectObject(s->dc, s->olddib); DeleteDC(s->dc); s->dc = NULL; }
    if (s->dib) { DeleteObject(s->dib); s->dib = NULL; } s->bits = NULL;
}
static HRESULT ds_alloc(DSurface *s, int w, int h)
{
    BITMAPINFO bi; HDC scr;
    if (w < 0 || h < 0 || w > 16384 || h > 16384) return E_INVALIDARG;
    ds_free_gdi(s); s->w = w; s->h = h; s->version++;
    if (!w || !h) return S_OK;
    memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    scr = GetDC(NULL); s->dib = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &s->bits, NULL, 0); s->dc = CreateCompatibleDC(scr); ReleaseDC(NULL, scr);
    if (!s->dib || !s->dc) { ds_free_gdi(s); return E_OUTOFMEMORY; }
    s->olddib = (HBITMAP)SelectObject(s->dc, s->dib); return S_OK;
}
static void ds_destroy(CObj *o) { DSurface *s = (DSurface *)o; ds_free_gdi(s); }
static void ds_init(CObj *o) { DSurface *s = (DSurface *)o; s->pixfmt = 87; s->alphamode = 1; s->interop.obj = o; }

static HRESULT ds_create(CObj *comp, int w, int h, int pixfmt, int alpha, int cls, REFIID iid, void **out)
{
    CObj *o; DSurface *s; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
    if (pixfmt != 87) comp_log("CreateDrawingSurface: pixel format %d stored as premultiplied B8G8R8A8\n", pixfmt);
    o = comp_new(cls, comp); if (!o) return E_OUTOFMEMORY; s = (DSurface *)o; s->pixfmt = pixfmt; s->alphamode = alpha ? alpha : 1;
    hr = ds_alloc(s, w, h); if (FAILED(hr)) { comp_release(o); return hr; }
    hr = comp_qi(o, iid, out); comp_release(o); return hr;
}
#define DS(T) ((DSurface *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface, get_AlphaMode)(W(ICompositionDrawingSurface) *This, Windows_Graphics_DirectX_DirectXAlphaMode *r) { *r = DS(This)->alphamode; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface, get_PixelFormat)(W(ICompositionDrawingSurface) *This, Windows_Graphics_DirectX_DirectXPixelFormat *r) { *r = DS(This)->pixfmt; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface, get_Size)(W(ICompositionDrawingSurface) *This, Windows_Foundation_Size *r) { r->Width = (float)DS(This)->w; r->Height = (float)DS(This)->h; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface2, get_SizeInt32)(W(ICompositionDrawingSurface2) *This, Windows_Graphics_SizeInt32 *r) { r->Width = DS(This)->w; r->Height = DS(This)->h; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionDrawingSurface2, Resize)(W(ICompositionDrawingSurface2) *This, Windows_Graphics_SizeInt32 sz) { DSurface *s = DS(This); if (s->drawing) return E_FAIL; { HRESULT hr = ds_alloc(s, sz.Width, sz.Height); INVALIDATE(&s->o); return hr; } }

/* ---- ICompositionDrawingSurfaceInterop (IUnknown-derived) ---- */
typedef struct DSInteropVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *BeginDraw)(void *, const RECT *, REFIID, void **, POINT *); HRESULT (STDMETHODCALLTYPE *EndDraw)(void *);
    HRESULT (STDMETHODCALLTYPE *Resize)(void *, SIZE); HRESULT (STDMETHODCALLTYPE *Scroll)(void *, const RECT *, const RECT *, int, int);
    HRESULT (STDMETHODCALLTYPE *ResumeDraw)(void *); HRESULT (STDMETHODCALLTYPE *SuspendDraw)(void *); } DSInteropVtbl;
static HRESULT ds_get_rt(DSurface *s, ID2D1DCRenderTarget **out)
{
    D2D1_RENDER_TARGET_PROPERTIES rp; ID2D1Factory *f = comp_d2d_factory(); HRESULT hr;
    if (s->rt) { *out = (ID2D1DCRenderTarget *)s->rt; return S_OK; }
    if (!f) return E_FAIL;
    memset(&rp, 0, sizeof rp); rp.type = D2D1_RENDER_TARGET_TYPE_DEFAULT; rp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; rp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    rp.dpiX = 96.0f; rp.dpiY = 96.0f; rp.usage = D2D1_RENDER_TARGET_USAGE_NONE; rp.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    hr = ID2D1Factory_CreateDCRenderTarget(f, &rp, (ID2D1DCRenderTarget **)&s->rt); if (FAILED(hr)) { s->rt = NULL; comp_log("CreateDCRenderTarget failed 0x%08lX\n", (unsigned long)hr); return hr; }
    *out = (ID2D1DCRenderTarget *)s->rt; return S_OK;
}
static HRESULT STDMETHODCALLTYPE dsi_Begin(void *This, const RECT *upd, REFIID iid, void **obj, POINT *off)
{
    DSurface *s = (DSurface *)OBJ_OF(This); ID2D1DCRenderTarget *rt = NULL; RECT bind, u; HRESULT hr; D2D1_RECT_F clip;
    if (!obj || !off) return E_POINTER; *obj = NULL; off->x = off->y = 0;
    if (s->drawing) return E_FAIL; if (!s->dc) return E_FAIL;
    if (!IsEqualGUID(iid, &IID_ID2D1RenderTarget_) && !IsEqualGUID(iid, &IID_ID2D1DCRenderTarget_) && !IsEqualGUID(iid, &IID_IUnknown_s)) {
        comp_log("BeginDraw: interface not available on this system (only ID2D1RenderTarget / ID2D1DCRenderTarget; D2D 1.1 device contexts and DXGI surfaces are not provided)\n"); return E_NOINTERFACE; }
    if (upd) { u = *upd; if (u.left < 0 || u.top < 0 || u.right > s->w || u.bottom > s->h || u.left >= u.right || u.top >= u.bottom) return E_INVALIDARG; } else { u.left = u.top = 0; u.right = s->w; u.bottom = s->h; }
    hr = ds_get_rt(s, &rt); if (FAILED(hr)) return hr;
    bind.left = 0; bind.top = 0; bind.right = s->w; bind.bottom = s->h;
    hr = ID2D1DCRenderTarget_BindDC(rt, s->dc, &bind); if (FAILED(hr)) return hr;
    ID2D1DCRenderTarget_BeginDraw(rt);
    clip.left = (float)u.left; clip.top = (float)u.top; clip.right = (float)u.right; clip.bottom = (float)u.bottom;
    ID2D1DCRenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    s->drawing = 1; s->upd = u; off->x = u.left; off->y = u.top;
    *obj = rt; ID2D1DCRenderTarget_AddRef(rt); return S_OK;     /* caller draws at (offset + local); this surface does not clear the rectangle */
}
static HRESULT STDMETHODCALLTYPE dsi_End(void *This)
{
    DSurface *s = (DSurface *)OBJ_OF(This); HRESULT hr; ID2D1DCRenderTarget *rt = (ID2D1DCRenderTarget *)s->rt;
    if (!s->drawing || !rt) return E_FAIL;
    ID2D1DCRenderTarget_PopAxisAlignedClip(rt); hr = ID2D1DCRenderTarget_EndDraw(rt, NULL, NULL); s->drawing = 0; s->version++;
    if (FAILED(hr)) { comp_log("surface EndDraw failed 0x%08lX\n", (unsigned long)hr); if (hr == (HRESULT)0x8899000C) { ID2D1DCRenderTarget_Release(rt); s->rt = NULL; } }
    INVALIDATE(&s->o); return hr;
}
static HRESULT STDMETHODCALLTYPE dsi_Resize(void *This, SIZE sz) { DSurface *s = (DSurface *)OBJ_OF(This); HRESULT hr; if (s->drawing) return E_FAIL; hr = ds_alloc(s, sz.cx, sz.cy); INVALIDATE(&s->o); return hr; }
static HRESULT STDMETHODCALLTYPE dsi_Scroll(void *This, const RECT *scroll, const RECT *clip, int dx, int dy)
{
    DSurface *s = (DSurface *)OBJ_OF(This); RECT sc, cl; int y, x0, x1, w; UINT32 *px = (UINT32 *)s->bits;
    if (s->drawing || !px) return E_FAIL;
    sc.left = sc.top = 0; sc.right = s->w; sc.bottom = s->h; if (scroll) { sc = *scroll; }
    cl = sc; if (clip) cl = *clip;
    /* move the pixels of `sc` by (dx,dy) and keep only what lands inside `cl` */
    { RECT dst; int ys, ye, step; UINT32 *tmp;
      dst.left = sc.left + dx; dst.right = sc.right + dx; dst.top = sc.top + dy; dst.bottom = sc.bottom + dy;
      if (dst.left < cl.left) dst.left = cl.left; if (dst.top < cl.top) dst.top = cl.top; if (dst.right > cl.right) dst.right = cl.right; if (dst.bottom > cl.bottom) dst.bottom = cl.bottom;
      if (dst.left < 0) dst.left = 0; if (dst.top < 0) dst.top = 0; if (dst.right > s->w) dst.right = s->w; if (dst.bottom > s->h) dst.bottom = s->h;
      if (dst.left >= dst.right || dst.top >= dst.bottom) return S_OK;
      w = dst.right - dst.left; x0 = dst.left - dx; (void)x1;
      tmp = (UINT32 *)HeapAlloc(GetProcessHeap(), 0, (size_t)w * sizeof(UINT32)); if (!tmp) return E_OUTOFMEMORY;
      if (dy > 0) { ys = dst.bottom - 1; ye = dst.top - 1; step = -1; } else { ys = dst.top; ye = dst.bottom; step = 1; }
      for (y = ys; y != ye; y += step) { memcpy(tmp, px + (size_t)(y - dy) * s->w + x0, (size_t)w * 4); memcpy(px + (size_t)y * s->w + dst.left, tmp, (size_t)w * 4); }
      HeapFree(GetProcessHeap(), 0, tmp); }
    s->version++; INVALIDATE(&s->o); return S_OK;
}
static HRESULT STDMETHODCALLTYPE dsi_Resume(void *This) { (void)This; return S_OK; }
static HRESULT STDMETHODCALLTYPE dsi_Suspend(void *This) { (void)This; return S_OK; }
static const DSInteropVtbl VT_DSInterop = { cv_QI, cv_AddRef, cv_Release, dsi_Begin, dsi_End, dsi_Resize, dsi_Scroll, dsi_Resume, dsi_Suspend };
static HRESULT ds_qi_extra(CObj *o, REFIID r, void **p)
{
    DSurface *s = (DSurface *)o;
    if (IsEqualGUID(r, &IID_ICompositionDrawingSurfaceInterop_)) { s->interop.vt = &VT_DSInterop; s->interop.obj = o; comp_addref(o); *p = &s->interop; return S_OK; }
    return E_NOINTERFACE;
}

/* ============================ graphics device ============================ */
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice, CreateDrawingSurface)(W(ICompositionGraphicsDevice) *This, Windows_Foundation_Size sz, Windows_Graphics_DirectX_DirectXPixelFormat pf, Windows_Graphics_DirectX_DirectXAlphaMode am, W(ICompositionDrawingSurface) **r)
{ return ds_create(OBJ_OF(This)->comp, (int)(sz.Width + 0.5f), (int)(sz.Height + 0.5f), pf, am, CLS_CompositionDrawingSurface, &IID_(ICompositionDrawingSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice2, CreateDrawingSurface2)(W(ICompositionGraphicsDevice2) *This, Windows_Graphics_SizeInt32 sz, Windows_Graphics_DirectX_DirectXPixelFormat pf, Windows_Graphics_DirectX_DirectXAlphaMode am, W(ICompositionDrawingSurface) **r)
{ return ds_create(OBJ_OF(This)->comp, sz.Width, sz.Height, pf, am, CLS_CompositionDrawingSurface, &IID_(ICompositionDrawingSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice2, CreateVirtualDrawingSurface)(W(ICompositionGraphicsDevice2) *This, Windows_Graphics_SizeInt32 sz, Windows_Graphics_DirectX_DirectXPixelFormat pf, Windows_Graphics_DirectX_DirectXAlphaMode am, W(ICompositionVirtualDrawingSurface) **r)
{ return ds_create(OBJ_OF(This)->comp, sz.Width, sz.Height, pf, am, CLS_CompositionVirtualDrawingSurface, &IID_(ICompositionVirtualDrawingSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice, add_RenderingDeviceReplaced)(W(ICompositionGraphicsDevice) *This, Windows_Foundation_TypedEventHandler_2__Windows_UI_Composition_CompositionGraphicsDevice__Windows_UI_Composition_RenderingDeviceReplacedEventArgs *handler, Windows_Foundation_EventRegistrationToken *token) { (void)This; (void)handler; if (token) token->Value = 1; return S_OK; }   /* never fires */
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice, remove_RenderingDeviceReplaced)(W(ICompositionGraphicsDevice) *This, Windows_Foundation_EventRegistrationToken token) { (void)This; (void)token; return S_OK; }
HRESULT STDMETHODCALLTYPE CI_(ICompositionGraphicsDevice3, Trim)(W(ICompositionGraphicsDevice3) *This) { (void)This; return S_OK; }

/* ============================ ICompositorInterop ============================ */
typedef struct CInteropVtbl { HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *CreateCompositionSurfaceForHandle)(void *, HANDLE, W(ICompositionSurface) **);
    HRESULT (STDMETHODCALLTYPE *CreateCompositionSurfaceForSwapChain)(void *, IUnknown *, W(ICompositionSurface) **);
    HRESULT (STDMETHODCALLTYPE *CreateGraphicsDevice)(void *, IUnknown *, W(ICompositionGraphicsDevice) **); } CInteropVtbl;
static HRESULT STDMETHODCALLTYPE ci_Handle(void *This, HANDLE h, W(ICompositionSurface) **r) { (void)This; (void)h; if (r) *r = NULL; return comp_unimpl("ICompositorInterop.CreateCompositionSurfaceForHandle"); }
static HRESULT STDMETHODCALLTYPE ci_Swap(void *This, IUnknown *s, W(ICompositionSurface) **r) { (void)This; (void)s; if (r) *r = NULL; return comp_unimpl("ICompositorInterop.CreateCompositionSurfaceForSwapChain"); }
static HRESULT STDMETHODCALLTYPE ci_Dev(void *This, IUnknown *renderingDevice, W(ICompositionGraphicsDevice) **r)
{
    CObj *o; HRESULT hr; (void)renderingDevice; if (!r) return E_POINTER; *r = NULL;    /* the rendering device is not used: all surfaces live in system memory and are drawn by our Direct2D 1.0 renderer */
    o = comp_new(CLS_CompositionGraphicsDevice, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionGraphicsDevice), (void **)r); comp_release(o); return hr;
}
static const CInteropVtbl VT_CInterop = { cv_QI, cv_AddRef, cv_Release, ci_Handle, ci_Swap, ci_Dev };
HRESULT comp_compositor_interop_qi(CObj *o, REFIID r, void **p)
{
    Compositor *c = (Compositor *)o;
    if (IsEqualGUID(r, &IID_ICompositorInterop_)) { c->compInterop.vt = &VT_CInterop; c->compInterop.obj = o; comp_addref(o); *p = &c->compInterop; return S_OK; }
    return E_NOINTERFACE;
}

/* ============================ surface brush ============================ */
static void sb_init(CObj *o) { SBrush *b = (SBrush *)o; b->stretch = 1; /* Fill */ b->interp = 1; b->hr = b->vr = 0.5f; b->scale.X = b->scale.Y = 1.0f; b->tm.M11 = b->tm.M22 = 1.0f; }
static void sb_destroy(CObj *o) { SBrush *b = (SBrush *)o; if (b->surface) comp_release(b->surface); }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateSurfaceBrush)(W(ICompositor) *This, W(ICompositionSurfaceBrush) **r)
{ CObj *o; HRESULT hr; if (!r) return E_POINTER; o = comp_new(CLS_CompositionSurfaceBrush, OBJ_OF(This)); if (!o) return E_OUTOFMEMORY; hr = comp_qi(o, &IID_(ICompositionSurfaceBrush), (void **)r); comp_release(o); return hr; }
HRESULT STDMETHODCALLTYPE CI_(ICompositor, CreateSurfaceBrush_2)(W(ICompositor) *This, W(ICompositionSurface) *surf, W(ICompositionSurfaceBrush) **r)
{ HRESULT hr = CI_ICompositor_CreateSurfaceBrush(This, r); if (SUCCEEDED(hr) && surf) comp_assign(&((SBrush *)OBJ_OF(*r))->surface, OBJ_OF(surf)); return hr; }
#define SB(T) ((SBrush *)OBJ_OF(T))
HRESULT STDMETHODCALLTYPE CI_(ICompositionSurfaceBrush, get_Surface)(W(ICompositionSurfaceBrush) *This, W(ICompositionSurface) **r) { return comp_return(SB(This)->surface, &IID_(ICompositionSurface), (void **)r); }
HRESULT STDMETHODCALLTYPE CI_(ICompositionSurfaceBrush, put_Surface)(W(ICompositionSurfaceBrush) *This, W(ICompositionSurface) *v) { comp_assign(&SB(This)->surface, v ? OBJ_OF(v) : NULL); INVALIDATE(OBJ_OF(This)); return S_OK; }
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

/* ---- rendering helpers (called by comp_render.c with the window's render target) ---- */
static ID2D1Bitmap *surf_bitmap(ID2D1RenderTarget *rt, DSurface *s)
{
    D2D1_BITMAP_PROPERTIES bp; D2D1_SIZE_U sz;
    if (!s->bits || s->w <= 0 || s->h <= 0) return NULL;
    if (s->bmp && s->bmp_owner == rt && s->bmp_ver == s->version) return (ID2D1Bitmap *)s->bmp;
    if (s->bmp) { ID2D1Bitmap_Release((ID2D1Bitmap *)s->bmp); s->bmp = NULL; }
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED; bp.dpiX = 96.0f; bp.dpiY = 96.0f; sz.width = (UINT32)s->w; sz.height = (UINT32)s->h;
    if (FAILED(ID2D1RenderTarget_CreateBitmap(rt, sz, s->bits, (UINT32)s->w * 4, &bp, (ID2D1Bitmap **)&s->bmp))) { s->bmp = NULL; return NULL; }
    s->bmp_owner = rt; s->bmp_ver = s->version; return (ID2D1Bitmap *)s->bmp;
}
/* where the image lands inside a w x h box for the brush's Stretch / alignment */
static void sb_dest(SBrush *b, float iw, float ih, float w, float h, float *dx, float *dy, float *dw, float *dh)
{
    float sx = 1, sy = 1;
    switch (b->stretch) {
    case 0: break;                                                  /* None */
    case 2: { float k = w / iw < h / ih ? w / iw : h / ih; sx = sy = k; break; }                     /* Uniform */
    case 3: { float k = w / iw > h / ih ? w / iw : h / ih; sx = sy = k; break; }                     /* UniformToFill */
    default: sx = w / iw; sy = h / ih; break;                       /* Fill */
    }
    *dw = iw * sx; *dh = ih * sy; *dx = (w - *dw) * b->hr; *dy = (h - *dh) * b->vr;
}
void comp_surface_draw(void *rtp, SBrush *b, float w, float h, float opacity)
{
    ID2D1RenderTarget *rt = (ID2D1RenderTarget *)rtp; DSurface *s; ID2D1Bitmap *bmp; D2D1_RECT_F d, clip; float dx, dy, dw, dh;
    if (!b->surface || !comp_is_a(b->surface->cls, CLS_CompositionDrawingSurface)) return;
    s = (DSurface *)b->surface; bmp = surf_bitmap(rt, s); if (!bmp || s->w <= 0 || s->h <= 0) return;
    sb_dest(b, (float)s->w, (float)s->h, w, h, &dx, &dy, &dw, &dh);
    d.left = dx; d.top = dy; d.right = dx + dw; d.bottom = dy + dh; clip.left = 0; clip.top = 0; clip.right = w; clip.bottom = h;
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1RenderTarget_DrawBitmap(rt, bmp, &d, opacity, b->interp == 0 ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);
}
void *comp_surface_make_brush(void *rtp, SBrush *b, float x, float y, float w, float h, float opacity)
{
    ID2D1RenderTarget *rt = (ID2D1RenderTarget *)rtp; DSurface *s; ID2D1Bitmap *bmp; ID2D1BitmapBrush *bb = NULL; D2D1_BITMAP_BRUSH_PROPERTIES pp; D2D1_BRUSH_PROPERTIES bp; float dx, dy, dw, dh;
    if (!b->surface || !comp_is_a(b->surface->cls, CLS_CompositionDrawingSurface)) return NULL;
    s = (DSurface *)b->surface; bmp = surf_bitmap(rt, s); if (!bmp) return NULL;
    sb_dest(b, (float)s->w, (float)s->h, w, h, &dx, &dy, &dw, &dh);
    pp.extendModeX = D2D1_EXTEND_MODE_CLAMP; pp.extendModeY = D2D1_EXTEND_MODE_CLAMP; pp.interpolationMode = b->interp == 0 ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    bp.opacity = opacity; bp.transform._11 = dw / s->w; bp.transform._12 = 0; bp.transform._21 = 0; bp.transform._22 = dh / s->h; bp.transform._31 = x + dx; bp.transform._32 = y + dy;
    if (FAILED(rt->lpVtbl->CreateBitmapBrush(rt, bmp, &pp, &bp, &bb))) return NULL;
    return bb;    /* edge pixels extend outside the image box for Stretch None / Uniform when used as a shape fill (sprites use comp_surface_draw, which does not) */
}

void comp_register_impls_surface(void)
{
    g_impl[CLS_CompositionGraphicsDevice].size = sizeof(CObj);
    g_impl[CLS_CompositionDrawingSurface].size = sizeof(DSurface); g_impl[CLS_CompositionDrawingSurface].init = ds_init; g_impl[CLS_CompositionDrawingSurface].destroy = ds_destroy; g_impl[CLS_CompositionDrawingSurface].qi_extra = ds_qi_extra;
    g_impl[CLS_CompositionVirtualDrawingSurface].size = sizeof(DSurface);
    g_impl[CLS_CompositionSurfaceBrush].size = sizeof(SBrush); g_impl[CLS_CompositionSurfaceBrush].init = sb_init; g_impl[CLS_CompositionSurfaceBrush].destroy = sb_destroy;
}
