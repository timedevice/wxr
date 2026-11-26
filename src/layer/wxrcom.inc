/* wxrcom.inc - COM class creation for Win10 callers and the Global Interface Table. Included by wxrsys.c. */
/* ---- COM class creation: log every CLSID the image's DLLs ask for; WxrShellCreate() (shell stand-ins) gets the first look ---- */
static void guid_str(const GUID *g, char *o) { sprintf(o, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]); }
static HRESULT com_inproc(const GUID *clsid, IUnknown *outer, const GUID *iid, void **ppv, int getclass);
static int com_preferred(const GUID *clsid);
static void mf_register_local(void);

static void w7item_patch(void *item);
static void knownfolders_patch(void *factory);
static HRESULT ph_create(REFIID iid, void **out); static HRESULT STDMETHODCALLTYPE phcf_qi(void *t, REFIID iid, void **o); static const void *const *g_phcf; static int com_blocked(REFCLSID clsid); static const GUID CLSID_WxrMFPropertyHandler;   /* wxrprop.inc */static void storage_noaffinity(HMODULE ws);
static void w10item_patch(HMODULE ws, void **vt);
static HRESULT com_storage_first(const GUID *clsid, IUnknown *outer, const GUID *iid, void **ppv);
static HRESULT shell_create(const GUID *clsid, const GUID *iid, void **ppv);
static void *shell_builtin_factory(const WCHAR *cls);
static HMODULE ole32_mod(void) { static HMODULE m; if (!m) m = LoadLibraryA("ole32.dll"); return m; }
/* Global Interface Table for the image's Win10 DLLs (they reach CoCreateInstance through us; Win7 modules keep ole32's): Win7's GIT
   unmarshals inside ole32, past our CoUnmarshalInterface fallbacks, so Win10 objects registered there came back as E_UNEXPECTED
   (windows.storage AgileGitPtr::CopyLocal -> IAsyncOperation<StorageFile>::GetResults failed for every file of an opened folder).
   As with CoGetStdMarshalEx = FTM, Win10 objects are treated as agile: the table keeps the pointer, Get = QueryInterface. */
typedef struct { void *unk; GUID iid; } GitEnt;
static GitEnt *g_git; static DWORD g_ngit, g_capgit; static SRWLOCK g_gitlk = SRWLOCK_INIT;
static HRESULT STDMETHODCALLTYPE git_qi(void *t, REFIID iid, void **o) { static const GUID igit = {0x00000146, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (IsEqualGUID(iid, &igit) || IsEqualGUID(iid, &iu)) { *o = t; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE git_ref(void *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE git_reg(void *t, IUnknown *unk, REFIID iid, DWORD *cookie) { DWORD i; (void)t;
  if (!unk || !cookie) return E_INVALIDARG;
  AcquireSRWLockExclusive(&g_gitlk);
  for (i = 0; i < g_ngit && g_git[i].unk; i++) ;
  if (i == g_ngit) { if (g_ngit == g_capgit) { DWORD nc = g_capgit ? g_capgit * 2 : 256; GitEnt *n = (GitEnt *)realloc(g_git, nc * sizeof *n); if (!n) { ReleaseSRWLockExclusive(&g_gitlk); return E_OUTOFMEMORY; } g_git = n; g_capgit = nc; } g_ngit++; }
  { void *p = NULL; if (FAILED(unk->lpVtbl->QueryInterface(unk, iid, &p)) || !p) { p = unk; unk->lpVtbl->AddRef(unk); } g_git[i].unk = p; g_git[i].iid = *iid; }
  *cookie = 0x7700000u + i + 1; ReleaseSRWLockExclusive(&g_gitlk); return S_OK; }
static HRESULT STDMETHODCALLTYPE git_revoke(void *t, DWORD cookie) { DWORD i = cookie - 0x7700001u; void *p = NULL; (void)t;
  AcquireSRWLockExclusive(&g_gitlk); if (i < g_ngit) { p = g_git[i].unk; g_git[i].unk = NULL; } ReleaseSRWLockExclusive(&g_gitlk);
  if (!p) return E_INVALIDARG; ((IUnknown *)p)->lpVtbl->Release((IUnknown *)p); return S_OK; }
static HRESULT STDMETHODCALLTYPE git_get(void *t, DWORD cookie, REFIID iid, void **o) { DWORD i = cookie - 0x7700001u; IUnknown *p = NULL; HRESULT hr; (void)t;
  if (!o) return E_POINTER; *o = NULL;
  AcquireSRWLockShared(&g_gitlk); if (i < g_ngit && g_git[i].unk) { p = (IUnknown *)g_git[i].unk; p->lpVtbl->AddRef(p); } ReleaseSRWLockShared(&g_gitlk);
  if (!p) return E_INVALIDARG; hr = p->lpVtbl->QueryInterface(p, iid, o);
  if (FAILED(hr)) { char g[48]; void **vt = *(void ***)p; HMODULE m = NULL; WCHAR mn[MAX_PATH] = L"?"; guid_str(iid, g);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[0], &m)) GetModuleFileNameW(m, mn, MAX_PATH);
    slog("[git] Get(cookie %lx, %s) on %ls+0x%Ix (thread %lu) -> 0x%08lX", cookie, g, wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, (ULONG_PTR)vt - (ULONG_PTR)m, GetCurrentThreadId(), (unsigned long)hr); }
  p->lpVtbl->Release(p); return hr; }
static const void *const g_git_vt[6] = { (void *)git_qi, (void *)git_ref, (void *)git_ref, (void *)git_reg, (void *)git_revoke, (void *)git_get };
static const void *const *g_git_obj = g_git_vt;
/* CLSID_RestrictedErrorObject {00000352-...} with combase's private interface {66818B96-DC17-4C12-8CA1-8E1FBAA5BF80}: XAML's
   DirectUI::ErrorInfo::CreateRestrictedErrorInfo creates one for every error it reports with a message (slot 3 = initialise
   with two strings and the HRESULT, then QI IRestrictedErrorInfo). Win7's COM has no such interface: the E_NOINTERFACE replaced
   the real error and XAML reported that instead (Store: a tap on a product tile "did nothing"). The stand-in also
   logs what the error really was. */
typedef struct REObj { const void *vt_int, *vt_rei, *vt_ei; LONG ref; HRESULT hr; BSTR s1, s2; } REObj;
static const GUID RE_IID_INT = {0x66818B96, 0xDC17, 0x4C12, {0x8C, 0xA1, 0x8E, 0x1F, 0xBA, 0xA5, 0xBF, 0x80}};
static const GUID RE_IID_REI = {0x82BA7092, 0x4C88, 0x427D, {0xA7, 0xBC, 0x16, 0xDD, 0x93, 0xFE, 0xB6, 0x7E}};
static const GUID RE_IID_EI = {0x1CF2B120, 0x547D, 0x101B, {0x8E, 0x65, 0x08, 0x00, 0x2B, 0x2B, 0xF1, 0xD8}};
static const GUID RE_IID_UNK = {0x00000000, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static HRESULT re_qi(REObj *o, REFIID iid, void **out) {
  if (!out) return E_POINTER;
  if (IsEqualGUID(iid, &RE_IID_UNK) || IsEqualGUID(iid, &RE_IID_INT)) *out = &o->vt_int;
  else if (IsEqualGUID(iid, &RE_IID_REI)) *out = &o->vt_rei; else if (IsEqualGUID(iid, &RE_IID_EI)) *out = &o->vt_ei;
  else { *out = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&o->ref); return S_OK; }
static ULONG re_rel(REObj *o) { LONG r = InterlockedDecrement(&o->ref); if (!r) { SysFreeString(o->s1); SysFreeString(o->s2); free(o); } return r; }
#define RE_FROM(i, m) ((REObj *)((BYTE *)(i) - offsetof(REObj, m)))
static HRESULT WINAPI re_qi0(void *i, REFIID g, void **o) { return re_qi(RE_FROM(i, vt_int), g, o); }
static ULONG WINAPI re_ar0(void *i) { return InterlockedIncrement(&RE_FROM(i, vt_int)->ref); }
static ULONG WINAPI re_rl0(void *i) { return re_rel(RE_FROM(i, vt_int)); }
static HRESULT WINAPI re_qi1(void *i, REFIID g, void **o) { return re_qi(RE_FROM(i, vt_rei), g, o); }
static ULONG WINAPI re_ar1(void *i) { return InterlockedIncrement(&RE_FROM(i, vt_rei)->ref); }
static ULONG WINAPI re_rl1(void *i) { return re_rel(RE_FROM(i, vt_rei)); }
static HRESULT WINAPI re_qi2(void *i, REFIID g, void **o) { return re_qi(RE_FROM(i, vt_ei), g, o); }
static ULONG WINAPI re_ar2(void *i) { return InterlockedIncrement(&RE_FROM(i, vt_ei)->ref); }
static ULONG WINAPI re_rl2(void *i) { return re_rel(RE_FROM(i, vt_ei)); }
static HRESULT WINAPI re_init(void *i, const WCHAR *a, HRESULT hr, const WCHAR *b, void *c) {
  REObj *o = RE_FROM(i, vt_int); static volatile LONG n; (void)c;
  o->hr = hr; SysFreeString(o->s1); SysFreeString(o->s2); o->s1 = SysAllocString(a ? a : L""); o->s2 = SysAllocString(b ? b : L"");
  if (InterlockedIncrement(&n) <= 60) { slog("[xamlerr] XAML reports error 0x%08lX: \"%.200ls\" / \"%.200ls\"", (unsigned long)hr, a ? a : L"", b ? b : L""); if (n <= 12) log_callers("[xamlerr] origin"); }
  return S_OK; }
static HRESULT WINAPI re_details(void *i, BSTR *desc, HRESULT *hr, BSTR *rdesc, BSTR *sid) {
  REObj *o = RE_FROM(i, vt_rei);
  if (desc) *desc = SysAllocString(o->s1 ? o->s1 : L""); if (hr) *hr = o->hr; if (rdesc) *rdesc = SysAllocString(o->s2 ? o->s2 : L""); if (sid) *sid = NULL; return S_OK; }
static HRESULT WINAPI re_reference(void *i, BSTR *ref) { (void)i; if (ref) *ref = NULL; return S_OK; }
static HRESULT WINAPI re_guid(void *i, GUID *g) { (void)i; if (g) memset(g, 0, sizeof *g); return S_OK; }
static HRESULT WINAPI re_nostr(void *i, BSTR *s) { (void)i; if (s) *s = NULL; return S_OK; }
static HRESULT WINAPI re_desc(void *i, BSTR *s) { REObj *o = RE_FROM(i, vt_ei); if (s) *s = SysAllocString(o->s1 && *o->s1 ? o->s1 : (o->s2 ? o->s2 : L"")); return S_OK; }
static HRESULT WINAPI re_helpctx(void *i, DWORD *c) { (void)i; if (c) *c = 0; return S_OK; }
static const void *const re_vt_int[] = { re_qi0, re_ar0, re_rl0, re_init };
static const void *const re_vt_rei[] = { re_qi1, re_ar1, re_rl1, re_details, re_reference };
static const void *const re_vt_ei[] = { re_qi2, re_ar2, re_rl2, re_guid, re_nostr, re_desc, re_nostr, re_helpctx };
static HRESULT re_create(REFIID iid, void **ppv) {
  REObj *o = (REObj *)calloc(1, sizeof *o); HRESULT hr; if (!o) return E_OUTOFMEMORY;
  o->vt_int = re_vt_int; o->vt_rei = re_vt_rei; o->vt_ei = re_vt_ei; o->ref = 1; hr = re_qi(o, iid, ppv); re_rel(o); return hr; }
