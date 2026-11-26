/* wxrwin8.inc - small Win8+ APIs, user32/uxtheme bits, RoGetAgileReference, WinRT strings and errors. Included by wxrsys.c. */
/* ---- misc Win8+ APIs reached by the app/XAML path ---- */
__declspec(dllexport) HRESULT WINAPI GetScaleFactorForWindow(HWND h, int *out) {
  static const int ok[] = {100, 120, 125, 140, 150, 160, 175, 180, 200, 225, 250, 300, 350, 400, 450, 500}; int dpi = 96, pct, i, best = 100; HDC dc;
  (void)h; if (!out) return E_POINTER; dc = GetDC(NULL); if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc); }
  pct = dpi * 100 / 96; for (i = 0; i < 16; i++) if (abs(ok[i] - pct) < abs(best - pct)) best = ok[i];
  *out = best; return S_OK; }
__declspec(dllexport) HRESULT WINAPI WxrCreateUri(const WCHAR *uri, DWORD flags, DWORD_PTR res, void **out) {
  typedef HRESULT (WINAPI *F)(const WCHAR *, DWORD, DWORD_PTR, void **); static F f;
  HRESULT hr; static volatile LONG nlog;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("urlmon.dll"), "CreateUri"); if (!f) return E_NOTIMPL;
  hr = f(uri, flags, res, out);
  if (FAILED(hr) && InterlockedIncrement(&nlog) <= 20) { char e[400]; int k = 0; const WCHAR *s = uri ? uri : L"(null)";   /* non-ASCII and control characters escaped */
    for (; *s && k < 380; s++) { if (*s >= 0x20 && *s < 0x7F) e[k++] = (char)*s; else k += wsprintfA(e + k, "\\u%04X", (unsigned)*s); } e[k] = 0;
    slog("[uri] CreateUri('%s' len %d, 0x%lX) -> 0x%08lX", e, uri ? lstrlenW(uri) : 0, (unsigned long)flags, (unsigned long)hr); }
  return hr; }
__declspec(dllexport) LONG WINAPI TpSetWaitEx(void *wait, HANDLE h, void *timeout, void *reserved) {
  typedef void (NTAPI *F)(void *, HANDLE, void *); static F f; (void)reserved;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("ntdll.dll"), "TpSetWait"); if (!f) return (LONG)0xC0000002; f(wait, h, timeout); return 0; }
/* An exception escaping an app event handler reaches XAML's DirectUI::ErrorHelper::ReportUnhandledError, which fail-fasts when the app does
   not handle Application.UnhandledException. Some come from features Win7 cannot back yet (WinUI SwipeControl's VisualInteractionSource in
   every History entry): those are logged and survived, as wine-uwp does with hosted fail-fasts. Recognised by the return address
   (ReportUnhandledError+0x191 = Windows.UI.Xaml 19041 RVA 0x723b21). Every other fail-fast is fatal. */
static int mod_export_name_is(HMODULE hm, const char *name);
__declspec(dllexport) void WINAPI RoFailFastWithErrorContextInternal2(HRESULT hr, DWORD a, void *b, void *c) {
  void *ret = __builtin_return_address(0); HMODULE hm = NULL; WCHAR mn[MAX_PATH] = L"";
  slog("[failfast] RoFailFastWithErrorContextInternal2 hr=0x%08lX a=%lx", (unsigned long)hr, a); (void)b; (void)c;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ret, &hm) && hm) GetModuleFileNameW(hm, mn, MAX_PATH);
  { void *bt[24]; USHORT n = RtlCaptureStackBackTrace(0, 24, bt, NULL), i; char b[1400]; int k = 0;
    for (i = 0; i < n && k < 1300; i++) { HMODULE m2 = NULL; WCHAR p[MAX_PATH], *s;
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)bt[i], &m2) && m2 && GetModuleFileNameW(m2, p, MAX_PATH)) {
        s = wcsrchr(p, L'\\'); k += _snprintf(b + k, 1399 - k, " %ls+0x%llx", s ? s + 1 : p, (unsigned long long)((BYTE *)bt[i] - (BYTE *)m2)); }
      else k += _snprintf(b + k, 1399 - k, " %p", bt[i]); }
    b[k < 1399 ? k : 1399] = 0; slog("[failfast] stack:%s", b); }
  /* ReportUnhandledError+0x191: 0x723b21 in 19041.6456, 0x721771 in 19041.3803 (the image; function at 0x7215e0, same code) */
  if (hm && ((ULONG_PTR)ret - (ULONG_PTR)hm == 0x723b21 || (ULONG_PTR)ret - (ULONG_PTR)hm == 0x721771) && mod_export_name_is(hm, "Windows.UI.Xaml.dll")
      ) {
    slog("[failfast] SURVIVED: unhandled app-event error 0x%08lX reported by XAML's ErrorHelper::ReportUnhandledError (%ls); continuing", (unsigned long)hr, mn);
    return; }
  RaiseFailFastException(NULL, NULL, 0); }
/* ---- user32 / uxtheme / imm32 bits that exist on Win10 only (via ext-ms-win-* contracts) ---- */
__declspec(dllexport) UINT WINAPI WxrGetDoubleClickTime(void) {
  typedef UINT (WINAPI *F)(void); static F f; if (!f) f = (F)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDoubleClickTime"); return f ? f() : 500; }
__declspec(dllexport) HRESULT WINAPI GetUserColorPreference(DWORD *pref, BOOL reload) { (void)reload; if (!pref) return E_POINTER; pref[0] = 0x00D77800; pref[1] = 0x00D77800; return S_OK; }
__declspec(dllexport) DWORD WINAPI GetColorFromPreference(const DWORD *pref, DWORD type, BOOL b, DWORD mode) {
  (void)type; (void)b; (void)mode; return 0xFF000000u | (pref ? (pref[1] & 0xFFFFFF) : 0xD77800); }
__declspec(dllexport) BOOL WINAPI ImmDisableLegacyIME(void) { return TRUE; }
/* RtlGetAppContainerNamedObjectPath(token, sid, relative, UNICODE_STRING *out): AppContainer object namespace does not exist on Win7;
   named objects the app creates go to the session's BaseNamedObjects instead (same visibility inside the process family). */
__declspec(dllexport) LONG WINAPI RtlGetAppContainerNamedObjectPath(HANDLE tok, void *sid, BOOLEAN rel, struct { USHORT Length, Max; WCHAR *Buf; } *out) {
  WCHAR p[80]; DWORD sess = 0; size_t n; WCHAR *b; (void)tok; (void)sid;
  ProcessIdToSessionId(GetCurrentProcessId(), &sess);
  if (rel) lstrcpyW(p, L"BaseNamedObjects"); else if (sess) _snwprintf(p, 79, L"\\Sessions\\%lu\\BaseNamedObjects", sess); else lstrcpyW(p, L"\\BaseNamedObjects");
  if (!out) return (LONG)0xC000000D; n = wcslen(p); b = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * 2); if (!b) return (LONG)0xC0000017;
  memcpy(b, p, (n + 1) * 2); out->Length = (USHORT)(n * 2); out->Max = (USHORT)((n + 1) * 2); out->Buf = b;
  slog("[impl] RtlGetAppContainerNamedObjectPath -> %ls", p); return 0; }
/* Win8+ process mitigation policies: nothing is restricted here (ProcessDynamicCodePolicy = 0 -> mrt100's thunk heap uses plain VirtualAlloc RWX) */
__declspec(dllexport) BOOL WINAPI GetProcessMitigationPolicy(HANDLE h, int policy, void *buf, SIZE_T len) { (void)h; (void)policy; if (buf && len) memset(buf, 0, len); return TRUE; }
__declspec(dllexport) BOOL WINAPI SetProcessMitigationPolicy(int policy, void *buf, SIZE_T len) { (void)policy; (void)buf; (void)len; return TRUE; }
__declspec(dllexport) BOOL WINAPI SetProcessValidCallTargets(HANDLE h, void *base, SIZE_T sz, ULONG n, void *info) { (void)h; (void)base; (void)sz; (void)n; (void)info; return TRUE; }

/* ---- RoGetAgileReference (Win8.1+) on top of the Global Interface Table ---- */
typedef struct AgRef_ { const void *vtbl; LONG ref; DWORD cookie; IGlobalInterfaceTable *git; } AgRef;
static const GUID IID_IAgileRef_ = {0xC03F6A43, 0x65A4, 0x9818, {0x98, 0x7E, 0xE0, 0xB8, 0x10, 0xD2, 0xA6, 0xF2}};
static HRESULT WINAPI agr_QI(AgRef *a, REFIID iid, void **o) {
  static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (!memcmp(iid, &iu, sizeof iu) || !memcmp(iid, &IID_IAgileRef_, sizeof iu)) { *o = a; InterlockedIncrement(&a->ref); return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI agr_AddRef(AgRef *a) { return InterlockedIncrement(&a->ref); }
static ULONG WINAPI agr_Release(AgRef *a) { LONG r = InterlockedDecrement(&a->ref); if (!r) { a->git->lpVtbl->RevokeInterfaceFromGlobal(a->git, a->cookie); a->git->lpVtbl->Release(a->git); free(a); } return r; }
static HRESULT WINAPI agr_Resolve(AgRef *a, REFIID iid, void **o) { return a->git->lpVtbl->GetInterfaceFromGlobal(a->git, a->cookie, iid, o); }
static const void *g_agr_vtbl[] = { agr_QI, agr_AddRef, agr_Release, agr_Resolve };
__declspec(dllexport) HRESULT WINAPI RoGetAgileReference(int options, REFIID iid, IUnknown *unk, void **out) {
  IGlobalInterfaceTable *git; AgRef *a; HRESULT hr; (void)options;
  if (!out || !unk) return E_INVALIDARG; *out = NULL;
  /* our in-process table (as Win10 objects get through CoCreateInstance): ole32's GIT would have to marshal Win10 objects for real -
     no Win7 proxy for e.g. IThreadAffineStorageQueryServer -> Resolve E_NOINTERFACE (folder queries failed) */
  git = (IGlobalInterfaceTable *)(void *)&g_git_obj;
  a = (AgRef *)calloc(1, sizeof *a); if (!a) { git->lpVtbl->Release(git); return E_OUTOFMEMORY; }
  hr = git->lpVtbl->RegisterInterfaceInGlobal(git, unk, iid, &a->cookie);
  if (FAILED(hr)) { git->lpVtbl->Release(git); free(a); return hr; }
  a->vtbl = g_agr_vtbl; a->ref = 1; a->git = git; *out = a; return S_OK; }
/* Win10 api-set presence queries: claim every contract is present (the shims behind them either forward or log) */
static int apiset_present(void *name) { /* the XAML platform abstraction layer (phone shell hooks) is absent on desktop SKUs, so XAML must see it as not implemented */
  struct { USHORT L, M; WCHAR *B; } *u = (void *)name; if (u && u->B && u->L >= 38 && !_wcsnicmp(u->B, L"ext-ms-win-xaml-pal", 19)) return 0;
  /* Win10-only msctf additions (TextInputClientWrapperCreate...): Win7's msctf has none, and the stub's S_OK with no object crashed every
     TextBox; reported absent, TextInputFramework uses its own client instead */
  if (u && u->B && u->L >= 42 && !_wcsnicmp(u->B, L"ext-ms-win-tsf-msctf", 20)) return 0; return 1; }
__declspec(dllexport) LONG WINAPI ApiSetQueryApiSetPresence(void *name, unsigned char *present) { if (present) *present = (unsigned char)apiset_present(name); return 0; }
__declspec(dllexport) LONG WINAPI ApiSetQueryApiSetPresenceEx(void *name, unsigned char *schema, unsigned char *present) { if (schema) *schema = 1; if (present) *present = (unsigned char)apiset_present(name); return 0; }
/* user32 private ordinal #2587 (api-ms-win-rtcore-ntuser-private-l1-1-4; real name unknown, mapped in ordmap): DirectManipulation's
   InitializeInput registers its PreDispatchHook(MSG *, LRESULT *, void **) / PostDispatchHook with win32k, which calls them around
   message dispatch. Failing (the stub's 0) made IDirectManipulationManager::Activate fail with a stale last error and XAML stopped
   rendering. Kept here for the input layer; win32k-side dispatch is not emulated yet. */
void *g_dmanip_prehook, *g_dmanip_posthook;
__declspec(dllexport) BOOL WINAPI WxrDManipRegisterDispatchHooks(DWORD flags, void *pre, void *post) {
  slog("[dmanip] dispatch hooks registered: flags=%lu pre=%p post=%p", flags, pre, post);
  g_dmanip_prehook = pre; g_dmanip_posthook = post; return TRUE; }
/* #2503 / #2504 (api-ms-win-rtcore-ntuser-private-l1-1-1, names unknown): CManagerImpl::Activate registers the window it manipulates
   (id, s_DelegateInputCallback(MSG *, void *), context, hwnd, flags 0x1000, 0); _ClearWindow(hwnd, 2) drops it. Recorded for the
   input layer, which will hand the window's input to the callback. */
typedef struct { DWORD id; void *cb, *ctx; HWND hwnd; } DmWin;
DmWin g_dmanip_win[8];
__declspec(dllexport) BOOL WINAPI WxrDManipRegisterWindow(DWORD id, void *cb, void *ctx, HWND hwnd, DWORD flags, DWORD_PTR extra) {
  int i, free_ = -1;
  slog("[dmanip] register window %p id=%lu cb=%p ctx=%p flags=0x%lx extra=%p", (void *)hwnd, id, cb, ctx, flags, (void *)extra);
  for (i = 0; i < 8; i++) { if (g_dmanip_win[i].hwnd == hwnd) { free_ = i; break; } if (!g_dmanip_win[i].hwnd && free_ < 0) free_ = i; }
  if (free_ < 0) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
  g_dmanip_win[free_].id = id; g_dmanip_win[free_].cb = cb; g_dmanip_win[free_].ctx = ctx; g_dmanip_win[free_].hwnd = hwnd; return TRUE; }
__declspec(dllexport) BOOL WINAPI WxrDManipUnregisterWindow(HWND hwnd, DWORD how) {
  int i; slog("[dmanip] unregister window %p how=%lu", (void *)hwnd, how);
  for (i = 0; i < 8; i++) if (g_dmanip_win[i].hwnd == hwnd) memset(&g_dmanip_win[i], 0, sizeof g_dmanip_win[i]);
  return TRUE; }
/* RtlGetDeviceFamilyInfoEnum(ULONGLONG *version, ULONG *family, ULONG *form): DEVICEFAMILYINFOENUM_DESKTOP = 3; XAML takes its "phone/PAL" paths for other families */
__declspec(dllexport) void WINAPI RtlGetDeviceFamilyInfoEnum(unsigned long long *ver, DWORD *family, DWORD *form) {
  if (ver) *ver = 0x000A00004A610000ull; if (family) *family = 3; if (form) *form = 0; }
__declspec(dllexport) HRESULT WINAPI CoCreateInstanceFromApp(REFCLSID clsid, IUnknown *outer, DWORD ctx, void *reserved, ULONG n, MULTI_QI *mq) {
  typedef HRESULT (WINAPI *F)(REFCLSID, IUnknown *, DWORD, COSERVERINFO *, DWORD, MULTI_QI *); static F f; HRESULT hr; char a[48];
  if (!f) f = (F)GetProcAddress(ole32_mod(), "CoCreateInstanceEx");
  hr = f(clsid, outer, ctx, NULL, n, mq); guid_str(clsid, a);
  slog("[com] CoCreateInstanceFromApp(%s, ctx=%lx, n=%lu) -> 0x%08lX", a, ctx, n, (unsigned long)hr); return hr;
}


/* log the module+offset of the first frames of the current stack (diagnostics for "who asked for that interface") */
static void log_callers(const char *tag) {
  void *fr[12]; USHORT n = CaptureStackBackTrace(1, 12, fr, NULL), i; char line[600]; int p = 0;
  for (i = 0; i < n && p < 560; i++) { HMODULE m = NULL; char nm[MAX_PATH] = "?"; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fr[i], &m);
    if (m) { GetModuleFileNameA(m, nm, MAX_PATH); { char *s = strrchr(nm, '\\'); if (s) memmove(nm, s + 1, strlen(s)); } }
    p += _snprintf(line + p, 600 - p, " %s+0x%lx", nm, (unsigned long)((char *)fr[i] - (char *)m)); }
  line[p < 599 ? p : 599] = 0; slog("[stack] %s:%s", tag, line); }
/* more user32 private entry points reached by CoreWindow / XAML (all "succeed": no touch hit-testing, no DWM resize synchronization here) */
__declspec(dllexport) BOOL WINAPI RegisterTouchHitTestingWindow(HWND h, void *a) { (void)h; (void)a; return TRUE; }
__declspec(dllexport) BOOL WINAPI NtUserEnableMouseInPointerForWindow(HWND h, BOOL on) {
  if (on && h && GetWindowThreadProcessId(h, NULL) == GetCurrentThreadId()) in_install("EnableMouseInPointerForWindow"); return TRUE; }
__declspec(dllexport) BOOL WINAPI NtUserEnableResizeLayoutSynchronization(HWND h, BOOL on) { (void)h; (void)on; return TRUE; }
/* GetPackageGlobalizationProperty(context, type 1|2, *size, *buf): Win10 kernelbase returns one flag bit of the context (type 1: bit 0, type 2: bit 1) as a DWORD; neither is set for a normal package */
__declspec(dllexport) LONG WINAPI GetPackageGlobalizationProperty(void *ctx, int type, DWORD *size, DWORD *buf) {
  if (!ctx || !size || (*size && !buf)) return ERROR_INVALID_PARAMETER; if (type != 1 && type != 2) return ERROR_INVALID_PARAMETER;
  if (*size < 4) return ERROR_INSUFFICIENT_BUFFER; *buf = 0; *size = 4; return 0; }
/* file-open tracing for the package resource files (diagnosing MRM / XAML resource lookups) */
__declspec(dllexport) HANDLE WINAPI CreateFileW(LPCWSTR n, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl) {
  typedef HANDLE (WINAPI *F)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE); static F f; HANDLE h; DWORD le;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileW");
  h = f(n, acc, share, sa, disp, fl, tmpl); le = GetLastError();
  if (n && ((wcsstr(n, L".pri") || wcsstr(n, L".xbf") || wcsstr(n, L".xaml") || wcsstr(n, L"AppxManifest") || wcsstr(n, L".winmd") || wcsstr(n, L"\\Packages\\") || (h == INVALID_HANDLE_VALUE && n[0] != '\\' && le != ERROR_PATH_NOT_FOUND + 1000)))) slog("[file] CreateFileW(%ls) -> %p err=%lu", n, (void *)h, h == INVALID_HANDLE_VALUE ? le : 0);
  SetLastError(le); return h; }
/* WNF subscriptions: nothing ever publishes, so a subscription is just a handle that never fires */
__declspec(dllexport) NTSTATUS NTAPI RtlSubscribeWnfStateChangeNotification(void **sub, void *name, ULONG stamp, void *cb, void *ctx, void *type, ULONG grp, ULONG flags) {
  (void)name; (void)stamp; (void)cb; (void)ctx; (void)type; (void)grp; (void)flags; if (sub) *sub = (void *)(ULONG_PTR)0x77; return 0; }
__declspec(dllexport) NTSTATUS NTAPI RtlUnsubscribeWnfStateChangeNotification(void *cb) { (void)cb; return 0; }
/* RtlQueryWnfStateData(stamp*, name, callback, ctx, type): the callback sees the state as never published (stamp 0, no data). Windows.Graphics'
   DisplayInformation reads its orientation/DPI states this way (add_OrientationChanged failed, so Pivot did) */
typedef NTSTATUS (NTAPI *WNF_CB)(ULONGLONG name, ULONG stamp, void *type, void *ctx, const void *buf, ULONG len);
__declspec(dllexport) NTSTATUS NTAPI RtlQueryWnfStateData(ULONG *stamp, ULONGLONG name, WNF_CB cb, void *ctx, void *type) {
  if (stamp) *stamp = 0; return cb ? cb(name, 0, type, ctx, NULL, 0) : 0; }
__declspec(dllexport) NTSTATUS NTAPI RtlUnsubscribeWnfNotificationWaitForCompletion(void *sub) { (void)sub; return 0; }
__declspec(dllexport) NTSTATUS NTAPI RtlUnsubscribeWnfNotificationWithCompletionCallback(void *sub, void *cb) { (void)sub; (void)cb; return 0; }
__declspec(dllexport) NTSTATUS NTAPI NtUpdateWnfStateData(void *name, void *data, ULONG len, void *type, void *scope, ULONG stamp, ULONG check) { (void)name; (void)data; (void)len; (void)type; (void)scope; (void)stamp; (void)check; return 0; }/* out-of-process activation servers do not exist here: a plain app has no server classes (the in-proc ones come from the manifest via the pseudo-registry) */
__declspec(dllexport) HRESULT WINAPI RoGetServerActivatableClasses(void *server, void **ids, DWORD *count) {
  if (ids) *ids = NULL; if (count) *count = 0; slog("[impl] RoGetServerActivatableClasses -> none"); (void)server; return S_OK; }
__declspec(dllexport) HRESULT WINAPI RoRegisterActivationFactories(void **ids, void **factories, UINT32 n, void **cookie) {
  typedef HRESULT (WINAPI *F)(void **, void **, UINT32, void **); static F f; HRESULT hr;
  if (!n) { if (cookie) *cookie = (void *)(ULONG_PTR)1; slog("[impl] RoRegisterActivationFactories(0) -> S_OK"); return S_OK; }
  if (!f) f = (F)GetProcAddress(LoadLibraryA("kxcom.dll"), "RoRegisterActivationFactories");
  hr = f ? f(ids, factories, n, cookie) : E_NOTIMPL; slog("[impl] RoRegisterActivationFactories(%u) -> 0x%08lX", n, (unsigned long)hr); return hr; }
__declspec(dllexport) void WINAPI RoRevokeActivationFactories(void *cookie) {
  typedef void (WINAPI *F)(void *); static F f; if ((ULONG_PTR)cookie == 1) return;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("kxcom.dll"), "RoRevokeActivationFactories"); if (f) f(cookie); }
PKGSTUB(GetStateRootFolder) PKGSTUB(GetSystemAppDataKey)
PKGSTUB(PsmGetKeyFromToken) PKGSTUB(PsmGetKeyFromProcess) PKGSTUB(PsmGetPackageFullNameFromKey)
PKGSTUB(_OpenMuiStringCache) PKGSTUB(_AddMUIStringToCache) PKGSTUB(_GetMUIStringFromCache)

/* ---------------- windows runtime strings [impl] ---------------- */
typedef void *HS; typedef HRESULT (WINAPI *PWCS)(const WCHAR *, UINT32, HS *); typedef const WCHAR *(WINAPI *PGRB)(HS, UINT32 *);
static PWCS p_create; static PGRB p_raw;
static int hs_init(void) {
  if (!p_create) {
    HMODULE m = GetModuleHandleA("combase.dll"); if (!m) m = LoadLibraryA("combase.dll");
    if (!m) m = GetModuleHandleA("api-ms-win-core-winrt-string-l1-1-0.dll");
    if (!m) m = LoadLibraryA("api-ms-win-core-winrt-string-l1-1-0.dll");
    if (m) { p_create = (PWCS)GetProcAddress(m, "WindowsCreateString"); p_raw = (PGRB)GetProcAddress(m, "WindowsGetStringRawBuffer"); }
  }
  return p_create && p_raw;
}
static HRESULT hs_make(const WCHAR *s, void **out) { if (!hs_init()) return E_NOTIMPL; return p_create(s, (UINT32)lstrlenW(s), out); }
static HRESULT trim(HS s, HS t, HS *out, int start, int end) {
  UINT32 n = 0, tn = 0; const WCHAR *a, *b; UINT32 i, j;
  if (!out) return E_INVALIDARG; if (!hs_init()) return E_NOTIMPL;
  a = p_raw(s, &n); b = p_raw(t, &tn);
  if (!tn) return E_INVALIDARG;
  i = 0; j = n;
  if (start) while (i < j) { UINT32 k; for (k = 0; k < tn; k++) if (a[i] == b[k]) break; if (k == tn) break; i++; }
  if (end) while (j > i) { UINT32 k; for (k = 0; k < tn; k++) if (a[j - 1] == b[k]) break; if (k == tn) break; j--; }
  return p_create(a + i, j - i, out);
}
__declspec(dllexport) HRESULT WINAPI WindowsTrimStringStart(HS s, HS t, HS *o) { return trim(s, t, o, 1, 0); }
__declspec(dllexport) HRESULT WINAPI WindowsTrimStringEnd(HS s, HS t, HS *o) { return trim(s, t, o, 0, 1); }
#define HSSTUB(name) __declspec(dllexport) ULONG WINAPI name(void) { STUB_LOG(#name); return 0; }
HSSTUB(HSTRING_UserSize) HSSTUB(HSTRING_UserMarshal) HSSTUB(HSTRING_UserUnmarshal) HSSTUB(HSTRING_UserFree)
HSSTUB(HSTRING_UserSize64) HSSTUB(HSTRING_UserMarshal64) HSSTUB(HSTRING_UserUnmarshal64) HSSTUB(HSTRING_UserFree64)

/* ---------------- winrt error ---------------- */
__declspec(dllexport) BOOL WINAPI IsErrorPropagationEnabled(void) { return FALSE; }
__declspec(dllexport) HRESULT WINAPI RoReportFailedDelegate(PVOID a, PVOID b) { (void)a;(void)b; return S_OK; }

/* ---------------- CoreMessaging / user32 / security: real work belongs to later milestones ---------------- */
#define GENSTUB(name) ULONG_PTR WINAPI g_##name(void) { STUB_LOG(#name); return 0; }
GENSTUB(CoreUICreate) GENSTUB(CoreUICallCreateConversationHost) GENSTUB(CoreUICallSend) GENSTUB(CoreUICallCreateEndpointHost)
GENSTUB(CoreUICallCreateEndpointHostWithSendPriority) GENSTUB(CoreUICallReceive) GENSTUB(CoreUIOpenExisting) GENSTUB(CreateDispatcherQueueController_unused)
GENSTUB(GetPointerDeviceProperties) GENSTUB(GetRawPointerDeviceData)   /* the other pointer APIs: wxrinput.inc */
GENSTUB(CreateDCompositionHwndTarget) GENSTUB(DestroyDCompositionHwndTarget)
GENSTUB(GetSecurityInfo) GENSTUB(GetStagedPackagePath_unused)

/* CoreMessaging asks for the default SD of a transient kernel object (type 8 = its ALPC registrar port). Everyone full access. */
__declspec(dllexport) DWORD WINAPI g_QueryTransientObjectSecurityDescriptor(DWORD type, const WCHAR *name, PSECURITY_DESCRIPTOR *sd) {
  typedef BOOL (WINAPI *PCV)(LPCWSTR, DWORD, PSECURITY_DESCRIPTOR *, PULONG); PCV f; PSECURITY_DESCRIPTOR d = NULL; HMODULE a = LoadLibraryW(L"advapi32.dll");
  slog("[sd] QueryTransientObjectSecurityDescriptor(type=%lu, name=%ls)", type, name ? name : L"(null)");
  if (!sd) return ERROR_INVALID_PARAMETER; f = (PCV)GetProcAddress(a, "ConvertStringSecurityDescriptorToSecurityDescriptorW");
  if (!f || !f(L"D:(A;;GA;;;WD)", 1, &d, NULL)) return GetLastError(); *sd = d; return ERROR_SUCCESS; }
__declspec(dllexport) void WINAPI g_FreeTransientObjectSecurityDescriptor(PSECURITY_DESCRIPTOR sd) { if (sd) LocalFree(sd); }
