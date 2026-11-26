/* wxrapisets.inc - logging ntdll stubs, feature staging, small api sets, package identity, profapi private exports. Included by wxrsys.c. */
/* ---------------- stubs that log (ntdll) ---------------- */
#define NTSTUB(name) __declspec(dllexport) NTSTATUS NTAPI name(void) { STUB_LOG(#name); return STATUS_NOT_IMPLEMENTED; }
/* NtAlpcConnectPortEx (Windows 8+) -> Windows 7's NtAlpcConnectPort (ALPC exists in the Win7 kernel since Vista).  Differences: the Ex form takes the
   port name inside an OBJECT_ATTRIBUTES, and SIZE_T buffer length. */
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR_;
typedef struct { ULONG Length; HANDLE Root; void *ObjectName; ULONG Attributes; PVOID Sd, Sqos; } OBJA;
typedef struct { ULONG Alloc, Valid; } MATTR_;   /* ALPC_MESSAGE_ATTRIBUTES: AllocatedAttributes, ValidAttributes */
static void hexs(char *o, size_t n, const void *p, size_t len) { size_t i; o[0] = 0; if (!p) { _snprintf(o, n, "null"); return; } for (i = 0; i < len && i * 2 + 3 < n; i++) _snprintf(o + i * 2, 3, "%02x", ((const unsigned char *)p)[i]); }
static void pattr_str(char *o, size_t n, const void *pa) {   /* ALPC_PORT_ATTRIBUTES: Flags, SecurityQos(12 bytes), then MaxMessageLength etc. */
  const ULONG *u = (const ULONG *)pa; if (!pa) { _snprintf(o, n, "null"); return; }
  _snprintf(o, n, "{flags=0x%lx qos=%lx.%lx.%lx maxmsg=%llu membw=%llu maxpool=%llu maxsect=%llu maxview=%llu maxtotal=%llu dup=0x%lx}", u[0], u[1], u[2], u[3],
    (unsigned long long)*(SIZE_T *)(u + 4), (unsigned long long)*(SIZE_T *)(u + 6), (unsigned long long)*(SIZE_T *)(u + 8), (unsigned long long)*(SIZE_T *)(u + 10),
    (unsigned long long)*(SIZE_T *)(u + 12), (unsigned long long)*(SIZE_T *)(u + 14), *(ULONG *)(u + 16));
}
/* ALPC port names: a non-elevated user cannot create ports in (Sessions\N\)BaseNamedObjects (STATUS_PRIVILEGE_NOT_HELD); \RPC Control is where Win7 user-mode ALPC servers live */
static OBJA *alpc_remap(const OBJA *in, OBJA *out, USTR_ *us, WCHAR *buf, size_t cap) {
  USTR_ *n = in ? (USTR_ *)in->ObjectName : NULL; const WCHAR *w, *rest = NULL; size_t len;
  if (!n || !n->Buffer || in->Root) return (OBJA *)in;
  w = n->Buffer; len = n->Length / 2;
  if (len > 18 && !_wcsnicmp(w, L"\\BaseNamedObjects\\", 18)) rest = w + 18;
  else if (len > 10 && !_wcsnicmp(w, L"\\Sessions\\", 10)) { const WCHAR *p = w + 10; while ((size_t)(p - w) < len && *p >= L'0' && *p <= L'9') p++;
    if ((size_t)(p - w) + 18 <= len && !_wcsnicmp(p, L"\\BaseNamedObjects\\", 18)) rest = p + 18; }
  if (!rest) return (OBJA *)in;
  len = len - (size_t)(rest - w); if (len + 14 >= cap) return (OBJA *)in;
  memcpy(buf, L"\\RPC Control\\", 13 * 2); memcpy(buf + 13, rest, len * 2); buf[13 + len] = 0;
  us->Buffer = buf; us->Length = (USHORT)((13 + len) * 2); us->MaximumLength = (USHORT)(us->Length + 2);
  *out = *in; out->ObjectName = us; return out;
}typedef NTSTATUS (NTAPI *PNtAlpcConnectPort)(PHANDLE, void *, OBJA *, PVOID, ULONG, PVOID, PVOID, PULONG, PVOID, PVOID, PLARGE_INTEGER);
__declspec(dllexport) NTSTATUS NTAPI NtAlpcConnectPortEx(PHANDLE h, OBJA *conn, OBJA *client, PVOID pattr, ULONG flags, PVOID sd, PVOID cmsg,
    SIZE_T *blen, PVOID outA, PVOID inA, PLARGE_INTEGER to) {
  static PNtAlpcConnectPort f; ULONG l32 = 0; NTSTATUS st; OBJA mo; USTR_ mu; WCHAR mb[400]; (void)sd;
  if (!f) f = (PNtAlpcConnectPort)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAlpcConnectPort");
  if (!f) return STATUS_NOT_IMPLEMENTED; if (!conn) return STATUS_INVALID_PARAMETER;
  if (blen) l32 = (ULONG)*blen;
  { char pa[300], hx[200], oa[40] = "-", ia[40] = "-";
    pattr_str(pa, sizeof pa, pattr); hexs(hx, sizeof hx, cmsg, cmsg ? 40 + (((USHORT *)cmsg)[0]) : 0);
    if (outA) _snprintf(oa, sizeof oa, "0x%lx/0x%lx", ((ULONG *)outA)[0], ((ULONG *)outA)[1]); if (inA) _snprintf(ia, sizeof ia, "0x%lx/0x%lx", ((ULONG *)inA)[0], ((ULONG *)inA)[1]);
    slog("[alpc] ConnectPortEx pre: flags=0x%lx sd=%p pattr=%s cmsg=%p blen=%lu outAttr=%s inAttr=%s sqos=%p cmsg=%s", flags, sd, pa, cmsg, l32, oa, ia, client, hx); }
  conn = alpc_remap(conn, &mo, &mu, mb, 400);
  st = f(h, conn->ObjectName, client, pattr, flags, NULL, cmsg, blen ? &l32 : NULL, outA, inA, to);
  if (blen) *blen = l32;
  { USTR_ *u = (USTR_ *)conn->ObjectName; slog("[alpc] NtAlpcConnectPortEx(name=%.*ls root=%p flags=0x%lx) -> 0x%08lX", u ? u->Length / 2 : 0, u ? u->Buffer : L"", conn->Root, (unsigned long)flags, (unsigned long)st); } return st;
}
/* Plain NtAlpcConnectPort (Win7 has it natively). Forced so the client gets the same name remap as NtAlpcCreatePort below: WinUI 3's
   CoreMessagingXP runs its registrar in-process, creates "\Sessions\N\BaseNamedObjects\CoreMessagingRegistrar-..." (we move it to
   \RPC Control\) and then connects with this plain form under the original name -> not found -> CFlat abandonment. */
__declspec(dllexport) NTSTATUS NTAPI NtAlpcConnectPort(PHANDLE h, USTR_ *name, OBJA *client, PVOID pattr, ULONG flags, PVOID sid, PVOID cmsg,
    PULONG blen, PVOID outA, PVOID inA, PLARGE_INTEGER to) {
  static PNtAlpcConnectPort f; NTSTATUS st; OBJA in, mo; USTR_ mu; WCHAR mb[400]; const OBJA *r;
  if (!f) f = (PNtAlpcConnectPort)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAlpcConnectPort");
  if (!f) return STATUS_NOT_IMPLEMENTED;
  memset(&in, 0, sizeof in); in.Length = sizeof in; in.ObjectName = name;
  r = alpc_remap(&in, &mo, &mu, mb, 400); if (r != &in) name = (USTR_ *)r->ObjectName;
  st = f(h, name, client, pattr, flags, sid, cmsg, blen, outA, inA, to);
  slog("[alpc] NtAlpcConnectPort(name=%.*ls flags=0x%lx) -> 0x%08lX", name ? name->Length / 2 : 0, name && name->Buffer ? name->Buffer : L"", (unsigned long)flags, (unsigned long)st);
  return st;
}
/* server side: the port the registrar creates (forced for the image's DLLs) */
typedef NTSTATUS (NTAPI *PNtAlpcCreatePort)(PHANDLE, OBJA *, PVOID);
__declspec(dllexport) NTSTATUS NTAPI NtAlpcCreatePort(PHANDLE h, OBJA *oa, PVOID attr) {
  static PNtAlpcCreatePort f; NTSTATUS st; USTR_ *u = oa ? (USTR_ *)oa->ObjectName : NULL; OBJA mo; USTR_ mu; WCHAR mb[400];
  if (!f) f = (PNtAlpcCreatePort)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAlpcCreatePort"); if (!f) return STATUS_NOT_IMPLEMENTED;
  oa = alpc_remap(oa, &mo, &mu, mb, 400); u = oa ? (USTR_ *)oa->ObjectName : NULL; st = f(h, oa, attr);
  if (st == (NTSTATUS)0xC0000061 && oa && oa->Sd) { OBJA o3 = *oa; o3.Sd = NULL; st = f(h, &o3, attr); slog("[alpc] NtAlpcCreatePort: retried without the security descriptor -> 0x%08lX", (unsigned long)st); }
  if (st == (NTSTATUS)0xC0000061 && attr) { ULONG pa[18]; static const SIZE_T tries[] = { 0x8000, 0x4000, 0x2100, 0x1000 }; int k;
    for (k = 0; k < 4 && st == (NTSTATUS)0xC0000061; k++) { memcpy(pa, attr, sizeof pa); *(SIZE_T *)(pa + 4) = tries[k]; st = f(h, oa, pa); if (!st) slog("[alpc] NtAlpcCreatePort: MaxMessageLength clamped to 0x%llx", (unsigned long long)tries[k]); } }
  { char pa[300]; pattr_str(pa, sizeof pa, attr); slog("[alpc] NtAlpcCreatePort pattr=%s", pa); }
  slog("[alpc] NtAlpcCreatePort(name=%.*ls root=%p attrs=%lx) -> 0x%08lX handle=%p", u ? u->Length / 2 : 0, u ? u->Buffer : L"(none)", oa ? oa->Root : NULL, oa ? oa->Attributes : 0, (unsigned long)st, h ? *h : NULL); return st; }
NTSTUB(RtlGetTokenNamedObjectPath)
NTSTUB(RtlAllocateWnfSerializationGroup)
NTSTUB(RtlDeriveCapabilitySidsFromName)
NTSTUB(RtlIsParentOfChildAppContainer)

/* ---------------- feature staging [impl]: "no overrides, use compiled-in defaults" ---------------- */
__declspec(dllexport) UINT32 WINAPI GetFeatureEnabledState(UINT32 id, int usage) { (void)id; (void)usage; return 0; /* DEFAULT */ }
__declspec(dllexport) void WINAPI RecordFeatureUsage(UINT32 id, UINT32 a, UINT32 b, PCSTR n) { (void)id;(void)a;(void)b;(void)n; }
__declspec(dllexport) void WINAPI RecordFeatureError(UINT32 id, PVOID e) { (void)id;(void)e; }
__declspec(dllexport) void WINAPI SubscribeFeatureStateChangeNotification(PVOID *sub, PVOID cb, PVOID ctx) { (void)cb;(void)ctx; if (sub) *sub = (PVOID)(ULONG_PTR)1; }
__declspec(dllexport) void WINAPI UnsubscribeFeatureStateChangeNotification(PVOID sub) { (void)sub; }
__declspec(dllexport) UINT32 WINAPI GetFeatureVariant(UINT32 id, int usage, UINT32 *payloadId, BOOL *hasPayload) {
  (void)id;(void)usage; if (payloadId) *payloadId = 0; if (hasPayload) *hasPayload = FALSE; return 0; }

/* ---------------- misc api sets ---------------- */
/* quirk 0x20046: XAML's desktop input path (CJupiterWindow::UseCoreWindowSubclassedEvents) subclasses the CoreWindow and reads WM_POINTER*
   itself; without it XAML never subscribes to pointer input (wine-uwp "Clicks work"). Every other quirk stays off. */
__declspec(dllexport) BOOL WINAPI QuirkIsEnabled(PVOID q) { static volatile LONG n; if (InterlockedIncrement(&n) <= 20) slog("[quirk] QuirkIsEnabled(0x%Ix)", (ULONG_PTR)q); return (ULONG_PTR)q == 0x20046; }
__declspec(dllexport) BOOL WINAPI QuirkIsEnabledForPackage(PCWSTR pkg, PVOID q) { (void)pkg;(void)q; return FALSE; }
__declspec(dllexport) HRESULT WINAPI IsDeveloperModeEnabled(BOOL *on) { if (on) *on = FALSE; return S_OK; }
__declspec(dllexport) BOOL WINAPI IsOneCoreTransformMode(void) { return FALSE; }
__declspec(dllexport) BOOL WINAPI OOBEComplete(BOOL *c) { if (c) *c = TRUE; return TRUE; }
__declspec(dllexport) BOOL WINAPI IsApiSetImplemented(PCSTR n) { slog("[impl] IsApiSetImplemented(%s) -> FALSE", n ? n : "?"); return FALSE; }
__declspec(dllexport) HRESULT WINAPI CapabilityCheck(HANDLE tok, PCWSTR cap, BOOL *has) { (void)tok;(void)cap; if (has) *has = TRUE; return S_OK; }
__declspec(dllexport) UINT32 WINAPI GetScaleFactorForDevice(int type) { (void)type; return 100; }
__declspec(dllexport) void WINAPI TerminateProcessOnMemoryExhaustion(SIZE_T n) { (void)n; TerminateProcess(GetCurrentProcess(), (UINT)STATUS_NO_MEMORY); }
__declspec(dllexport) DWORD WINAPI GetPersistedRegistryValueW(PCWSTR a, PCWSTR b, PVOID c, PVOID d) { (void)a;(void)b;(void)c;(void)d; STUB_LOG("GetPersistedRegistryValueW"); return ERROR_FILE_NOT_FOUND; }
__declspec(dllexport) HRESULT WINAPI SHLoadIndirectString(PCWSTR src, PWSTR out, UINT cch, PVOID *reserved) {
  typedef HRESULT (WINAPI *F)(PCWSTR, PWSTR, UINT, PVOID *); static F f;
  if (!f) { HMODULE m = LoadLibraryW(L"shlwapi.dll"); if (m) f = (F)GetProcAddress(m, "SHLoadIndirectString"); }
  return f ? f(src, out, cch, reserved) : E_NOTIMPL;
}
/* SHTaskPoolQueueTask (shcore, Win8+): runs an IComPoolTask (IUnknown + void Run()) on a shell pool thread. Win10 windows.storage creates
   its broker statics (KnownFolders.MusicLibrary ...) this way and reads the result right after the call. The task runs inline on the
   calling thread: a temporary STA thread died with the object's apartment (0x80010012 when the result was unmarshalled).
   Args seen: (pool 5, flags 0x21, apartment|0x40000000, NULL, task, NULL). */
/* Only the 0x20 flag (seen with 0x21: the caller waits for the result) runs inline. Every other call is fire-and-forget and goes to a worker
   thread: run inline, XAML's window-activation material-policy task waited on the very UI thread that queued it (deadlock when a file dialog
   opened). */
static DWORD WINAPI tp_worker(void *task) {
  typedef void (STDMETHODCALLTYPE *RUN)(void *); typedef ULONG (STDMETHODCALLTYPE *REL)(void *); void **vt = *(void ***)task;
  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); ((RUN)vt[3])(task); ((REL)vt[2])(task); if (SUCCEEDED(hr)) CoUninitialize(); return 0; }
__declspec(dllexport) HRESULT WINAPI SHTaskPoolQueueTask(DWORD pool, DWORD flags, DWORD apt, void *r9, void *task, void **out) {
  typedef void (STDMETHODCALLTYPE *RUN)(void *); typedef ULONG (STDMETHODCALLTYPE *ADD)(void *); void **vt; (void)r9;
  if (out) *out = NULL; if (!task) return E_INVALIDARG;
  vt = *(void ***)task;
  /* flags 0x20 = the caller waits for the task. Apartment 0 = the pool's MTA: from an STA caller it runs on an MTA worker while the caller
     waits pumping COM (windows.storage CStorageQueryServer::GetItemCount from the UI thread: inline on the STA its data-layer objects
     answered E_NOINTERFACE -> Media Player's "Open folder" crashed). Other apartments (0x21 broker statics ...) stay inline. */
  if ((flags & 0x20) && apt == 0) {
    APTTYPE at = APTTYPE_MTA; APTTYPEQUALIFIER aq; HANDLE th; DWORD idx;
    if (SUCCEEDED(CoGetApartmentType(&at, &aq)) && (at == APTTYPE_STA || at == APTTYPE_MAINSTA)) {
      ((ADD)vt[1])(task);
      th = CreateThread(NULL, 0, tp_worker, task, 0, NULL);
      if (th) { CoWaitForMultipleHandles(0, INFINITE, 1, &th, &idx); CloseHandle(th); slog("[tpool] SHTaskPoolQueueTask pool=%lu flags=0x%lx apt=0x%lx (MTA worker, waited)", pool, flags, apt); return S_OK; }
      ((void (STDMETHODCALLTYPE *)(void *))vt[2])(task); } }
  if (flags & 0x20) { slog("[tpool] SHTaskPoolQueueTask pool=%lu flags=0x%lx apt=0x%lx (inline)", pool, flags, apt); ((RUN)vt[3])(task); return S_OK; }
  slog("[tpool] SHTaskPoolQueueTask pool=%lu flags=0x%lx apt=0x%lx (worker)", pool, flags, apt);
  ((ADD)vt[1])(task);
  if (!QueueUserWorkItem(tp_worker, task, WT_EXECUTEDEFAULT)) tp_worker(task);
  return S_OK; }
__declspec(dllexport) HRESULT WINAPI SHTaskPoolAllowThreadReuse(void) { return S_OK; }
__declspec(dllexport) HRESULT WINAPI GetSecurityInfo_stub(void) { return E_NOTIMPL; }

/* ---------------- package identity (kernelbase / appmodel-state / psm): "not packaged" until R2 ---------------- */
#define PKGSTUB(name) __declspec(dllexport) LONG WINAPI name(void) { STUB_LOG(#name); return ERROR_NO_PACKAGE_ID; }
/* Media Foundation policy queries (Win10 mfplat/Windows.Media). A stub's E_NOTIMPL made MFStartup fail, and every MF object then answered
   MF_E_SHUTDOWN ("Media Player cannot start"). Codec loading: 0 = all codecs allowed. Media behaviours: callers pre-zero the BOOL, so S_OK = "not enabled". */
__declspec(dllexport) LONG WINAPI AppPolicyGetMediaFoundationCodecLoading(HANDLE token, int *policy) { (void)token; if (policy) *policy = 0; return ERROR_SUCCESS; }
__declspec(dllexport) HRESULT WINAPI IsMediaBehaviorEnabled(void *behavior, void *enabled) { (void)behavior; (void)enabled; return S_OK; }
/* Store media extensions (codec packages): none on Win7. The stub's "success" with a NULL factory crashed mfplat's
   CStoreByteStreamRegistrar while the source resolver looked up the FLAC handler. */
__declspec(dllexport) HRESULT WINAPI GetMediaExtensionCommunicationFactory(void **out) { if (out) *out = NULL; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
/* the other CompPkgSup entry points (mfplat's call sites: (0, HSTRING category, IVector<IPropertySet> **out) for the package info):
   no media component packages either - failures, which the callers handle; the stubs' S_OK with NULL results crashed */
__declspec(dllexport) HRESULT WINAPI GetMediaComponentPackageInfo(void *a, void *b, void **out) { (void)a; (void)b; if (out) *out = NULL; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
__declspec(dllexport) HRESULT WINAPI InstantiateComponentFromPackage(void *a, void *b, void *c, void **out) { (void)a; (void)b; (void)c; if (out) *out = NULL; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
__declspec(dllexport) HRESULT WINAPI GetServerForPMP(void **out) { if (out) *out = NULL; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
__declspec(dllexport) HRESULT WINAPI RequireNetworkDuringMediaTaskCompletion(DWORD a, void *b) { (void)a; (void)b; return S_OK; }   /* callers pre-zero the BOOL */
/* GetCurrentPackageInfo/2/3, the *Context APIs, GetStagedPackagePathByFullName(2), AppXGetOSMaxVersionTested: see wxrpackage.inc */
/* GetPackageResourcesProperty(ctx, id, UINT32 *size, void *buf, UINT32 *flags): Win10 reads a property list kept in the context and answers
   ERROR_NOT_FOUND for ids 1-6 it does not hold. We hold none. (As a "no package" stub it became an InvalidOperationException in the Store.) */
__declspec(dllexport) LONG WINAPI GetPackageResourcesProperty(void *ctx, ULONG id, UINT32 *size, void *buf, UINT32 *flags) {
  static LONG n; (void)flags; if (!ctx || !size || (*size && !buf)) return ERROR_INVALID_PARAMETER;
  if (InterlockedIncrement(&n) <= 8) slog("[pkg] GetPackageResourcesProperty(id %lu) -> not found", id); return (id >= 1 && id <= 6) ? ERROR_NOT_FOUND : ERROR_INVALID_PARAMETER; }
PKGSTUB(GetSystemMetadataPathForPackage)
PKGSTUB(GetPackageStatusForUser) PKGSTUB(GetStagedPackageOrigin)

/* Win10 keeps the user's language list under HKCU\Control Panel\International\User Profile (Languages = REG_MULTI_SZ, one subkey per language); Win7 has no such key,
   so WinRT ApplicationLanguages / MRM resource lookups would see an empty list. Create it once from the Win7 UI language (+ en-US as the fallback). */
static void ensure_user_profile_languages(void) {
  static LONG done; HKEY k = NULL, lk; WCHAR langs[200] = L""; ULONG n = 0, sz = 190; DWORD i, len = 0; typedef BOOL (WINAPI *PGet)(DWORD, PULONG, PWSTR, PULONG); PGet g;
  if (InterlockedExchange(&done, 1)) return;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\International\\User Profile", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &k, NULL)) return;
  if (!RegQueryValueExW(k, L"Languages", NULL, NULL, NULL, &len) && len > 4) { RegCloseKey(k); return; }   /* already present */
  g = (PGet)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetUserPreferredUILanguages");
  if (g && g(MUI_LANGUAGE_NAME, &n, langs, &sz) && n) { /* langs is already a MULTI_SZ */ for (len = 0; len < 190 && !(langs[len] == 0 && langs[len + 1] == 0); len++) ; len += 2; }
  else { lstrcpyW(langs, L"en-US"); len = 6; langs[len] = 0; }
  { int has_en = 0; WCHAR *p; for (p = langs; *p; p += wcslen(p) + 1) if (!_wcsnicmp(p, L"en-US", 5)) has_en = 1;
    if (!has_en) { len = 0; for (p = langs; *p; p += wcslen(p) + 1) len += (DWORD)wcslen(p) + 1; lstrcpyW(langs + len, L"en-US"); len += 6; langs[len] = 0; len++; } }
  RegSetValueExW(k, L"Languages", 0, REG_MULTI_SZ, (BYTE *)langs, len * 2);
  { WCHAR *p; for (p = langs; *p; p += wcslen(p) + 1) if (!RegCreateKeyExW(k, p, 0, NULL, 0, KEY_WRITE, NULL, &lk, NULL)) RegCloseKey(lk); }
  RegCloseKey(k); (void)i;
}
/* OpenGlobalizationUserSettingsKey(REGSAM sam, PCWSTR subkey|NULL, HKEY *out): HKCU\Control Panel\International[\subkey] */
__declspec(dllexport) LONG WINAPI OpenGlobalizationUserSettingsKey(DWORD sam, const WCHAR *sub, HKEY *out) {
  WCHAR path[260]; ensure_user_profile_languages(); _snwprintf(path, 259, L"Control Panel\\International%s%s", (sub && *sub) ? L"\\" : L"", (sub && *sub) ? sub : L""); path[259] = 0;
  return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, sam, out);
}/* state repository is unavailable on Win7: callers treat a failed Open as "no cache" */
__declspec(dllexport) LONG WINAPI SRCacheManager_Open(void) { STUB_LOG("SRCacheManager_Open"); return (LONG)0x80070426; }
__declspec(dllexport) LONG WINAPI SRCacheContext_Open(void) { STUB_LOG("SRCacheContext_Open"); return (LONG)0x80070426; }

/* Win10 lets PROCESS_QUERY_LIMITED_INFORMATION handles be used for GetModuleFileNameEx etc.; Win7 needs query+vm_read */
__declspec(dllexport) HANDLE WINAPI OpenProcess(DWORD access, BOOL inh, DWORD pid) {
  typedef HANDLE (WINAPI *F)(DWORD, BOOL, DWORD); static F f; HANDLE h;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "OpenProcess");
  h = f(access, inh, pid);
  if ((access & 0x1000) && !(access & 0x410)) { HANDLE h2 = f(access | 0x410, inh, pid); if (h2) { if (h) CloseHandle(h); h = h2; } }
  return h;
}

/* ---- profapi private exports (Win10 ordinals 104/113/114 are imported by ordinal; names from the public PDB) ---- */
static HRESULT prof_copy(const WCHAR *src, WCHAR *buf, DWORD cch) {
  size_t n = wcslen(src); if (!buf || n + 1 > cch) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  memcpy(buf, src, (n + 1) * sizeof(WCHAR)); return S_OK;
}
/* GetBasicProfileFolderPath(type, token, buf, cch): 1 Users, 2 Default, 3 Public, 4 ProgramData, 5/6 user profile, 7 AppData, 8 Local AppData */
__declspec(dllexport) HRESULT WINAPI GetBasicProfileFolderPath(DWORD type, HANDLE tok, WCHAR *buf, DWORD cch) {
  WCHAR v[MAX_PATH], t[MAX_PATH], *s; DWORD n = 0; (void)tok;
  switch (type) {
  case 4: n = GetEnvironmentVariableW(L"ALLUSERSPROFILE", v, MAX_PATH); break;
  case 5: case 6: n = GetEnvironmentVariableW(L"USERPROFILE", v, MAX_PATH); break;
  case 7: n = GetEnvironmentVariableW(L"APPDATA", v, MAX_PATH); break;
  case 8: n = GetEnvironmentVariableW(L"LOCALAPPDATA", v, MAX_PATH); break;
  case 1: case 2: case 3:
    n = GetEnvironmentVariableW(L"USERPROFILE", t, MAX_PATH); if (!n || n >= MAX_PATH) break;
    s = wcsrchr(t, L'\\'); if (s) *s = 0;
    _snwprintf(v, MAX_PATH, L"%s%s", t, type == 1 ? L"" : type == 2 ? L"\\Default" : L"\\Public"); v[MAX_PATH - 1] = 0; n = (DWORD)wcslen(v); break;
  }
  if (!n || n >= MAX_PATH) return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
  return prof_copy(v, buf, cch);
}
LONG WINAPI GetCurrentPackageFamilyName(UINT32 *len, WCHAR *name);
static void prof_pkg_base(WCHAR *out) {
  WCHAR pfn[256], la[MAX_PATH]; UINT32 n = 256; pfn[0] = 0;
  GetCurrentPackageFamilyName(&n, pfn);
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) la[0] = 0;
  _snwprintf(out, MAX_PATH, L"%s\\Packages\\%s", la, pfn[0] ? pfn : L"unpackaged"); out[MAX_PATH - 1] = 0;
}
/* GetAppContainerSpecificSubPath(sid_or_name, kind, buf, cch): per-package storage root */
__declspec(dllexport) HRESULT WINAPI GetAppContainerSpecificSubPath(void *id, void *kind, WCHAR *buf, DWORD cch) {
  WCHAR p[MAX_PATH]; (void)id; (void)kind; prof_pkg_base(p); CreateDirectoryW(p, NULL); return prof_copy(p, buf, cch);
}
/* GetAppContainerRegistryHandleFromName(name, reserved, subpath, sam, HKEY*): HKCU\Software\WXR\AppContainer\<name>\<subpath> */
__declspec(dllexport) HRESULT WINAPI GetAppContainerRegistryHandleFromName(const WCHAR *name, void *res, const WCHAR *sub, DWORD sam, HKEY *out) {
  WCHAR path[600]; LONG r; DWORD disp; (void)res;
  if (!out) return E_POINTER; *out = NULL;
  if (name && IsBadStringPtrW(name, 256)) name = NULL;
  _snwprintf(path, 599, L"Software\\WXR\\AppContainer\\%s%s%s", (name && *name) ? name : L"default", (sub && *sub) ? L"\\" : L"", (sub && *sub) ? sub : L""); path[599] = 0;
  r = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, sam, NULL, out, &disp);
  return r ? HRESULT_FROM_WIN32(r) : S_OK;
}

/* combase / shcore private apartment + DPI helpers: single-apartment no-op emulation */
__declspec(dllexport) HRESULT WINAPI CoGetApartmentIdentifier(unsigned long long *id) { if (!id) return E_INVALIDARG; *id = (unsigned long long)GetCurrentThreadId(); return S_OK; }
__declspec(dllexport) HRESULT WINAPI CoRegisterForApartmentShutdown(void *cb, unsigned long long *id, void **cookie) {
  (void)cb; if (id) *id = (unsigned long long)GetCurrentThreadId(); if (cookie) *cookie = (void *)(ULONG_PTR)1; return S_OK; }
__declspec(dllexport) HRESULT WINAPI CoRegisterServerShutdownDelay(DWORD ms) { (void)ms; return S_OK; }
__declspec(dllexport) void WINAPI CoSignalPendingGitRegistrationWaits(void) { }
__declspec(dllexport) HRESULT WINAPI RegisterScaleChangeNotificationsForWindow(HWND h, void *sink, void *ctx, DWORD *cookie) { (void)h; (void)sink; (void)ctx; if (cookie) *cookie = 1; return S_OK; }
__declspec(dllexport) HRESULT WINAPI RevokeScaleChangeNotificationsForWindow(HWND h, DWORD cookie) { (void)h; (void)cookie; return S_OK; }
__declspec(dllexport) HRESULT WINAPI RegisterScaleChangeSinkForWindow(void *a, void *b, void *c) { (void)a; (void)b; (void)c; return S_OK; }
