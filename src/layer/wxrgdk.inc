/* wxrgdk.inc - GDK (Gaming) stand-in objects. Included by wxrsys.c. */
/* ---- GDK stub objects (Gaming) ------------------------------------------------------------------------------------
   GamingServices COM classes are normally served out-of-proc by GamingServices.exe (absent on Win7/WXR). The game (via
   xgameruntime InitializeApiImplEx2) and gamelaunchhelper CoCreateInstance them during GDK init; without them they get
   0x80040154 (class not registered) and bail (the license path -> 0x87E00196, the runtime path -> WinMain exits). WXR
   runs content the user has already installed, so these objects only need to exist so that init can proceed. Two classes seen so far:
     {834366DA-..} iid {D8ADEEB3-..}  = the launch gate. slot 3 (this, BYTE *out) writes a status byte that lets the process
       start. It does NOT grant a license: the game's own XStore license check still finds none, so the game runs as a trial.
       A full license must come from a real entitlement check of the signed-in account, never from a stand-in here.
     {42A916AC-..} iid {EF8615E3-..}  = the GDK RUNTIME service (created inside InitializeApiImplEx2, xgameruntime 0x1B010):
       obj->slot3(&info) -> info->slot3(&HSTRING eventName) -> OpenEventW(SYNCHRONIZE) + threadpool wait (kind 2 below).
   One shared stub object (kind 1 = license): QI returns self for its iid + IUnknown; slot 3 does the license byte on
   the license object, else S_OK; all other slots log + return S_OK (permissive) so init can proceed. We learn from the
   logs which slots the game actually uses and tighten later. */
static const GUID CLSID_GdkLicense_ = {0x834366DA, 0x2D43, 0x4FE3, {0x8D, 0xCD, 0x42, 0xFF, 0x22, 0x74, 0xBD, 0x0D}};
static const GUID IID_GdkLicense_   = {0xD8ADEEB3, 0x9E06, 0x4568, {0xA6, 0x34, 0xD5, 0xE8, 0xDD, 0x18, 0x43, 0xEE}};
static const GUID CLSID_GdkRuntime_ = {0x42A916AC, 0x911D, 0x47DB, {0x86, 0x76, 0x88, 0x62, 0xEC, 0x17, 0xCC, 0x54}};
static const GUID IID_GdkRuntime_   = {0xEF8615E3, 0xB070, 0x48DE, {0xB1, 0x15, 0x30, 0x2F, 0xF9, 0xA8, 0xA1, 0xB8}};
typedef struct { const void *const *vt; LONG ref; GUID iid; int kind; } GdkObj;   /* kind 1 = license */
static HRESULT gdk_new(int kind, void **out);   /* fwd */
static HRESULT hs_make(const WCHAR *s, void **out);   /* fwd (windows runtime strings) */
LONG WINAPI GetCurrentPackageFullName(UINT32 *len, PWSTR out);   /* wxrpackage.inc */
static HRESULT STDMETHODCALLTYPE gdk_qi(void *t, REFIID iid, void **o) {
  GdkObj *g = (GdkObj *)t; static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  static const GUID nomarshal = {0xECC8691B, 0xC1DB, 0x4DC0, {0x85, 0x5E, 0x65, 0xF6, 0xC5, 0x51, 0xAF, 0x49}};
  if (!o) return E_POINTER; *o = NULL;
  /* not the COM infrastructure ones (IMarshal, IStdMarshalInfo, IExternalConnection, ... = {000000xx-0000-0000-C000-000000000046};
     INoMarshal): answering those with this vtable would break RoGetAgileReference / marshaling. IAgileObject is fine (free-threaded). */
  if ((iid->Data1 && iid->Data1 < 0x200 && !iid->Data2 && !iid->Data3 && !memcmp(iid->Data4, iu.Data4, 8)) || IsEqualGUID(iid, &nomarshal)) return E_NOINTERFACE;
  /* wildcard: every GamingServices sub-interface the game asks for gets this permissive object (they are all stubs) */
  if (!IsEqualGUID(iid, &g->iid) && !IsEqualGUID(iid, &iu)) { char gg[48]; guid_str(iid, gg); static LONG n; if (InterlockedIncrement(&n) <= 12) slog("[gdk] QI(%s) -> self (wildcard)", gg); }
  *o = t; g->ref++; return S_OK; }
static ULONG STDMETHODCALLTYPE gdk_ar(void *t) { return (ULONG)++((GdkObj *)t)->ref; }
static ULONG STDMETHODCALLTYPE gdk_rl(void *t) { GdkObj *g = (GdkObj *)t; LONG r = --g->ref; if (r <= 0) { free(g); return 0; } return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE gdk_m3(void *t, void *a, void *b, void *c, void *d) {   /* slot 3 */
  GdkObj *g = (GdkObj *)t; (void)b; (void)c; (void)d;
  if (g->kind == 1) { int v = 1; if (a) *(BYTE *)a = (BYTE)v;
    { static LONG n; if (InterlockedIncrement(&n) <= 8) slog("[gdk] license query -> status %d", v); } return S_OK; }
  if (g->kind == 3) {   /* GameRuntime root service: slot 3 = Register(IUnknown *clientSink, UINT64 *cookie), slot 4 = Unregister(cookie)
                           (xgameruntime 0xFB7EA / teardown 0xFD03C). The sink is the service->client callback object; keep it alive. */
    static UINT64 next = 0x5752580000000001ull;
    if (a) ((IUnknown *)a)->lpVtbl->AddRef((IUnknown *)a);
    if (b) *(UINT64 *)b = next++;
    { static LONG n; if (InterlockedIncrement(&n) <= 4) slog("[gdk] runtime root: client sink %p registered", a); }
    return S_OK; }
  if (g->kind == 5) {   /* package-of-exe service {1CD4BEF6}/{E4CDEE6B} slot 3 = (const WCHAR *exePath, struct { HSTRING a; HSTRING
                           packageFullName; BYTE flag; } *out) (xgameruntime 0x180126878): an empty packageFullName -> E_UNEXPECTED;
                           'a' only feeds an is-empty flag. Answer with this process's package identity. */
    struct { void *a; void *full; BYTE flag; } *o = b; WCHAR full[256]; UINT32 n = 256; LONG r; HRESULT hr = S_OK;
    if (!o) return E_POINTER; o->a = NULL; o->full = NULL; o->flag = 0;
    r = GetCurrentPackageFullName(&n, full);
    if (r == ERROR_SUCCESS) hr = hs_make(full, &o->full); else hr = HRESULT_FROM_WIN32(r);
    { static LONG k; if (InterlockedIncrement(&k) <= 4) slog("[gdk] package of %ls -> %ls (0x%08lX)", a ? (const WCHAR *)a : L"?", r == ERROR_SUCCESS ? full : L"<none>", (unsigned long)hr); }
    return hr; }
  if (g->kind == 4) { static LONG n; if (InterlockedIncrement(&n) <= 4) slog("[gdk] plain object slot 3 (%p, %p) -> S_OK", a, b); return S_OK; }
  if (g->kind == 2) {   /* service-info sub-object: slot 3 = (HSTRING *out) the name of an event the service owns; xgameruntime
                           OpenEventW(SYNCHRONIZE)s it and threadpool-waits on it as "service went away" (signalled = teardown). Our
                           "service" lives as long as the process, so: a per-process named event that is never signalled. */
    static HANDLE ev; WCHAR nm[64]; HRESULT hr;
    wsprintfW(nm, L"Local\\WXR_GdkRuntime_%lu", GetCurrentProcessId());
    if (!ev) ev = CreateEventW(NULL, TRUE, FALSE, nm);
    if (!a) return E_POINTER; *(void **)a = NULL;
    hr = ev ? hs_make(nm, (void **)a) : HRESULT_FROM_WIN32(GetLastError());
    { static LONG n; if (InterlockedIncrement(&n) <= 4) slog("[gdk] runtime service event %ls -> 0x%08lX", nm, (unsigned long)hr); }
    return hr; }
  if (a) { gdk_new(2, (void **)a); { static LONG n; if (InterlockedIncrement(&n) <= 8) slog("[gdk] runtime slot 3 -> service-info sub-object"); } }
  return S_OK; }
#define GDK_SLOT(n) static HRESULT STDMETHODCALLTYPE gdk_m##n(void *t, void *a, void *b, void *c, void *d) { GdkObj *g = (GdkObj *)t; (void)a; (void)b; (void)c; (void)d; { static LONG m; if (InterlockedIncrement(&m) <= 3) slog("[gdk] %s slot %d -> S_OK (extend if it needs output)", g->kind == 1 ? "license" : "runtime", n); } return S_OK; }
GDK_SLOT(4) GDK_SLOT(5) GDK_SLOT(6) GDK_SLOT(7) GDK_SLOT(8) GDK_SLOT(9) GDK_SLOT(10) GDK_SLOT(11) GDK_SLOT(12) GDK_SLOT(13)
GDK_SLOT(14) GDK_SLOT(15) GDK_SLOT(16) GDK_SLOT(17) GDK_SLOT(18) GDK_SLOT(19) GDK_SLOT(20) GDK_SLOT(21) GDK_SLOT(22) GDK_SLOT(23)
GDK_SLOT(24) GDK_SLOT(25) GDK_SLOT(26) GDK_SLOT(27) GDK_SLOT(28) GDK_SLOT(29) GDK_SLOT(30) GDK_SLOT(31)
static const void *const gdk_vt[] = { gdk_qi, gdk_ar, gdk_rl, gdk_m3, gdk_m4, gdk_m5, gdk_m6, gdk_m7, gdk_m8, gdk_m9, gdk_m10,
  gdk_m11, gdk_m12, gdk_m13, gdk_m14, gdk_m15, gdk_m16, gdk_m17, gdk_m18, gdk_m19, gdk_m20, gdk_m21, gdk_m22, gdk_m23,
  gdk_m24, gdk_m25, gdk_m26, gdk_m27, gdk_m28, gdk_m29, gdk_m30, gdk_m31 };
static HRESULT gdk_create(REFIID iid_want, const GUID *obj_iid, int kind, void **ppv) {
  GdkObj *o; HRESULT hr; if (!ppv) return E_POINTER;
  if (!(o = (GdkObj *)calloc(1, sizeof *o))) return E_OUTOFMEMORY;
  o->vt = gdk_vt; o->ref = 1; o->iid = *obj_iid; o->kind = kind; hr = gdk_qi(o, iid_want, ppv); gdk_rl(o); return hr; }
/* a permissive sub-object handed back by a stub method's [out] interface param (QI is wildcard, so any iid resolves). */
static HRESULT gdk_new(int kind, void **out) {
  GdkObj *o; if (!out) return E_POINTER; *out = NULL;
  if (!(o = (GdkObj *)calloc(1, sizeof *o))) return E_OUTOFMEMORY;
  o->vt = gdk_vt; o->ref = 1; memset(&o->iid, 0, sizeof o->iid); o->kind = kind; *out = o; return S_OK; }
__declspec(dllexport) HRESULT WINAPI CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD ctx, REFIID iid, void **ppv) {
  { static const GUID creo = {0x00000352, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
    if (!outer && ppv && IsEqualGUID(clsid, &creo) && IsEqualGUID(iid, &RE_IID_INT)) return re_create(iid, ppv); }
  {   /* WinUI 3's Microsoft.ui.xaml.dll + Win7's TSF: CDocumentInputManager::AdviseSink gets a sink GUID Win7 does not know and its
         GenericAdviseSink indexes with -1 (AV in msctf). As with UWP XAML (ext-ms-win-tsf-msctf absent), XAML then works without TSF;
         keyboard input still arrives as window messages. */
    static const GUID ctm = {0x529a9e6b, 0x6587, 0x4f23, {0xab, 0x9e, 0x9c, 0x7d, 0x68, 0x3e, 0x3c, 0x50}};   /* CLSID_TF_ThreadMgr */
    /* a WinUI 3 process = CoreMessagingXP.dll loaded (WinUI 2 in UWP apps has the same Microsoft.UI.Xaml.dll name, but not that) */
    if (IsEqualGUID(clsid, &ctm) && GetModuleHandleA("CoreMessagingXP.dll")) {
      static volatile LONG nl; if (InterlockedIncrement(&nl) <= 2) slog("[tsf] WinUI 3 process: TF_ThreadMgr refused (Win7 TSF)"); if (ppv) *ppv = NULL; return REGDB_E_CLASSNOTREG; } }
  { static const GUID cgit = {0x00000323, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
    if (!outer && ppv && IsEqualGUID(clsid, &cgit)) return git_qi((void *)&g_git_obj, iid, ppv); }
  typedef HRESULT (WINAPI *F)(REFCLSID, IUnknown *, DWORD, REFIID, void **); static F f; HRESULT hr; char a[48], b[48];
  if (!f) f = (F)GetProcAddress(ole32_mod(), "CoCreateInstance");
  guid_str(clsid, a); guid_str(iid, b);
  {
    if (IsEqualGUID(clsid, &CLSID_GdkLicense_)) { hr = gdk_create(iid, &IID_GdkLicense_, 1, ppv); slog("[gdk] CoCreateInstance(license {834366DA}, iid=%s) -> 0x%08lX (stub)", b, (unsigned long)hr); return hr; }
    if (IsEqualGUID(clsid, &CLSID_GdkRuntime_)) { hr = gdk_create(iid, &IID_GdkRuntime_, 0, ppv); slog("[gdk] CoCreateInstance(runtime {42A916AC}, iid=%s) -> 0x%08lX (stub)", b, (unsigned long)hr); return hr; }
    { static const struct { GUID c; int kind; const char *what; } more[] = {
        { {0x8A27D3CE, 0x19F3, 0x4CE7, {0x8E, 0x51, 0xCB, 0xBD, 0xC8, 0xDE, 0xE2, 0x91}}, 3, "runtime root" },     /* iid {1FEC1B52} */
        { {0xD0425B83, 0x0B06, 0x45F7, {0x87, 0xBB, 0x28, 0x24, 0xE4, 0xD6, 0xDA, 0x9E}}, 4, "D0425B83" },        /* iid {F8C70F35} */
        { {0xD271B28B, 0x505C, 0x406F, {0xBC, 0xC8, 0x46, 0xCA, 0xBB, 0x24, 0x9E, 0x2E}}, 4, "D271B28B" },        /* iid {903DE535}: 0x18018DAD8, slot 8 query is non-fatal */
        { {0xD529741A, 0x1BF1, 0x4D1E, {0x99, 0x76, 0x35, 0x08, 0x96, 0x22, 0xE7, 0x58}}, 4, "D529741A" },        /* iid {0B505C36}: 0x180113A3A, slot 5 (DWORD *flag) stays 0 */
        { {0x1CD4BEF6, 0xAEB3, 0x41D7, {0xAB, 0xBC, 0x61, 0xC3, 0x5C, 0xCB, 0xD4, 0xAD}}, 5, "1CD4BEF6" } };       /* iid {E4CDEE6B}: 0x18011F8B3, held via RoGetAgileReference */
      unsigned k; for (k = 0; k < sizeof more / sizeof more[0]; k++) if (IsEqualGUID(clsid, &more[k].c)) {
        hr = gdk_create(iid, iid, more[k].kind, ppv); slog("[gdk] CoCreateInstance(%s %s, iid=%s) -> 0x%08lX (stub)", more[k].what, a, b, (unsigned long)hr); return hr; } }
  }
  { static const GUID me = {0xB44392DA, 0x499B, 0x446B, {0xA4, 0xCB, 0x00, 0x5F, 0xEA, 0xD0, 0xE6, 0xD5}}; if (!memcmp(clsid, &me, sizeof me)) mf_register_local(); }   /* Media Engine: Win10 codecs first */
  if (com_preferred(clsid) && SUCCEEDED(hr = com_inproc(clsid, outer, iid, ppv, 0))) { slog("[com] CoCreateInstance(%s) -> Win10 class", a); return hr; }
  if (IsEqualGUID(clsid, &CLSID_WxrMFPropertyHandler)) { hr = ph_create(iid, ppv); slog("[com] CoCreateInstance(WxrMFPropertyHandler, iid=%s) -> 0x%08lX", b, (unsigned long)hr); return hr; }
  if ((ctx & CLSCTX_INPROC_SERVER) && SUCCEEDED(hr = com_storage_first(clsid, outer, iid, ppv))) return hr;
  if ((ctx & CLSCTX_INPROC_SERVER) && com_blocked(clsid)) { if (ppv) *ppv = NULL; return REGDB_E_CLASSNOTREG; }
  hr = f(clsid, outer, ctx, iid, ppv);
  if (FAILED(hr)) { HRESULT h2 = com_inproc(clsid, outer, iid, ppv, 0); if (h2 != REGDB_E_CLASSNOTREG) hr = h2; }
  if (hr == REGDB_E_CLASSNOTREG && (ctx & CLSCTX_LOCAL_SERVER) && !outer) {
    /* the rest of the GamingServices.exe classes xgameruntime activates (LOCAL_SERVER; list from its CoCreateInstance sites). Not
       specialized yet: plain stubs (every method S_OK, no output) - only when no real server answered. Specialize when a trace
       shows a method's output is needed (see the kinds in gdk_m3). */
    static const GUID gs[] = {
      {0x22372F89, 0xF1F5, 0x49B3, {0x9F, 0xA5, 0x42, 0xDF, 0xFC, 0x91, 0x4D, 0x1F}}, {0x4FF525D5, 0xAC7F, 0x4D25, {0x8C, 0xEC, 0x23, 0x68, 0x6C, 0x02, 0xA7, 0xC9}},
      {0x51FAC510, 0xA336, 0x45D6, {0x85, 0x78, 0x63, 0x7A, 0x8D, 0x65, 0x26, 0xE9}}, {0x710318A4, 0x861A, 0x4599, {0x9D, 0xA2, 0x50, 0xC8, 0x4E, 0xE5, 0x9E, 0xD8}},
      {0x9A4948D9, 0x13FC, 0x4FAC, {0xB6, 0x0A, 0xFB, 0xA6, 0xEE, 0x0F, 0xB1, 0x1C}}, {0xB5FB9AC1, 0xAD68, 0x45C5, {0xB7, 0xEB, 0x6F, 0x24, 0x98, 0xAE, 0xFA, 0xA7}},
      {0xDEA688F3, 0x0625, 0x45AB, {0xAF, 0x1A, 0xEF, 0xCF, 0x9B, 0xB4, 0x40, 0xF6}}, {0xE97EABA8, 0x9BCD, 0x4930, {0x99, 0x2E, 0x2A, 0xDC, 0x66, 0x17, 0x68, 0x17}} };
    unsigned k; for (k = 0; k < sizeof gs / sizeof gs[0]; k++) if (IsEqualGUID(clsid, &gs[k])) {
      hr = gdk_create(iid, iid, 4, ppv); slog("[gdk] CoCreateInstance(%s, iid=%s): not registered -> plain GamingServices stub 0x%08lX", a, b, (unsigned long)hr); return hr; } }
  slog("[com] CoCreateInstance(%s, ctx=%lx, iid=%s) -> 0x%08lX", a, ctx, b, (unsigned long)hr);

  return hr;
}
/* CoCreateInstanceEx: same routing as CoCreateInstance (Win10 windows.storage creates shell classes this way too); the first
   interface is created, the others are QueryInterface'd from it. Remote activation (server info) goes to ole32 untouched. */
__declspec(dllexport) HRESULT WINAPI CoCreateInstanceEx(REFCLSID clsid, IUnknown *outer, DWORD ctx, COSERVERINFO *srv, DWORD n, MULTI_QI *mq) {
  typedef HRESULT (WINAPI *F)(REFCLSID, IUnknown *, DWORD, COSERVERINFO *, DWORD, MULTI_QI *); static F f; HRESULT hr; DWORD i, ok = 0; IUnknown *u = NULL;
  if (!f) f = (F)GetProcAddress(ole32_mod(), "CoCreateInstanceEx");
  if (srv || !n || !mq || !(ctx & CLSCTX_INPROC_SERVER)) return f(clsid, outer, ctx, srv, n, mq);
  hr = CoCreateInstance(clsid, outer, ctx, mq[0].pIID, (void **)&u);
  if (FAILED(hr)) return f(clsid, outer, ctx, srv, n, mq);
  for (i = 0; i < n; i++) { mq[i].pItf = NULL; mq[i].hr = i == 0 ? S_OK : u->lpVtbl->QueryInterface(u, mq[i].pIID, (void **)&mq[i].pItf); if (i == 0) { mq[0].pItf = u; u->lpVtbl->AddRef(u); } if (SUCCEEDED(mq[i].hr)) ok++; }
  u->lpVtbl->Release(u);
  return ok == n ? S_OK : ok ? CO_S_NOTALLINTERFACES : E_NOINTERFACE;
}
__declspec(dllexport) HRESULT WINAPI CoGetClassObject(REFCLSID clsid, DWORD ctx, void *srv, REFIID iid, void **ppv) {
  typedef HRESULT (WINAPI *F)(REFCLSID, DWORD, void *, REFIID, void **); static F f; HRESULT hr; char a[48], b[48];
  if (!f) f = (F)GetProcAddress(ole32_mod(), "CoGetClassObject");
  if ((ctx & CLSCTX_INPROC_SERVER) && com_blocked(clsid)) { if (ppv) *ppv = NULL; return REGDB_E_CLASSNOTREG; }   /* wxrprop.inc: no third-party in-proc COM in WXR apps */
  if (IsEqualGUID(clsid, &CLSID_WxrMFPropertyHandler) && ppv) { slog("[com] CoGetClassObject(WxrMFPropertyHandler)"); return phcf_qi((void *)&g_phcf, iid, ppv); }
  hr = f(clsid, ctx, srv, iid, ppv); guid_str(clsid, a); guid_str(iid, b);
  if (FAILED(hr)) { HRESULT h2 = com_inproc(clsid, NULL, iid, ppv, 1); if (h2 != REGDB_E_CLASSNOTREG) hr = h2; }
  slog("[com] CoGetClassObject(%s, ctx=%lx, iid=%s) -> 0x%08lX", a, ctx, b, (unsigned long)hr); return hr;
}
/* The FTM handed to app code (non-aggregated CoCreateFreeThreadedMarshaler) sits behind a proxy that also answers IAgileObject:
   Win7's FTM has no IAgileObject (Win8+), and CsWinRT then compared IMarshal::GetUnmarshalClass with the FTM class, which it learns
   by creating an FTM and checking it the same way - endless recursion, a stack overflow at the Store's start. */
typedef struct FtmP { const void *const *vt; LONG ref; IUnknown *in; IUnknown *inm; } FtmP;
static const GUID FTM_IID_IMarshal = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID FTM_IID_IUnknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID FTM_IID_IAgileObject = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static HRESULT STDMETHODCALLTYPE ftmp_qi(FtmP *p, REFIID iid, void **o) {
  if (IsEqualGUID(iid, &FTM_IID_IUnknown) || IsEqualGUID(iid, &FTM_IID_IMarshal) || IsEqualGUID(iid, &FTM_IID_IAgileObject)) { *o = p; InterlockedIncrement(&p->ref); return S_OK; }
  return p->in->lpVtbl->QueryInterface(p->in, iid, o); }
static ULONG STDMETHODCALLTYPE ftmp_ar(FtmP *p) { return (ULONG)InterlockedIncrement(&p->ref); }
static ULONG STDMETHODCALLTYPE ftmp_rl(FtmP *p) { LONG n = InterlockedDecrement(&p->ref); if (!n) { if (p->inm) p->inm->lpVtbl->Release(p->inm); p->in->lpVtbl->Release(p->in); HeapFree(GetProcessHeap(), 0, p); } return (ULONG)n; }
#define FTMP_M(k, nm) static HRESULT STDMETHODCALLTYPE ftmp_##k(FtmP *p, void *a, void *b, void *c, void *d, void *e, void *f) { \
  return ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *, void *, void *, void *, void *))(*(void ***)p->inm)[k])(p->inm, a, b, c, d, e, f); }
FTMP_M(3, "GetUnmarshalClass") FTMP_M(4, "GetMarshalSizeMax") FTMP_M(5, "MarshalInterface") FTMP_M(6, "UnmarshalInterface") FTMP_M(7, "ReleaseMarshalData") FTMP_M(8, "DisconnectObject")
static const void *const g_ftmp_vt[9] = { (void *)ftmp_qi, (void *)ftmp_ar, (void *)ftmp_rl, (void *)ftmp_3, (void *)ftmp_4, (void *)ftmp_5, (void *)ftmp_6, (void *)ftmp_7, (void *)ftmp_8 };
static IUnknown *ftm_proxy(IUnknown *in) { FtmP *p; void *m = NULL;
  if (!in || FAILED(in->lpVtbl->QueryInterface(in, &FTM_IID_IMarshal, &m)) || !(p = (FtmP *)HeapAlloc(GetProcessHeap(), 0, sizeof *p))) { if (m) ((IUnknown *)m)->lpVtbl->Release((IUnknown *)m); return in; }
  p->vt = g_ftmp_vt; p->ref = 1; p->in = in; p->inm = (IUnknown *)m; return (IUnknown *)p; }
__declspec(dllexport) HRESULT WINAPI CoCreateFreeThreadedMarshaler(IUnknown *outer, IUnknown **out) {   /* non-aggregated: behind the agile proxy above */
  typedef HRESULT (WINAPI *F)(IUnknown *, IUnknown **); static F f;
  if (!f) f = (F)GetProcAddress(ole32_mod(), "CoCreateFreeThreadedMarshaler");
  if (!outer) { HRESULT hr = f(NULL, out);
    if (SUCCEEDED(hr) && out && *out) *out = ftm_proxy(*out); return hr; }
  return f(outer, out); }
