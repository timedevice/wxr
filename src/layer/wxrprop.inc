/* wxrprop.inc - file properties (tags) for WXR apps, and no third-party in-proc COM in them. Included from wxrsys.c.
   1. Property handler: stock Win7 has no .flac property handler, and a third-party one (Icaros from K-Lite) crashed inside Media
      Player. Our class WxrMFPropertyHandler is only the seam Win7's registry can name: IInitializeWithStream / IInitializeWithFile
      create Win10's own "MF FLAC Media Property Handler" {C10E23A2} (msflacdecoder.dll from the image) and forward its IPropertyStore
      read-only. (The Win10 registration also sets EnableShareDenyNone - see SHManagedCreateStreamOnFile in wxrstream.inc.)
   2. Lookup: windows.storage asks propsys #444 PSLookupPropertyHandlerCLSIDAndPath (reads Win7's registry). It and
      PSLookupPropertyHandlerCLSID are wrapped (loader forced list), so the extensions in g_ph_ext resolve to our class whatever Win7's
      registry says.
   3. Third-party block (strict): an in-proc class whose DLL is outside %SystemRoot%, the WXR root and Common Files' Microsoft Shared /
      System folders is refused (REGDB_E_CLASSNOTREG) in WXR processes - shell extensions, property / thumbnail handlers and the like
      from other software do not load into Win10 apps. */
static const GUID CLSID_WxrMFPropertyHandler = {0x75570001, 0x7700, 0x4E2B, {0x9A, 0x3C, 0x55, 0x57, 0x70, 0x0F, 0x1A, 0xC0}};
static const WCHAR *const g_ph_ext[] = { L".flac", NULL };
static const WCHAR g_ph_clsid_str[] = L"{75570001-7700-4E2B-9A3C-5557700F1AC0}";
static const GUID PH_IID_IUnknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID PH_IID_IPropertyStore = {0x886D8EEB, 0x8CF2, 0x4446, {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};
static const GUID PH_IID_IInitializeWithStream = {0xB824B49D, 0x22AC, 0x4161, {0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F}};
static const GUID PH_IID_IInitializeWithFile = {0xB7D14566, 0x0509, 0x4CCE, {0xA7, 0x1F, 0x0A, 0x55, 0x42, 0x33, 0xBD, 0x9B}};
static const GUID PH_IID_IPropertyStoreCapabilities = {0xC8E2D566, 0x186E, 0x4D49, {0xBF, 0x41, 0x69, 0x09, 0xEA, 0xD5, 0x6A, 0xCC}};
#define PH_VT(o, k, T) ((T)(*(void ***)(o))[k])

/* ---- the handler ---- */
typedef struct { const void *vt_ps, *vt_iws, *vt_iwf, *vt_cap; LONG ref; IUnknown *ps; } PHObj;
#define PH_FROM(i, f) ((PHObj *)((BYTE *)(i) - offsetof(PHObj, f)))
static HRESULT ph_qi_(PHObj *o, REFIID iid, void **out) { if (!out) return E_POINTER; *out = NULL;
  if (IsEqualGUID(iid, &PH_IID_IUnknown) || IsEqualGUID(iid, &PH_IID_IPropertyStore)) *out = &o->vt_ps;
  else if (IsEqualGUID(iid, &PH_IID_IInitializeWithStream)) *out = &o->vt_iws; else if (IsEqualGUID(iid, &PH_IID_IInitializeWithFile)) *out = &o->vt_iwf;
  else if (IsEqualGUID(iid, &PH_IID_IPropertyStoreCapabilities)) *out = &o->vt_cap;
  else { static LONG n; char g[48]; guid_str(iid, g); if (InterlockedIncrement(&n) <= 30) slog("[props] handler %p QI %s refused", (void *)o, g); return E_NOINTERFACE; }
  { static LONG n; char g[48]; guid_str(iid, g); if (InterlockedIncrement(&n) <= 30) slog("[props] handler %p QI %s ok", (void *)o, g); }
  InterlockedIncrement(&o->ref); return S_OK; }
static ULONG ph_rl_(PHObj *o) { LONG n = InterlockedDecrement(&o->ref);
  if (!n) { slog("[props] handler %p released (initialized: %s)", (void *)o, o->ps ? "yes" : "no"); if (o->ps) o->ps->lpVtbl->Release(o->ps);
    HeapFree(GetProcessHeap(), 0, o); } return (ULONG)n; }
#define PH_IUNK(f, pre) static HRESULT STDMETHODCALLTYPE pre##_qi(void *t, REFIID i, void **o) { return ph_qi_(PH_FROM(t, f), i, o); } \
  static ULONG STDMETHODCALLTYPE pre##_ar(void *t) { return (ULONG)InterlockedIncrement(&PH_FROM(t, f)->ref); } static ULONG STDMETHODCALLTYPE pre##_rl(void *t) { return ph_rl_(PH_FROM(t, f)); }
PH_IUNK(vt_ps, phs) PH_IUNK(vt_iws, phw) PH_IUNK(vt_iwf, phf) PH_IUNK(vt_cap, phc)
/* Initialize: Win10's own property handler for the type does the work ("MF FLAC Media Property Handler" {C10E23A2} in msflacdecoder.dll,
   from the image through g_comcls); this class only routes to it - Win7's registry has no Win10 handler, and Win7's own .flac entry
   is a third-party one. Its IPropertyStore is forwarded read-only. */
static const GUID PH_CLSID_W10_FLAC = {0xC10E23A2, 0xBFF0, 0x4113, {0xBC, 0xE8, 0xC9, 0x1B, 0xE7, 0x54, 0xE2, 0x81}};
static HRESULT ph_open(PHObj *o, IStream *st, const WCHAR *name, DWORD mode) {
  IUnknown *iws = NULL, *ps = NULL; HRESULT hr; LARGE_INTEGER z; z.QuadPart = 0;
  if (o->ps) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
  hr = com_inproc(&PH_CLSID_W10_FLAC, NULL, &PH_IID_IInitializeWithStream, (void **)&iws, 0);
  if (SUCCEEDED(hr)) { st->lpVtbl->Seek(st, z, STREAM_SEEK_SET, NULL); hr = PH_VT(iws, 3, HRESULT (STDMETHODCALLTYPE *)(void *, IStream *, DWORD))(iws, st, mode); }
  if (SUCCEEDED(hr)) hr = iws->lpVtbl->QueryInterface(iws, &PH_IID_IPropertyStore, (void **)&ps);
  { static LONG n; DWORD c = 0; if (SUCCEEDED(hr) && ps) PH_VT(ps, 3, HRESULT (STDMETHODCALLTYPE *)(void *, DWORD *))(ps, &c);
    if (InterlockedIncrement(&n) <= 20 || FAILED(hr)) slog("[props] Win10 FLAC property handler %ls -> 0x%08lX (%lu properties)", name ? name : L"?", (unsigned long)hr, c); }
  if (iws) iws->lpVtbl->Release(iws);
  if (SUCCEEDED(hr) && ps) { o->ps = ps; return S_OK; }
  if (ps) ps->lpVtbl->Release(ps);
  return FAILED(hr) ? hr : E_FAIL; }
static HRESULT STDMETHODCALLTYPE phw_init(void *t, IStream *st, DWORD mode) { STATSTG s; HRESULT hr; slog("[props] handler %p IInitializeWithStream::Initialize(%p, mode 0x%lx)", (void *)PH_FROM(t, vt_iws), (void *)st, mode); if (!st) return E_INVALIDARG;
  memset(&s, 0, sizeof s); st->lpVtbl->Stat(st, &s, STATFLAG_DEFAULT); hr = ph_open(PH_FROM(t, vt_iws), st, s.pwcsName, mode); if (s.pwcsName) CoTaskMemFree(s.pwcsName); return hr; }
static HRESULT STDMETHODCALLTYPE phf_init(void *t, LPCWSTR path, DWORD mode) { slog("[props] handler %p IInitializeWithFile::Initialize(%ls)", (void *)PH_FROM(t, vt_iwf), path ? path : L"?");
  typedef HRESULT (WINAPI *PSF)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **); static PSF sf; IStream *st = NULL; HRESULT hr; (void)mode;
  if (!sf) sf = (PSF)GetProcAddress(LoadLibraryA("shlwapi.dll"), "SHCreateStreamOnFileEx"); if (!sf || !path) return E_INVALIDARG;
  if (FAILED(hr = sf(path, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, NULL, &st))) return hr;
  hr = ph_open(PH_FROM(t, vt_iwf), st, path, STGM_READ); st->lpVtbl->Release(st); return hr; }
static HRESULT STDMETHODCALLTYPE phs_count(void *t, DWORD *n) { PHObj *o = PH_FROM(t, vt_ps); if (!n) return E_POINTER; if (!o->ps) { *n = 0; return S_OK; } return PH_VT(o->ps, 3, HRESULT (STDMETHODCALLTYPE *)(void *, DWORD *))(o->ps, n); }
static HRESULT STDMETHODCALLTYPE phs_getat(void *t, DWORD i, void *key) { PHObj *o = PH_FROM(t, vt_ps); if (!o->ps) return E_BOUNDS; return PH_VT(o->ps, 4, HRESULT (STDMETHODCALLTYPE *)(void *, DWORD, void *))(o->ps, i, key); }
static HRESULT STDMETHODCALLTYPE phs_get(void *t, const void *key, void *pv) { PHObj *o = PH_FROM(t, vt_ps); HRESULT hr; static LONG n; char g[48];
  if (!pv) return E_POINTER; if (!o->ps) { memset(pv, 0, 24); hr = S_OK; }
  else hr = PH_VT(o->ps, 5, HRESULT (STDMETHODCALLTYPE *)(void *, const void *, void *))(o->ps, key, pv);
  if (InterlockedIncrement(&n) <= 40) { guid_str((const GUID *)key, g); slog("[props] GetValue(%s %lu) -> 0x%08lX vt %u%s", g, *(const DWORD *)((const BYTE *)key + 16), (unsigned long)hr, *(const WORD *)pv, o->ps ? "" : " (not initialized)"); }
  return hr; }
static HRESULT STDMETHODCALLTYPE phs_set(void *t, const void *key, const void *pv) { (void)t; (void)key; (void)pv; return STG_E_ACCESSDENIED; }
static HRESULT STDMETHODCALLTYPE phs_commit(void *t) { (void)t; return STG_E_ACCESSDENIED; }
static HRESULT STDMETHODCALLTYPE phc_writable(void *t, const void *key) { (void)t; (void)key; return S_FALSE; }
static const void *const g_ph_vt_ps[] = { phs_qi, phs_ar, phs_rl, phs_count, phs_getat, phs_get, phs_set, phs_commit };
static const void *const g_ph_vt_iws[] = { phw_qi, phw_ar, phw_rl, phw_init };
static const void *const g_ph_vt_iwf[] = { phf_qi, phf_ar, phf_rl, phf_init };
static const void *const g_ph_vt_cap[] = { phc_qi, phc_ar, phc_rl, phc_writable };
/* class factory (CoGetClassObject) for our CLSID */
static HRESULT ph_create(REFIID iid, void **out);
static HRESULT STDMETHODCALLTYPE phcf_qi(void *t, REFIID iid, void **o) { static const GUID icf = {0x00000001, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (!o) return E_POINTER; if (IsEqualGUID(iid, &PH_IID_IUnknown) || IsEqualGUID(iid, &icf)) { *o = t; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE phcf_ref(void *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE phcf_create(void *t, IUnknown *outer, REFIID iid, void **o) { (void)t; if (outer) return CLASS_E_NOAGGREGATION; return ph_create(iid, o); }
static HRESULT STDMETHODCALLTYPE phcf_lock(void *t, BOOL l) { (void)t; (void)l; return S_OK; }
static const void *const g_phcf_vt[] = { phcf_qi, phcf_ref, phcf_ref, phcf_create, phcf_lock }; static const void *const *g_phcf = g_phcf_vt;
/* when Win7's ole32 itself loads us through that registration (a caller that bypasses our COM hooks) */
__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **o) {
  if (!o) return E_POINTER; *o = NULL; if (!IsEqualGUID(clsid, &CLSID_WxrMFPropertyHandler)) return CLASS_E_CLASSNOTAVAILABLE;
  slog("[com] DllGetClassObject(WxrMFPropertyHandler)"); return phcf_qi((void *)&g_phcf, iid, o); }
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
/* Win10's shell checks the handler's registration before creating it (SHShouldCreateExtension -> _GetServerInfo reads
   CLSID\{x}\InprocServer32): our class is registered per user, pointing at wxrsys.dll. Only WXR's lookup ever names this CLSID. */
static void ph_register(void) {
  static LONG done; HKEY k; WCHAR key[96], self[MAX_PATH]; HMODULE m = NULL;
  if (InterlockedExchange(&done, 1)) return;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ph_register, &m); if (!m || !GetModuleFileNameW(m, self, MAX_PATH)) return;
  wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\InprocServer32", g_ph_clsid_str);
  if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
    RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)self, (DWORD)(lstrlenW(self) + 1) * sizeof(WCHAR));
    RegSetValueExW(k, L"ThreadingModel", 0, REG_SZ, (const BYTE *)L"Both", 10); RegCloseKey(k); slog("[props] WxrMFPropertyHandler registered for this user (%ls)", self); } }
static HRESULT ph_create(REFIID iid, void **out) {
  PHObj *o = (PHObj *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *o); HRESULT hr; if (!out) return E_POINTER; *out = NULL; if (!o) return E_OUTOFMEMORY;
  o->vt_ps = g_ph_vt_ps; o->vt_iws = g_ph_vt_iws; o->vt_iwf = g_ph_vt_iwf; o->vt_cap = g_ph_vt_cap; o->ref = 1;
  hr = ph_qi_(o, iid, out); ph_rl_(o); return hr; }

/* ---- lookup: windows.storage asks propsys #444 PSLookupPropertyHandlerCLSIDAndPath(path, CLSID *, DWORD *flags, PWSTR *dllPath)
   (CFileSysItemString::GetHandlerClsidAndFlags); others may use the documented PSLookupPropertyHandlerCLSID(path, CLSID *). Both are on
   the loader's forced list (wxrldr.inc), so WXR modules get these wrappers: the extensions in g_ph_ext -> our class, others -> the
   image's propsys. ---- */
static int ph_ext_ours(const WCHAR *path) { const WCHAR *e = path ? wcsrchr(path, L'.') : NULL; int i;
  if (!e || wcschr(e, L'\\')) return 0;
  for (i = 0; g_ph_ext[i]; i++) if (!lstrcmpiW(e, g_ph_ext[i])) return 1; return 0; }
__declspec(dllexport) HRESULT WINAPI PSLookupPropertyHandlerCLSIDAndPath(const WCHAR *path, GUID *clsid, DWORD *flags, WCHAR **dll) {
  typedef HRESULT (WINAPI *P)(const WCHAR *, GUID *, DWORD *, WCHAR **); static P real; HMODULE m;
  if (ph_ext_ours(path) && clsid && flags) { static LONG n; ph_register(); *clsid = CLSID_WxrMFPropertyHandler; *flags = 0; if (dll) *dll = NULL;
    if (InterlockedIncrement(&n) <= 10) slog("[props] property handler for %ls -> WxrMFPropertyHandler", path); return S_OK; }
  if (!real && (m = wxr_module_for("propsys.dll")) != NULL) real = (P)wxr_export(m, NULL, 444, 0);
  return real ? real(path, clsid, flags, dll) : E_NOTIMPL; }
__declspec(dllexport) HRESULT WINAPI PSLookupPropertyHandlerCLSID(const WCHAR *path, GUID *clsid) {
  typedef HRESULT (WINAPI *P)(const WCHAR *, GUID *); static P real; HMODULE m;
  if (ph_ext_ours(path) && clsid) { ph_register(); *clsid = CLSID_WxrMFPropertyHandler; return S_OK; }
  if (!real && (m = wxr_module_for("propsys.dll")) != NULL) real = (P)wxr_export(m, "PSLookupPropertyHandlerCLSID", 0, 0);
  return real ? real(path, clsid) : E_NOTIMPL; }
/* ---- third-party in-proc COM (strict) ---- */
static int com_dir_allowed(const WCHAR *path) {
  WCHAR d[MAX_PATH]; int n;
  if (!path || !*path) return 1;
  if (!wcschr(path, L'\\')) return 1;   /* bare name: found on the system search path */
  if ((n = (int)GetSystemWindowsDirectoryW(d, MAX_PATH)) > 0 && !_wcsnicmp(path, d, n)) return 1;
  if (g_wxr_root[0] && !_wcsnicmp(path, g_wxr_root, lstrlenW(g_wxr_root))) return 1;
  if ((n = (int)GetEnvironmentVariableW(L"CommonProgramFiles", d, MAX_PATH)) > 0 && n < MAX_PATH - 20 && !_wcsnicmp(path, d, n)) {
    const WCHAR *r = path + n; if (!_wcsnicmp(r, L"\\Microsoft Shared\\", 18) || !_wcsnicmp(r, L"\\System\\", 8)) return 1; }
  return 0; }
static int com_blocked(REFCLSID clsid) {
  static struct { GUID c; int b; } cache[64]; static int nc; static SRWLOCK lk = SRWLOCK_INIT; WCHAR key[80], raw[MAX_PATH], path[MAX_PATH]; DWORD cb = sizeof raw, ty; char g[48]; int i, b = 0;
  if (!g_wxr_on) return 0;
  AcquireSRWLockShared(&lk); for (i = 0; i < nc; i++) if (IsEqualGUID(&cache[i].c, clsid)) { b = cache[i].b; ReleaseSRWLockShared(&lk); return b; } ReleaseSRWLockShared(&lk);
  guid_str(clsid, g); wsprintfW(key, L"CLSID\\%S\\InprocServer32", g);
  if (RegGetValueW(HKEY_CLASSES_ROOT, key, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &ty, raw, &cb) == ERROR_SUCCESS) {
    ExpandEnvironmentStringsW(raw, path, MAX_PATH); { WCHAR *s = path; if (*s == L'"') { s++; if (wcschr(s, L'"')) *wcschr(s, L'"') = 0; memmove(path, s, (lstrlenW(s) + 1) * sizeof(WCHAR)); } }
    b = !com_dir_allowed(path);
    if (b) slog("[com] third-party class %s (%ls) refused in a WXR app", g, path); }
  AcquireSRWLockExclusive(&lk); if (nc < 64) { cache[nc].c = *clsid; cache[nc].b = b; nc++; } ReleaseSRWLockExclusive(&lk);
  return b; }
