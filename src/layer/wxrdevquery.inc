/* wxrdevquery.inc - devquery (api-ms-win-devices-query): the backend of Windows.Devices.Enumeration. Included by wxrsys.c. */
/* ---- devquery (api-ms-win-devices-query-l1-1-0/1, Win8+; Windows.Devices.Enumeration's backend) ----------------------------
   Win7 has no devquery. The generated thunks "succeeded" without ever calling the result callback, so DeviceInformation
   FindAllAsync/CreateFromIdAsync never completed (Bedrock's main thread waited forever). Here every query completes: the
   callback gets, asynchronously, StateChange(EnumerationCompleted) with no objects = "none found"; FromId queries fail with ERROR_NOT_FOUND. A watcher
   stays open and quiet until DevCloseObjectQuery. */
typedef void (WINAPI *DevQCb)(void *hq, void *ctx, const void *action);
typedef struct { LONG ref; DevQCb cb; void *ctx; } DevQ;
static void devq_rel(DevQ *q) { if (InterlockedDecrement(&q->ref) == 0) free(q); }
static DWORD WINAPI devq_run(void *p) {
  DevQ *q = (DevQ *)p; struct { int action; int state; BYTE pad[56]; } ad;   /* DEV_QUERY_RESULT_ACTION_DATA: Action + union{State, DEV_OBJECT} */
  memset(&ad, 0, sizeof ad); ad.action = 0; ad.state = 1;                    /* DevQueryResultStateChange, DevQueryStateEnumCompleted */
  if (q->cb) q->cb(q, q->ctx, &ad); devq_rel(q); return 0; }
static HRESULT devq_new(const char *what, int type, const WCHAR *id, void *cb, void *ctx, void **out) {
  DevQ *q; if (!out) return E_POINTER; *out = NULL; if (!cb) return E_INVALIDARG;
  /* a FromId query that "completes" without the object becomes a DeviceInformation with no data (W.D.E then derefs NULL):
     report the id as not found instead, so CreateFromIdAsync fails cleanly. */
  if (id) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 6) slog("[devq] %s(type %d, id %ls) -> not found", what, type, id); return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
  if (!(q =(DevQ *)calloc(1, sizeof *q))) return E_OUTOFMEMORY;
  q->ref = 2; q->cb = (DevQCb)cb; q->ctx = ctx; *out = q;
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 6) slog("[devq] %s(type %d%s%ls) -> empty result", what, type, id ? ", id " : "", id ? id : L""); }
  if (!QueueUserWorkItem(devq_run, q, WT_EXECUTEDEFAULT)) { q->ref = 1; devq_run(q); }
  return S_OK; }
__declspec(dllexport) HRESULT WINAPI DevCreateObjectQuery(int type, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, void *cb, void *ctx, void **hq) {
  (void)fl; (void)np; (void)props; (void)nf; (void)filt; return devq_new("DevCreateObjectQuery", type, NULL, cb, ctx, hq); }
__declspec(dllexport) HRESULT WINAPI DevCreateObjectQueryEx(int type, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, ULONG ne, const void *ext, void *cb, void *ctx, void **hq) {
  (void)fl; (void)np; (void)props; (void)nf; (void)filt; (void)ne; (void)ext; return devq_new("DevCreateObjectQueryEx", type, NULL, cb, ctx, hq); }
__declspec(dllexport) HRESULT WINAPI DevCreateObjectQueryFromId(int type, const WCHAR *id, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, void *cb, void *ctx, void **hq) {
  (void)fl; (void)np; (void)props; (void)nf; (void)filt; return devq_new("DevCreateObjectQueryFromId", type, id, cb, ctx, hq); }
__declspec(dllexport) HRESULT WINAPI DevCreateObjectQueryFromIdEx(int type, const WCHAR *id, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, ULONG ne, const void *ext, void *cb, void *ctx, void **hq) {
  (void)fl; (void)np; (void)props; (void)nf; (void)filt; (void)ne; (void)ext; return devq_new("DevCreateObjectQueryFromIdEx", type, id, cb, ctx, hq); }
__declspec(dllexport) HRESULT WINAPI DevCreateObjectQueryFromIds(int type, const WCHAR *ids, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, void *cb, void *ctx, void **hq) {
  (void)fl; (void)np; (void)props; (void)nf; (void)filt; return devq_new("DevCreateObjectQueryFromIds", type, ids, cb, ctx, hq); }
__declspec(dllexport) void WINAPI DevCloseObjectQuery(void *hq) { if (hq) devq_rel((DevQ *)hq); }
__declspec(dllexport) HRESULT WINAPI DevGetObjects(int type, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, ULONG *n, const void **objs) {
  (void)type; (void)fl; (void)np; (void)props; (void)nf; (void)filt; if (n) *n = 0; if (objs) *objs = NULL; return S_OK; }
__declspec(dllexport) HRESULT WINAPI DevGetObjectsEx(int type, ULONG fl, ULONG np, const void *props, ULONG nf, const void *filt, ULONG ne, const void *ext, ULONG *n, const void **objs) {
  (void)type; (void)fl; (void)np; (void)props; (void)nf; (void)filt; (void)ne; (void)ext; if (n) *n = 0; if (objs) *objs = NULL; return S_OK; }
__declspec(dllexport) void WINAPI DevFreeObjects(ULONG n, const void *objs) { (void)n; (void)objs; }
__declspec(dllexport) HRESULT WINAPI DevGetObjectProperties(int type, const WCHAR *id, ULONG fl, ULONG np, const void *props, ULONG *n, const void **out) {
  (void)type; (void)id; (void)fl; (void)np; (void)props; if (n) *n = 0; if (out) *out = NULL; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
__declspec(dllexport) HRESULT WINAPI DevGetObjectPropertiesEx(int type, const WCHAR *id, ULONG fl, ULONG np, const void *props, ULONG ne, const void *ext, ULONG *n, const void **out) {
  (void)type; (void)id; (void)fl; (void)np; (void)props; (void)ne; (void)ext; if (n) *n = 0; if (out) *out = NULL; return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); }
__declspec(dllexport) void WINAPI DevFreeObjectProperties(ULONG n, const void *props) { (void)n; (void)props; }
__declspec(dllexport) const void *WINAPI DevFindProperty(const void *key, int store, const WCHAR *locale, ULONG n, const void *props) { (void)key; (void)store; (void)locale; (void)n; (void)props; return NULL; }
__declspec(dllexport) NTSTATUS NTAPI RtlConvertDeviceFamilyInfoToString(PULONG fsz, PULONG msz, PWSTR fam, PWSTR form) {
  static const WCHAR F[] = L"Windows.Desktop", M[] = L"Unknown"; NTSTATUS r = STATUS_SUCCESS;
  if (!fsz || !msz) return STATUS_INVALID_PARAMETER;
  if (!fam || *fsz < sizeof F) { *fsz = sizeof F; r = STATUS_BUFFER_TOO_SMALL; } else { memcpy(fam, F, sizeof F); *fsz = sizeof F; }
  if (!form || *msz < sizeof M) { *msz = sizeof M; r = STATUS_BUFFER_TOO_SMALL; } else { memcpy(form, M, sizeof M); *msz = sizeof M; }
  return r;
}
/* RtlQueryPackageClaims and the GetCurrentPackage... / GetPackage... family live in wxrpackage.inc */
static void log_callers(const char *tag);
__declspec(dllexport) NTSTATUS NTAPI NtQueryWnfStateData(PVOID a, PVOID b, PVOID c, PVOID d, PVOID e, PVOID f) {
  (void)b;(void)c;(void)e; { static volatile LONG n; static unsigned long long seen[32]; static int ns; unsigned long long nm = a ? *(unsigned long long *)a : 0; int k, hit = 0; LONG cnt = InterlockedIncrement(&n);
    for (k = 0; k < ns; k++) if (seen[k] == nm) hit = 1; if (!hit && ns < 32) { seen[ns++] = nm; slog("[wnf] NtQueryWnfStateData new state %016llX (bufsize %lu) call #%ld", nm, f ? *(ULONG *)f : 0, cnt); log_callers("wnf"); } }
  /* no publisher exists for any WNF state here: report "never published" (change stamp 0, no data) */
  if (d) *(ULONG *)d = 0; if (f) *(ULONG *)f = 0; return STATUS_SUCCESS; }
__declspec(dllexport) NTSTATUS NTAPI ZwQueryWnfStateData(PVOID a, PVOID b, PVOID c, PVOID d, PVOID e, PVOID f) { return NtQueryWnfStateData(a,b,c,d,e,f); }
__declspec(dllexport) NTSTATUS NTAPI NtSetInformationVirtualMemory(HANDLE p, int cls, ULONG_PTR n, PVOID e, PVOID i, ULONG l) {
  (void)p;(void)cls;(void)n;(void)e;(void)i;(void)l; return STATUS_SUCCESS; /* a hint API (prefetch); ignoring is correct */ }
