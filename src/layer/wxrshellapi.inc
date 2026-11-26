/* wxrshellapi.inc - shell helpers Win10 Windows.Storage imports and Win7 lacks. Included by wxrsys.c. */
/* ---- shell helpers Win10's Windows.Storage (its own shell namespace) imports and Win7 lacks (or only has by ordinal) ---- */
/* SHWeakQueryInterface: QI the inner object and drop the reference that QI added to the (aggregating) outer - CRegFolder gets its inner folder this way */
__declspec(dllexport) HRESULT WINAPI SHWeakQueryInterface(IUnknown *outer, IUnknown *inner, REFIID riid, void **out) {
  HRESULT hr; if (!out) return E_POINTER; *out = NULL; if (!inner) return E_NOINTERFACE;
  hr = inner->lpVtbl->QueryInterface(inner, riid, out); if (SUCCEEDED(hr) && outer) outer->lpVtbl->Release(outer); return hr; }
__declspec(dllexport) BOOL WINAPI SHWindowsPolicy(REFGUID policy) { (void)policy; return FALSE; }                         /* no policy is set */
__declspec(dllexport) HRESULT WINAPI SHWindowsPolicyGetValue(REFGUID policy, DWORD *v) { (void)policy; if (v) *v = 0; return S_FALSE; }
__declspec(dllexport) DWORD WINAPI SHRestrictionLookup(int rest, const WCHAR *path, const void *items, DWORD *cache) { (void)rest; (void)path; (void)items; (void)cache; return 0; }
__declspec(dllexport) HRESULT WINAPI SHKnownFolderFromCSIDL_Extension(int csidl, GUID *out) { (void)csidl; if (out) memset(out, 0, sizeof *out); return E_NOTIMPL; }
__declspec(dllexport) BOOL WINAPI IsAppCompatModeEnabled(void) { return FALSE; }
__declspec(dllexport) HRESULT WINAPI IUnknown_GetClassID(IUnknown *u, CLSID *clsid) {
  IPersist *p = NULL; HRESULT hr; static const GUID iid_persist = {0x0000010c, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (!u || !clsid) return E_INVALIDARG; hr = u->lpVtbl->QueryInterface(u, &iid_persist, (void **)&p); if (FAILED(hr)) return hr;
  hr = p->lpVtbl->GetClassID(p, clsid); p->lpVtbl->Release(p); return hr; }
/* shcore's "unaligned" string helpers (shcore #211 ualstrcmpiW ...): on x64 unaligned WCHAR access is fine, so they are the lstr* functions.
   They were unimplemented thunks returning 0 - ualstrcmpiW said every pair of names was equal, so Win10 propsys
   (CMemPropStore::s_FindValue) merged all named properties when serializing/loading a property store: the search item's
   AutolistCacheKey became AutolistCacheTime -> folder queries failed (Media Player "Open folder"). */
__declspec(dllexport) int WINAPI ualstrcmpiW(const WCHAR *a, const WCHAR *b) { return lstrcmpiW(a, b); }
__declspec(dllexport) int WINAPI ualstrcmpW(const WCHAR *a, const WCHAR *b) { return lstrcmpW(a, b); }
__declspec(dllexport) int WINAPI ualstrlenW(const WCHAR *s) { return s ? lstrlenW(s) : 0; }
/* StrCpyNXW (shlwapi): copies up to n-1 chars, always terminates, returns a pointer to the terminating null in the destination */
__declspec(dllexport) WCHAR *WINAPI StrCpyNXW(WCHAR *d, const WCHAR *s, int n) {
  if (!d || n <= 0) return d; if (!s) s = L"";
  while (--n > 0 && *s) *d++ = *s++;
  *d = 0; return d; }
/* GUIDFromStringW (shell32/shcore): parses the "{xxxxxxxx-...}" at the start of the string and ignores what follows - Win10 CRegFolder parses
   "::{04731B67-...},<search parameters>" this way (the whole string through CLSIDFromString was CO_E_CLASSSTRING -> no search folder ->
   folder queries failed). No ProgIDs. */
__declspec(dllexport) BOOL WINAPI GUIDFromStringW(const WCHAR *s, GUID *g) {
  WCHAR b[39];
  if (!s || !g || s[0] != L'{' || lstrlenW(s) < 38 || s[37] != L'}') return FALSE;
  memcpy(b, s, 38 * sizeof(WCHAR)); b[38] = 0;
  return SUCCEEDED(IIDFromString(b, g)); }
__declspec(dllexport) LONG WINAPI SHGlobalCounterGetValue(int id) { (void)id; return 0; }
__declspec(dllexport) int WINAPI SHStringFromGUIDW(REFGUID g, WCHAR *buf, int cch) { return StringFromGUID2(g, buf, cch); }
/* PathIsValidCharW(ch, PIVC_* flags): letters/digits always; '?' 0x1 '*' 0x2 '.' 0x4 '\' '/' 0x8 ':' 0x10 ';' 0x20 ',' 0x40 ' ' 0x80 '"' 0x200;
   other punctuation and non-ASCII need PIVC_ALLOW_NONALPAHABETIC 0x100; control chars, '<' '>' '|' never */
__declspec(dllexport) BOOL WINAPI PathIsValidCharW(WCHAR c, DWORD f) {
  if (c < 0x20 || c == '<' || c == '>' || c == '|') return FALSE;
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return TRUE;
  switch (c) { case '?': return !!(f & 1); case '*': return !!(f & 2); case '.': return !!(f & 4); case '\\': case '/': return !!(f & 8); case ':': return !!(f & 0x10);
    case ';': return !!(f & 0x20); case ',': return !!(f & 0x40); case ' ': return !!(f & 0x80); case '"': return !!(f & 0x200); }
  return !!(f & 0x100); }
__declspec(dllexport) WCHAR *WINAPI ualstrcpynW(WCHAR *dst, const WCHAR *src, int n) { return lstrcpynW(dst, src, n); }
/* DoesStringRoundTripW(s, ansi buffer, cch): TRUE when s survives a trip through the ANSI code page */
__declspec(dllexport) BOOL WINAPI DoesStringRoundTripW(const WCHAR *s, char *buf, int cch) {
  char tmp[1024]; BOOL bad = FALSE; char *b = buf && cch > 0 ? buf : tmp; int n = buf && cch > 0 ? cch : (int)sizeof tmp;
  if (!s) return TRUE; if (!WideCharToMultiByte(CP_ACP, 0, s, -1, b, n, NULL, &bad)) { b[0] = 0; return FALSE; } return !bad; }
__declspec(dllexport) BOOL WINAPI SHQueueUserWorkItem(LPTHREAD_START_ROUTINE fn, void *ctx, LONG prio, DWORD_PTR tag, DWORD_PTR *id, LPCSTR mod, DWORD flags) {
  (void)prio; (void)tag; (void)mod; (void)flags; if (id) *id = 0; return QueueUserWorkItem(fn, ctx, WT_EXECUTEDEFAULT); }
/* ApphelpCheckShellObject(clsid, shim, *flags): no app-compat blocks here - allow every shell extension */
__declspec(dllexport) BOOL WINAPI ApphelpCheckShellObject(REFCLSID c, BOOL shim, ULONGLONG *flags) { (void)c; (void)shim; if (flags) *flags = 0; return TRUE; }
/* SHCreatePropertyBagOnRegKey: Win7 shlwapi has it as unnamed ordinal 471 (same signature). The old E_NOTIMPL stub made Win10 windows.storage
   fail to load every folder type (CFolderTypeDescription::_LoadFolderTypeInfo) -> QueryOptions(CommonFileQuery) E_NOTIMPL -> Media Player's
   "Open folder" crashed. */
__declspec(dllexport) HRESULT WINAPI SHCreatePropertyBagOnRegKey(HKEY k, const WCHAR *sub, DWORD mode, REFIID riid, void **out) {
  typedef HRESULT (WINAPI *F)(HKEY, const WCHAR *, DWORD, REFIID, void **); static F f; static int init;
  if (!init) { init = 1; f = (F)GetProcAddress(LoadLibraryA("shlwapi.dll"), MAKEINTRESOURCEA(471)); }
  if (!f) { if (out) *out = NULL; return E_NOTIMPL; }
  return f(k, sub, mode, riid, out); }
/* shcore #120 SHRegGetCLSIDKey(clsid, subkey, per-user, create, mode, &key) - Win10 has a 5th argument before the key (Windows.Storage
   CCLSIDInfoCache passes 1, read access); result: [HKCU\Software\Classes | HKCR]\CLSID\{clsid}[\subkey] */
__declspec(dllexport) HRESULT WINAPI SHRegGetCLSIDKey(REFGUID g, const WCHAR *sub, BOOL user, BOOL create, DWORD mode, HKEY *out) {
  WCHAR p[300], gs[40]; LONG r; HKEY root = user ? HKEY_CURRENT_USER : HKEY_CLASSES_ROOT; (void)mode; if (!out) return E_INVALIDARG; *out = NULL;
  StringFromGUID2(g, gs, 40); _snwprintf(p, 300, L"%lsCLSID\\%ls%ls%ls", user ? L"Software\\Classes\\" : L"", gs, sub && *sub ? L"\\" : L"", sub ? sub : L"");
  r = create ? RegCreateKeyExW(root, p, 0, NULL, 0, KEY_ALL_ACCESS, NULL, out, NULL) : RegOpenKeyExW(root, p, 0, KEY_READ, out);
  return HRESULT_FROM_WIN32(r); }
__declspec(dllexport) LONG WINAPI SHRegGetValueFromHKCUHKLM(const WCHAR *key, const WCHAR *val, DWORD flags, DWORD *type, void *data, DWORD *cb) {
  DWORD keep = cb ? *cb : 0; LONG r = RegGetValueW(HKEY_CURRENT_USER, key, val, flags & 0xFFFF, type, data, cb);
  if (r == ERROR_FILE_NOT_FOUND) { if (cb) *cb = keep; r = RegGetValueW(HKEY_LOCAL_MACHINE, key, val, flags & 0xFFFF, type, data, cb); }
  return r; }
/* the _Extension variants are Win10 hooks for package-aware known folders: the plain shell32 call is right for us */
__declspec(dllexport) HRESULT WINAPI SHGetKnownFolderPath_Extension(REFGUID id, DWORD flags, HANDLE token, WCHAR **path) {
  typedef HRESULT (WINAPI *F)(REFGUID, DWORD, HANDLE, WCHAR **); static F f;
  if (path) *path = NULL; if (!f) f = (F)GetProcAddress(LoadLibraryA("shell32.dll"), "SHGetKnownFolderPath"); return f ? f(id, flags, token, path) : E_NOTIMPL; }
/* HSTRING buffers: VxKex's WindowsPromoteStringBuffer refuses buffers twinapi.appcore fills correctly (AnalyticsInfo's device family -> E_INVALIDARG).
   Ours: the HSTRING_BUFFER is a heap block [length][chars...]; promoting copies it into a normal string. */
typedef struct HsBuf { UINT32 len; UINT32 magic; WCHAR chars[1]; } HsBuf;
#define HSBUF_MAGIC 0x48534246u
__declspec(dllexport) HRESULT WINAPI WindowsPreallocateStringBuffer(UINT32 len, WCHAR **chars, void **buf) {
  HsBuf *b; if (!chars || !buf) return E_POINTER; *chars = NULL; *buf = NULL;
  b = (HsBuf *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, offsetof(HsBuf, chars) + ((SIZE_T)len + 1) * sizeof(WCHAR)); if (!b) return E_OUTOFMEMORY;
  b->len = len; b->magic = HSBUF_MAGIC; *chars = b->chars; *buf = b; return S_OK; }
__declspec(dllexport) HRESULT WINAPI WindowsDeleteStringBuffer(void *buf) { HsBuf *b = (HsBuf *)buf; if (!b) return S_OK; if (b->magic != HSBUF_MAGIC) return E_INVALIDARG; b->magic = 0; HeapFree(GetProcessHeap(), 0, b); return S_OK; }
__declspec(dllexport) HRESULT WINAPI WindowsPromoteStringBuffer(void *buf, void **out) {
  HsBuf *b = (HsBuf *)buf; HRESULT hr; if (!out) return E_POINTER; *out = NULL; if (!b || b->magic != HSBUF_MAGIC) return E_INVALIDARG;
  if (!hs_init()) return E_FAIL; hr = p_create(b->chars, b->len, out);
  /* On Windows the preallocated buffer *becomes* the string's storage, and callers rely on it: XAML's TStringBuilder::DetachString promotes,
     then (when the promoted length differs from what it wrote) copies the text again from the old mutable pointer. Freeing the buffer here
     made that copy read freed memory: every library control's themes/generic.xaml address became garbage and their default styles never
     loaded (the Community Toolkit controls on Calculator's Settings page). The buffer is retired instead and freed 256 promotions later. */
  if (SUCCEEDED(hr)) {
    static HsBuf *volatile ring[256]; static volatile LONG pos; HsBuf *old;
    b->magic = 0;                                                  /* no longer a valid HSTRING_BUFFER handle */
    old = (HsBuf *)InterlockedExchangePointer((void *volatile *)&ring[(InterlockedIncrement(&pos) - 1) & 255], b);
    if (old) HeapFree(GetProcessHeap(), 0, old);
  }
  return hr; }
