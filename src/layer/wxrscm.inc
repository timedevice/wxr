/* wxrscm.inc - service-control stand-ins and fail-fast with a stack trace. Included by wxrsys.c. */
/* ================= service-control stand-ins =================
   CoreMessaging.dll *is* the CoreMessagingRegistrar service.  The server side (wxrsvc.exe calls its ServiceMain) registers with the SCM; clients check that the
   service exists/runs.  There is no SCM involved here: these answer for the one service we host ourselves and pass everything else to advapi32. */
#define FAKE_SCM ((SC_HANDLE)(ULONG_PTR)0xC0DE0001)
#define FAKE_SVC ((SC_HANDLE)(ULONG_PTR)0xC0DE0002)
#define FAKE_STH ((SERVICE_STATUS_HANDLE)(ULONG_PTR)0xC0DE0003)
static FARPROC adv(const char *n) { HMODULE m = GetModuleHandleA("advapi32.dll"); if (!m) m = LoadLibraryA("advapi32.dll"); return m ? GetProcAddress(m, n) : NULL; }
__declspec(dllexport) SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR m, LPCWSTR d, DWORD a) { slog("[scm] OpenSCManagerW -> fake"); (void)m;(void)d;(void)a; return FAKE_SCM; }
__declspec(dllexport) SC_HANDLE WINAPI OpenServiceW(SC_HANDLE h, LPCWSTR n, DWORD a) {
  /* other services (AudioSrv: Windows.Media.MediaControl's SoundLevelBroker checks it when the audio renderer rebuilds its device, e.g. on a
     playback-rate change; "does not exist" became "We can't find an audio playback device") come from Win7's real SCM */
  if (h == FAKE_SCM) { static SC_HANDLE real; SC_HANDLE s;
    if (n && !lstrcmpiW(n, L"CoreMessagingRegistrar")) { slog("[scm] OpenServiceW(%ls) -> fake", n); return FAKE_SVC; }
    /* Windows Search: Win10's Windows.Storage queries a folder that Win7 indexes through the indexer with a Win10-only OLE DB command
       interface {48B37235} that Win7's Search.CollatorDSO lacks: GetFilesAsync failed E_NOINTERFACE (windows.storage.search
       CResultSetFactory::_CreateCommand) and Media Player's "Open folder" died. "Not installed" makes Win10 code
       (IsMSSearchEnabled) use its file-system enumeration, which works for every folder. */
    if (n && !lstrcmpiW(n, L"wsearch")) { slog("[scm] OpenServiceW(%ls) -> not installed (Win10 search uses the file system)", n); SetLastError(ERROR_SERVICE_DOES_NOT_EXIST); return NULL; }
    if (!real) { typedef SC_HANDLE (WINAPI *M)(LPCWSTR, LPCWSTR, DWORD); M mf = (M)adv("OpenSCManagerW"); SC_HANDLE r = mf ? mf(NULL, NULL, SC_MANAGER_CONNECT) : NULL;
      if (r && InterlockedCompareExchangePointer((void **)&real, r, NULL)) { typedef BOOL (WINAPI *C)(SC_HANDLE); ((C)adv("CloseServiceHandle"))(r); } }
    { typedef SC_HANDLE (WINAPI *F)(SC_HANDLE, LPCWSTR, DWORD); F f = (F)adv("OpenServiceW"); s = (real && f) ? f(real, n, a) : NULL; }
    slog("[scm] OpenServiceW(%ls) -> real %p (err %lu)", n ? n : L"?", (void *)s, s ? 0 : GetLastError()); if (!s && !real) SetLastError(ERROR_SERVICE_DOES_NOT_EXIST); return s; }
  { typedef SC_HANDLE (WINAPI *F)(SC_HANDLE, LPCWSTR, DWORD); F f = (F)adv("OpenServiceW"); return f ? f(h, n, a) : NULL; } }
__declspec(dllexport) BOOL WINAPI CloseServiceHandle(SC_HANDLE h) { if (h == FAKE_SCM || h == FAKE_SVC) return TRUE; { typedef BOOL (WINAPI *F)(SC_HANDLE); F f = (F)adv("CloseServiceHandle"); return f ? f(h) : FALSE; } }
__declspec(dllexport) BOOL WINAPI QueryServiceStatus(SC_HANDLE h, LPSERVICE_STATUS st) {
  if (h == FAKE_SVC) { memset(st, 0, sizeof *st); st->dwServiceType = 0x20; st->dwCurrentState = SERVICE_RUNNING; st->dwControlsAccepted = 1; slog("[scm] QueryServiceStatus -> RUNNING"); return TRUE; }
  { typedef BOOL (WINAPI *F)(SC_HANDLE, LPSERVICE_STATUS); F f = (F)adv("QueryServiceStatus"); return f ? f(h, st) : FALSE; } }
__declspec(dllexport) BOOL WINAPI StartServiceW(SC_HANDLE h, DWORD n, LPCWSTR *a) {
  if (h == FAKE_SVC) { slog("[scm] StartServiceW -> ok (registrar is hosted by wxrsvc.exe)"); return TRUE; }
  { typedef BOOL (WINAPI *F)(SC_HANDLE, DWORD, LPCWSTR *); F f = (F)adv("StartServiceW"); return f ? f(h, n, a) : FALSE; } }
__declspec(dllexport) SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExW(LPCWSTR n, LPHANDLER_FUNCTION_EX h, LPVOID c) { (void)h;(void)c; slog("[scm] RegisterServiceCtrlHandlerExW(%ls)", n ? n : L"?"); return FAKE_STH; }
__declspec(dllexport) BOOL WINAPI SetServiceStatus(SERVICE_STATUS_HANDLE h, LPSERVICE_STATUS st) { slog("[scm] SetServiceStatus state=%lu controls=0x%lx", st ? st->dwCurrentState : 0, st ? st->dwControlsAccepted : 0); (void)h; return TRUE; }

/* ================= fail-fast with a stack trace ================= */
static void log_frames(const char *tag, int skip) {
  void *fr[20]; USHORT n = CaptureStackBackTrace(skip, 20, fr, NULL), i;
  for (i = 0; i < n; i++) { HMODULE m = NULL; char nm[MAX_PATH] = "?"; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fr[i], &m);
    if (m) { char *b; GetModuleFileNameA(m, nm, MAX_PATH); b = strrchr(nm, '\\'); if (b) memmove(nm, b + 1, strlen(b)); }
    slog("[%s]   #%u %s+0x%llx", tag, i, nm, (unsigned long long)((char *)fr[i] - (char *)m)); }
}
__declspec(dllexport) VOID WINAPI RaiseFailFastException(PEXCEPTION_RECORD r, PCONTEXT c, DWORD flags) {
  typedef VOID (WINAPI *F)(PEXCEPTION_RECORD, PCONTEXT, DWORD); F f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "RaiseFailFastException");
  slog("[failfast] code=0x%08lX flags=0x%lx params=%llx,%llx,%llx,%llx", r ? r->ExceptionCode : 0, flags, r && r->NumberParameters > 0 ? (unsigned long long)r->ExceptionInformation[0] : 0ull,
       r && r->NumberParameters > 1 ? (unsigned long long)r->ExceptionInformation[1] : 0ull, r && r->NumberParameters > 2 ? (unsigned long long)r->ExceptionInformation[2] : 0ull, r && r->NumberParameters > 3 ? (unsigned long long)r->ExceptionInformation[3] : 0ull);
  log_frames("failfast", 0);
  if (f) f(r, c, flags); TerminateProcess(GetCurrentProcess(), 0xC0000409);
}
