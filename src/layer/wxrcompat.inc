/* wxrcompat.inc - Win10 behaviour of Win7 APIs: strings, geo ids, system folders, resources, timers, registry query sources. Included by wxrsys.c. */
/* WindowsDuplicateString: a duplicate must outlive a fast-pass (reference) source, whose buffer belongs to the caller. With KxCom's, XAML's
   copy of a string handed over by reference (the Community Toolkit's DefaultStyleResourceUri text, from .NET) pointed at freed memory: the
   style URI arrived as two characters of garbage, failed to parse, and the SettingsCard / SettingsExpander templates never loaded. Here every
   duplicate is a real copy (an extra allocation for strings that were already heap strings; correct either way). */
/* WindowsIsStringEmpty: KxCom's is unimplemented and answers FALSE, so Win10 windows.storage took its empty file-type filter for a list,
   parsed one empty extension and failed every StorageFile.GetFileFromPathAsync with E_INVALIDARG. NULL or zero length = empty. */
__declspec(dllexport) BOOL WINAPI WindowsIsStringEmpty(void *s)
{
  UINT32 n = 0; if (!s) return TRUE; if (!hs_init()) return TRUE; p_raw(s, &n); return n == 0;
}
__declspec(dllexport) HRESULT WINAPI WindowsDuplicateString(void *s, void **out)
{
  UINT32 n = 0; const WCHAR *b;
  if (!out) return E_INVALIDARG; *out = NULL; if (!s) return S_OK; if (!hs_init()) return E_FAIL;
  b = p_raw(s, &n); return n ? p_create(b, n, out) : S_OK;
}
/* WindowsConcatString: KxCom's copies the first string's length in bytes, not characters ('StandardMode' + 'Text' -> 'Standa' + garbage), so
   every C++/CX `key + "Text"` resource lookup in Calculator came back empty (menu item names, the mode header). */
__declspec(dllexport) HRESULT WINAPI WindowsConcatString(void *s1, void *s2, void **out)
{
  UINT32 n1 = 0, n2 = 0; const WCHAR *a, *b; WCHAR *t; HRESULT hr;
  if (!out) return E_INVALIDARG; *out = NULL; if (!hs_init()) return E_FAIL;
  a = s1 ? p_raw(s1, &n1) : L""; b = s2 ? p_raw(s2, &n2) : L"";
  if (!n1 && !n2) return S_OK;                                        /* empty + empty = the empty (NULL) HSTRING */
  if ((UINT64)n1 + n2 > 0x7FFFFFFF) return E_OUTOFMEMORY;
  t = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)n1 + n2) * sizeof(WCHAR)); if (!t) return E_OUTOFMEMORY;
  memcpy(t, a, (SIZE_T)n1 * sizeof(WCHAR)); memcpy(t + n1, b, (SIZE_T)n2 * sizeof(WCHAR));
  hr = p_create(t, n1 + n2, out); HeapFree(GetProcessHeap(), 0, t); return hr;
}
/* EnumSystemGeoID(GEOCLASS_ALL = 0, ...) is Win10 1709+; Win7 knows GEOCLASS_REGION (14) and GEOCLASS_NATION (16) only.
   Windows.Globalization's GeographicRegion enumerates class 0 (GlobalizationPreferences.HomeGeographicRegion threw E_INVALIDARG). */
/* Win7 also requires ParentGeoId = 0: "World" (39070) means everything; other parents are filtered through GetGeoInfoW(GEO_PARENT) ancestry */
static __declspec(thread) GEOID t_geo_parent; static __declspec(thread) GEO_ENUMPROC t_geo_proc; static __declspec(thread) BOOL t_geo_stop;
static BOOL geo_is_descendant(GEOID g, GEOID anc) {
  WCHAR b[32]; int k; for (k = 0; k < 8; k++) { if (!GetGeoInfoW(g, GEO_PARENT, b, 32, 0)) return FALSE; g = (GEOID)_wtoi(b); if (g == anc) return TRUE; if (!g || g == 39070) return FALSE; } return FALSE; }
static BOOL CALLBACK geo_filter(GEOID g) { if (t_geo_stop) return FALSE; if (geo_is_descendant(g, t_geo_parent) && !t_geo_proc(g)) { t_geo_stop = TRUE; return FALSE; } return TRUE; }
__declspec(dllexport) BOOL WINAPI EnumSystemGeoID(GEOCLASS cls, GEOID parent, GEO_ENUMPROC proc) {
  typedef BOOL (WINAPI *F)(GEOCLASS, GEOID, GEO_ENUMPROC); static F f; GEO_ENUMPROC use = proc; BOOL r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "EnumSystemGeoID"); if (!f) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
  if (parent == 39070) parent = 0;
  if (parent) { t_geo_parent = parent; t_geo_proc = proc; t_geo_stop = FALSE; use = geo_filter; }
  r = f(GEOCLASS_NATION, 0, use);   /* Win7 enumerates GEOCLASS_NATION only (REGION/ALL -> ERROR_INVALID_FLAGS); nations are what region lookups need */
  { DWORD le = GetLastError(); static volatile LONG nl; if (InterlockedIncrement(&nl) <= 6) slog("[geo] EnumSystemGeoID(class %d, parent %ld) -> %d (err %lu)", (int)cls, (long)parent, r, r ? 0 : le); SetLastError(le); }
  return r; }
/* GetSystemDirectoryW: WinTypes (ApiInformation, RoGetMetaDataFile) reads Windows' metadata from <system dir>\WinMetadata\, which Win7
   does not have, so ApiInformation answered "absent" to every type and contract and WinUI fell back to its pre-RS1 code paths (NavigationView
   header row: the UI sat 80 px low). For WinTypes only, the system directory is the image's System32, which holds Win10's WinMetadata.
   Every other caller gets the real one. */
static const WCHAR *wm_root(void) {
  static WCHAR root[MAX_PATH]; static LONG done; WCHAR t[MAX_PATH + 32];
  if (InterlockedExchange(&done, 1)) return root[0] ? root : NULL;
  lstrcpynW(root, g_wxr_img, MAX_PATH);   /* <root>\Windows\System32\WinMetadata */
  lstrcpyW(t, root); lstrcatW(t, L"\\WinMetadata"); if (!root[0] || GetFileAttributesW(t) == INVALID_FILE_ATTRIBUTES) { slog("[winmd] no WinMetadata under '%ls'; ApiInformation stays blind", root); root[0] = 0; return NULL; }
  slog("[winmd] WinTypes reads Windows metadata from %ls\\WinMetadata", root); return root; }
/* identifies a module by the name in its export directory */
static int mod_export_name_is(HMODULE hm, const char *name) {
  BYTE *b = (BYTE *)hm; IMAGE_NT_HEADERS *nt; IMAGE_DATA_DIRECTORY *d;
  if (!b || ((IMAGE_DOS_HEADER *)b)->e_magic != IMAGE_DOS_SIGNATURE) return 0; nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
  d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]; if (!d->VirtualAddress || !d->Size) return 0;
  return !lstrcmpiA((const char *)(b + ((IMAGE_EXPORT_DIRECTORY *)(b + d->VirtualAddress))->Name), name); }
__declspec(dllexport) UINT WINAPI GetSystemDirectoryW(LPWSTR buf, UINT cch) {
  typedef UINT (WINAPI *F)(LPWSTR, UINT); static F real; static HMODULE wintypes; HMODULE from = NULL; const WCHAR *r; UINT n;
  if (!real) real = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemDirectoryW");
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &from) && from
      && (from == wintypes || (!wintypes && mod_export_name_is(from, "WinTypes.dll") && (wintypes = from))) && (r = wm_root())) {
    n = (UINT)lstrlenW(r); if (!buf || cch <= n) return n + 1; lstrcpyW(buf, r); return n; }
  return real(buf, cch); }
/* GetSystemWindowsDirectoryW: Win10's icu.dll loads its data from <windows dir>\Globalization\ICU\icudtl.dat, which Win7 lacks; without it
   ucal_getTimeZoneIDForWindowsID finds nothing and Windows.Globalization throws E_INVALIDARG "timeZoneId" for every Calendar /
   DateTimeFormatter (Date calculation and the converters did nothing). For icu.dll only, the Windows directory is the folder holding
   Win10's Globalization\ICU: the image's <root>\Windows. */
__declspec(dllexport) UINT WINAPI GetSystemWindowsDirectoryW(LPWSTR buf, UINT cch) {
  typedef UINT (WINAPI *F)(LPWSTR, UINT); static F real; static HMODULE icu; static WCHAR root[MAX_PATH]; static LONG init; HMODULE from = NULL; UINT n;
  if (!real) real = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemWindowsDirectoryW");
  if (!InterlockedExchange(&init, 1)) {
    WCHAR t[MAX_PATH + 48];
    _snwprintf(root, MAX_PATH, L"%ls\\Windows", g_wxr_root);   /* <root>\Windows\Globalization\ICU */
    lstrcpyW(t, root); lstrcatW(t, L"\\Globalization\\ICU\\icudtl.dat");
    if (!root[0] || GetFileAttributesW(t) == INVALID_FILE_ATTRIBUTES) { slog("[icu] no %ls; ICU stays without data", t); root[0] = 0; } else slog("[icu] icu.dll reads its data from %ls", t);
  }
  if (root[0] && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &from) && from
      && (from == icu || (!icu && mod_export_name_is(from, "icu.dll") && (icu = from)))) {
    n = (UINT)lstrlenW(root); if (!buf || cch <= n) return n + 1; lstrcpyW(buf, root); return n; }
  return real(buf, cch); }
/* the .mun resource file of an image module: <root>\Windows\SystemResources\<name>.mun (else its en-US .mui), loaded as an image-resource
   datafile once per module; NULL for modules that are not the image's or have neither */
static HMODULE g_mun[256];   /* the .mun / .mui datafile mappings opened by mun_module (handles carry the datafile flag bits) */
static HMODULE mun_module(HMODULE mod) {
  HMODULE *mun = g_mun; static BYTE tried[256]; WCHAR mp[MAX_PATH]; int i;
  if (!mod || !GetModuleFileNameW(mod, mp, MAX_PATH)) return NULL;
  alock(1); act_init(); alock(0);
  for (i = 0; i < g_ndll; i++) if (!lstrcmpiW(g_dllpath[i], mp)) break;
  if (i == g_ndll) return NULL;
  if (!tried[i]) { WCHAR p[MAX_PATH]; tried[i] = 1;
    _snwprintf(p, MAX_PATH, L"%ls\\Windows\\SystemResources\\%ls.mun", g_wxr_root, g_dllname[i]);   /* the image's SystemResources */
    mun[i] = LoadLibraryExW(p, NULL, 0x20 /* LOAD_LIBRARY_AS_IMAGE_RESOURCE */ | 0x2 /* AS_DATAFILE */);
    if (!mun[i]) { _snwprintf(p, MAX_PATH, L"%ls\\en-US\\%ls.mui", g_wxr_img, g_dllname[i]); mun[i] = LoadLibraryExW(p, NULL, 0x20 | 0x2); }   /* no .mun: the language resources */
    slog("[res] %ls -> %ls %s", g_dllname[i], p, mun[i] ? "loaded" : "absent"); }
  return mun[i]; }
/* Resource lookups of image modules fall back to their .mun (Win10 1903+ keeps strings, XML, ... there): windows.storage's
   GetSearchFolderName LoadStringW failed -> E_FAIL -> no search item -> folder queries E_NOINTERFACE ("Open folder" crash).
   LoadResource / SizeofResource take an HRSRC from the .mun with the original module handle, so they map it to the .mun module. */
/* the module an HRSRC belongs to. Datafile / image-resource mappings (our .mun files) are not in the loader's list, so GetModuleHandleEx
   does not find them: they are matched by address range against the mappings mun_module opened. Without this, LoadResource got the
   .mun's HRSRC with the original module handle: ERROR_BAD_EXE_FORMAT - Win10 d2d1 could not read its effect descriptions from
   d2d1.dll.mun, so EVERY Direct2D effect failed to be created (WinUI 3: rounded fills, nine-grids, masks, acrylic all missing). */
static HMODULE mun_owner_of(const void *p) {
  HMODULE m = NULL; int i;
  if (!p) return NULL;
  for (i = 0; i < 256; i++) if (g_mun[i]) { BYTE *b = (BYTE *)((ULONG_PTR)g_mun[i] & ~(ULONG_PTR)3); IMAGE_DOS_HEADER *dh = (IMAGE_DOS_HEADER *)b; SIZE_T sz = 0;
    if (dh->e_magic == IMAGE_DOS_SIGNATURE) { IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + dh->e_lfanew); if (nt->Signature == IMAGE_NT_SIGNATURE) sz = nt->OptionalHeader.SizeOfImage; }
    if (!sz) { MEMORY_BASIC_INFORMATION mi; if (VirtualQuery(b, &mi, sizeof mi)) { BYTE *q = b; SIZE_T tot = 0; while (VirtualQuery(q, &mi, sizeof mi) && mi.AllocationBase == (void *)b) { tot += mi.RegionSize; q += mi.RegionSize; } sz = tot; } }
    if ((const BYTE *)p >= b && (const BYTE *)p < b + sz) return g_mun[i]; }
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m)) return m;
  return NULL; }
static HMODULE mun_module(HMODULE mod);
__declspec(dllexport) HRSRC WINAPI WxrFindResourceExW(HMODULE mod, const WCHAR *type, const WCHAR *name, WORD lang) {
  HRSRC r = FindResourceExW(mod, type, name, lang); HMODULE mun;
  if (!r && mod && (mun = mun_module(mod)) != NULL) r = FindResourceExW(mun, type, name, lang);
  return r; }
__declspec(dllexport) HRSRC WINAPI WxrFindResourceW(HMODULE mod, const WCHAR *name, const WCHAR *type) {
  HRSRC r = FindResourceW(mod, name, type); HMODULE mun;
  if (!r && mod && (mun = mun_module(mod)) != NULL) r = FindResourceW(mun, name, type);
  return r; }
__declspec(dllexport) HGLOBAL WINAPI WxrLoadResource(HMODULE mod, HRSRC r) { HMODULE o = mun_owner_of(r); return LoadResource(o && o != mod ? o : mod, r); }
__declspec(dllexport) DWORD WINAPI WxrSizeofResource(HMODULE mod, HRSRC r) { HMODULE o = mun_owner_of(r); return SizeofResource(o && o != mod ? o : mod, r); }
__declspec(dllexport) int WINAPI WxrLoadStringW(HINSTANCE mod, UINT id, WCHAR *buf, int n) {
  int r = LoadStringW(mod, id, buf, n); HMODULE mun;
  if (!r && mod && (mun = mun_module(mod)) != NULL) r = LoadStringW(mun, id, buf, n);
  return r; }
/* SHCreateStreamOnModuleResourceW (shcore #109, Win8+): a read-only memory stream over a module resource. Win10 propsys loads its system
   property schema this way (CSchemaCache::_AddResourceToPropertyCache); the unimplemented thunk left it a NULL stream -> crash in xmllite,
   no Win10 property schema at all. */
__declspec(dllexport) HRESULT WINAPI SHCreateStreamOnModuleResourceW(HMODULE mod, const WCHAR *name, const WCHAR *type, IStream **out) {
  typedef IStream *(WINAPI *PMS)(const BYTE *, UINT); static PMS ms; HRSRC r; HGLOBAL g; DWORD n; const BYTE *p; WCHAR mn[MAX_PATH] = L"?";
  if (!out) return E_POINTER; *out = NULL;
  if (!ms) ms = (PMS)GetProcAddress(LoadLibraryA("shlwapi.dll"), MAKEINTRESOURCEA(12));   /* SHCreateMemStream */
  GetModuleFileNameW(mod, mn, MAX_PATH);
  r = FindResourceW(mod, name, type);
  if (!r) {   /* Win10 1903+: resources moved to %windir%\SystemResources\<module>.mun (Win7's loader does not look there) */
    HMODULE mun = mun_module(mod); if (mun && (r = FindResourceW(mun, name, type)) != NULL) mod = mun; }
  slog("[res] SHCreateStreamOnModuleResourceW(%ls, %ls%ls, type %ls%ls) -> %s", wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, IS_INTRESOURCE(name) ? L"#" : name, IS_INTRESOURCE(name) ? L"id" : L"",
       IS_INTRESOURCE(type) ? L"#" : type, IS_INTRESOURCE(type) ? L"id" : L"", r ? "found" : "NOT FOUND");
  if (!r) return HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_RESOURCE_NAME_NOT_FOUND);
  if (!(g = LoadResource(mod, r)) || !(p = (const BYTE *)LockResource(g))) return E_FAIL;
  n = SizeofResource(mod, r); *out = ms ? ms(p, n) : NULL;
  return *out ? S_OK : E_OUTOFMEMORY; }
/* IShellFolder_GetDisplayNameOf (shlwapi helper, unnamed ordinal on Win7): IShellFolder::GetDisplayNameOf, retrying FORADDRESSBAR
   without that flag as shlwapi does. Reached once Win10 windows.storage serves the shell namespace API set. */
__declspec(dllexport) HRESULT WINAPI IShellFolder_GetDisplayNameOf(void *sf, const void *pidl, DWORD flags, void *out) {   /* IShellFolder slot 11 */
  typedef HRESULT (STDMETHODCALLTYPE *F)(void *, const void *, DWORD, void *); HRESULT hr; if (!sf) return E_INVALIDARG;
  hr = ((F)(*(void ***)sf)[11])(sf, pidl, flags, out);
  if (FAILED(hr) && (flags & 0x4000 /* SHGDN_FORADDRESSBAR */)) hr = ((F)(*(void ***)sf)[11])(sf, pidl, flags & ~0x4000u, out);
  return hr; }
/* SetThreadpoolTimer: Win10 rtworkq arms its timers (MFScheduleWorkItem, the audio renderer's 100 ms refill) with msWindowLength
   0xFFFFFFFF. Win7 treats the window as coalescing tolerance and postponed those timers indefinitely: the SAR played its 200 ms preroll
   and never refilled. Windows above 1 s are dropped to 0. */
__declspec(dllexport) VOID WINAPI SetThreadpoolTimer(PTP_TIMER timer, PFILETIME due, DWORD period, DWORD win) {
  typedef VOID (WINAPI *F)(PTP_TIMER, PFILETIME, DWORD, DWORD); static F real;
  if (!real) real = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadpoolTimer");
  if (win > 1000) win = 0;
  real(timer, due, period, win); }
/* CreateWaitableTimerExW: CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (2, Win10 1803+) is rejected by Win7 (Win10 rtworkq: RtwqStartup failed
   with E_OUTOFMEMORY, so MFStartup did). Dropped: an ordinary timer, coarser resolution. */
__declspec(dllexport) HANDLE WINAPI CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access) {
  typedef HANDLE (WINAPI *F)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD); static F real;
  if (!real) real = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateWaitableTimerExW");
  return real ? real(sa, name, flags & ~2u, access) : NULL; }
/* QuerySourceCreateFromKey (shcore, Win8+): an IQuerySource over a registry key. Win10 windows.storage's file-association code keeps
   one per association element and calls it without checking (the stub's NULL crashed StorageFile/shell parsing). Plain registry calls.
   IQuerySource: EnumValues, EnumSources, QueryValueString, QueryValueDword, QueryValueExists, QueryValueDirect, OpenSource, SetValueDirect. */
static const GUID IID_IQuerySource_ = {0xc7478486, 0x7583, 0x49e7, {0xa6, 0xc2, 0xfa, 0xf8, 0xf0, 0x2b, 0xc3, 0x0e}};
static const GUID IID_IEnumString_ = {0x00000101, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IUnknown_qs = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
typedef struct { const void *vt; LONG ref; HKEY key; } QSrc;
typedef struct { const void *vt; LONG ref; WCHAR **s; ULONG n, pos; } QEnum;
static ULONG STDMETHODCALLTYPE qe_addref(QEnum *e) { return InterlockedIncrement(&e->ref); }
static ULONG STDMETHODCALLTYPE qe_release(QEnum *e) { LONG r = InterlockedDecrement(&e->ref); ULONG i; if (!r) { for (i = 0; i < e->n; i++) free(e->s[i]); free(e->s); free(e); } return r; }
static HRESULT STDMETHODCALLTYPE qe_qi(QEnum *e, REFIID iid, void **o) { if (IsEqualGUID(iid, &IID_IUnknown_qs) || IsEqualGUID(iid, &IID_IEnumString_)) { *o = e; qe_addref(e); return S_OK; } *o = NULL; return E_NOINTERFACE; }
static HRESULT STDMETHODCALLTYPE qe_next(QEnum *e, ULONG c, LPOLESTR *out, ULONG *got) {
  ULONG k = 0; for (; k < c && e->pos < e->n; k++, e->pos++) { size_t l = (wcslen(e->s[e->pos]) + 1) * 2; out[k] = (LPOLESTR)CoTaskMemAlloc(l); if (!out[k]) break; memcpy(out[k], e->s[e->pos], l); }
  if (got) *got = k; return k == c ? S_OK : S_FALSE; }
static HRESULT STDMETHODCALLTYPE qe_skip(QEnum *e, ULONG c) { e->pos = e->pos + c > e->n ? e->n : e->pos + c; return e->pos < e->n ? S_OK : S_FALSE; }
static HRESULT STDMETHODCALLTYPE qe_reset(QEnum *e) { e->pos = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE qe_clone(QEnum *e, void **o) { (void)e; *o = NULL; return E_NOTIMPL; }
static const void *const qe_vt[] = { (void *)qe_qi, (void *)qe_addref, (void *)qe_release, (void *)qe_next, (void *)qe_skip, (void *)qe_reset, (void *)qe_clone };
static HRESULT qe_make(HKEY k, int values, void **out) {
  QEnum *e = (QEnum *)calloc(1, sizeof *e); WCHAR nm[512]; DWORD i, l;
  if (!e) return E_OUTOFMEMORY; e->vt = qe_vt; e->ref = 1;
  for (i = 0; ; i++) { LONG r; WCHAR **ns; l = 512;
    r = values ? RegEnumValueW(k, i, nm, &l, NULL, NULL, NULL, NULL) : RegEnumKeyExW(k, i, nm, &l, NULL, NULL, NULL, NULL);
    if (r != ERROR_SUCCESS) break; ns = (WCHAR **)realloc(e->s, (e->n + 1) * sizeof *ns); if (!ns) break; e->s = ns; e->s[e->n++] = _wcsdup(nm); }
  *out = e; return S_OK; }
static HKEY qs_sub(QSrc *q, LPCWSTR sub, HKEY *tmp) { *tmp = NULL; if (!sub || !*sub) return q->key; if (RegOpenKeyExW(q->key, sub, 0, KEY_READ, tmp) != ERROR_SUCCESS) return NULL; return *tmp; }
static ULONG STDMETHODCALLTYPE qs_addref(QSrc *q) { return InterlockedIncrement(&q->ref); }
static ULONG STDMETHODCALLTYPE qs_release(QSrc *q) { LONG r = InterlockedDecrement(&q->ref); if (!r) { RegCloseKey(q->key); free(q); } return r; }
static HRESULT STDMETHODCALLTYPE qs_qi(QSrc *q, REFIID iid, void **o) { if (IsEqualGUID(iid, &IID_IUnknown_qs) || IsEqualGUID(iid, &IID_IQuerySource_)) { *o = q; qs_addref(q); return S_OK; } *o = NULL; return E_NOINTERFACE; }
static HRESULT STDMETHODCALLTYPE qs_enumvalues(QSrc *q, void **o) { return qe_make(q->key, 1, o); }
static HRESULT STDMETHODCALLTYPE qs_enumsources(QSrc *q, void **o) { return qe_make(q->key, 0, o); }
static HRESULT STDMETHODCALLTYPE qs_string(QSrc *q, LPCWSTR sub, LPCWSTR val, LPWSTR *out) {
  HKEY t, k = qs_sub(q, sub, &t); DWORD ty = 0, cb = 0; LONG r; WCHAR *b; *out = NULL;
  if (!k) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  r = RegQueryValueExW(k, val, NULL, &ty, NULL, &cb);
  if (r == ERROR_SUCCESS && (ty == REG_SZ || ty == REG_EXPAND_SZ) && (b = (WCHAR *)CoTaskMemAlloc(cb + 4)) != NULL) {
    r = RegQueryValueExW(k, val, NULL, &ty, (BYTE *)b, &cb); b[cb / 2] = 0;
    if (r == ERROR_SUCCESS && ty == REG_EXPAND_SZ) { DWORD n = ExpandEnvironmentStringsW(b, NULL, 0); WCHAR *x = n ? (WCHAR *)CoTaskMemAlloc(n * 2) : NULL; if (x) { ExpandEnvironmentStringsW(b, x, n); CoTaskMemFree(b); b = x; } }
    if (r == ERROR_SUCCESS) *out = b; else CoTaskMemFree(b);
  } else if (r == ERROR_SUCCESS) r = ERROR_DATATYPE_MISMATCH;
  if (t) RegCloseKey(t); return HRESULT_FROM_WIN32(r); }
static HRESULT STDMETHODCALLTYPE qs_dword(QSrc *q, LPCWSTR sub, LPCWSTR val, DWORD *out) {
  HKEY t, k = qs_sub(q, sub, &t); DWORD ty = 0, cb = 4; LONG r; *out = 0;
  if (!k) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  r = RegQueryValueExW(k, val, NULL, &ty, (BYTE *)out, &cb); if (r == ERROR_SUCCESS && ty != REG_DWORD) r = ERROR_DATATYPE_MISMATCH;
  if (t) RegCloseKey(t); return HRESULT_FROM_WIN32(r); }
static HRESULT STDMETHODCALLTYPE qs_exists(QSrc *q, LPCWSTR sub, LPCWSTR val) {
  HKEY t, k = qs_sub(q, sub, &t); LONG r;
  if (!k) return S_FALSE;
  r = RegQueryValueExW(k, val, NULL, NULL, NULL, NULL);
  if (t) RegCloseKey(t); return r == ERROR_SUCCESS ? S_OK : S_FALSE; }
static HRESULT STDMETHODCALLTYPE qs_direct(QSrc *q, LPCWSTR sub, LPCWSTR val, void **blob) {   /* FLAGGED_BYTE_BLOB { ULONG fFlags (type); ULONG clSize; BYTE abData[] } */
  HKEY t, k = qs_sub(q, sub, &t); DWORD ty = 0, cb = 0; LONG r; ULONG *b; *blob = NULL;
  if (!k) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  r = RegQueryValueExW(k, val, NULL, &ty, NULL, &cb);
  if (r == ERROR_SUCCESS) { b = (ULONG *)CoTaskMemAlloc(8 + cb); if (!b) r = ERROR_OUTOFMEMORY; else { r = RegQueryValueExW(k, val, NULL, &ty, (BYTE *)(b + 2), &cb); b[0] = ty; b[1] = cb; if (r == ERROR_SUCCESS) *blob = b; else CoTaskMemFree(b); } }
  if (t) RegCloseKey(t); return HRESULT_FROM_WIN32(r); }
HRESULT WINAPI QuerySourceCreateFromKey(HKEY hk, LPCWSTR sub, BOOL create, REFIID iid, void **out);
static HRESULT STDMETHODCALLTYPE qs_open(QSrc *q, LPCWSTR sub, BOOL create, void **out) { return QuerySourceCreateFromKey(q->key, sub, create, &IID_IQuerySource_, out); }
static HRESULT STDMETHODCALLTYPE qs_set(QSrc *q, LPCWSTR sub, LPCWSTR val, ULONG ty, DWORD cb, const BYTE *data) {
  HKEY k = q->key, t = NULL; LONG r;
  if (sub && *sub) { r = RegCreateKeyExW(q->key, sub, 0, NULL, 0, KEY_WRITE, NULL, &t, NULL); if (r) return HRESULT_FROM_WIN32(r); k = t; }
  r = RegSetValueExW(k, val, 0, ty, data, cb); if (t) RegCloseKey(t); return HRESULT_FROM_WIN32(r); }
static const void *const qs_vt[] = { (void *)qs_qi, (void *)qs_addref, (void *)qs_release, (void *)qs_enumvalues, (void *)qs_enumsources, (void *)qs_string, (void *)qs_dword,
                                     (void *)qs_exists, (void *)qs_direct, (void *)qs_open, (void *)qs_set };
/* Ex (shcore #127): (hkey, subkey, create, extra REGSAM such as KEY_WOW64_32KEY, riid, ppv) - from windows.storage's call sites */
__declspec(dllexport) HRESULT WINAPI QuerySourceCreateFromKeyEx(HKEY hk, LPCWSTR sub, BOOL create, REGSAM sam, REFIID iid, void **out) {
  HKEY k = NULL; LONG r; QSrc *q; HRESULT hr;
  if (!out) return E_POINTER; *out = NULL;
  r = create ? RegCreateKeyExW(hk, sub ? sub : L"", 0, NULL, 0, KEY_READ | KEY_WRITE | sam, NULL, &k, NULL) : RegOpenKeyExW(hk, sub ? sub : L"", 0, KEY_READ | sam, &k);
  if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
  if (!(q = (QSrc *)calloc(1, sizeof *q))) { RegCloseKey(k); return E_OUTOFMEMORY; }
  q->vt = qs_vt; q->ref = 1; q->key = k; hr = qs_qi(q, iid, out); qs_release(q); return hr; }
__declspec(dllexport) HRESULT WINAPI QuerySourceCreateFromKey(HKEY hk, LPCWSTR sub, BOOL create, REFIID iid, void **out) {
  HKEY k = NULL; LONG r; QSrc *q; HRESULT hr;
  if (!out) return E_POINTER; *out = NULL;
  r = create ? RegCreateKeyExW(hk, sub ? sub : L"", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &k, NULL) : RegOpenKeyExW(hk, sub ? sub : L"", 0, KEY_READ, &k);
  if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
  if (!(q = (QSrc *)calloc(1, sizeof *q))) { RegCloseKey(k); return E_OUTOFMEMORY; }
  q->vt = qs_vt; q->ref = 1; q->key = k; hr = qs_qi(q, iid, out); qs_release(q); return hr; }
/* SHCoCreateInstance (api-ms-win-shell-shellcom): Win10's worker in windows.storage serves classes registered to shell32.dll /
   windows.storage.dll from windows.storage itself. Win7's (the host) built Win7 shell32 folders instead, so a path parsed by Win10
   windows.storage ended in Win7's file-system folder and Win7 shell items, which do not know BHID_RandomAccessStream:
   StorageFile.OpenAsync failed with MK_E_NOOBJECT (Media Player: "0x800401E5" for every file). Win7's stays the fallback. */
__declspec(dllexport) HRESULT WINAPI SHCoCreateInstance(LPCWSTR scls, const CLSID *clsid, IUnknown *outer, REFIID iid, void **ppv)
{
  typedef HRESULT (WINAPI *F)(LPCWSTR, const CLSID *, IUnknown *, REFIID, void **); static F w10, w7; static LONG init; HRESULT hr = E_FAIL; int d;
  if (!InterlockedExchange(&init, 1)) {
    alock(1); act_init(); alock(0);
    for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"windows.storage.dll")) { HMODULE m = act_load(d); if (m) w10 = (F)GetProcAddress(m, "SHCoCreateInstanceWorker"); break; }
    { HMODULE m = LoadLibraryA("shell32.dll"); if (m) w7 = (F)GetProcAddress(m, "SHCoCreateInstance"); }
  }
  if (ppv) *ppv = NULL;
  { char g[48]; if (clsid) guid_str(clsid, g); else lstrcpyA(g, "(string)"); hr = w10 ? w10(scls, clsid, outer, iid, ppv) : E_FAIL; slog("[com] SHCoCreateInstance(%s) Win10 -> 0x%08lX", g, (unsigned long)hr); }
  if (SUCCEEDED(hr)) return hr;
  if (w7) { HRESULT h7 = w7(scls, clsid, outer, iid, ppv); if (SUCCEEDED(h7) || !w10) return h7; }
  return hr;
}
/* RtlIsMultiUsersInSessionSku (ntdll, Win10 multi-session SKUs): FALSE. The stub's non-zero result sent Win10 windows.storage down its
   multi-user path (CapturedCallerContext -> a user-manager service: 0x80070426 for every StorageFile.GetFileFromPathAsync). */
__declspec(dllexport) BOOLEAN WINAPI RtlIsMultiUsersInSessionSku(void) { return FALSE; }
/* Win10 helpers on Windows.Storage's StorageFile path (generated thunks returned E_NOTIMPL, whose low byte reads as TRUE / mode 1). */
__declspec(dllexport) BOOLEAN WINAPI RtlAreLongPathsEnabled(void) { return FALSE; }
__declspec(dllexport) CHAR WINAPI RtlQueryThreadPlaceholderCompatibilityMode(void) { return 0; }          /* PHCM_APPLICATION_DEFAULT */
__declspec(dllexport) CHAR WINAPI RtlSetThreadPlaceholderCompatibilityMode(CHAR m) { (void)m; return 0; }  /* previous mode */
__declspec(dllexport) HRESULT WINAPI IUnknown_RemoveBackReferences(IUnknown *u) { (void)u; return S_OK; }
__declspec(dllexport) void WINAPI SHWeakReleaseInterface(IUnknown *outer, IUnknown **pp) {   /* undo a weak (outer-released) reference */
  if (pp && *pp) { if (outer) outer->lpVtbl->AddRef(outer); (*pp)->lpVtbl->Release(*pp); *pp = NULL; } }
__declspec(dllexport) LONG WINAPI GetPackageFamilyName(HANDLE p, UINT32 *len, PWSTR out);   /* wxrpackage.inc */
__declspec(dllexport) LONG WINAPI GetPackageFamilyNameFromToken(HANDLE tok, UINT32 *len, PWSTR out) { (void)tok; return GetPackageFamilyName(GetCurrentProcess(), len, out); }
/* CoWaitForMultipleHandles: COWAIT_DISPATCH_CALLS (8) / COWAIT_DISPATCH_WINDOW_MESSAGES (0x10) are Win8+; Win7 answers E_INVALIDARG.
   Win10 mfplat waits for every WinRT stream read that way (flags 0x18): the FLAC source failed its first read ("E_INVALIDARG" from
   EndCreateObject, Media Player stuck loading). Win7 knows WAITALL 1, ALERTABLE 2, INPUTAVAILABLE 4; its wait dispatches anyway. */
__declspec(dllexport) HRESULT WINAPI CoWaitForMultipleHandles(DWORD flags, DWORD timeout, ULONG n, LPHANDLE h, LPDWORD idx) {
  typedef HRESULT (WINAPI *F)(DWORD, DWORD, ULONG, LPHANDLE, LPDWORD); static F f;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("ole32.dll"), "CoWaitForMultipleHandles");
  return f ? f(flags & 7u, timeout, n, h, idx) : E_NOTIMPL; }
