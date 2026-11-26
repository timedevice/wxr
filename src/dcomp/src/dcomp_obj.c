/* dcomp_obj.c - COM glue for hand-vtabled objects, logging stubs, shared D3D11 / D2D 1.1 device */
#include "dcomp_priv.h"
#include <stdarg.h>

const char *dc_guid(const GUID *g, char *b) { _snprintf(b, 40, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]); return b; }
HRESULT dc_stub(const char *table, int slot, const char *name, HRESULT hr)
{
    static LONG seen[16][96]; static const char *names[16]; int t, k = -1;
    for (t = 0; t < 16; t++) { if (names[t] == table) { k = t; break; } if (!names[t]) { names[t] = table; k = t; break; } }
    if (k >= 0 && slot >= 0 && slot < 96) { if (InterlockedExchange(&seen[k][slot], 1)) return hr; }
    comp_log("UNIMPLEMENTED(dcomp slot) %s[%d] %s -> 0x%08lX\n", table, slot, name, (unsigned long)hr); return hr;
}

/* ---------------- HObj ---------------- */
HObj *hobj_new(size_t size, const char *name, const HIface *ifs, int n, void (*dtor)(HObj *))
{
    HObj *o = (HObj *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size < sizeof(HObj) ? sizeof(HObj) : size); int i;
    if (!o) return NULL; o->ref = 1; o->cls = -1; o->name = name; o->ifs = ifs; o->nifs = n > 6 ? 6 : n; o->dtor = dtor;
    for (i = 0; i < o->nifs; i++) { o->views[i].vt = ifs[i].vt; o->views[i].obj = o; }
    return o;
}
static HRESULT hobj_qi(HObj *o, REFIID r, void **p)
{
    int i; char g[40]; if (!p) return E_POINTER; *p = NULL;
    if (IsEqualGUID(r, &IID_IUnknown)) { *p = &o->views[0]; InterlockedIncrement(&o->ref); return S_OK; }
    for (i = 0; i < o->nifs; i++) if (IsEqualGUID(o->ifs[i].iid, r)) { *p = &o->views[i]; InterlockedIncrement(&o->ref); return S_OK; }
    { static DWORD seen[256]; static LONG nseen; DWORD h = (DWORD)(ULONG_PTR)o->name ^ r->Data1 ^ ((DWORD)r->Data2 << 16) ^ r->Data4[7]; LONG k, ns = nseen; int dup = 0;   /* each (object kind, interface) once */
      for (k = 0; k < ns && k < 256; k++) if (seen[k] == h) { dup = 1; break; }
      if (!dup) { if (ns < 256) { seen[ns] = h; nseen = ns + 1; } comp_log("QI %s iid=%s -> E_NOINTERFACE (logged once)\n", o->name, dc_guid(r, g)); } }
    return E_NOINTERFACE;
}
HRESULT hobj_return(HObj *o, REFIID iid, void **out) { HRESULT hr; if (!o) return E_OUTOFMEMORY; hr = hobj_qi(o, iid, out); hv_Release(&o->views[0]); return hr; }
HRESULT STDMETHODCALLTYPE hv_QI(void *This, REFIID r, void **p)
{
    CObj *o = ((CView *)This)->obj;
    if (!o) { char g[40]; comp_log("hv_QI on view %p with no object (iid %s)\n", This, dc_guid(r, g)); if (p) *p = NULL; return E_NOINTERFACE; }
    if (o->cls < 0) return hobj_qi((HObj *)o, r, p);
    return cv_QI(&o->views[0], r, p);
}
ULONG STDMETHODCALLTYPE hv_AddRef(void *This) { CObj *o = ((CView *)This)->obj; if (o->cls >= 0) return cv_AddRef(&o->views[0]); return (ULONG)InterlockedIncrement(&o->ref); }
ULONG STDMETHODCALLTYPE hv_Release(void *This)
{
    CObj *o = ((CView *)This)->obj; LONG n;
    if (o->cls >= 0) return cv_Release(&o->views[0]);
    n = InterlockedDecrement(&o->ref); if (n == 0) { HObj *h = (HObj *)o; if (h->dtor) h->dtor(h); HeapFree(GetProcessHeap(), 0, h); } return (ULONG)n;
}

/* ---------------- shared devices ---------------- */
CRITICAL_SECTION g_dc_lock; HWND g_dc_target_hwnd;
static DcDev g_dev; static INIT_ONCE g_dev_once = INIT_ONCE_STATIC_INIT;
static void dev_from_dxgi(IDXGIDevice *dx, ID3D11Device *d3d)
{
    D2D1_FACTORY_OPTIONS o; HRESULT hr; IDXGIAdapter *ad = NULL; memset(&o, 0, sizeof o);
    g_dev.dxgi = dx; IDXGIDevice_AddRef(dx);
    g_dev.d3d = d3d; if (d3d) ID3D11Device_AddRef(d3d);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, &IID_ID2D1Factory1, &o, (void **)&g_dev.f1);
    if (SUCCEEDED(hr)) hr = ID2D1Factory1_CreateDevice(g_dev.f1, dx, &g_dev.d2d);
    if (FAILED(hr)) comp_log("D2D 1.1 device creation failed 0x%08lX (Platform Update missing?)\n", (unsigned long)hr);
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dx, &ad))) { IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory2, (void **)&g_dev.dxf); IDXGIAdapter_Release(ad); }
    comp_log("dcomp devices: d3d=%p d2d=%p dxgiFactory2=%p (%s device)\n", (void *)g_dev.d3d, (void *)g_dev.d2d, (void *)g_dev.dxf, g_dev.given ? "client-supplied" : "default");
}
static BOOL CALLBACK dev_default(PINIT_ONCE io, PVOID p, PVOID *c)
{
    ID3D11Device *d3d = NULL; IDXGIDevice *dx = NULL; D3D_FEATURE_LEVEL fl; HRESULT hr; int i; static const D3D_DRIVER_TYPE dt[2] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    (void)io; (void)p; (void)c;
    if (g_dev.d2d) return TRUE;
    {   /* XAML's D3D device when it already exists (wxrsys records it): XAML draws into our surfaces with its own device (IDXGISurface route),
           so they must live on that device. A separate default device made Clock's surfaces cross-device: device removed (0x887A0005). */
        typedef HRESULT (WINAPI *PGD)(REFIID, void **); PGD gd = (PGD)GetProcAddress(GetModuleHandleW(L"wxrsys.dll"), "WxrDx_GetDevice");
        if (gd && SUCCEEDED(gd(&IID_ID3D11Device, (void **)&d3d)) && d3d) {
            ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dx);
            if (dx) { dev_from_dxgi(dx, d3d); IDXGIDevice_Release(dx); }
            ID3D11Device_Release(d3d);
            if (g_dev.d2d) { comp_log("dcomp default device: XAML's D3D device\n"); return TRUE; }
            d3d = NULL; } }
    for (i = 0; i < 2; i++) { hr = D3D11CreateDevice(NULL, dt[i], NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, &fl, NULL); if (SUCCEEDED(hr)) break; }
    if (!d3d) { comp_log("D3D11CreateDevice failed 0x%08lX\n", (unsigned long)hr); return TRUE; }
    {   /* XAML's render thread draws into our surfaces (BeginDraw/EndDraw) while the UI thread composes and presents on this device:
           serialise the immediate context (Clock crashed in the driver's worker thread on its first frames) */
        static const GUID iid_mt = {0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};   /* ID3D10Multithread */
        void *mt = NULL;
        if (SUCCEEDED(ID3D11Device_QueryInterface(d3d, &iid_mt, &mt)) && mt) {
            ((BOOL (STDMETHODCALLTYPE *)(void *, BOOL))(*(void ***)mt)[5])(mt, TRUE); ((IUnknown *)mt)->lpVtbl->Release((IUnknown *)mt); } }
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dx); if (dx) { dev_from_dxgi(dx, d3d); IDXGIDevice_Release(dx); } ID3D11Device_Release(d3d); return TRUE;
}
DcDev *dc_dev(void) { InitOnceExecuteOnce(&g_dev_once, dev_default, NULL, NULL); return g_dev.d2d ? &g_dev : NULL; }
HRESULT dc_set_rendering_device(IUnknown *u)
{
    IDXGIDevice *dx = NULL; ID3D11Device *d3d = NULL; ID2D1Device *d2 = NULL; HRESULT hr;
    if (!u) return E_POINTER; if (g_dev.d2d) return S_FALSE;                                       /* first device wins (XAML's own, normally) */
    ID3D11Device_QueryInterface((ID3D11Device *)u, &IID_ID3D11Device, (void **)&d3d);
    if (d3d) hr = ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dx);
    else hr = IUnknown_QueryInterface(u, &IID_IDXGIDevice, (void **)&dx);
    if ((FAILED(hr) || !dx) && SUCCEEDED(IUnknown_QueryInterface(u, &IID_ID2D1Device, (void **)&d2))) {
        /* XAML 19041 hands CreateSurfaceFactory its ID2D1Device: draw with that device (Win10 d2d1, the same one XAML's text uses). Its D3D device is
           not reachable through public API; wxrsys recorded XAML's D3D11CreateDevice (WxrDx_GetDevice). */
        typedef HRESULT (WINAPI *PGD)(REFIID, void **); PGD gd = (PGD)GetProcAddress(GetModuleHandleW(L"wxrsys.dll"), "WxrDx_GetDevice"); ID2D1Factory *fac = NULL; IDXGIAdapter *ad = NULL;
        if (!gd || FAILED(gd(&IID_ID3D11Device, (void **)&d3d)) || !d3d || FAILED(ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dx))) { comp_log("rendering device is an ID2D1Device but XAML's D3D device is unknown: defaults\n"); if (d3d) ID3D11Device_Release(d3d); ((ULONG (WINAPI *)(void *))(*(void ***)d2)[2])(d2); return E_NOINTERFACE; }
        g_dev.given = TRUE; g_dev.d2d = d2; g_dev.d3d = d3d; g_dev.dxgi = dx;
        ((void (WINAPI *)(void *, ID2D1Factory **))(*(void ***)d2)[3])(d2, &fac);   /* ID2D1Resource::GetFactory */ if (fac) { ID2D1Factory_QueryInterface(fac, &IID_ID2D1Factory1, (void **)&g_dev.f1); ID2D1Factory_Release(fac); }
        if (SUCCEEDED(IDXGIDevice_GetAdapter(dx, &ad))) { IDXGIAdapter_GetParent(ad, &IID_IDXGIFactory2, (void **)&g_dev.dxf); IDXGIAdapter_Release(ad); }
        comp_log("dcomp devices from XAML's ID2D1Device: d3d=%p d2d=%p f1=%p dxgiFactory2=%p\n", (void *)d3d, (void *)d2, (void *)g_dev.f1, (void *)g_dev.dxf);
        return S_OK; }
    if (FAILED(hr) || !dx) return E_NOINTERFACE;
    g_dev.given = TRUE; if (!d3d) IDXGIDevice_QueryInterface(dx, &IID_ID3D11Device, (void **)&d3d);
    dev_from_dxgi(dx, d3d); IDXGIDevice_Release(dx); if (d3d) ID3D11Device_Release(d3d);
    return g_dev.d2d ? S_OK : E_FAIL;
}
/* Client devices: a dcomp device created with a D3D device other than the primary one (WinUI 3's lifted compositor, dwmcorei, renders on
   its own hardware device). Its surfaces stay on the primary device (the renderer composes them there) but are created SHARED, and
   BeginDraw(ID3D11Texture2D) hands out the texture opened on the client's device: D2D on that device refuses a texture of another device
   (D2DERR_UNSUPPORTED_OPERATION, the content stayed black). Keyed by the compositor object; the device reference is kept. */
static struct { CObj *comp; ID3D11Device *d3d; } g_cdev[16]; static LONG g_ncdev;
void dc_note_client_device(CObj *comp, IUnknown *u)
{
    ID3D11Device *d3d = NULL; IDXGIDevice *dx = NULL; DcDev *d = dc_dev(); LONG k;
    if (!comp || !u || !d || !d->d3d) return;
    if (FAILED(IUnknown_QueryInterface(u, &IID_ID3D11Device, (void **)&d3d)) || !d3d) {
        if (SUCCEEDED(IUnknown_QueryInterface(u, &IID_IDXGIDevice, (void **)&dx)) && dx) { IDXGIDevice_QueryInterface(dx, &IID_ID3D11Device, (void **)&d3d); IDXGIDevice_Release(dx); } }
    if (!d3d) return;
    if (d3d == d->d3d) { ID3D11Device_Release(d3d); return; }
    k = InterlockedIncrement(&g_ncdev) - 1; if (k >= 16) { InterlockedDecrement(&g_ncdev); ID3D11Device_Release(d3d); return; }
    g_cdev[k].d3d = d3d; g_cdev[k].comp = comp;
    comp_log("client device: compositor %p renders on D3D device %p (primary %p): its surfaces are shared to it\n", (void *)comp, (void *)d3d, (void *)d->d3d);
}
ID3D11Device *dc_client_device(CObj *comp)
{
    LONG k, n = g_ncdev; if (n > 16) n = 16;
    for (k = 0; k < n; k++) if (g_cdev[k].comp == comp) return g_cdev[k].d3d;
    return NULL;
}
ID2D1Factory *comp_d2d_factory(void) { DcDev *d = dc_dev(); return d ? (ID2D1Factory *)d->f1 : NULL; }
