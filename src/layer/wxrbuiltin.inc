/* wxrbuiltin.inc - built-in WinRT classes for Win10 DLLs the image does not have. Included by wxrsys.c. */
/* ================= built-in WinRT classes that live in Win10 DLLs we do not have (rmclient.dll, ...) =================
   Windows.System.MemoryManager and Windows.Foundation.Diagnostics.AsyncCausalityTracer: statics-only classes. The app (and XAML) activate
   them unconditionally; tiny in-process implementations are enough (no memory limits, tracing disabled). */
typedef struct { const GUID *iid; const void *const *vt; } BiIface;
typedef struct { const WCHAR *cls; const BiIface *ifaces; int n; } BiClass;
static HRESULT WINAPI bi_noop(void) { return S_OK; }
static void log_callers(const char *tag);
static HRESULT WINAPI bi_notimpl(void) { log_callers("E_NOTIMPL from a built-in WinRT class"); return E_NOTIMPL; }
static HRESULT WINAPI bi_ok_u64(void *t, unsigned long long *o) { (void)t; if (o) *o = 0; return S_OK; }
static HRESULT WINAPI bi_limit_u64(void *t, unsigned long long *o) { (void)t; if (o) *o = 0x100000000ull; return S_OK; }
static HRESULT WINAPI bi_ok_i32(void *t, int *o) { (void)t; if (o) *o = 0; return S_OK; }
static HRESULT WINAPI bi_add(void *t, void *h, unsigned long long *tok) { (void)t; (void)h; if (tok) *tok = 1; return S_OK; }
static ULONG WINAPI bi_addref(void *t) { (void)t; return 2; }
static ULONG WINAPI bi_release(void *t) { (void)t; return 1; }
static HRESULT WINAPI bi_getiids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT WINAPI bi_gettrust(void *t, int *lvl) { (void)t; if (lvl) *lvl = 0; return S_OK; }
__declspec(dllexport) HRESULT WINAPI RoGetActivationFactory(void *hs, const GUID *iid, void **out);
/* KnownFolders.MusicLibrary/PicturesLibrary/VideosLibrary/DocumentsLibrary: Win10's library code (shell libraries through Win10's namespace)
   fails on Win7 (MusicLibrary 0x80070002, VideosLibrary E_NOINTERFACE -> unhandled InvalidCastException in Media Player's "Open folder").
   When Win10's getter fails, the user's plain folder (FOLDERID_Music ...) is returned as a StorageFolder - what a Win7 library saves to.
   IKnownFoldersStatics vtable slots 6..9; patched once. */
static void *g_kf_orig[4], *g_kf_rem;
static HRESULT kf_fallback(int k, void **out) {
  static const GUID fid[4] = { {0x4BD8D571, 0x6D19, 0x48D3, {0xBE, 0x97, 0x42, 0x22, 0x20, 0x08, 0x0E, 0x43}},   /* FOLDERID_Music */
    {0x33E28130, 0x4E1E, 0x4676, {0x83, 0x5A, 0x98, 0x39, 0x5C, 0x3B, 0xC3, 0xBB}},   /* Pictures */
    {0x18989B1D, 0x99B5, 0x455B, {0x84, 0x1C, 0xAB, 0x7C, 0x74, 0xE4, 0xDD, 0xFC}},   /* Videos */
    {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}} }; /* Documents */
  static const GUID isfs = {0x08F327FF, 0x85D5, 0x48B9, {0xAE, 0xE9, 0x28, 0x51, 0x1E, 0x33, 0x9F, 0x9F}}, iinfo = {0x00000036, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  typedef HRESULT (WINAPI *PKFP)(REFGUID, DWORD, HANDLE, WCHAR **); PKFP kfp = (PKFP)GetProcAddress(LoadLibraryA("shell32.dll"), "SHGetKnownFolderPath");
  WCHAR *path = NULL; void *hc = NULL, *hp = NULL, *st = NULL; IUnknown *op = NULL, *info = NULL; HRESULT hr; int status = 0; DWORD t0 = GetTickCount();
  if (k == 5) {   /* RemovableDevices: Win10's virtual "removable drives" folder cannot be built here; an empty folder = no devices */
    WCHAR lp[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", lp, MAX_PATH); if (!n || n >= MAX_PATH - 40) return E_FAIL;
    lstrcatW(lp, L"\\wxr"); CreateDirectoryW(lp, NULL); lstrcatW(lp, L"\\RemovableDevices"); CreateDirectoryW(lp, NULL);
    path = (WCHAR *)CoTaskMemAlloc((lstrlenW(lp) + 1) * sizeof(WCHAR)); if (!path) return E_OUTOFMEMORY; lstrcpyW(path, lp); }
  else if (!kfp || FAILED(hr = kfp(&fid[k], 0, NULL, &path))) return E_FAIL;
  hr = hs_init() ? p_create(L"Windows.Storage.StorageFolder", 29, &hc) : E_FAIL; if (SUCCEEDED(hr)) hr = p_create(path, (UINT32)lstrlenW(path), &hp);
  if (SUCCEEDED(hr)) hr = RoGetActivationFactory(hc, &isfs, &st);
  if (SUCCEEDED(hr)) hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, IUnknown **))(*(void ***)st)[6])(st, hp, &op);   /* GetFolderFromPathAsync */
  if (SUCCEEDED(hr) && op && SUCCEEDED(hr = op->lpVtbl->QueryInterface(op, &iinfo, (void **)&info))) {
    while (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, int *))(*(void ***)info)[7])(info, &status)) && status == 0 && GetTickCount() - t0 < 10000) { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT); }
    hr = status == 1 ? ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)op)[8])(op, out) : E_FAIL; }
  slog("[storage] KnownFolders fallback %d: %ls -> 0x%08lX (status %d)", k, path, (unsigned long)hr, status);
  if (info) info->lpVtbl->Release(info); if (op) op->lpVtbl->Release(op); if (st) ((IUnknown *)st)->lpVtbl->Release((IUnknown *)st);
  { typedef HRESULT (WINAPI *PDS)(void *); static PDS ds; if (!ds) ds = (PDS)GetProcAddress(GetModuleHandleA("combase.dll") ? GetModuleHandleA("combase.dll") : LoadLibraryA("api-ms-win-core-winrt-string-l1-1-0.dll"), "WindowsDeleteString"); if (ds) { if (hc) ds(hc); if (hp) ds(hp); } } CoTaskMemFree(path); return hr; }
#define KF_GET(n) static HRESULT STDMETHODCALLTYPE kf_get##n(void *t, void **out) { HRESULT hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))g_kf_orig[n])(t, out); \
  if (FAILED(hr) && out) { HRESULT h2 = kf_fallback(n, out); if (SUCCEEDED(h2)) return h2; } return hr; }
KF_GET(0) KF_GET(1) KF_GET(2) KF_GET(3)
static HRESULT STDMETHODCALLTYPE kf_getrem(void *t, void **out) { HRESULT hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))g_kf_rem)(t, out);
  if (FAILED(hr) && out) { HRESULT h2 = kf_fallback(5, out); if (SUCCEEDED(h2)) return h2; } return hr; }
static void knownfolders_patch(void *factory) {
  static const GUID ikfs = {0x5A2A7520, 0x4802, 0x452D, {0x9A, 0xD9, 0x43, 0x51, 0xAD, 0xA7, 0xEC, 0x35}}; static LONG done; void *s = NULL;
  if (InterlockedExchange(&done, 1)) return;
  if (SUCCEEDED(((IUnknown *)factory)->lpVtbl->QueryInterface((IUnknown *)factory, &ikfs, &s)) && s) { void **vt = *(void ***)s; DWORD old; static void *const fns[4] = { (void *)kf_get0, (void *)kf_get1, (void *)kf_get2, (void *)kf_get3 }; int k;
    if (VirtualProtect(&vt[6], 4 * sizeof(void *), PAGE_READWRITE, &old)) { for (k = 0; k < 4; k++) { g_kf_orig[k] = vt[6 + k]; vt[6 + k] = fns[k]; } VirtualProtect(&vt[6], 4 * sizeof(void *), old, &old);
      if (VirtualProtect(&vt[11], sizeof(void *), PAGE_READWRITE, &old)) { g_kf_rem = vt[11]; vt[11] = (void *)kf_getrem; VirtualProtect(&vt[11], sizeof(void *), old, &old); } slog("[storage] KnownFolders libraries: plain-folder fallback installed"); }
    ((IUnknown *)s)->lpVtbl->Release((IUnknown *)s); } }
static HRESULT WINAPI bi_getclassname(void *t, void **hs);
static HRESULT WINAPI bi_activate(void *t, void **o) { (void)t; if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT WINAPI bi_qi(void *t, REFIID iid, void **o);
#define BI_BASE bi_qi, bi_addref, bi_release, bi_getiids, bi_getclassname, bi_gettrust
static const void *const vt_factory[] = { BI_BASE, bi_activate };
static const void *const vt_mm1[] = { BI_BASE, bi_ok_u64, bi_limit_u64, bi_ok_i32, bi_add, bi_noop, bi_add, bi_noop, bi_add, bi_noop };
/* MemoryManager.GetAppMemoryReport / GetProcessMemoryReport (IMemoryManagerStatics2): .NET Native's runtime polls them (~20 s after start);
   E_NOTIMPL there became an unhandled exception and killed the app. Reports are filled from the process's own counters; every property is a UInt64
   getter (app: PrivateCommit, PeakPrivateCommit, TotalCommit, TotalCommitLimit; process: PrivateWorkingSet, TotalWorkingSet). */
typedef struct { const void *vt; unsigned long long v[4]; LONG ref; } MemRep;
static HRESULT WINAPI mr_qi(void *t, REFIID iid, void **o) {
  static const BYTE com_tail[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0x46};
  if (!o) return E_POINTER; *o = NULL;
  /* classic COM IIDs other than IUnknown (IMarshal, INoMarshal, ...) are refused; the WinRT report interfaces (whose IIDs the callers own) are accepted */
  if (!memcmp(iid->Data4, com_tail, 8) && iid->Data2 == 0 && iid->Data3 == 0 && iid->Data1 != 0) return E_NOINTERFACE;
  InterlockedIncrement(&((MemRep *)t)->ref); *o = t; return S_OK; }
static ULONG WINAPI mr_ar(void *t) { return InterlockedIncrement(&((MemRep *)t)->ref); }
static ULONG WINAPI mr_rl(void *t) { LONG r = InterlockedDecrement(&((MemRep *)t)->ref); if (!r) free(t); return r; }
static HRESULT WINAPI mr_iids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT WINAPI mr_name(void *t, void **hs) { (void)t; if (hs) *hs = NULL; return S_OK; }
static HRESULT WINAPI mr_trust(void *t, int *l) { (void)t; if (l) *l = 0; return S_OK; }
#define MR_GET(k) static HRESULT WINAPI mr_get##k(void *t, unsigned long long *o) { if (!o) return E_POINTER; *o = ((MemRep *)t)->v[k]; return S_OK; }
MR_GET(0) MR_GET(1) MR_GET(2) MR_GET(3)
static const void *const vt_memrep[] = { mr_qi, mr_ar, mr_rl, mr_iids, mr_name, mr_trust, mr_get0, mr_get1, mr_get2, mr_get3 };
typedef struct { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage,
  QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage; } MrPmc;
static HRESULT mr_make(int app, void **o) {
  typedef BOOL (WINAPI *PGPMI)(HANDLE, MrPmc *, DWORD); PGPMI g = (PGPMI)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo");
  MrPmc c; MemRep *r; if (!o) return E_POINTER; *o = NULL; memset(&c, 0, sizeof c); c.cb = sizeof c;
  if (g) g(GetCurrentProcess(), &c, sizeof c);
  if (!(r = (MemRep *)calloc(1, sizeof *r))) return E_OUTOFMEMORY; r->vt = vt_memrep; r->ref = 1;
  if (app) { r->v[0] = c.PrivateUsage; r->v[1] = c.PeakPagefileUsage; r->v[2] = c.PagefileUsage; r->v[3] = 0x100000000ull; }   /* limit as AppMemoryUsageLimit */
  else { r->v[0] = c.PrivateUsage < c.WorkingSetSize ? c.PrivateUsage : c.WorkingSetSize; r->v[1] = c.WorkingSetSize; }
  *o = r; return S_OK; }
static HRESULT WINAPI bi_mm_appreport(void *t, void **o) { (void)t; return mr_make(1, o); }
static HRESULT WINAPI bi_mm_procreport(void *t, void **o) { (void)t; return mr_make(0, o); }
static const void *const vt_mm2[] = { BI_BASE, bi_mm_appreport, bi_mm_procreport };
static HRESULT WINAPI bi_mm_trysetlimit(void *t, unsigned long long v, unsigned char *ok) { (void)t; (void)v; if (ok) *ok = 0; return S_OK; }
static const void *const vt_mm3[] = { BI_BASE, bi_mm_trysetlimit };
static const void *const vt_mm4[] = { BI_BASE, bi_limit_u64 };
static const void *const vt_act[] = { BI_BASE, bi_noop, bi_noop, bi_noop, bi_noop, bi_noop, bi_add, bi_noop };
static const GUID bi_iid_act = {0x00000035, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, bi_iid_insp = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}},
  bi_iid_unk = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, bi_iid_agile = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}},
  bi_iid_mm1 = {0x5C6C279C, 0xD7CA, 0x4779, {0x91, 0x88, 0x40, 0x57, 0x21, 0x9C, 0xE6, 0x4C}}, bi_iid_mm2 = {0x6EEE351F, 0x6D62, 0x423F, {0x94, 0x79, 0xB0, 0x1F, 0x9C, 0x9F, 0x76, 0x69}},
  bi_iid_mm3 = {0x149B59CE, 0x92AD, 0x4E35, {0x89, 0xEB, 0x50, 0xDF, 0xB4, 0xC0, 0xD9, 0x1C}}, bi_iid_mm4 = {0xC5A94828, 0xE84E, 0x4886, {0x8A, 0x0D, 0x44, 0xB3, 0x19, 0x0E, 0x3B, 0x72}},
  bi_iid_act1 = {0x50850B26, 0x267E, 0x451B, {0xA8, 0x90, 0xAB, 0x6A, 0x37, 0x02, 0x45, 0xEE}};
static const void *const *bi_cur_pool[8];
/* each class instance = array of vtable pointers; the pointer handed out is the address of one slot */
/* Windows.Networking.Connectivity.NetworkInformation (statics only): Win10's implementation sits on newer network services. Calculator's
   NetworkManager (every converter page builds one) subscribes NetworkStatusChanged and asks GetInternetConnectionProfile; NULL = no
   Internet profile, so it takes its offline path (currency: cached rates). VxKex's generic fallback factory had no statics interface, the
   E_NOINTERFACE threw, and no converter page opened. INetworkInformationStatics {5074F851} 6-13, ...Statics2 {459CED14} 6. */
static HRESULT WINAPI bi_net_noprofile(void *t, void **o) { (void)t; if (o) *o = NULL; return S_OK; }
static HRESULT WINAPI bi_net_notimpl(void *t, void *a, void *b, void *c) { (void)t; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static const void *const vt_net1[] = { BI_BASE, bi_net_notimpl, bi_net_noprofile, bi_net_notimpl, bi_net_notimpl, bi_net_notimpl, bi_net_notimpl, bi_add, bi_noop };
static const void *const vt_net2[] = { BI_BASE, bi_net_notimpl };
static const GUID bi_iid_net1 = {0x5074F851, 0x950D, 0x4165, {0x9C, 0x15, 0x36, 0x56, 0x19, 0x48, 0x1E, 0xEA}}, bi_iid_net2 = {0x459CED14, 0x2832, 0x49B6, {0xBA, 0x6E, 0xE2, 0x65, 0xF0, 0x47, 0x86, 0xA8}};
static const void *bi_mm_obj[5], *bi_act_obj[2], *bi_net_obj[3];
static const BiIface bi_mm_if[] = { { &bi_iid_act, vt_factory }, { &bi_iid_mm1, vt_mm1 }, { &bi_iid_mm2, vt_mm2 }, { &bi_iid_mm3, vt_mm3 }, { &bi_iid_mm4, vt_mm4 } };
static const BiIface bi_act_if[] = { { &bi_iid_act, vt_factory }, { &bi_iid_act1, vt_act } };
static const BiIface bi_net_if[] = { { &bi_iid_act, vt_factory }, { &bi_iid_net1, vt_net1 }, { &bi_iid_net2, vt_net2 } };
static const BiClass g_biclass[] = { { L"Windows.System.MemoryManager", bi_mm_if, 5 }, { L"Windows.Foundation.Diagnostics.AsyncCausalityTracer", bi_act_if, 2 },
  { L"Windows.Networking.Connectivity.NetworkInformation", bi_net_if, 3 } };
#define BI_NCLASS 3
static const void **bi_slots(const BiClass *c) { return c->ifaces == bi_mm_if ? bi_mm_obj : c->ifaces == bi_net_if ? bi_net_obj : bi_act_obj; }
static const BiClass *bi_class_of(void *t) { int i, k; for (i = 0; i < BI_NCLASS; i++) { const void **s = bi_slots(&g_biclass[i]); for (k = 0; k < g_biclass[i].n; k++) if ((const void **)t == &s[k]) return &g_biclass[i]; } return NULL; }
static HRESULT WINAPI bi_qi(void *t, REFIID iid, void **o) {
  const BiClass *c = bi_class_of(t); const void **s; int k; if (!c || !o) return E_POINTER; s = bi_slots(c); *o = NULL;
  if (!memcmp(iid, &bi_iid_unk, sizeof(GUID)) || !memcmp(iid, &bi_iid_insp, sizeof(GUID)) || !memcmp(iid, &bi_iid_agile, sizeof(GUID))) { *o = &s[0]; return S_OK; }
  for (k = 0; k < c->n; k++) if (!memcmp(iid, c->ifaces[k].iid, sizeof(GUID))) { *o = &s[k]; return S_OK; }
  return E_NOINTERFACE; }
static HRESULT WINAPI bi_getclassname(void *t, void **hs) { const BiClass *c = bi_class_of(t); if (!c || !hs || !hs_init()) return E_FAIL; return p_create(c->cls, (UINT32)wcslen(c->cls), hs); }
/* returns an IActivationFactory* for a built-in class name, or NULL */
static void *bi_factory(const WCHAR *cls) {
  int i, k; for (i = 0; i < BI_NCLASS; i++) if (!lstrcmpiW(cls, g_biclass[i].cls)) { const void **s = bi_slots(&g_biclass[i]); for (k = 0; k < g_biclass[i].n; k++) s[k] = g_biclass[i].ifaces[k].vt; slog("[act] %ls -> built-in", cls); return &s[0]; }
  return NULL; }
static HRESULT get_factory(void *hs, void **f) {
  const WCHAR *c; UINT32 n = 0; WCHAR cls[200]; int i; HRESULT hr = REGDB_E_CLASSNOTREG;
  if (!hs_init()) return E_NOTIMPL; c = p_raw(hs, &n); if (n > 190) n = 190; memcpy(cls, c, n * 2); cls[n] = 0;
  { void *b = bi_factory(cls); if (!b) b = shell_builtin_factory(cls); if (!b && !lstrcmpW(cls, L"Windows.Foundation.Uri")) b = uri_factory();
    if (b) { *f = b; return S_OK; } }
  alock(1); act_init(); alock(0);
  {   /* wxrstub.dll (generated stand-ins for classes whose Win10 implementation needs a Win10 service: background tasks, notifications);
         it answers REGDB_E_CLASSNOTREG for everything else. */
    typedef HRESULT (WINAPI *PDGAF)(void *, void **); static PDGAF stub; static LONG tried;
    if (!InterlockedExchange(&tried, 1)) {
      WCHAR p[MAX_PATH]; HMODULE m; _snwprintf(p, MAX_PATH, L"%s\\wxrstub.dll", g_dir); m = LoadLibraryW(p);
      stub = m ? (PDGAF)GetProcAddress(m, "DllGetActivationFactory") : NULL; slog("[act] stand-in classes: %ls %s", p, stub ? "loaded" : "not loaded"); }
    if (stub && SUCCEEDED(stub(hs, f)) && *f) { slog("[act] %ls -> stand-in (wxrstub)", cls); return S_OK; } }
  for (i = 0; i < g_ncat; i++) if (!lstrcmpiW(g_cat[i].cls, cls)) { hr = try_dll(g_cat[i].dll, hs, f); if (!(SUCCEEDED(hr) && *f) && (hr != (HRESULT)0x80040111 || wcsstr(cls, L"UIContext"))) slog("[act] %ls: %ls DllGetActivationFactory -> 0x%08lX (f=%p)", cls, g_dllname[g_cat[i].dll], (unsigned long)hr, *f); if (SUCCEEDED(hr) && *f) { slog("[act] %ls -> %ls (0x%08lX)", cls, g_dllname[g_cat[i].dll], (unsigned long)hr); if (!lstrcmpW(cls, L"Windows.Storage.KnownFolders")) knownfolders_patch(*f); return hr; } }
  /* Windows.UI.Composition.* classes the catalog does not list (e.g. CompositionEffectSourceParameter): dcomp serves that namespace */
  if (!wcsncmp(cls, L"Windows.UI.Composition.", 23)) for (i = 0; i < g_ndll; i++) if (!lstrcmpiW(g_dllname[i], L"dcomp.dll")) {
    HRESULT h2 = try_dll(i, hs, f); slog("[act] %ls -> dcomp (not in catalog) 0x%08lX", cls, (unsigned long)h2); if (SUCCEEDED(h2) && *f) return h2; break; }
  return hr;
}
__declspec(dllexport) HRESULT WINAPI RoGetActivationFactory(void *hs, const GUID *iid, void **out) {
  void *f = NULL; HRESULT hr; PRoGet h;
  if (!out) return E_INVALIDARG; *out = NULL;
  hr = get_factory(hs, &f);
  { static int trace = -1; if (trace < 0) trace = GetEnvironmentVariableA("WXR_LOGACT", NULL, 0) ? 1 : 0; if (trace) { char gs[48]; guid_str(iid, gs); slog("[rgaf] %ls %s -> factory 0x%08lX", p_raw(hs, NULL), gs, (unsigned long)hr); } }
  if (SUCCEEDED(hr) && f) { void **vt = *(void ***)f; HRESULT (WINAPI *qi)(void *, const GUID *, void **) = (void *)vt[0]; ULONG (WINAPI *rel)(void *) = (void *)vt[2]; hr = qi(f, iid, out); rel(f); if (FAILED(hr)) { char gs[48]; guid_str(iid, gs); slog("[act] %ls: factory QI %s -> 0x%08lX", p_raw(hs, NULL), gs, (unsigned long)hr); }

    return hr; }
  /* only Windows.* classes can live in the host; KxCom says S_OK for e.g. WinUI's CBSPackageDetector,
     which makes WinUI believe it is the inbox (CBS) copy */
  if (wcsncmp(p_raw(hs, NULL), L"Windows.", 8)) { slog("[act] %ls: not registered (no factory)", p_raw(hs, NULL)); return REGDB_E_CLASSNOTREG; }
  h = host_get(); if (h) { hr = h(hs, iid, out); if (SUCCEEDED(hr) && !*out) hr = REGDB_E_CLASSNOTREG; { const WCHAR *c = p_raw ? p_raw(hs, NULL) : L"?"; { char gs[48]; guid_str(iid, gs); slog("[act] %ls -> HOST fallback 0x%08lX (iid %s)", c, (unsigned long)hr, gs); } }
    if (hr == E_NOINTERFACE) hr = REGDB_E_CLASSNOTREG;   /* Win7's combase answers E_NOINTERFACE for WinRT classes it does not know; Windows says class-not-registered */
    return hr; }
  return REGDB_E_CLASSNOTREG;
}
__declspec(dllexport) HRESULT WINAPI RoActivateInstance(void *hs, void **inst) {
  void *f = NULL; HRESULT hr; if (!inst) return E_INVALIDARG; *inst = NULL;
  hr = get_factory(hs, &f);
  { static int trace = -1; if (trace < 0) trace = GetEnvironmentVariableA("WXR_LOGACT", NULL, 0) ? 1 : 0; if (trace) { slog("[rgaf] RoActivateInstance %ls -> factory 0x%08lX", p_raw(hs, NULL), (unsigned long)hr); } }
  if (SUCCEEDED(hr) && f) { void **vt = *(void ***)f; HRESULT (WINAPI *act)(void *, void **) = (void *)vt[6]; ULONG (WINAPI *rel)(void *) = (void *)vt[2]; hr = act(f, inst); rel(f); return hr; }
  /* the host (KxCom) as for RoGetActivationFactory: it says S_OK for classes it does not have, with no instance - App Installer's
     XmlDocument came back NULL and was dereferenced (AV). Success without an instance = class not registered; logged. */
  { static PRoAct h; if (!h) { HMODULE m = LoadLibraryA("api-ms-win-core-winrt-l1-1-0.dll"); if (m) h = (PRoAct)GetProcAddress(m, "RoActivateInstance"); }
    if (h) { hr = h(hs, inst); if (SUCCEEDED(hr) && !*inst) hr = REGDB_E_CLASSNOTREG; if (hr == E_NOINTERFACE) hr = REGDB_E_CLASSNOTREG;
      slog("[act] RoActivateInstance %ls -> HOST fallback 0x%08lX", p_raw(hs, NULL), (unsigned long)hr); return hr; } }
  return REGDB_E_CLASSNOTREG;
}
