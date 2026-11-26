/* wxrdx.inc - D3D 11.2/11.3 and DXGI 1.3 on Win7 (D3D 11.1 + platform update), included by wxrsys.c.
   Win10 d2d1 (XAML's text and brush rendering) QIs the device for ID3D11Device3 and calls GetImmediateContext3 (it fails CreateDevice
   with E_NOINTERFACE otherwise). Tear-offs: every slot Win7 already has jumps straight into the real 11.1 object (asm thunks swap
   `this` and leave every other argument - float and stack ones included - untouched); only the 11.2/11.3 additions are emulated.
   The QueryInterface of the real DXGI device, D3D11 device and immediate context (vtables shared by all instances) is hooked once,
   so asking any of them for a newer interface hands out a tear-off. Slot numbers from mingw's d3d11_3.h. */
static const GUID IID_D3D11Device1_ = {0xa04bfb29, 0x08ef, 0x43d6, {0xa4, 0x9c, 0xa9, 0xbd, 0xbd, 0xcb, 0xe6, 0x86}};
static const GUID IID_D3D11Device2_ = {0x9d06dffa, 0xd1e5, 0x4d07, {0x83, 0xa8, 0x1b, 0xb1, 0x23, 0xf2, 0xf8, 0x41}};
static const GUID IID_D3D11Device3_ = {0xa05c8c37, 0xd2c6, 0x4732, {0xb3, 0xa0, 0x9c, 0xe0, 0xb0, 0xdc, 0x9a, 0xe6}};
static const GUID IID_D3D11Device_ = {0xdb6f6ddb, 0xac77, 0x4e88, {0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40}};
static const GUID IID_D3D11Ctx1_ = {0xbb2c6faa, 0xb5fb, 0x4082, {0x8e, 0x6b, 0x38, 0x8b, 0x8c, 0xfa, 0x90, 0xe1}};
static const GUID IID_D3D11Ctx2_ = {0x420d5b32, 0xb90c, 0x4da4, {0xbe, 0xf0, 0x35, 0x9f, 0x6a, 0x24, 0xa8, 0x3a}};
static const GUID IID_D3D11Ctx3_ = {0xb4e3c01d, 0xe79e, 0x4637, {0x91, 0xb2, 0x51, 0x0e, 0x9f, 0x4c, 0x9b, 0x8f}};
static const GUID IID_DXGIDevice_ = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
static const GUID IID_DXGIDevice1_ = {0x77db970f, 0x6276, 0x48ba, {0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c}};
static const GUID IID_DXGIDevice2_ = {0x05008617, 0xfbfd, 0x4051, {0xa7, 0x90, 0x14, 0x48, 0x84, 0xb4, 0xf6, 0xa9}};
static const GUID IID_DXGIDevice3_ = {0x6007896c, 0x3244, 0x4afd, {0xbf, 0x18, 0xa6, 0xd3, 0xbe, 0xda, 0x50, 0x23}};

/* dxfwd_<k>: this = ((DxT *)this)->real; jmp real->vtbl[k] */
__asm__(".text\n.altmacro\n"
        ".macro DXFWD n\n.p2align 4\ndxfwd_\\n:\n  movq 8(%rcx), %rcx\n  movq (%rcx), %rax\n  jmp *(8*\\n)(%rax)\n.endm\n"
        ".macro DXENT n\n  .quad dxfwd_\\n\n.endm\n"
        ".set dxi, 0\n.rept 147\n  DXFWD %dxi\n  .set dxi, dxi+1\n.endr\n"
        ".section .rdata,\"dr\"\n.p2align 3\n.globl dxfwd_tab\ndxfwd_tab:\n.set dxi, 0\n.rept 147\n  DXENT %dxi\n  .set dxi, dxi+1\n.endr\n"
        ".noaltmacro\n.text\n");
extern const void *const dxfwd_tab[147];
static void dx_guard_warp_escape(void *obj);

typedef struct DxT { const void *const *vt; void *real; LONG ref; } DxT;     /* real must stay at +8 (the thunks read it) */
typedef HRESULT (WINAPI *PQI_)(void *, REFIID, void **);
static PQI_ g_qi_dxgi, g_qi_dev, g_qi_ctx;                                  /* the original QueryInterface of each shared vtable */
static const void *g_devt_vt[69], *g_ctxt_vt[149], *g_dxgt_vt[18];
#define VCALL(o, k, T) ((T)(*(void ***)(o))[k])
static ULONG dx_release_real(void *o) { return VCALL(o, 2, ULONG (WINAPI *)(void *))(o); }
static void dx_log(const char *what) { static volatile LONG n; if (InterlockedIncrement(&n) <= 40) slog("[dx] %s", what); }
/* one tear-off per real object: d2d1 compares device pointers (CreateBitmapFromDxgiSurface asks the surface for its ID3D11Device1 and
   expects the device it was created with), so every upgrade of the same object must hand out the same pointer */
static struct { void *real; DxT *t; } g_dxcache[16]; static CRITICAL_SECTION g_dxcs; static LONG g_dxcs_init;
static void dx_lock(int on) { if (!g_dxcs_init) { if (!InterlockedCompareExchange(&g_dxcs_init, 1, 0)) { InitializeCriticalSection(&g_dxcs); g_dxcs_init = 2; } while (g_dxcs_init != 2) Sleep(0); } if (on) EnterCriticalSection(&g_dxcs); else LeaveCriticalSection(&g_dxcs); }
static HRESULT dx_new(const void *const *vt, void *real, void **o, const char *what) {
  DxT *t; int i, fr = -1;
  dx_lock(1);
  for (i = 0; i < 16; i++) { if (g_dxcache[i].real == real && g_dxcache[i].t->vt == vt) { t = g_dxcache[i].t; InterlockedIncrement(&t->ref); dx_lock(0); dx_release_real(real); *o = t; return S_OK; } if (!g_dxcache[i].real && fr < 0) fr = i; }
  t = (DxT *)HeapAlloc(GetProcessHeap(), 0, sizeof *t); if (!t) { dx_lock(0); dx_release_real(real); return E_OUTOFMEMORY; }
  t->vt = vt; t->real = real; t->ref = 1; if (fr >= 0) { g_dxcache[fr].real = real; g_dxcache[fr].t = t; }
  dx_lock(0); *o = t; dx_log(what); return S_OK; }
static void dx_uncache(DxT *t) { int i; dx_lock(1); for (i = 0; i < 16; i++) if (g_dxcache[i].t == t) { g_dxcache[i].real = NULL; g_dxcache[i].t = NULL; } dx_lock(0); }
static HRESULT dx_upgrade_dev(void *dev, void **o);
static HRESULT dx_upgrade_ctx(void *ctx, void **o);
static HRESULT dx_upgrade_dxgi(void *dxgi, void **o);
static const GUID IID_DXGIPriv26_;                                          /* defined with the probe below */
static HRESULT dx_probe26(void *dev, void **o);
static int dx_caller_is_system(void *ra);

/* ---- tear-off IUnknown ---- */
static ULONG WINAPI dxt_ar(DxT *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG WINAPI dxt_rl(DxT *t) { LONG n = InterlockedDecrement(&t->ref); if (!n) { dx_uncache(t); dx_release_real(t->real); HeapFree(GetProcessHeap(), 0, t); } return (ULONG)n; }
static const GUID IID_D3D11Device4_ = {0x8992ab71, 0x02e6, 0x4b8d, {0xba, 0x48, 0xb0, 0x56, 0xdc, 0xda, 0x42, 0xc4}};
static const GUID IID_D3D11Device5_ = {0x8ffde202, 0xa0e7, 0x45df, {0x9e, 0x01, 0xe8, 0x37, 0x80, 0x1b, 0x5e, 0xa0}};
static const GUID IID_D3D11Ctx4_ = {0x917600da, 0xf58c, 0x4c33, {0x98, 0xd8, 0x3e, 0x15, 0xb3, 0x90, 0xfa, 0x24}};
/* Device5 / Context4 (fences, slots 67/68 and 147/148): WinUI 3's dwmcorei CD3DDevice::Init requires both interfaces */
static int dx_is_dev123(REFIID r) { return IsEqualGUID(r, &IID_D3D11Device1_) || IsEqualGUID(r, &IID_D3D11Device2_) || IsEqualGUID(r, &IID_D3D11Device3_) || IsEqualGUID(r, &IID_D3D11Device4_) || IsEqualGUID(r, &IID_D3D11Device5_); }
static int dx_is_ctx123(REFIID r) { return IsEqualGUID(r, &IID_D3D11Ctx1_) || IsEqualGUID(r, &IID_D3D11Ctx2_) || IsEqualGUID(r, &IID_D3D11Ctx3_) || IsEqualGUID(r, &IID_D3D11Ctx4_); }
/* failed QIs on the D3D/DXGI objects, with the caller (who is missing what) - first 40 */
static void dx_qimiss(const char *what, REFIID r, HRESULT hr, void *caller) {
  static volatile LONG n; char g[48]; WCHAR mp[MAX_PATH] = L"?"; HMODULE cm = NULL; const WCHAR *b;
  if (InterlockedIncrement(&n) > 40) return; guid_str(r, g);
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)caller, &cm)) GetModuleFileNameW(cm, mp, MAX_PATH);
  b = wcsrchr(mp, L'\\'); slog("[dx] %s QI %s -> 0x%08lX (caller %ls+0x%llx)", what, g, (unsigned long)hr, b ? b + 1 : mp, (unsigned long long)((BYTE *)caller - (BYTE *)cm)); }
static HRESULT WINAPI devt_qi(DxT *t, REFIID r, void **o) {
  HRESULT hr;
  if (dx_is_dev123(r)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; }
  if (IsEqualGUID(r, &IID_DXGIPriv26_)) return dx_probe26(t->real, o);
  if (IsEqualGUID(r, &IID_DXGIDevice2_) && !dx_caller_is_system(__builtin_return_address(0))) { void *dx = NULL; if (SUCCEEDED(VCALL(t->real, 0, PQI_)(t->real, &IID_DXGIDevice_, &dx)) && dx) { hr = dx_upgrade_dxgi(dx, o); dx_release_real(dx); return hr; } }
  hr = VCALL(t->real, 0, PQI_)(t->real, r, o);
  if (hr == E_NOINTERFACE && IsEqualGUID(r, &IID_DXGIDevice3_)) { void *dx = NULL; if (SUCCEEDED(VCALL(t->real, 0, PQI_)(t->real, &IID_DXGIDevice_, &dx)) && dx) { hr = dx_upgrade_dxgi(dx, o); dx_release_real(dx); return hr; } }
  if (FAILED(hr)) dx_qimiss("ID3D11Device3 tear-off", r, hr, __builtin_return_address(0)); return hr; }
static HRESULT WINAPI ctxt_qi(DxT *t, REFIID r, void **o) {
  if (dx_is_ctx123(r)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; }
  return VCALL(t->real, 0, PQI_)(t->real, r, o); }
static HRESULT WINAPI dxgt_qi(DxT *t, REFIID r, void **o) {
  if (IsEqualGUID(r, &IID_DXGIDevice3_) || IsEqualGUID(r, &IID_DXGIDevice2_)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; }
  { HRESULT hr = VCALL(t->real, 0, PQI_)(t->real, r, o); if (FAILED(hr)) dx_qimiss("IDXGIDevice3 tear-off", r, hr, __builtin_return_address(0)); return hr; } }

/* ---- ID3D11Device2/3 additions (slots 50..64) ---- */
static void WINAPI devt_get_ctx23(DxT *t, void **ctx) {                      /* GetImmediateContext2 / 3 */
  void *c = NULL; *ctx = NULL; VCALL(t->real, 40, void (WINAPI *)(void *, void **))(t->real, &c); if (c) { dx_upgrade_ctx(c, ctx); dx_release_real(c); } }
static HRESULT WINAPI devt_deferred23(DxT *t, UINT f, void **o) { (void)t; (void)f; if (o) *o = NULL; dx_log("CreateDeferredContext2/3 refused"); return E_NOTIMPL; }
static void WINAPI devt_tiling(DxT *t, void *res, UINT *ntiles, void *mip, void *shape, UINT *nsub, UINT first, void *sub) {
  (void)t; (void)res; (void)mip; (void)shape; (void)first; (void)sub; if (ntiles) *ntiles = 0; if (nsub) *nsub = 0; dx_log("GetResourceTiling (no tiled resources)"); }
static HRESULT WINAPI devt_msaa1(DxT *t, UINT fmt, UINT count, UINT flags, UINT *levels) { (void)flags; return VCALL(t->real, 30, HRESULT (WINAPI *)(void *, UINT, UINT, UINT *))(t->real, fmt, count, levels); }
/* the *1 descriptions extend the 11.0 ones at the end (TextureLayout, ConservativeRaster, PlaneSlice, ContextType): the prefix is the old description */
static HRESULT WINAPI devt_tex2d1(DxT *t, const void *d, const void *init, void **o) { dx_log("CreateTexture2D1 -> CreateTexture2D"); return VCALL(t->real, 5, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(t->real, d, init, o); }
static HRESULT WINAPI devt_tex3d1(DxT *t, const void *d, const void *init, void **o) { dx_log("CreateTexture3D1 -> CreateTexture3D"); return VCALL(t->real, 6, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(t->real, d, init, o); }
static HRESULT WINAPI devt_rs2(DxT *t, const void *d, void **o) { return VCALL(t->real, 46, HRESULT (WINAPI *)(void *, const void *, void **))(t->real, d, o); }
static HRESULT WINAPI devt_srv1(DxT *t, void *res, const void *d, void **o) { return VCALL(t->real, 7, HRESULT (WINAPI *)(void *, void *, const void *, void **))(t->real, res, d, o); }
static HRESULT WINAPI devt_uav1(DxT *t, void *res, const void *d, void **o) { return VCALL(t->real, 8, HRESULT (WINAPI *)(void *, void *, const void *, void **))(t->real, res, d, o); }
static HRESULT WINAPI devt_rtv1(DxT *t, void *res, const void *d, void **o) { return VCALL(t->real, 9, HRESULT (WINAPI *)(void *, void *, const void *, void **))(t->real, res, d, o); }
static HRESULT WINAPI devt_query1(DxT *t, const void *d, void **o) { return VCALL(t->real, 24, HRESULT (WINAPI *)(void *, const void *, void **))(t->real, d, o); }
static void dx_hook_clearview_state(void *dev, void *state); static void dx_hook_clearview_ctx(void *ctx);
/* slot 47 CreateDeviceContextState(flags, levels, n, sdk, REFIID emulated, *chosen, **state): d2d1 asks to emulate ID3D11Device3, Win7 knows up to Device1 */
static HRESULT WINAPI devt_ctxstate(DxT *t, UINT flags, const void *levels, UINT n, UINT sdk, REFIID iid, void *chosen, void **state) {
  const GUID *use = (IsEqualGUID(iid, &IID_D3D11Device2_) || IsEqualGUID(iid, &IID_D3D11Device3_)) ? &IID_D3D11Device1_ : iid;
  /* flag 1 (SINGLETHREADED) is only valid on a single-threaded device: WinUI 3's dwmcorei (CSurfaceShaderComposer::Create) asks for it, and
     on XAML's device - which it shares (see D3D11CreateDevice) - D3D returned E_INVALIDARG */
  if ((flags & 1) && !(VCALL(t->real, 38, UINT (WINAPI *)(void *))(t->real) & 1)) flags &= ~1u;   /* ID3D11Device::GetCreationFlags */
  { HRESULT hr = VCALL(t->real, 47, HRESULT (WINAPI *)(void *, UINT, const void *, UINT, UINT, const GUID *, void *, void **))(t->real, flags, levels, n, sdk, use, chosen, state);
    if (SUCCEEDED(hr) && state && *state) dx_hook_clearview_state(t->real, *state); return hr; } }
/* slot 33 CheckFeatureSupport: Win7's runtime rejects the newer D3D11_FEATURE values (d2d1 asks 14 = D3D11_OPTIONS2): answer "nothing optional" */
static HRESULT WINAPI devt_feature(DxT *t, UINT feat, void *data, UINT size) {
  HRESULT hr;
  /* features 10+ (D3D11_OPTIONS1 and later) are Windows 8+: answered "nothing supported" without asking Win7, whose d3d11 called through a
     bad driver pointer for them on D2D's own (single-threaded) device instead of failing */
  if (feat >= 10 && data && size <= 256) { static LONG n; memset(data, 0, size); if (InterlockedIncrement(&n) <= 8) { char b[64]; _snprintf(b, sizeof b, "CheckFeatureSupport(%u) -> zeros", feat); dx_log(b); } return S_OK; }
  hr = VCALL(t->real, 33, HRESULT (WINAPI *)(void *, UINT, void *, UINT))(t->real, feat, data, size);
  /* feature 5 = D3D11_OPTIONS (14 BOOLs, Win8 additions): Win7's runtime answers for a WDDM 1.1 driver. The constant-buffer and
     no-overwrite items are cleared: Win10 D2D chose its effect constant path from them and its effects (flood, alpha mask - WinUI 3's
     rounded fills, nine-grids, acrylic) rendered transparent */
  if (feat == 5 && SUCCEEDED(hr) && data && size >= 14 * 4) { BOOL *o = (BOOL *)data; static LONG nl;
    if (InterlockedIncrement(&nl) <= 2) slog("[dx] D3D11_OPTIONS from Win7: logicop %d uavforced %d discard %d updcopyflags %d clearview %d copyoverlap %d cbpartial %d cboffset %d mapnooverwriteCB %d mapnooverwriteSRV %d msaa1 %d sad4 %d dbl %d sharing %d",
                                            o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8], o[9], o[10], o[11], o[12], o[13]);
    o[6] = o[7] = o[8] = o[9] = FALSE; }
  if (hr == E_INVALIDARG && feat >= 10 && data && size <= 256) { char b[64]; memset(data, 0, size); _snprintf(b, sizeof b, "CheckFeatureSupport(%u) -> zeros", feat); dx_log(b); hr = S_OK; }
  return hr; }
static void WINAPI devt_wsub(DxT *t) { (void)t; dx_log("WriteToSubresource ignored"); }
static void WINAPI devt_rsub(DxT *t) { (void)t; dx_log("ReadFromSubresource ignored"); }
/* failure logging on the hot creation / map paths (d2d1 text renders nothing: find the D3D call it gives up on) */
static volatile LONG g_dxfail;
#define DXFAIL(fmt, ...) do { if (InterlockedIncrement(&g_dxfail) <= 60) slog("[dx] FAILED " fmt, __VA_ARGS__); } while (0)
typedef struct { UINT w, h, mips, arr, fmt, sc, sq, usage, bind, cpu, misc; } TexDesc_;
typedef struct { UINT size, usage, bind, cpu, misc, stride; } BufDesc_;
static HRESULT WINAPI devt_buffer(DxT *t, const BufDesc_ *d, const void *init, void **o) {
  HRESULT hr = VCALL(t->real, 3, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(t->real, d, init, o);
  if (FAILED(hr)) DXFAIL("CreateBuffer size %u usage %u bind 0x%x cpu 0x%x misc 0x%x -> 0x%08lX", d->size, d->usage, d->bind, d->cpu, d->misc, (unsigned long)hr); return hr; }
/* D3D11_RESOURCE_MISC_SHARED_NTHANDLE (0x800) is Win8+: Win7 fails the whole texture. WinUI 3's lifted compositor (marshal.dll
   CSurfaceManager::CreateCompositionBuffer, misc 0x802) shares its buffers inside this process only: legacy sharing (SHARED) is kept. */
static const TexDesc_ *dx_tex_fix(const TexDesc_ *d, TexDesc_ *tmp) {
  if (!d || !(d->misc & 0x800)) return d; *tmp = *d; tmp->misc &= ~0x800u; if (!(tmp->misc & 0x100)) tmp->misc |= 0x2;   /* not with KEYEDMUTEX */
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 5) slog("[dx] CreateTexture2D %ux%u: SHARED_NTHANDLE dropped (Win8+), misc 0x%x -> 0x%x", d->w, d->h, d->misc, tmp->misc); }
  return tmp; }
static PQI_ g_qi_tex; static HRESULT WINAPI tex_qi_hook(void *self, REFIID r, void **o); static int dx_patch(void *obj, PQI_ *orig, PQI_ hook);
static HRESULT WINAPI devt_tex2d(DxT *t, const TexDesc_ *d, const void *init, void **o) {
  TexDesc_ tmp; HRESULT hr = VCALL(t->real, 5, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(t->real, dx_tex_fix(d, &tmp), init, o);
  if (SUCCEEDED(hr) && o && *o && d && (d->misc & 0x800)) dx_patch(*o, &g_qi_tex, tex_qi_hook);
  if (FAILED(hr)) DXFAIL("CreateTexture2D %ux%u fmt %u usage %u bind 0x%x cpu 0x%x misc 0x%x -> 0x%08lX", d->w, d->h, d->fmt, d->usage, d->bind, d->cpu, d->misc, (unsigned long)hr); return hr; }
static HRESULT WINAPI devt_srv(DxT *t, void *res, const UINT *d, void **o) {
  HRESULT hr = VCALL(t->real, 7, HRESULT (WINAPI *)(void *, void *, const void *, void **))(t->real, res, d, o);
  if (FAILED(hr)) DXFAIL("CreateShaderResourceView fmt %u dim %u -> 0x%08lX", d ? d[0] : 0, d ? d[1] : 0, (unsigned long)hr); return hr; }
#define DXSHADER(k, name) static HRESULT WINAPI devt_sh##k(DxT *t, const void *bc, SIZE_T n, void *link, void **o) { \
  HRESULT hr = VCALL(t->real, k, HRESULT (WINAPI *)(void *, const void *, SIZE_T, void *, void **))(t->real, bc, n, link, o); \
  if (FAILED(hr)) DXFAIL(name " (%Iu bytes) -> 0x%08lX", n, (unsigned long)hr); return hr; }
DXSHADER(12, "CreateVertexShader") DXSHADER(15, "CreatePixelShader")
static HRESULT WINAPI ctxt_map(DxT *t, void *res, UINT sub, UINT type, UINT flags, void *m) {
  HRESULT hr = VCALL(t->real, 14, HRESULT (WINAPI *)(void *, void *, UINT, UINT, UINT, void *))(t->real, res, sub, type, flags, m);
  if (FAILED(hr)) DXFAIL("Map sub %u type %u flags 0x%x -> 0x%08lX", sub, type, flags, (unsigned long)hr); return hr; }
/* ID3D11Device4 (slots 65/66): XAML's CD3D11Device::RegisterDeviceRemovedEvent. Win7 has no removal notification: hand out a cookie, never signal */
static HRESULT WINAPI devt_reg_removed(DxT *t, HANDLE ev, DWORD *cookie) { static LONG c; (void)t; (void)ev; if (!cookie) return E_INVALIDARG; *cookie = (DWORD)InterlockedIncrement(&c); return S_OK; }
static void WINAPI devt_unreg_removed(DxT *t, DWORD cookie) { (void)t; (void)cookie; }
/* ID3D11Device5 (67 OpenSharedFence, 68 CreateFence) and ID3D11DeviceContext4 (147 Signal, 148 Wait): Win7 has no fences */
static HRESULT WINAPI devt_open_fence(DxT *t, HANDLE h, REFIID r, void **o) { (void)t; (void)h; (void)r; if (o) *o = NULL; dx_log("OpenSharedFence refused (no fences on Win7)"); return E_NOTIMPL; }
static HRESULT WINAPI devt_create_fence(DxT *t, UINT64 v, UINT f, REFIID r, void **o) { (void)t; (void)v; (void)f; (void)r; if (o) *o = NULL; dx_log("CreateFence refused (no fences on Win7)"); return E_NOTIMPL; }
static HRESULT WINAPI ctxt_fence_op(DxT *t, void *fence, UINT64 v) { (void)t; (void)fence; (void)v; dx_log("context Signal/Wait refused (no fences on Win7)"); return E_NOTIMPL; }

/* ---- ID3D11DeviceContext2/3 additions (slots 134..146) ---- */
static HRESULT WINAPI ctxt_tiles_hr(DxT *t) { (void)t; dx_log("tiled-resource call refused"); return E_NOTIMPL; }
static void WINAPI ctxt_tiles_void(DxT *t) { (void)t; dx_log("tiled-resource call ignored"); }
static BOOL WINAPI ctxt_annot(DxT *t) { (void)t; return FALSE; }
static void WINAPI ctxt_marker(DxT *t) { (void)t; }
static void WINAPI ctxt_flush1(DxT *t, int type, HANDLE ev) { (void)type; VCALL(t->real, 111, void (WINAPI *)(void *))(t->real); if (ev) SetEvent(ev); }
static void WINAPI ctxt_set_hwprot(DxT *t, BOOL on) { (void)t; (void)on; }
/* ID3D11DeviceContext1::ClearView (slot 132) with rectangles: on Win7 (WDDM 1.1) the rectangles are not honoured and the whole view is
   cleared. WinUI 3's XAML clears one atlas slot before drawing each rounded-corner mask (meshes into A8 atlases): every mask but the last
   was wiped - cards, panels and other rounded fills never showed. Emulated for render target views: a scratch texture of the view's format
   is cleared to the colour and copied into each rectangle. */
static const GUID IID_D3D11RTV_ = {0xdfdba067, 0x0b8d, 0x4865, {0x87, 0x5b, 0xd7, 0xb4, 0x51, 0x6c, 0xc1, 0x64}};
static const GUID IID_D3D11Tex2D_ = {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};
static void WINAPI ctxt_clearview(DxT *t, void *view, const float *col, const RECT *rc, UINT n) {
  typedef void (WINAPI *PCV)(void *, void *, const float *, const RECT *, UINT); static LONG nl;
  void *rtv = NULL, *res = NULL, *tex = NULL, *dev = NULL; TexDesc_ td; UINT rd[4] = {0}; UINT i;
  if (!n || !rc || !view || FAILED(VCALL(view, 0, PQI_)(view, &IID_D3D11RTV_, &rtv)) || !rtv) { VCALL(t->real, 132, PCV)(t->real, view, col, rc, n); return; }
  VCALL(rtv, 7, void (WINAPI *)(void *, void **))(rtv, &res);                      /* ID3D11View::GetResource */
  VCALL(rtv, 8, void (WINAPI *)(void *, void *))(rtv, rd);                         /* ID3D11RenderTargetView::GetDesc: Format, ViewDimension, MipSlice */
  if (res && SUCCEEDED(VCALL(res, 0, PQI_)(res, &IID_D3D11Tex2D_, &tex)) && tex && rd[1] == 4 /* TEXTURE2D */) {
    VCALL(tex, 10, void (WINAPI *)(void *, TexDesc_ *))(tex, &td); VCALL(t->real, 3, void (WINAPI *)(void *, void **))(t->real, &dev);
    for (i = 0; dev && i < n; i++) { LONG w = rc[i].right - rc[i].left, h = rc[i].bottom - rc[i].top; TexDesc_ sd; void *scr = NULL, *srtv = NULL;
      if (w <= 0 || h <= 0) continue;
      memset(&sd, 0, sizeof sd); sd.w = (UINT)w; sd.h = (UINT)h; sd.mips = 1; sd.arr = 1; sd.fmt = rd[0] ? rd[0] : td.fmt; sd.sc = 1; sd.bind = 0x20 /* RENDER_TARGET */;
      if (SUCCEEDED(VCALL(dev, 5, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(dev, &sd, NULL, &scr)) && scr) {
        if (SUCCEEDED(VCALL(dev, 9, HRESULT (WINAPI *)(void *, void *, const void *, void **))(dev, scr, NULL, &srtv)) && srtv) {
          VCALL(t->real, 50, void (WINAPI *)(void *, void *, const float *))(t->real, srtv, col);                          /* ClearRenderTargetView */
          VCALL(t->real, 46, void (WINAPI *)(void *, void *, UINT, UINT, UINT, UINT, void *, UINT, const void *))(t->real, tex, rd[2], (UINT)rc[i].left, (UINT)rc[i].top, 0, scr, 0, NULL);   /* CopySubresourceRegion */
          dx_release_real(srtv); }
        dx_release_real(scr); } }
    if (InterlockedIncrement(&nl) <= 4) slog("[dx] ClearView with %u rect(s) on a %ux%u fmt %u target emulated (first %ld,%ld-%ld,%ld)", n, td.w, td.h, rd[0], rc[0].left, rc[0].top, rc[0].right, rc[0].bottom);
    if (dev) dx_release_real(dev); dx_release_real(tex); }
  else VCALL(t->real, 132, PCV)(t->real, view, col, rc, n);
  if (res) dx_release_real(res); dx_release_real(rtv); }
/* The same ClearView emulation for callers on the REAL context: d2d1 and dwmcorei swap in their own ID3DDeviceContextState, after which the
   (per-object) vtable no longer reaches our tear-off. XAML's/dcompi's atlases (glyph runs, corner masks) are cleared slot by slot with
   rectangles; Win7 cleared the whole atlas each time, so only the most recently rendered text/mask survived (resizing re-rendered a different
   set: the hero title appeared, the cards' text vanished). d3d11's implementation function is patched (jmp to cv_hook) - every per-object
   vtable points at it. Whole-view clears: RTV -> ClearRenderTargetView, UAV -> ClearUnorderedAccessViewFloat. */
static int dx_guard_of_view(void *view, RECT *r);
static const GUID IID_D3D11UAV_ = {0x28acf509, 0x7f5c, 0x48f6, {0x86, 0x11, 0xf3, 0x16, 0x01, 0x0a, 0x63, 0x80}};
static void WINAPI cv_hook(void *ctx, void *view, const float *col, const RECT *rc, UINT n) {
  void *rtv = NULL, *uav = NULL, *res = NULL, *tex = NULL, *dev = NULL; TexDesc_ td; UINT rd[4] = {0}; UINT i;
  if (!view) return;
  if (FAILED(VCALL(view, 0, PQI_)(view, &IID_D3D11RTV_, &rtv)) || !rtv) {
    if (SUCCEEDED(VCALL(view, 0, PQI_)(view, &IID_D3D11UAV_, &uav)) && uav) { if (!n) VCALL(ctx, 52, void (WINAPI *)(void *, void *, const float *))(ctx, uav, col); dx_release_real(uav); }
    return; }
  if (!n || !rc) { RECT g; if (dx_guard_of_view(rtv, &g)) { dx_release_real(rtv); if (g.right > g.left && g.bottom > g.top) cv_hook(ctx, view, col, &g, 1); return; }
    VCALL(ctx, 50, void (WINAPI *)(void *, void *, const float *))(ctx, rtv, col); dx_release_real(rtv); return; }
  VCALL(rtv, 7, void (WINAPI *)(void *, void **))(rtv, &res); VCALL(rtv, 8, void (WINAPI *)(void *, void *))(rtv, rd);
  if (res && SUCCEEDED(VCALL(res, 0, PQI_)(res, &IID_D3D11Tex2D_, &tex)) && tex && rd[1] == 4) {
    VCALL(tex, 10, void (WINAPI *)(void *, TexDesc_ *))(tex, &td); VCALL(ctx, 3, void (WINAPI *)(void *, void **))(ctx, &dev);
    for (i = 0; dev && i < n; i++) { RECT r = rc[i]; TexDesc_ sd; void *scr = NULL, *srtv = NULL;
      if (r.left < 0) r.left = 0; if (r.top < 0) r.top = 0; if (r.right > (LONG)td.w) r.right = (LONG)td.w; if (r.bottom > (LONG)td.h) r.bottom = (LONG)td.h;
      if (r.right <= r.left || r.bottom <= r.top) continue;
      memset(&sd, 0, sizeof sd); sd.w = (UINT)(r.right - r.left); sd.h = (UINT)(r.bottom - r.top); sd.mips = 1; sd.arr = 1; sd.fmt = rd[0] ? rd[0] : td.fmt; sd.sc = 1; sd.bind = 0x20;
      if (SUCCEEDED(VCALL(dev, 5, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(dev, &sd, NULL, &scr)) && scr) {
        if (SUCCEEDED(VCALL(dev, 9, HRESULT (WINAPI *)(void *, void *, const void *, void **))(dev, scr, NULL, &srtv)) && srtv) {
          VCALL(ctx, 50, void (WINAPI *)(void *, void *, const float *))(ctx, srtv, col);
          VCALL(ctx, 46, void (WINAPI *)(void *, void *, UINT, UINT, UINT, UINT, void *, UINT, const void *))(ctx, tex, rd[2], (UINT)r.left, (UINT)r.top, 0, scr, 0, NULL);
          dx_release_real(srtv); }
        dx_release_real(scr); } }
    { static LONG nl; if (InterlockedIncrement(&nl) <= 4) slog("[dx] real-context ClearView with %u rect(s) on a %ux%u fmt %u target emulated (first %ld,%ld-%ld,%ld)", n, td.w, td.h, rd[0], rc[0].left, rc[0].top, rc[0].right, rc[0].bottom); }
    dx_release_real(tex); }
  else VCALL(ctx, 50, void (WINAPI *)(void *, void *, const float *))(ctx, rtv, col);   /* not a plain 2D texture: whole view (Win7's behaviour) */
  if (dev) dx_release_real(dev); if (res) dx_release_real(res); dx_release_real(rtv); }
static void dx_hook_clearview_ctx(void *ctx) {
  static void *done[8]; static LONG nd; static CRITICAL_SECTION cs; static LONG init; BYTE *fn; int i; DWORD old; BYTE jmp[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
  if (!ctx) return;
  if (InterlockedCompareExchange(&init, 1, 0) == 0) { InitializeCriticalSection(&cs); InterlockedExchange(&init, 2); } while (init != 2) Sleep(0);
  fn = (BYTE *)(*(void ***)ctx)[132]; if (!fn || fn == (BYTE *)cv_hook) return;
  EnterCriticalSection(&cs);
  for (i = 0; i < nd; i++) if (done[i] == fn) { LeaveCriticalSection(&cs); return; }
  if (nd < 8 && !(fn[0] == 0x48 && fn[1] == 0xB8 && fn[10] == 0xFF && fn[11] == 0xE0) && VirtualProtect(fn, 12, PAGE_EXECUTE_READWRITE, &old)) {
    void *h = (void *)cv_hook; memcpy(jmp + 2, &h, 8); memcpy(fn, jmp, 12); VirtualProtect(fn, 12, old, &old); FlushInstructionCache(GetCurrentProcess(), fn, 12);
    done[nd++] = fn; { HMODULE m = NULL; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)fn, &m);
      slog("[dx] ClearView implementation %p (module+0x%llx) patched: rectangles emulated on every context", fn, (unsigned long long)(fn - (BYTE *)m)); } }
  LeaveCriticalSection(&cs); }
/* a context state's vtable variant: swap the new state in briefly, patch its ClearView, swap back */
static void dx_hook_clearview_state(void *dev, void *state) {
  void *ctx = NULL, *prev = NULL; if (!dev || !state) return;
  VCALL(dev, 40, void (WINAPI *)(void *, void **))(dev, &ctx); if (!ctx) return;   /* ID3D11Device::GetImmediateContext (real) */
  dx_hook_clearview_ctx(ctx);
  VCALL(ctx, 131, void (WINAPI *)(void *, void *, void **))(ctx, state, &prev);       /* ID3D11DeviceContext1::SwapDeviceContextState */
  dx_hook_clearview_ctx(ctx);
  VCALL(ctx, 131, void (WINAPI *)(void *, void *, void **))(ctx, prev, NULL); if (prev) dx_release_real(prev);
  dx_release_real(ctx); }
/* CopySubresourceRegion1 (slot 115): flag bits above NO_OVERWRITE | DISCARD are Win8+ (dwmcorei passes 4); Win7's D3D 11.1 dropped the
   whole copy. Stripped. */
static void WINAPI ctxt_copyreg1(DxT *t, void *dst, UINT ds, UINT x, UINT y, UINT z, void *src, UINT ss, const UINT *box, UINT fl) {
  if (fl & ~3u) { static LONG nf; if (InterlockedIncrement(&nf) <= 2) slog("[dx] CopySubresourceRegion1 flags 0x%x: bits above NO_OVERWRITE|DISCARD unknown to Win7 D3D11.1 (the copy was dropped) - stripped", fl); fl &= 3u; }
  VCALL(t->real, 115, void (WINAPI *)(void *, void *, UINT, UINT, UINT, UINT, void *, UINT, const UINT *, UINT))(t->real, dst, ds, x, y, z, src, ss, box, fl); }
/* DiscardResource / DiscardView / DiscardView1 (slots 117, 118, 133) are hints and are not passed on: Win7 discarded whole shared atlases
   (XAML's glyph runs and corner masks) that were still being sampled. */
static void WINAPI ctxt_discres(DxT *t, void *r) { (void)t; (void)r; }
static void WINAPI ctxt_discview(DxT *t, void *v) { (void)t; (void)v; }
static void WINAPI ctxt_discview1(DxT *t, void *v, const RECT *rc, UINT n) { (void)t; (void)v; (void)rc; (void)n; }
static void WINAPI ctxt_clearrtv_guard(DxT *t, void *v, const float *c);
static void WINAPI ctxt_get_hwprot(DxT *t, BOOL *on) { (void)t; if (on) *on = FALSE; }

/* ---- {26C5DC23-E49C-4B0A-8F79-E7B1AC804D32}: private Win10 D3D device interface d2d1's CD3DDeviceCommon::Initialize requires (no public
   name). Slots 6..8 are guard rects (below); every other slot logs its number once and returns S_OK. ---- */
static const GUID IID_DXGIPriv26_ = {0x26c5dc23, 0xe49c, 0x4b0a, {0x8f, 0x79, 0xe7, 0xb1, 0xac, 0x80, 0x4d, 0x32}};
#define DXPROBE(k) static HRESULT WINAPI dxp_##k(DxT *t) { char b[64]; (void)t; _snprintf(b, sizeof b, "DXGI private {26C5DC23} slot %d called", k); dx_log(b); return S_OK; }
DXPROBE(3) DXPROBE(4) DXPROBE(5) DXPROBE(6) DXPROBE(7) DXPROBE(8) DXPROBE(9) DXPROBE(10) DXPROBE(11) DXPROBE(12) DXPROBE(13) DXPROBE(14) DXPROBE(15) DXPROBE(16)
/* slots 6/7/8 are ID3D11PartnerDevice guard rects: SetGuardRect(texture, const RECT *), SetEmptyGuardRect(texture), RemoveGuardRect(texture).
   WinUI 3's dcompi brackets each atlas-slot draw with one; Win10's runtime confines even a full ClearRenderTargetView to it. d2d1 clears the
   whole target (deferred ClearRenderTargetView) for XAML's Clear(transparent) - on Win7 that wiped every glyph run and corner mask in the
   shared atlas except the newest (cards' text vanished as the title appeared on resize; card fills, header tiles never stayed). Enforced on
   clears: ctxt_clearrtv_guard / cv_hook. */
typedef struct { void *res; RECT r; int empty; } DxGuard_;
static DxGuard_ g_dxguard[64]; static SRWLOCK g_dxguard_lk = SRWLOCK_INIT;
static const GUID IID_DxUnk_ = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static void *dx_unk(void *o) { void *u = NULL; if (o && SUCCEEDED(VCALL(o, 0, PQI_)(o, &IID_DxUnk_, &u)) && u) dx_release_real(u); return u; }
static void dx_guard_set(void *tex, const RECT *r, int empty) { void *u = dx_unk(tex); int i, fr = -1; if (!u) return;
  AcquireSRWLockExclusive(&g_dxguard_lk);
  for (i = 0; i < 64; i++) { if (g_dxguard[i].res == u) { fr = i; break; } if (fr < 0 && !g_dxguard[i].res) fr = i; }
  if (fr >= 0) { if (r || empty) { g_dxguard[fr].res = u; g_dxguard[fr].empty = empty; if (r) g_dxguard[fr].r = *r; else SetRectEmpty(&g_dxguard[fr].r); } else if (g_dxguard[fr].res == u) g_dxguard[fr].res = NULL; }
  ReleaseSRWLockExclusive(&g_dxguard_lk); }
/* 1 = a guard is set for the view's resource (*r = the rect, empty -> r empty) */
static int dx_guard_of_view(void *view, RECT *r) { void *res = NULL, *u; int i, hit = 0; if (!view) return 0;
  VCALL(view, 7, void (WINAPI *)(void *, void **))(view, &res); if (!res) return 0; u = dx_unk(res); dx_release_real(res);
  AcquireSRWLockShared(&g_dxguard_lk); for (i = 0; i < 64; i++) if (g_dxguard[i].res && g_dxguard[i].res == u) { *r = g_dxguard[i].empty ? (RECT){0, 0, 0, 0} : g_dxguard[i].r; hit = 1; break; } ReleaseSRWLockShared(&g_dxguard_lk);
  return hit; }
static HRESULT WINAPI dxp_setguard(DxT *t, void *tex, const RECT *r) { static LONG n; (void)t; if (r && InterlockedIncrement(&n) <= 3) slog("[dx] partner SetGuardRect %ld,%ld-%ld,%ld (enforced on clears)", r->left, r->top, r->right, r->bottom); dx_guard_set(tex, r, 0); return S_OK; }
static HRESULT WINAPI dxp_emptyguard(DxT *t, void *tex) { (void)t; dx_guard_set(tex, NULL, 1); return S_OK; }
static HRESULT WINAPI dxp_rmguard(DxT *t, void *tex) { (void)t; dx_guard_set(tex, NULL, 0); return S_OK; }
static void WINAPI cv_hook(void *ctx, void *view, const float *col, const RECT *rc, UINT n);
/* ClearRenderTargetView (slot 50) under a guard rect: only the guard's area (none for an empty guard) */
static void WINAPI ctxt_clearrtv_guard(DxT *t, void *v, const float *c) { RECT g;
  if (dx_guard_of_view(v, &g)) { if (g.right > g.left && g.bottom > g.top) cv_hook(t->real, v, c, &g, 1); return; }
  VCALL(t->real, 50, void (WINAPI *)(void *, void *, const float *))(t->real, v, c); }
static HRESULT WINAPI dxpt_qi(DxT *t, REFIID r, void **o) { if (IsEqualGUID(r, &IID_DXGIPriv26_)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; } return VCALL(t->real, 0, PQI_)(t->real, r, o); }
static const void *const g_dxpt_vt[17] = { (void *)dxpt_qi, (void *)dxt_ar, (void *)dxt_rl, (void *)dxp_3, (void *)dxp_4, (void *)dxp_5, (void *)dxp_setguard, (void *)dxp_emptyguard, (void *)dxp_rmguard,
  (void *)dxp_9, (void *)dxp_10, (void *)dxp_11, (void *)dxp_12, (void *)dxp_13, (void *)dxp_14, (void *)dxp_15, (void *)dxp_16 };
/* asked of the D3D11 device (d2d1 CD3DDeviceCommon::Initialize QIs its ID3D11Device3) */
static HRESULT dx_probe26(void *dev, void **o) { void *real = NULL; if (FAILED(g_qi_dev(dev, &IID_D3D11Device_, &real))) return E_NOINTERFACE; return dx_new(g_dxpt_vt, real, o, "D3D11 private {26C5DC23} probe"); }

/* ---- IDXGIDevice3 (Trim, slot 17) over IDXGIDevice2 ---- */
static void WINAPI dxgt_trim(DxT *t) { (void)t; }
/* IDXGIDevice2::EnqueueSetEvent (slot 16): Win7's DXGI 1.2 has the slot but returns DXGI_ERROR_UNSUPPORTED. WinUI 3's lifted compositor
   (marshal.dll CTokenManager::ScheduleUpdate <- LM_PresentCompositionBuffers) needs it for every composition-buffer present; the failure
   reached XAML's first frame as 0x887A0004 and the Gallery window was never shown. Emulated: event query, Flush, wait for the GPU, SetEvent. */
static HRESULT WINAPI dxgt_enqueue_set_event(DxT *t, HANDLE ev) {
  void *dev = NULL, *ctx = NULL, *q = NULL; UINT qd[2] = {0 /* D3D11_QUERY_EVENT */, 0}; HRESULT hr = S_OK; static LONG nl;
  if (!ev) return E_INVALIDARG;
  if (SUCCEEDED(VCALL(t->real, 0, PQI_)(t->real, &IID_D3D11Device_, &dev)) && dev) {
    VCALL(dev, 40, void (WINAPI *)(void *, void **))(dev, &ctx);
    if (ctx) {
      if (SUCCEEDED(VCALL(dev, 24, HRESULT (WINAPI *)(void *, const void *, void **))(dev, qd, &q)) && q) {
        BOOL done = FALSE; DWORD t0 = GetTickCount(); HRESULT g;
        VCALL(ctx, 28, void (WINAPI *)(void *, void *))(ctx, q);
        VCALL(ctx, 111, void (WINAPI *)(void *))(ctx);
        while ((g = VCALL(ctx, 29, HRESULT (WINAPI *)(void *, void *, void *, UINT, UINT))(ctx, q, &done, sizeof done, 0)) == S_FALSE && GetTickCount() - t0 < 2000) Sleep(0);
        if (g != S_OK && InterlockedIncrement(&nl) <= 4) slog("[dx] EnqueueSetEvent: GPU wait ended 0x%08lX after %lu ms", (unsigned long)g, GetTickCount() - t0);
        dx_release_real(q); }
      else VCALL(ctx, 111, void (WINAPI *)(void *))(ctx);
      dx_release_real(ctx); }
    dx_release_real(dev); }
  if (InterlockedIncrement(&nl) <= 2) slog("[dx] EnqueueSetEvent emulated (query + flush + SetEvent)");
  if (!SetEvent(ev)) hr = HRESULT_FROM_WIN32(GetLastError());
  return hr; }

static HRESULT WINAPI devt_open_shared1(DxT *t, HANDLE h, REFIID r, void **o);
static void dx_tables(void) {
  static LONG done; int k; if (InterlockedCompareExchange(&done, 1, 0)) return;
  for (k = 3; k < 50; k++) g_devt_vt[k] = dxfwd_tab[k];
  g_devt_vt[48] = (void *)devt_open_shared1;   /* ID3D11Device1::OpenSharedResource1 (NT handles of ours, see dx_open_shared1) */
  g_devt_vt[0] = (void *)devt_qi; g_devt_vt[1] = (void *)dxt_ar; g_devt_vt[2] = (void *)dxt_rl;
  g_devt_vt[50] = (void *)devt_get_ctx23; g_devt_vt[51] = (void *)devt_deferred23; g_devt_vt[52] = (void *)devt_tiling; g_devt_vt[53] = (void *)devt_msaa1;
  g_devt_vt[54] = (void *)devt_tex2d1; g_devt_vt[55] = (void *)devt_tex3d1; g_devt_vt[56] = (void *)devt_rs2; g_devt_vt[57] = (void *)devt_srv1; g_devt_vt[58] = (void *)devt_uav1;
  g_devt_vt[59] = (void *)devt_rtv1; g_devt_vt[60] = (void *)devt_query1; g_devt_vt[61] = (void *)devt_get_ctx23; g_devt_vt[62] = (void *)devt_deferred23;
  g_devt_vt[63] = (void *)devt_wsub; g_devt_vt[64] = (void *)devt_rsub; g_devt_vt[65] = (void *)devt_reg_removed; g_devt_vt[66] = (void *)devt_unreg_removed;
  g_devt_vt[67] = (void *)devt_open_fence; g_devt_vt[68] = (void *)devt_create_fence; g_ctxt_vt[147] = (void *)ctxt_fence_op; g_ctxt_vt[148] = (void *)ctxt_fence_op; g_devt_vt[47] = (void *)devt_ctxstate; g_devt_vt[33] = (void *)devt_feature;
  g_devt_vt[40] = (void *)devt_get_ctx23; g_devt_vt[43] = (void *)devt_get_ctx23;     /* GetImmediateContext / 1: the same context tear-off everywhere */
  g_devt_vt[3] = (void *)devt_buffer; g_devt_vt[5] = (void *)devt_tex2d; g_devt_vt[7] = (void *)devt_srv; g_devt_vt[12] = (void *)devt_sh12; g_devt_vt[15] = (void *)devt_sh15;
  for (k = 3; k < 134; k++) g_ctxt_vt[k] = dxfwd_tab[k];
  g_ctxt_vt[14] = (void *)ctxt_map; g_ctxt_vt[132] = (void *)ctxt_clearview; g_ctxt_vt[115] = (void *)ctxt_copyreg1;
  g_ctxt_vt[117] = (void *)ctxt_discres; g_ctxt_vt[118] = (void *)ctxt_discview; g_ctxt_vt[133] = (void *)ctxt_discview1; g_ctxt_vt[50] = (void *)ctxt_clearrtv_guard;
  g_ctxt_vt[0] = (void *)ctxt_qi; g_ctxt_vt[1] = (void *)dxt_ar; g_ctxt_vt[2] = (void *)dxt_rl;
  g_ctxt_vt[134] = (void *)ctxt_tiles_hr; g_ctxt_vt[135] = (void *)ctxt_tiles_hr; g_ctxt_vt[136] = (void *)ctxt_tiles_void; g_ctxt_vt[137] = (void *)ctxt_tiles_void;
  g_ctxt_vt[138] = (void *)ctxt_tiles_hr; g_ctxt_vt[139] = (void *)ctxt_tiles_void; g_ctxt_vt[140] = (void *)ctxt_annot; g_ctxt_vt[141] = (void *)ctxt_marker;
  g_ctxt_vt[142] = (void *)ctxt_marker; g_ctxt_vt[143] = (void *)ctxt_marker; g_ctxt_vt[144] = (void *)ctxt_flush1; g_ctxt_vt[145] = (void *)ctxt_set_hwprot; g_ctxt_vt[146] = (void *)ctxt_get_hwprot;
  for (k = 3; k < 17; k++) g_dxgt_vt[k] = dxfwd_tab[k];
  g_dxgt_vt[0] = (void *)dxgt_qi; g_dxgt_vt[1] = (void *)dxt_ar; g_dxgt_vt[2] = (void *)dxt_rl; g_dxgt_vt[17] = (void *)dxgt_trim;
  g_dxgt_vt[16] = (void *)dxgt_enqueue_set_event;
}
static HRESULT dx_upgrade_dev(void *dev, void **o) { void *r = NULL; if (FAILED(g_qi_dev(dev, &IID_D3D11Device1_, &r))) return E_NOINTERFACE;
  return dx_new(g_devt_vt, r, o, "ID3D11Device3 tear-off"); }
static HRESULT dx_upgrade_ctx(void *ctx, void **o) { void *r = NULL; if (FAILED(g_qi_ctx(ctx, &IID_D3D11Ctx1_, &r))) return E_NOINTERFACE; return dx_new(g_ctxt_vt, r, o, "ID3D11DeviceContext3 tear-off"); }
static HRESULT dx_upgrade_dxgi(void *dxgi, void **o) { void *r = NULL; if (FAILED(g_qi_dxgi(dxgi, &IID_DXGIDevice2_, &r))) return E_NOINTERFACE; return dx_new(g_dxgt_vt, r, o, "IDXGIDevice3 tear-off"); }

/* ---- hooked QueryInterface of the real objects ---- */
/* the caller is a module from the system directory (Win7's own dxgi/d3d11/d2d1...): those keep Win7's objects for interfaces Win7 has */
static int dx_caller_is_system(void *ra) { HMODULE m = NULL; WCHAR p[MAX_PATH], s[MAX_PATH]; UINT n;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &m) || !m || !GetModuleFileNameW(m, p, MAX_PATH)) return 0;
  n = GetSystemDirectoryW(s, MAX_PATH); return n && !_wcsnicmp(p, s, n) && p[n] == L'\\'; }
static HRESULT WINAPI dxgi_qi_hook(void *self, REFIID r, void **o) {
  HRESULT hr; void *dev = NULL;
  /* IDXGIDevice2 for callers outside system32: the tear-off (EnqueueSetEvent works there) */
  if (IsEqualGUID(r, &IID_DXGIDevice2_) && !dx_caller_is_system(__builtin_return_address(0))) return dx_upgrade_dxgi(self, o);
  if (dx_is_dev123(r)) { if (FAILED(g_qi_dxgi(self, &IID_D3D11Device_, &dev))) return E_NOINTERFACE; hr = dx_upgrade_dev(dev, o); dx_release_real(dev); return hr; }
  hr = g_qi_dxgi(self, r, o);
  if (hr != E_NOINTERFACE) return hr;
  if (IsEqualGUID(r, &IID_DXGIDevice3_)) return dx_upgrade_dxgi(self, o);
  dx_qimiss("DXGI device", r, hr, __builtin_return_address(0));
  return hr; }
/* the real device/context answer Device1/Context1 themselves, but must give out the tear-off so pointers compare equal */
/* devices created as WARP (D3D_DRIVER_TYPE_WARP): only there does Win10 D2D's WARP escape reach Win7's WARP */
static void *volatile g_warp_dev[8];
static void dx_note_warp(void *dev) { int i; for (i = 0; i < 8; i++) if (!g_warp_dev[i] && InterlockedCompareExchangePointer(&g_warp_dev[i], dev, NULL) == NULL) return; }
static int dx_is_warp(void *dev) { int i; for (i = 0; i < 8; i++) if (g_warp_dev[i] == dev) return 1; return 0; }
/* the caller is the image's (Win10) d2d1.dll - Win7's d2d1 (loaded too) keeps its own d3d11's private interfaces */
static int dx_caller_is_w10_d2d(void *ra) { HMODULE m = NULL; WCHAR p[MAX_PATH], s[MAX_PATH]; const WCHAR *b; UINT n;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &m) || !m || !GetModuleFileNameW(m, p, MAX_PATH)) return 0;
  b = wcsrchr(p, L'\\'); if (!b || lstrcmpiW(b + 1, L"d2d1.dll")) return 0;
  n = GetSystemDirectoryW(s, MAX_PATH); return !(n && (UINT)(b - p) == n && !_wcsnicmp(p, s, n)); }
/* Win7 d3d11 (6.2.9200) CDevice::WarpEscape(void *) is slot 3 of one of its private device interfaces. Win10's D2D, on a WARP device (XAML
   rasterises TextBlock alpha masks on one), sends escape 0x23, which Win7's WARP does not have: it called through its escape table out of
   bounds. That slot is patched once it is seen: escape 0x23 is answered S_OK without doing anything, everything else goes to Win7. Identified by the
   exact address (d3d11+0x6EE54) and its prologue. */
typedef HRESULT (WINAPI *PWESC_)(void *, void *);
static PWESC_ g_warp_esc_real;
static HRESULT WINAPI warp_esc_hook(void *self, void *data) {
  if (data && *(const DWORD *)data == 0x23) { static LONG n; if (InterlockedIncrement(&n) <= 4) slog("[dx] WARP escape 0x23 (Win10) answered S_OK, nothing done"); return S_OK; }   /* E_NOTIMPL made D2D fail and XAML fail-fast */
  /* 0x1A and above: Win7's WARP (6.2.9200.22592) has no such escapes and jumps to a garbage address with them (rax = the code at the
     fault). XAML decoding an SVG image through D2D sends 0x1A, then 0x1D (SvgImageDecoder::DecodeFrame, Clock). The bound is empirical:
     6, 7 and 8 are served by Win7; nothing between 9 and 0x19 has been seen. Same answer as 0x23: S_OK, nothing done. */
  if (data && *(const DWORD *)data >= 0x1A) { static volatile LONG seen2[16]; static LONG n2; DWORD code = *(const DWORD *)data; int i, dup = 0;
    for (i = 0; i < 16 && i < n2; i++) if ((DWORD)seen2[i] == code) dup = 1;
    if (!dup && n2 < 16) { seen2[InterlockedIncrement(&n2) - 1] = (LONG)code; slog("[dx] WARP escape 0x%lX (Win10) answered S_OK, nothing done", code); }
    return S_OK; }
  { static volatile LONG seen[16]; static LONG ns; DWORD code = data ? *(const DWORD *)data : 0xFFFFFFFF; int i, dup = 0;   /* each escape code that goes on to Win7's WARP, once */
    for (i = 0; i < 16 && i < ns; i++) if ((DWORD)seen[i] == code) dup = 1;
    if (!dup && ns < 16) { seen[InterlockedIncrement(&ns) - 1] = (LONG)code; slog("[dx] WARP escape 0x%lX passed to Win7 (data %p: %08lX %08lX %08lX %08lX)", code, data, data ? ((const DWORD *)data)[0] : 0, data ? ((const DWORD *)data)[1] : 0, data ? ((const DWORD *)data)[2] : 0, data ? ((const DWORD *)data)[3] : 0); } }
  return g_warp_esc_real(self, data); }
static void dx_guard_warp_escape(void *obj) {
  static const BYTE sig[16] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
  void **vt = *(void ***)obj; HMODULE m = NULL; DWORD old; WCHAR p[MAX_PATH], s[MAX_PATH]; UINT n;
  /* the module is taken from the slot itself (inside the layer, GetModuleHandle names can resolve to an image copy) */
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[3], &m) || !m || !GetModuleFileNameW(m, p, MAX_PATH)
      || !wcsrchr(p, L'\\') || lstrcmpiW(wcsrchr(p, L'\\') + 1, L"d3d11.dll")) return;
  BYTE *fn; if (!m || vt[3] == (void *)warp_esc_hook) return;
  fn = (BYTE *)vt[3];   /* an adjustor thunk (sub/add rcx, imm8|imm32; jmp rel32) in front of CDevice::WarpEscape */
  if (fn[0] == 0x48 && (fn[1] == 0x83 || fn[1] == 0x81) && (fn[2] == 0xE9 || fn[2] == 0xC1)) { int il = fn[1] == 0x83 ? 1 : 4; BYTE *j = fn + 3 + il; if (j[0] == 0xE9) fn = j + 5 + *(INT32 *)(j + 1); }
  if (fn != (BYTE *)m + 0x6EE54) return;
  if (memcmp(fn, sig, sizeof sig)) { slog("[dx] WARP escape guard: prologue differs, not installed"); return; }
  n = GetSystemDirectoryW(s, MAX_PATH); if (!n || _wcsnicmp(p, s, n)) { slog("[dx] WARP escape guard: d3d11 is %ls, not installed", p); return; }   /* Win7's own d3d11 only */
  if (g_warp_esc_real && g_warp_esc_real != (PWESC_)vt[3]) return;
  if (VirtualProtect(&vt[3], sizeof(void *), PAGE_READWRITE, &old)) { g_warp_esc_real = (PWESC_)vt[3]; vt[3] = (void *)warp_esc_hook; VirtualProtect(&vt[3], sizeof(void *), old, &old); slog("[dx] WARP escape guard installed (d3d11 CDevice::WarpEscape, vtable %p)", (void *)vt); } else slog("[dx] WARP escape guard: VirtualProtect failed %lu", GetLastError()); }
static HRESULT WINAPI dev_qi_hook(void *self, REFIID r, void **o) {
  HRESULT hr;
  if (dx_is_dev123(r)) return dx_upgrade_dev(self, o);
  /* {26C5DC23} is Win10 d3d11's private interface for D2D; Win7's d3d11 answers that IID on a WARP device with an object of its own, and
     D2D's call of its slot 3 then jumped to a garbage address (Store: TextBlock alpha masks). Never let Win7 answer it. */
  if (IsEqualGUID(r, &IID_DXGIPriv26_)) return dx_probe26(self, o);
  /* IDXGIDevice2 asked of the D3D11 device (WinUI 3 marshal.dll CTokenManager::ScheduleUpdate -> EnqueueSetEvent): the DXGI tear-off */
  if (IsEqualGUID(r, &IID_DXGIDevice2_) && !dx_caller_is_system(__builtin_return_address(0))) { void *dx = NULL; if (SUCCEEDED(g_qi_dev(self, &IID_DXGIDevice_, &dx)) && dx) { hr = dx_upgrade_dxgi(dx, o); dx_release_real(dx); return hr; } }
  hr = g_qi_dev(self, r, o);
  if (SUCCEEDED(hr) && o && *o) dx_guard_warp_escape(*o);
  /* IDXGIDevice3 asked of the D3D11 device (WinUI 3 dcompi CDxDevice::Initialize): the DXGI device's tear-off */
  if (hr == E_NOINTERFACE && IsEqualGUID(r, &IID_DXGIDevice3_)) { void *dx = NULL; if (SUCCEEDED(g_qi_dev(self, &IID_DXGIDevice_, &dx)) && dx) { hr = dx_upgrade_dxgi(dx, o); dx_release_real(dx); return hr; } }
  if (FAILED(hr)) dx_qimiss("D3D11 device", r, hr, __builtin_return_address(0));
  return hr; }
static HRESULT WINAPI ctx_qi_hook(void *self, REFIID r, void **o) {
  if (dx_is_ctx123(r)) return dx_upgrade_ctx(self, o);
  return g_qi_ctx(self, r, o); }
static int dx_patch(void *obj, PQI_ *orig, PQI_ hook) {
  void **vt = *(void ***)obj; DWORD old;
  /* one original per hook, but several vtables can share it: a single-threaded device (D2D makes its own with flag 0x1) has its own
     context vtable. Patching only the first left D2D's context without ID3D11DeviceContext3 -> D2DERR_NO_HARDWARE_DEVICE, and XAML
     fail-fast when the Store rasterised a TextBlock alpha mask. */
  if (vt[0] == (void *)hook) return 1;
  if (*orig && vt[0] != (void *)*orig) { static LONG n; if (InterlockedIncrement(&n) <= 4) slog("[dx] vtable %p: a different QueryInterface %p, not hooked", (void *)vt, vt[0]); return 0; }
  if (!VirtualProtect(&vt[0], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return 0;
  *orig = (PQI_)vt[0]; vt[0] = (void *)hook; VirtualProtect(&vt[0], sizeof(void *), old, &old); return 1; }
static void dx_patch_slot(void *obj, int k, void **orig, void *hook) {
  void **vt = *(void ***)obj; DWORD old; if (*orig || vt[k] == hook) return;
  if (VirtualProtect(&vt[k], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) { *orig = vt[k]; vt[k] = hook; VirtualProtect(&vt[k], sizeof(void *), old, &old); } }
/* Win7's d3d11 objects have per-object vtables (heap copies): patch this object's slot too when it still holds the same original */
static void dx_patch_slot_each(void *obj, int k, void **orig, void *hook) {
  void **vt = *(void ***)obj; DWORD old; if (vt[k] == hook) return; if (*orig && vt[k] != *orig) return;
  if (VirtualProtect(&vt[k], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) { if (!*orig) *orig = vt[k]; vt[k] = hook; VirtualProtect(&vt[k], sizeof(void *), old, &old); } }
/* ---- NT-handle sharing (Win8+) over legacy shared handles, for sharing inside this process (WinUI 3 marshal.dll <-> dwmcorei) ----
   IDXGIResource1::CreateSharedHandle returns a real kernel handle: a small page-file section holding a magic and the texture's legacy
   shared handle (IDXGIResource::GetSharedHandle). Duplicates and CloseHandle work as with an NT handle; ID3D11Device1::
   OpenSharedResource1 maps it and opens the legacy handle (OpenSharedResource). Not usable across processes (the legacy handle is). */
#define DX_NTSH_MAGIC 0x48535455u   /* "UTSH" */
typedef struct { UINT magic; UINT pad; ULONGLONG legacy; } DxNtShare_;
static const GUID IID_DXGIResource_ = {0x035f3ab4, 0x482e, 0x4e50, {0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}};
static const GUID IID_DXGIResource1_ = {0x30961379, 0x4609, 0x4a41, {0x99, 0x8e, 0x54, 0xfe, 0x56, 0x7e, 0xe0, 0xc1}};
static const GUID IID_DXGISurface2_ = {0xaba496dd, 0xb617, 0x4cb8, {0xa8, 0x66, 0xbc, 0x44, 0xd7, 0xeb, 0x1f, 0xa2}};
static const GUID IID_DXGISurface1_ = {0x4ae63092, 0x6327, 0x4c1b, {0x80, 0xae, 0xbf, 0xe1, 0x2e, 0xa3, 0x2b, 0x86}};
static const void *g_dxrt_vt[14];
static HRESULT WINAPI dxrt_qi(DxT *t, REFIID r, void **o) { if (IsEqualGUID(r, &IID_DXGIResource1_)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; } return VCALL(t->real, 0, PQI_)(t->real, r, o); }
/* CreateSubresourceSurface (Win8): subresource 0 is the texture's own surface. dwmcorei CD2DBitmap::EnsureBitmap needs it for every dcomp
   surface it draws into; the E_NOTIMPL stub failed each frame's BeginFrame (localapprendertarget.cpp(190)) and the XAML content stayed black. */
static HRESULT WINAPI dxrt_subsurf(DxT *t, UINT i, void **o) {
  HRESULT hr; static LONG nl; if (!o) return E_INVALIDARG; *o = NULL;
  if (i != 0) { if (InterlockedIncrement(&nl) <= 8) slog("[dx] IDXGIResource1::CreateSubresourceSurface(%u) refused (only subresource 0)", i); return E_NOTIMPL; }
  hr = VCALL(t->real, 0, PQI_)(t->real, &IID_DXGISurface2_, o);
  if (FAILED(hr)) hr = VCALL(t->real, 0, PQI_)(t->real, &IID_DXGISurface1_, o);
  if (InterlockedIncrement(&nl) <= 4) slog("[dx] IDXGIResource1::CreateSubresourceSurface(0) -> the texture's surface 0x%08lX", (unsigned long)hr);
  return hr; }
static HRESULT WINAPI dxrt_create_shared(DxT *t, const SECURITY_ATTRIBUTES *sa, DWORD access, const WCHAR *name, HANDLE *out) {
  HANDLE legacy = NULL, sec; DxNtShare_ *v; HRESULT hr; (void)access;
  if (!out) return E_INVALIDARG; *out = NULL;
  hr = VCALL(t->real, 8, HRESULT (WINAPI *)(void *, HANDLE *))(t->real, &legacy);   /* IDXGIResource::GetSharedHandle */
  if (FAILED(hr) || !legacy) { slog("[dx] CreateSharedHandle: texture has no legacy shared handle (0x%08lX)", (unsigned long)hr); return FAILED(hr) ? hr : E_INVALIDARG; }
  sec = CreateFileMappingW(INVALID_HANDLE_VALUE, (SECURITY_ATTRIBUTES *)sa, PAGE_READWRITE, 0, sizeof *v, name); if (!sec) return HRESULT_FROM_WIN32(GetLastError());
  v = (DxNtShare_ *)MapViewOfFile(sec, FILE_MAP_WRITE, 0, 0, sizeof *v); if (!v) { CloseHandle(sec); return E_OUTOFMEMORY; }
  v->magic = DX_NTSH_MAGIC; v->legacy = (ULONGLONG)(ULONG_PTR)legacy; UnmapViewOfFile(v);
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 5) slog("[dx] CreateSharedHandle -> %p (legacy %p)", (void *)sec, (void *)legacy); }
  *out = sec; return S_OK; }
static int dx_ntshare_legacy(HANDLE h, HANDLE *legacy) {
  DxNtShare_ *v = h ? (DxNtShare_ *)MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof *v) : NULL; int ok = 0;
  if (v) { if (v->magic == DX_NTSH_MAGIC) { *legacy = (HANDLE)(ULONG_PTR)v->legacy; ok = 1; } UnmapViewOfFile(v); } return ok; }
static void dxr_tables(void) {
  static LONG done; int k; if (InterlockedCompareExchange(&done, 1, 0)) return;
  for (k = 3; k < 12; k++) g_dxrt_vt[k] = dxfwd_tab[k];
  g_dxrt_vt[0] = (void *)dxrt_qi; g_dxrt_vt[1] = (void *)dxt_ar; g_dxrt_vt[2] = (void *)dxt_rl; g_dxrt_vt[12] = (void *)dxrt_subsurf; g_dxrt_vt[13] = (void *)dxrt_create_shared; }
/* textures' QueryInterface (shared vtable of Win7 d3d11's texture class): IDXGIResource1 */
static HRESULT WINAPI tex_qi_hook(void *self, REFIID r, void **o) {
  HRESULT hr = g_qi_tex(self, r, o); void *res = NULL;
  /* Win7's own IDXGIResource1 (Platform Update) answers CreateSharedHandle with E_NOTIMPL: always hand out ours */
  if (!IsEqualGUID(r, &IID_DXGIResource1_)) return hr;
  if (SUCCEEDED(hr) && o && *o) { dx_release_real(*o); *o = NULL; }
  if (SUCCEEDED(g_qi_tex(self, &IID_DXGIResource_, &res)) && res) { dxr_tables(); return dx_new(g_dxrt_vt, res, o, "IDXGIResource1 tear-off"); }
  return E_NOINTERFACE; }
/* ID3D11Device1::OpenSharedResource1 (slot 48) / the device's OpenSharedResource (slot 28) with one of our handles */
typedef HRESULT (WINAPI *POPENSH_)(void *, HANDLE, REFIID, void **);
static HRESULT dx_open_shared1(void *dev, HANDLE h, REFIID r, void **o) {
  HANDLE legacy; if (!o) return E_POINTER; *o = NULL;
  if (!dx_ntshare_legacy(h, &legacy)) { slog("[dx] OpenSharedResource1(%p): not a handle of ours", (void *)h); return E_INVALIDARG; }
  return VCALL(dev, 28, POPENSH_)(dev, legacy, r, o); }
static HRESULT WINAPI devt_open_shared1(DxT *t, HANDLE h, REFIID r, void **o) { return dx_open_shared1(t->real, h, r, o); }
/* the real device's CreateTexture2D (slot 5): callers holding the plain ID3D11Device bypass the tear-off */
typedef HRESULT (WINAPI *PTEX2D_)(void *, const void *, const void *, void **); static PTEX2D_ g_real_tex2d;
static HRESULT WINAPI real_tex2d_hook(void *self, const TexDesc_ *d, const void *init, void **o) {
  TexDesc_ tmp; HRESULT hr = g_real_tex2d(self, dx_tex_fix(d, &tmp), init, o);
  if (FAILED(hr) && d) DXFAIL("real CreateTexture2D %ux%u fmt %u usage %u bind 0x%x cpu 0x%x misc 0x%x -> 0x%08lX", d->w, d->h, d->fmt, d->usage, d->bind, d->cpu, d->misc, (unsigned long)hr);
  if (SUCCEEDED(hr) && o && *o && d && (d->misc & 0x800)) { int ok = dx_patch(*o, &g_qi_tex, tex_qi_hook); slog("[dx] NT-shared texture %p (vtbl %p): QI hook %s", *o, **(void ***)o, ok ? "on" : "FAILED"); }   /* textures shared by NT handle get IDXGIResource1 */
  return hr; }
/* ---- IDXGIAdapter2/3/4 over Win7's IDXGIAdapter1 ----
   WinUI 3's dwmcorei CD3DDevice::Init QIs its adapter for IDXGIAdapter4 and calls GetDesc3 (slot 18); Win7 (DXGI 1.2) answers at most
   IDXGIAdapter2. Slots 3..10 = IDXGIObject/Adapter/Adapter1 of the real adapter; 11 GetDesc2, 18 GetDesc3 = DESC1 + two zero preemption
   granularities (same layout up to Flags); 12/13 and 16/17 event registrations are inert; 14 QueryVideoMemoryInfo reports the adapter's
   dedicated (local) or shared (non-local) memory as budget, nothing in use; 15 SetVideoMemoryReservation accepted. */
static const GUID IID_DXGIAdapter1_ = {0x29038f61, 0x3839, 0x4626, {0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05}};
static const GUID IID_DXGIAdapter3_ = {0x645967a4, 0x1392, 0x4310, {0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd}};
static const GUID IID_DXGIAdapter4_ = {0x3c8d99d1, 0x4fbf, 0x4181, {0xa8, 0x2c, 0xaf, 0x66, 0xbf, 0x7b, 0xd2, 0x4e}};
static const GUID IID_DXGIAdapter2_ = {0x0aa1ae0a, 0xfa0e, 0x4b84, {0x86, 0x44, 0xe0, 0x5f, 0xf8, 0xe5, 0xac, 0xb5}};
static const void *g_adpt_vt[19]; static PQI_ g_qi_adp;
static HRESULT WINAPI adpt_qi(DxT *t, REFIID r, void **o) {
  if (IsEqualGUID(r, &IID_DXGIAdapter2_) || IsEqualGUID(r, &IID_DXGIAdapter3_) || IsEqualGUID(r, &IID_DXGIAdapter4_)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; }
  return VCALL(t->real, 0, PQI_)(t->real, r, o); }
static HRESULT WINAPI adpt_desc23(DxT *t, BYTE *d) {   /* GetDesc2 / GetDesc3: DXGI_ADAPTER_DESC1 (312 bytes, Flags at 304) + 2 UINTs at 308/312 */
  BYTE d1[320]; HRESULT hr; if (!d) return E_INVALIDARG; memset(d1, 0, sizeof d1);
  hr = VCALL(t->real, 10, HRESULT (WINAPI *)(void *, void *))(t->real, d1); if (FAILED(hr)) return hr;
  memcpy(d, d1, 308); *(UINT *)(d + 308) = 0; *(UINT *)(d + 312) = 0; return S_OK; }
static HRESULT WINAPI adpt_reg(DxT *t, HANDLE ev, DWORD *cookie) { static LONG c; (void)t; (void)ev; if (!cookie) return E_INVALIDARG; *cookie = (DWORD)InterlockedIncrement(&c); return S_OK; }
static void WINAPI adpt_unreg(DxT *t, DWORD cookie) { (void)t; (void)cookie; }
static HRESULT WINAPI adpt_memInfo(DxT *t, UINT node, UINT group, UINT64 *info) {
  BYTE d1[320]; (void)node; if (!info) return E_INVALIDARG; memset(info, 0, 4 * sizeof(UINT64)); memset(d1, 0, sizeof d1);
  if (SUCCEEDED(VCALL(t->real, 10, HRESULT (WINAPI *)(void *, void *))(t->real, d1))) {
    SIZE_T ded = *(SIZE_T *)(d1 + 272), shr = *(SIZE_T *)(d1 + 288);
    info[0] = group == 0 ? (UINT64)ded : (UINT64)shr; info[2] = info[0] / 2; }   /* Budget, CurrentUsage 0, AvailableForReservation, CurrentReservation 0 */
  return S_OK; }
static HRESULT WINAPI adpt_reserve(DxT *t, UINT node, UINT group, UINT64 bytes) { (void)t; (void)node; (void)group; (void)bytes; return S_OK; }
static void adpt_tables(void) {
  static LONG done; int k; if (InterlockedCompareExchange(&done, 1, 0)) return;
  for (k = 3; k < 11; k++) g_adpt_vt[k] = dxfwd_tab[k];
  g_adpt_vt[0] = (void *)adpt_qi; g_adpt_vt[1] = (void *)dxt_ar; g_adpt_vt[2] = (void *)dxt_rl;
  g_adpt_vt[11] = (void *)adpt_desc23; g_adpt_vt[12] = (void *)adpt_reg; g_adpt_vt[13] = (void *)adpt_unreg; g_adpt_vt[14] = (void *)adpt_memInfo;
  g_adpt_vt[15] = (void *)adpt_reserve; g_adpt_vt[16] = (void *)adpt_reg; g_adpt_vt[17] = (void *)adpt_unreg; g_adpt_vt[18] = (void *)adpt_desc23; }
static HRESULT WINAPI adp_qi_hook(void *self, REFIID r, void **o) {
  HRESULT hr = g_qi_adp(self, r, o); void *a1 = NULL;
  if (hr != E_NOINTERFACE || !(IsEqualGUID(r, &IID_DXGIAdapter3_) || IsEqualGUID(r, &IID_DXGIAdapter4_))) return hr;
  if (FAILED(g_qi_adp(self, &IID_DXGIAdapter1_, &a1)) || !a1) return hr;
  adpt_tables(); return dx_new(g_adpt_vt, a1, o, "IDXGIAdapter4 tear-off over IDXGIAdapter1"); }
/* the QueryInterface of Win7's adapter vtable (shared by all adapters) is hooked the first time an adapter passes through the layer */
static void dx_hook_adapter(void *a) { if (a) dx_patch(a, &g_qi_adp, adp_qi_hook); }
/* IDXGIResource1::CreateSubresourceSurface (slot 12, Win8): Win7's DXGI 1.2 returns E_NOTIMPL. WinUI 3's dwmcorei CD2DBitmap::EnsureBitmap
   gets the IDXGISurface of a dcomp surface's texture this way for D2D (CreateBitmapFromDxgiSurface); the failure made every frame's
   BeginFrame fail (localapprendertarget.cpp(190)) and the XAML content stayed black. Subresource 0 of a texture is the texture's own surface:
   answered with its IDXGISurface2 (or 1). Patched in Win7's resource vtable (shared by all textures), Win7 first. */
typedef HRESULT (WINAPI *PSUBSURF_)(void *, UINT, void **);
static PSUBSURF_ g_real_subsurf;
static HRESULT WINAPI subsurf_hook(void *self, UINT index, void **out) {
  HRESULT hr = g_real_subsurf(self, index, out), h2; static LONG nl;
  if (SUCCEEDED(hr) || !out || index != 0) { if (FAILED(hr) && InterlockedIncrement(&nl) <= 8) slog("[dx] CreateSubresourceSurface(%u) -> 0x%08lX (not emulated)", index, (unsigned long)hr); return hr; }
  h2 = VCALL(self, 0, PQI_)(self, &IID_DXGISurface2_, out);
  if (FAILED(h2)) h2 = VCALL(self, 0, PQI_)(self, &IID_DXGISurface1_, out);
  if (InterlockedIncrement(&nl) <= 4) slog("[dx] CreateSubresourceSurface(0): Win7 0x%08lX -> the texture's own surface 0x%08lX", (unsigned long)hr, (unsigned long)h2);
  return h2; }
static void dx_hook_subsurf(void *dev) {   /* dev: Win7's real ID3D11Device */
  TexDesc_ d = {1, 1, 1, 1, 87 /* B8G8R8A8 */, 1, 0, 0 /* DEFAULT */, 0x8 /* SHADER_RESOURCE */, 0, 0}; void *tex = NULL, *res = NULL;
  if (g_real_subsurf) return;
  if (FAILED(VCALL(dev, 5, HRESULT (WINAPI *)(void *, const void *, const void *, void **))(dev, &d, NULL, &tex)) || !tex) return;
  if (SUCCEEDED(VCALL(tex, 0, PQI_)(tex, &IID_DXGIResource1_, &res)) && res) { dx_patch_slot(res, 12, (void **)&g_real_subsurf, (void *)subsurf_hook);
    slog("[dx] IDXGIResource1::CreateSubresourceSurface hook %s (vtable %p)", g_real_subsurf ? "installed" : "NOT installed", *(void **)res); dx_release_real(res); }
  else slog("[dx] CreateSubresourceSurface hook: no IDXGIResource1 on a test texture");
  dx_release_real(tex); }
static void dx_hook_device(void *dev) {
  void *dxgi = NULL, *ctx = NULL; PQI_ qi;
  if (!dev) return; dx_tables(); qi = VCALL(dev, 0, PQI_);
  if (SUCCEEDED(qi(dev, &IID_DXGIDevice_, &dxgi)) && dxgi) { void *ad = NULL; dx_patch(dxgi, &g_qi_dxgi, dxgi_qi_hook);
    if (SUCCEEDED(VCALL(dxgi, 7, HRESULT (WINAPI *)(void *, void **))(dxgi, &ad)) && ad) { dx_hook_adapter(ad); dx_release_real(ad); }   /* IDXGIDevice::GetAdapter */
    dx_release_real(dxgi); }
  VCALL(dev, 40, void (WINAPI *)(void *, void **))(dev, &ctx); if (ctx) { slog("[dx] device %p immediate context %p (vtbl %p)", dev, ctx, *(void **)ctx); dx_patch(ctx, &g_qi_ctx, ctx_qi_hook);
    dx_release_real(ctx); }
  dx_patch_slot(dev, 5, (void **)&g_real_tex2d, (void *)real_tex2d_hook);
  dx_hook_subsurf(dev);
  if (dx_patch(dev, &g_qi_dev, dev_qi_hook)) { static LONG once; if (!InterlockedExchange(&once, 1)) slog("[dx] hooked QueryInterface of the DXGI device, D3D11 device and immediate context (11.2/11.3 tear-offs)"); } }

/* ---- IDXGIFactory3..7 over Win7's IDXGIFactory2 (Platform Update = DXGI 1.2) ----
   WinUI 3's lifted compositor (dwmcorei CDisplayManager::InternalUpdateDXGIFactory) asks CreateDXGIFactory1 for IDXGIFactory7.
   Slots 3..24 = IDXGIObject/Factory/Factory1/Factory2 of the real factory; 25 GetCreationFlags, 26 EnumAdapterByLuid, 27 EnumWarpAdapter,
   28 CheckFeatureSupport, 29 EnumAdapterByGpuPreference, 30/31 Register/UnregisterAdaptersChangedEvent. */
static const GUID IID_DXGIFactory2_ = {0x50c83a1c, 0xe072, 0x4c48, {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}};
static const GUID g_dxf_iids[5] = {
  {0x25483823, 0xcd46, 0x4c7d, {0x86, 0xca, 0x47, 0xaa, 0x95, 0xb8, 0x37, 0xbd}},   /* IDXGIFactory3 */
  {0x1bc6ea02, 0xef36, 0x464f, {0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a}},   /* IDXGIFactory4 */
  {0x7632e1f5, 0xee65, 0x4dca, {0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d}},   /* IDXGIFactory5 */
  {0xc1b6694f, 0xff09, 0x44a9, {0xb0, 0x3c, 0x77, 0x90, 0x0a, 0x0a, 0x1d, 0x17}},   /* IDXGIFactory6 */
  {0xa4966eed, 0x76db, 0x44da, {0x84, 0xc1, 0xee, 0x9a, 0x7a, 0xfb, 0x20, 0xa8}} }; /* IDXGIFactory7 */
static int dxf_is_new(REFIID r) { int i; for (i = 0; i < 5; i++) if (IsEqualGUID(r, &g_dxf_iids[i])) return 1; return 0; }
static const void *g_dxft_vt[32];
static HRESULT WINAPI dxft_qi(DxT *t, REFIID r, void **o) { if (dxf_is_new(r) || IsEqualGUID(r, &IID_DXGIFactory2_)) { InterlockedIncrement(&t->ref); *o = t; return S_OK; } return VCALL(t->real, 0, PQI_)(t->real, r, o); }
static UINT WINAPI dxft_flags(DxT *t) { (void)t; return 0; }
/* adapter i (EnumAdapters1, slot 12) as riid */
static HRESULT dxft_adapter(DxT *t, UINT i, REFIID riid, void **o) {
  void *a = NULL; HRESULT hr = VCALL(t->real, 12, HRESULT (WINAPI *)(void *, UINT, void **))(t->real, i, &a); if (FAILED(hr)) return hr;
  hr = VCALL(a, 0, PQI_)(a, riid, o); dx_release_real(a);
  if (FAILED(hr)) { char g[48]; guid_str(riid, g); slog("[dx] adapter %u QI %s -> 0x%08lX", i, g, (unsigned long)hr); } return hr; }
static HRESULT WINAPI dxft_by_luid(DxT *t, LUID luid, REFIID riid, void **o) {
  UINT i; if (!o) return E_POINTER; *o = NULL;
  for (i = 0; i < 16; i++) { void *a = NULL; BYTE d[320]; if (FAILED(VCALL(t->real, 12, HRESULT (WINAPI *)(void *, UINT, void **))(t->real, i, &a))) break;
    /* IDXGIAdapter1::GetDesc1 (slot 10): DXGI_ADAPTER_DESC1, AdapterLuid at 296 */
    if (SUCCEEDED(VCALL(a, 10, HRESULT (WINAPI *)(void *, void *))(a, d)) && !memcmp(d + 296, &luid, sizeof luid)) { dx_release_real(a); return dxft_adapter(t, i, riid, o); }
    dx_release_real(a); }
  slog("[dx] EnumAdapterByLuid(%08lx:%08lx): no such adapter", (unsigned long)luid.HighPart, (unsigned long)luid.LowPart); return 0x887A0002; /* DXGI_ERROR_NOT_FOUND */ }
static HRESULT WINAPI dxft_warp(DxT *t, REFIID riid, void **o) { (void)t; (void)riid; if (o) *o = NULL; dx_log("EnumWarpAdapter: none"); return 0x887A0002; }
static HRESULT WINAPI dxft_feature(DxT *t, UINT f, void *data, UINT size) { (void)t; (void)f; if (data && size) memset(data, 0, size); return S_OK; }   /* ALLOW_TEARING: FALSE */
static HRESULT WINAPI dxft_by_pref(DxT *t, UINT i, UINT pref, REFIID riid, void **o) { (void)pref; if (!o) return E_POINTER; *o = NULL; return dxft_adapter(t, i, riid, o); }
static HRESULT WINAPI dxft_reg_changed(DxT *t, HANDLE ev, DWORD *cookie) { static LONG c; (void)t; (void)ev; if (!cookie) return E_INVALIDARG; *cookie = (DWORD)InterlockedIncrement(&c); return S_OK; }
static HRESULT WINAPI dxft_unreg_changed(DxT *t, DWORD cookie) { (void)t; (void)cookie; return S_OK; }
/* swap chains: log failures (CreateSwapChainForHwnd 15, ForComposition 24) */
static HRESULT WINAPI dxft_sc_hwnd(DxT *t, void *dev, HWND h, const UINT *desc, const void *fs, void *out, void **sc) {
  HRESULT hr = VCALL(t->real, 15, HRESULT (WINAPI *)(void *, void *, HWND, const UINT *, const void *, void *, void **))(t->real, dev, h, desc, fs, out, sc);
  slog("[dx] CreateSwapChainForHwnd(%p, %ux%u fmt %u buffers %u effect %u flags 0x%x) -> 0x%08lX", (void *)h, desc ? desc[0] : 0, desc ? desc[1] : 0, desc ? desc[2] : 0, desc ? desc[7] : 0, desc ? desc[9] : 0, desc ? desc[11] : 0, (unsigned long)hr); return hr; }
static HRESULT WINAPI dxft_sc_comp(DxT *t, void *dev, const UINT *desc, void *out, void **sc) {
  HRESULT hr = VCALL(t->real, 24, HRESULT (WINAPI *)(void *, void *, const UINT *, void *, void **))(t->real, dev, desc, out, sc);
  slog("[dx] CreateSwapChainForComposition(%ux%u fmt %u buffers %u effect %u alpha %u flags 0x%x) -> 0x%08lX (caller %p)", desc ? desc[0] : 0, desc ? desc[1] : 0, desc ? desc[2] : 0, desc ? desc[7] : 0, desc ? desc[9] : 0, desc ? desc[10] : 0, desc ? desc[11] : 0, (unsigned long)hr, __builtin_return_address(0)); return hr; }
/* EnumAdapters (7) / EnumAdapters1 (12): the adapter's QueryInterface is hooked (IDXGIAdapter3/4 tear-off) */
static HRESULT WINAPI dxft_enum0(DxT *t, UINT i, void **o) { HRESULT hr = VCALL(t->real, 7, HRESULT (WINAPI *)(void *, UINT, void **))(t->real, i, o); if (SUCCEEDED(hr) && o) dx_hook_adapter(*o); return hr; }
static HRESULT WINAPI dxft_enum1(DxT *t, UINT i, void **o) { HRESULT hr = VCALL(t->real, 12, HRESULT (WINAPI *)(void *, UINT, void **))(t->real, i, o); if (SUCCEEDED(hr) && o) dx_hook_adapter(*o); return hr; }
static void dxf_tables(void) {
  static LONG done; int k; if (InterlockedCompareExchange(&done, 1, 0)) return;
  for (k = 3; k < 25; k++) g_dxft_vt[k] = dxfwd_tab[k];
  g_dxft_vt[7] = (void *)dxft_enum0; g_dxft_vt[12] = (void *)dxft_enum1;
  g_dxft_vt[15] = (void *)dxft_sc_hwnd; g_dxft_vt[24] = (void *)dxft_sc_comp;
  g_dxft_vt[0] = (void *)dxft_qi; g_dxft_vt[1] = (void *)dxt_ar; g_dxft_vt[2] = (void *)dxt_rl; g_dxft_vt[25] = (void *)dxft_flags;
  g_dxft_vt[26] = (void *)dxft_by_luid; g_dxft_vt[27] = (void *)dxft_warp; g_dxft_vt[28] = (void *)dxft_feature; g_dxft_vt[29] = (void *)dxft_by_pref;
  g_dxft_vt[30] = (void *)dxft_reg_changed; g_dxft_vt[31] = (void *)dxft_unreg_changed; }
/* Win7's own factory (handed out unwrapped for IDXGIFactory/IDXGIFactory1 requests): its swap-chain creators are patched in place
   (vtable shared by every factory of the process). Win8/10 swap chains Win7's DXGI refuses are translated to the blt model:
   FLIP_SEQUENTIAL(3) -> SEQUENTIAL(1), FLIP_DISCARD(4) -> DISCARD(0), scaling NONE/ASPECT -> STRETCH, flags ALLOW_TEARING (0x800) and
   FRAME_LATENCY_WAITABLE_OBJECT (0x40) dropped. bgfx (Minecraft Bedrock) asks for FLIP_DISCARD and, refused, gives D3D11 up and falls
   back to its OpenGL renderer. */
typedef HRESULT (WINAPI *PSC0_)(void *, void *, UINT *, void **);
typedef HRESULT (WINAPI *PSC1_)(void *, void *, HWND, const UINT *, const void *, void *, void **);
static PSC0_ g_sc0_real; static PSC1_ g_sc1_real;

static UINT dx_blt_effect(UINT e) { return e == 3 ? 1 : e == 4 ? 0 : e; }
static HRESULT WINAPI sc0_hook(void *f, void *dev, UINT *d, void **sc) {
  /* DXGI_SWAP_CHAIN_DESC: BufferCount @40, SwapEffect @60, Flags @64 (bytes) */
  BYTE tmp[72]; const BYTE *in = (const BYTE *)d; HRESULT hr; UINT e0 = 0, f0 = 0;
  if (d) { memcpy(tmp, d, sizeof tmp); e0 = *(UINT *)(tmp + 60); f0 = *(UINT *)(tmp + 64); *(UINT *)(tmp + 60) = dx_blt_effect(e0); *(UINT *)(tmp + 64) &= ~0x840u; in = tmp; }
  hr = g_sc0_real(f, dev, (UINT *)in, sc);
  slog("[dx] CreateSwapChain(%ux%u fmt %u buffers %u effect %u->%u flags 0x%x->0x%x) -> 0x%08lX", d ? d[0] : 0, d ? d[1] : 0, d ? d[4] : 0, d ? *(UINT *)((BYTE *)d + 40) : 0,
       e0, in ? *(UINT *)(in + 60) : 0, f0, in ? *(UINT *)(in + 64) : 0, (unsigned long)hr); return hr; }
static HRESULT WINAPI sc1_hook(void *f, void *dev, HWND h, const UINT *d, const void *fs, void *out, void **sc) {
  /* DXGI_SWAP_CHAIN_DESC1 as UINT[12]: [7] BufferCount, [8] Scaling, [9] SwapEffect, [10] AlphaMode, [11] Flags */
  UINT tmp[12]; const UINT *in = d; HRESULT hr;
  if (d) { memcpy(tmp, d, sizeof tmp); tmp[9] = dx_blt_effect(d[9]); if (tmp[8] != 0 && d[9] >= 3) tmp[8] = 0; tmp[11] &= ~0x840u; in = tmp; }
  hr = g_sc1_real(f, dev, h, in, fs, out, sc);
  slog("[dx] CreateSwapChainForHwnd(%p, %ux%u fmt %u buffers %u scaling %u effect %u->%u flags 0x%x->0x%x) -> 0x%08lX", (void *)h, d ? d[0] : 0, d ? d[1] : 0, d ? d[2] : 0, d ? d[7] : 0,
       d ? d[8] : 0, d ? d[9] : 0, in ? in[9] : 0, d ? d[11] : 0, in ? in[11] : 0, (unsigned long)hr); return hr; }
static void dx_patch_sc(void *anyfactory) {
  void *f2 = NULL, **vt; DWORD old; static LONG done;
  if (!anyfactory || done) return;
  if (FAILED(VCALL(anyfactory, 0, PQI_)(anyfactory, &IID_DXGIFactory2_, &f2)) || !f2) return;
  vt = *(void ***)f2;
  if (vt[10] != (void *)sc0_hook && VirtualProtect(&vt[10], 6 * sizeof(void *), PAGE_READWRITE, &old)) {
    g_sc0_real = (PSC0_)vt[10]; g_sc1_real = (PSC1_)vt[15]; vt[10] = (void *)sc0_hook; vt[15] = (void *)sc1_hook;
    VirtualProtect(&vt[10], 6 * sizeof(void *), old, &old); InterlockedExchange(&done, 1); slog("[dx] Win7 factory swap-chain creators patched (vtable %p)", (void *)vt); }
  dx_release_real(f2); }
static HRESULT dx_factory(UINT flags, REFIID riid, void **o, int ver) {
  typedef HRESULT (WINAPI *F1)(REFIID, void **); static F1 f1; HRESULT hr; void *real = NULL; (void)flags;
  if (!o) return E_POINTER; *o = NULL;
  if (!f1) f1 = (F1)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory1"); if (!f1) return E_NOTIMPL;
  /* IDXGIFactory2 itself is wrapped too (swap-chain logging); older interfaces go straight to Win7 */
  if (!dxf_is_new(riid) && !IsEqualGUID(riid, &IID_DXGIFactory2_)) { hr = f1(riid, o); if (SUCCEEDED(hr) && *o) dx_patch_sc(*o); return hr; }
  hr = f1(&IID_DXGIFactory2_, &real); if (FAILED(hr)) { slog("[dx] CreateDXGIFactory%d: no IDXGIFactory2 underneath (0x%08lX)", ver, (unsigned long)hr); return hr; }
  dx_patch_sc(real); dxf_tables(); return dx_new(g_dxft_vt, real, o, "IDXGIFactory3..7 tear-off over IDXGIFactory2"); }
__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **o) { return dx_factory(0, riid, o, 1); }
__declspec(dllexport) HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID riid, void **o) { return dx_factory(flags, riid, o, 2); }

/* ---- D3DKMT (Win8 additions over Win7's gdi32 thunks) ----
   WinUI 3's lifted compositor (dwmcorei CD3DDevice::GetAdapterInfo -> CKMAdapterHandle::Initialize) opens its adapter with
   D3DKMTOpenAdapterFromLuid (Win8+). The generic "returns 0" thunk gave it success without a handle, the following D3DKMTQueryAdapterInfo
   failed with STATUS_INVALID_PARAMETER, CreateD3D11Device failed, every composition-buffer realization failed (localcompositionsurfaceinfo.cpp
   (61) asserts) and nothing was drawn. Win7 opens adapters by GDI display name: each attached display is opened until the LUID matches. */
typedef struct { WCHAR name[32]; UINT h; LUID luid; UINT src; } KmtOpenGdi_;
typedef struct { LUID luid; UINT h; } KmtOpenLuid_;
typedef struct { UINT h; UINT type; void *data; UINT size; } KmtQai_;
static FARPROC dx_gdi(const char *n) { HMODULE g = GetModuleHandleA("gdi32.dll"); if (!g) g = LoadLibraryA("gdi32.dll"); return g ? GetProcAddress(g, n) : NULL; }
__declspec(dllexport) LONG WINAPI D3DKMTOpenAdapterFromLuid(KmtOpenLuid_ *a) {
  typedef LONG (WINAPI *POG)(KmtOpenGdi_ *); typedef LONG (WINAPI *PCL)(const UINT *); static POG og; static PCL cl; DISPLAY_DEVICEW dd; DWORD i; static LONG nl;
  if (!a) return (LONG)0xC000000D;
  if (!og) og = (POG)dx_gdi("D3DKMTOpenAdapterFromGdiDisplayName"); if (!cl) cl = (PCL)dx_gdi("D3DKMTCloseAdapter"); if (!og || !cl) return (LONG)0xC0000002;
  for (i = 0; ; i++) { KmtOpenGdi_ o; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd; if (!EnumDisplayDevicesW(NULL, i, &dd, 0)) break;
    if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
    memset(&o, 0, sizeof o); lstrcpynW(o.name, dd.DeviceName, 32); if (og(&o) < 0) continue;
    if (o.luid.LowPart == a->luid.LowPart && o.luid.HighPart == a->luid.HighPart) { a->h = o.h;
      if (InterlockedIncrement(&nl) <= 4) slog("[dx] D3DKMTOpenAdapterFromLuid(%08lx:%08lx) -> %ls handle 0x%x", (unsigned long)a->luid.HighPart, (unsigned long)a->luid.LowPart, dd.DeviceName, o.h);
      return 0; }
    cl(&o.h); }
  slog("[dx] D3DKMTOpenAdapterFromLuid(%08lx:%08lx): no display adapter has that LUID", (unsigned long)a->luid.HighPart, (unsigned long)a->luid.LowPart);
  return (LONG)0xC000000D; }
/* Win8 query types Win7 rejects: 13 DRIVERVERSION (answered WDDM 1.1 = 1105, so callers skip the WDDM 2.0 queries), 15 ADAPTERTYPE
   (render + display supported) */
__declspec(dllexport) LONG WINAPI D3DKMTQueryAdapterInfo(KmtQai_ *q) {
  typedef LONG (WINAPI *PQ)(KmtQai_ *); static PQ f; LONG st;
  if (!f) f = (PQ)dx_gdi("D3DKMTQueryAdapterInfo"); if (!f) return (LONG)0xC0000002;
  st = f(q);
  if (st < 0 && q && q->data && q->size >= 4 && (q->type == 13 || q->type == 15)) { static LONG nl;
    *(UINT *)q->data = q->type == 13 ? 1105u : 0x3u;
    if (InterlockedIncrement(&nl) <= 4) slog("[dx] D3DKMTQueryAdapterInfo type %u: Win7 said 0x%08lX, answered %u", q->type, (unsigned long)st, *(UINT *)q->data);
    st = 0; }
  return st; }

/* ---- DWrite: Windows 11 font families -> what Win7 / the Win10 image has ----
   WinUI 3 asks the system font collection for "Segoe UI Variable" (text) and "Segoe Fluent Icons" (symbols). Neither exists here.
   IDWriteFontCollection::FindFamilyName (slot 5, vtable shared by Win10 dwrite's collections) is patched: when a family is not found and
   it is one of these, the lookup is repeated with the substitute. */
typedef HRESULT (WINAPI *PFFN_)(void *, const WCHAR *, UINT32 *, BOOL *);
static PFFN_ g_real_ffn;
static HRESULT WINAPI ffn_hook(void *self, const WCHAR *name, UINT32 *index, BOOL *exists) {
  HRESULT hr = g_real_ffn(self, name, index, exists); const WCHAR *sub = NULL;
  if (FAILED(hr) || !exists || *exists || !name) return hr;
  if (!_wcsnicmp(name, L"Segoe UI Variable", 17)) sub = L"Segoe UI";
  else if (!_wcsicmp(name, L"Segoe Fluent Icons")) sub = L"Segoe MDL2 Assets";
  if (sub) { hr = g_real_ffn(self, sub, index, exists); { static LONG nl; if (InterlockedIncrement(&nl) <= 6) slog("[dw] font family '%ls' -> '%ls' (%s)", name, sub, SUCCEEDED(hr) && *exists ? "found" : "missing too"); } }
  return hr; }
/* installed on the dwrite.dll already loaded (the image's Win10 copy XAML imports), never loading one itself; called at D3D11CreateDevice */
static void dw_install_fontsubst(void) {
  typedef HRESULT (WINAPI *F)(int, REFIID, IUnknown **); static LONG done; HMODULE m; F f; IUnknown *fac = NULL; void *coll = NULL;
  static const GUID iid_fac = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};   /* IDWriteFactory */
  if (g_real_ffn) return;
  {   /* both are loaded: Win7's (for Win7's d2d1) and the image's Win10 copy (XAML's) - take the one outside the system directory */
    typedef BOOL (WINAPI *PEPM)(HANDLE, HMODULE *, DWORD, DWORD *); PEPM epm = (PEPM)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules");
    HMODULE mods[512]; DWORD need = 0, i; WCHAR sd[MAX_PATH], p[MAX_PATH]; UINT n = GetSystemDirectoryW(sd, MAX_PATH); m = NULL;
    if (epm && epm(GetCurrentProcess(), mods, sizeof mods, &need))
      for (i = 0; i < need / sizeof(HMODULE) && i < 512; i++) { const WCHAR *b; if (!GetModuleFileNameW(mods[i], p, MAX_PATH)) continue; b = wcsrchr(p, L'\\');
        if (b && !lstrcmpiW(b + 1, L"dwrite.dll") && !(n && !_wcsnicmp(p, sd, n) && p[n] == L'\\')) { m = mods[i]; break; } } }
  if (!m || !(f = (F)GetProcAddress(m, "DWriteCreateFactory"))) return;
  if (InterlockedExchange(&done, 1)) return;
  if (SUCCEEDED(f(0 /* shared */, &iid_fac, &fac)) && fac) {
    if (SUCCEEDED(VCALL(fac, 3, HRESULT (WINAPI *)(void *, void **, BOOL))(fac, &coll, FALSE)) && coll) {   /* GetSystemFontCollection */
      dx_patch_slot(coll, 5, (void **)&g_real_ffn, (void *)ffn_hook); dx_release_real(coll); }
    dx_release_real(fac); }
  { WCHAR p[MAX_PATH] = L"?"; GetModuleFileNameW(m, p, MAX_PATH); slog("[dw] FindFamilyName substitution %s (%ls)", g_real_ffn ? "installed" : "NOT installed", p); } }
/* GameInput's HKLM\SOFTWARE\Microsoft\GameInput RedistDir (written by GameInputRedist.msi on Win10): WXR's copy of the redistributable,
   <root>\Program Files\Microsoft GameInput\x64\ (put there from the game's own MSI by "wxrpkg gdk" / "wxrpkg add"), when it is there */
static WCHAR g_wxr_root[MAX_PATH];   /* wxrldr.inc */
static LONG gi_redist_dir(DWORD flags, DWORD *type, void *data, DWORD *cb) {
  WCHAR d[MAX_PATH], f[MAX_PATH]; DWORD need;
  if (!g_wxr_root[0]) return ERROR_FILE_NOT_FOUND;
  _snwprintf(d, MAX_PATH, L"%ls\\Program Files\\Microsoft GameInput\\x64\\", g_wxr_root); d[MAX_PATH - 1] = 0;
  _snwprintf(f, MAX_PATH, L"%lsGameInputRedist.dll", d); f[MAX_PATH - 1] = 0;
  if (GetFileAttributesW(f) == INVALID_FILE_ATTRIBUTES) return ERROR_FILE_NOT_FOUND;
  if (!(flags & RRF_RT_REG_SZ)) return ERROR_UNSUPPORTED_TYPE;
  need = (DWORD)(lstrlenW(d) + 1) * sizeof(WCHAR);
  if (type) *type = REG_SZ;
  if (!data) { if (cb) *cb = need; return ERROR_SUCCESS; }
  if (!cb || *cb < need) { if (cb) *cb = need; return ERROR_MORE_DATA; }
  memcpy(data, d, need); *cb = need;
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 4) slog("[reg] GameInput RedistDir -> %ls (WXR)", d); }
  return ERROR_SUCCESS; }
/* RegGetValueW: the WXR machine overlay (see WxrRegOpenKeyExW): a missing HKLM\<sub> is read from HKCU\Software\WXR\Machine\<sub> */
__declspec(dllexport) LONG WINAPI RegGetValueW(HKEY key, LPCWSTR sub, LPCWSTR val, DWORD flags, DWORD *type, void *data, DWORD *cb) {
  typedef LONG (WINAPI *F)(HKEY, LPCWSTR, LPCWSTR, DWORD, DWORD *, void *, DWORD *); static F f; LONG r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("advapi32.dll"), "RegGetValueW"); if (!f) return ERROR_PROC_NOT_FOUND;
  r = f(key, sub, val, flags, type, data, cb);
  if (r == ERROR_FILE_NOT_FOUND && key == HKEY_LOCAL_MACHINE && sub && *sub && lstrlenW(sub) < 470) {
    WCHAR p[512]; LONG r2; lstrcpyW(p, L"Software\\WXR\\Machine\\"); lstrcatW(p, sub);
    r2 = f(HKEY_CURRENT_USER, p, val, flags & ~0x00030000u, type, data, cb);   /* RRF_SUBKEY_WOW6464KEY/6432KEY: not for HKCU */
    if (r2 != ERROR_FILE_NOT_FOUND) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 20) slog("[reg] RegGetValueW HKLM\\%ls %ls -> HKCU\\%ls (WXR machine overlay) %ld", sub, val ? val : L"", p, r2); r = r2; }
    else if (val && !lstrcmpiW(sub, L"SOFTWARE\\Microsoft\\GameInput") && !lstrcmpiW(val, L"RedistDir")) r = gi_redist_dir(flags, type, data, cb); }
  return r; }
static void *volatile g_dx_lifted_dev;   /* the last device dwmcorei.dll created (held) */
__declspec(dllexport) HRESULT WINAPI WxrDx_GetLiftedDevice(REFIID riid, void **out);
static void *g_dx_first_dev;   /* the first device created (XAML's): dcomp renders on it when XAML hands it only an ID2D1Device */
__declspec(dllexport) HRESULT WINAPI D3D11CreateDevice(void *adapter, int type, HMODULE sw, UINT flags, const void *levels, UINT nlev, UINT sdk, void **dev, void *lvl, void **ctx) {
  typedef HRESULT (WINAPI *F)(void *, int, HMODULE, UINT, const void *, UINT, UINT, void **, void *, void **); static F f; HRESULT hr;
  UINT keep[16], nk = 0, k;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("d3d11.dll"), "D3D11CreateDevice"); if (!f) return E_NOTIMPL;
  /* feature levels 12_0/12_1 (0xC000/0xC100): Win7's 11.1 runtime fails the whole call with E_INVALIDARG (WinUI 3's dwmcorei
     CD3DDevice::CreateD3D11Device lists them first). Dropped; the rest keep their order. */
  if (levels && nlev && nlev <= 16) { for (k = 0; k < nlev; k++) if (((const UINT *)levels)[k] <= 0xB100) keep[nk++] = ((const UINT *)levels)[k];
    if (nk && nk < nlev) { static LONG nl; if (InterlockedIncrement(&nl) <= 4) slog("[dx] D3D11CreateDevice: %u of %u feature levels above 11_1 dropped (Win7)", nlev - nk, nlev); levels = keep; nlev = nk; } }
  /* WinUI 3's Microsoft.UI.Windowing.Core asks for a WARP device (caption drawing); its composition buffers are then opened by the lifted
     compositor on the hardware device, and Win7 cannot share a WARP texture with a GPU (NVIDIA's driver crashed creating the RTV).
     Given the hardware device instead. */
  if (type == 5 && !adapter) { HMODULE cm = NULL; WCHAR mp[MAX_PATH]; const WCHAR *b;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &cm) && GetModuleFileNameW(cm, mp, MAX_PATH)
        && (b = wcsrchr(mp, L'\\')) && !lstrcmpiW(b + 1, L"Microsoft.UI.Windowing.Core.dll")) { type = 1; slog("[dx] D3D11CreateDevice: WARP asked by Windowing.Core -> hardware (Win7 cannot share WARP textures with the GPU)"); } }
  /* dwmcorei's own device: not single-threaded (flag 1), so dcomp may flush it from the composing thread - its D2D work (solid and rounded
     fills) is submitted at D2D's EndDraw, after dcomp's EndDraw flush, and never reached the GPU before the frame was composed */
  if (flags & 1) { HMODULE cm = NULL; WCHAR mp[MAX_PATH]; const WCHAR *b;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &cm) && GetModuleFileNameW(cm, mp, MAX_PATH)
        && (b = wcsrchr(mp, L'\\')) && !lstrcmpiW(b + 1, L"dwmcorei.dll")) flags &= ~1u; }
  hr = f(adapter, type, sw, flags, levels, nlev, sdk, dev, lvl, ctx);
  if (FAILED(hr)) { static LONG nf; if (InterlockedIncrement(&nf) <= 6) slog("[dx] D3D11CreateDevice(adapter %p, type %d, flags 0x%x, %u levels) FAILED 0x%08lX (caller %p)", adapter, type, flags, nlev, (unsigned long)hr, __builtin_return_address(0)); }
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 8) { void *ra = __builtin_return_address(0); HMODULE cm = NULL; WCHAR mp[MAX_PATH] = L"?"; const WCHAR *b;
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &cm)) GetModuleFileNameW(cm, mp, MAX_PATH); b = wcsrchr(mp, L'\\');
      slog("[dx] D3D11CreateDevice(adapter %p, type %d, flags 0x%x) -> 0x%08lX dev=%p (caller %ls+0x%llx)", adapter, type, flags, (unsigned long)hr, dev ? *dev : NULL, b ? b + 1 : mp, (unsigned long long)((BYTE *)ra - (BYTE *)cm)); } }
  /* dcomp composes on XAML's device from the UI thread while XAML's render thread draws on it (Win10 composes in DWM, another process):
     the immediate context must be serialised. Without it Clock's first frames crashed in the driver's worker thread. */
  if (SUCCEEDED(hr) && dev && *dev) { static const GUID iid_mt = {0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};   /* ID3D10Multithread */
    void *mt = NULL; if (SUCCEEDED(VCALL(*dev, 0, HRESULT (WINAPI *)(void *, const GUID *, void **))(*dev, &iid_mt, &mt)) && mt) {
      VCALL(mt, 5, BOOL (WINAPI *)(void *, BOOL))(mt, TRUE); VCALL(mt, 2, ULONG (WINAPI *)(void *))(mt); } }
  if (SUCCEEDED(hr) && dev && *dev && type == 5) dx_note_warp(*dev);
  /* the device WinUI 3's lifted compositor (dwmcorei) renders with: dcomp opens its surfaces on it (WxrDx_GetLiftedDevice) */
  if (SUCCEEDED(hr) && dev && *dev) { HMODULE cm = NULL; WCHAR mp[MAX_PATH]; const WCHAR *b;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &cm) && GetModuleFileNameW(cm, mp, MAX_PATH)
        && (b = wcsrchr(mp, L'\\')) && !lstrcmpiW(b + 1, L"dwmcorei.dll")) { void *old; VCALL(*dev, 1, ULONG (WINAPI *)(void *))(*dev);
      old = InterlockedExchangePointer(&g_dx_lifted_dev, *dev); if (old) VCALL(old, 2, ULONG (WINAPI *)(void *))(old); slog("[dx] lifted compositor (dwmcorei) device %p recorded", *dev); } }
  if (SUCCEEDED(hr)) dw_install_fontsubst();
  if (SUCCEEDED(hr) && dev && *dev) { dx_hook_device(*dev); if (!g_dx_first_dev && InterlockedCompareExchangePointer(&g_dx_first_dev, *dev, NULL) == NULL) VCALL(*dev, 1, ULONG (WINAPI *)(void *))(*dev); }
  if (SUCCEEDED(hr) && dev && *dev) { void *ic = NULL; VCALL(*dev, 40, void (WINAPI *)(void *, void **))(*dev, &ic); if (ic) { dx_hook_clearview_ctx(ic); dx_release_real(ic); } }

  /* Win10's device is ONE object whose vtable already holds ID3D11Device1..5, so apps QI ID3D11Device3, note "version 3", Release the
     QI result and keep calling Device3 methods on the ORIGINAL pointer: bgfx (Minecraft Bedrock) called CreateRasterizerState2 (slot 56)
     on it, past the end of Win7's 50-slot vtable -> jump into the next vtable -> AV. For devices the app's own exe creates, the primary
     pointers handed back are already the Device3 / DeviceContext3 tear-offs (same cached objects later QIs return). */
  if (SUCCEEDED(hr) && dev && *dev) { void *ra = __builtin_return_address(0); HMODULE cm = NULL; int on;
    on = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &cm) && cm == GetModuleHandleW(NULL);
    if (on) { void *t = NULL, *c = NULL;
      if (SUCCEEDED(dx_upgrade_dev(*dev, &t)) && t) { dx_release_real(*dev); *dev = t; }
      if (ctx && *ctx && SUCCEEDED(dx_upgrade_ctx(*ctx, &c)) && c) { dx_release_real(*ctx); *ctx = c; }
      slog("[dx] D3D11CreateDevice: app gets the Device3/Context3 tear-offs as primary pointers (dev %p, ctx %p)", *dev, ctx ? *ctx : NULL); } }
  return hr; }
/* WxrDx_GetDevice(riid, out): XAML's D3D11 device (an ID2D1Device does not expose its D3D device publicly; dcomp needs it for surfaces) */
__declspec(dllexport) HRESULT WINAPI WxrDx_GetLiftedDevice(REFIID riid, void **out) {
  void *d = g_dx_lifted_dev; if (!out) return E_POINTER; *out = NULL; if (!d) return E_FAIL; return VCALL(d, 0, PQI_)(d, riid, out); }
__declspec(dllexport) HRESULT WINAPI WxrDx_GetDevice(REFIID riid, void **out) {
  void *d = g_dx_first_dev; if (!out) return E_POINTER; *out = NULL; if (!d) return E_FAIL; return VCALL(d, 0, PQI_)(d, riid, out); }
