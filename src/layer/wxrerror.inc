/* wxrerror.inc - RoOriginateError family, restricted error info, crash-report metadata. Included by wxrsys.c. */
/* RoOriginateError family: pass-through to the system's (VxKex) implementation. With WXR_LOGERR=1 every originated failure is logged
   with its message and the originating module+RVA, which names the WinRT call behind an exception the app swallows. */
static void err_log(const char *fn, HRESULT hr, const WCHAR *msg, UINT32 n, void *ret)
{
  static int on = -1; static volatile LONG cnt; HMODULE m = NULL; WCHAR mn[MAX_PATH] = L"?";
  if (on < 0) on = GetEnvironmentVariableA("WXR_LOGERR", NULL, 0) ? 1 : 0;
  if (!on || SUCCEEDED(hr) || hr == (HRESULT)0x80040111 /* CLASS_E_CLASSNOTAVAILABLE: DllGetActivationFactory probes */ || InterlockedIncrement(&cnt) > 200) return;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ret, &m); if (m) GetModuleFileNameW(m, mn, MAX_PATH);
  slog("[err] %s 0x%08lX from %ls+0x%Ix: %.*ls", fn, (unsigned long)hr, wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, (ULONG_PTR)ret - (ULONG_PTR)m, (int)(n > 200 ? 200 : n), msg ? msg : L"");
  if (GetEnvironmentVariableA("WXR_LOGERR_STACK", NULL, 0)) log_callers("[err]   stack");
}
static FARPROC err_real(const char *name) { HMODULE m = LoadLibraryA("api-ms-win-core-winrt-error-l1-1-0.dll"); FARPROC p = m ? GetProcAddress(m, name) : NULL; return p; }
__declspec(dllexport) BOOL WINAPI RoOriginateError(HRESULT hr, void *msg)
{ typedef BOOL (WINAPI *F)(HRESULT, void *); static F f; UINT32 n = 0; const WCHAR *s = msg && hs_init() ? p_raw(msg, &n) : NULL;
  err_log("RoOriginateError", hr, s, n, __builtin_return_address(0));
  { char w[16], md[64] = ""; static LONG k; if (GetEnvironmentVariableA("WXR_ERRSTACK", w, 16) && (HRESULT)strtoul(w, NULL, 16) == hr) {   /* WXR_ERRSTACK=<hr hex> [WXR_ERRSTACK_MOD=<module name part>] */
      HMODULE em = NULL; char ep[MAX_PATH] = ""; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)__builtin_return_address(0), &em); if (em) GetModuleFileNameA(em, ep, MAX_PATH);
      if ((!GetEnvironmentVariableA("WXR_ERRSTACK_MOD", md, 64) || strstr(CharLowerA(ep), CharLowerA(md))) && InterlockedIncrement(&k) <= 12) log_callers("error"); } }
  if (!f) f = (F)err_real("RoOriginateError"); return f ? f(hr, msg) : FALSE; }
/* diagnostic (WXR_LOGERR): .NET (CsWinRT) asks for the restricted error info whenever a WinRT call it made failed - the only trace of
   failures that stay inside managed code. Logs the error and the app-code stack. */
__declspec(dllexport) HRESULT WINAPI GetRestrictedErrorInfo(IUnknown **out) {
  typedef HRESULT (WINAPI *F)(IUnknown **); static F f; static int on = -1; HRESULT hr; HMODULE cm = NULL; WCHAR cp[MAX_PATH] = L"";
  if (!f) f = (F)err_real("GetRestrictedErrorInfo"); hr = f ? f(out) : E_NOTIMPL;
  if (on < 0) on = GetEnvironmentVariableA("WXR_LOGERR", NULL, 0) ? 1 : 0;
  if (on && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &cm)
      && GetModuleFileNameW(cm, cp, MAX_PATH) && wcsstr(cp, L"\\WindowsApps\\")) { static LONG k; HRESULT e = 0; BSTR d = NULL, rd = NULL, sid = NULL;
    if (hr == S_OK && out && *out) { typedef HRESULT (STDMETHODCALLTYPE *GD)(IUnknown *, BSTR *, HRESULT *, BSTR *, BSTR *); ((GD)(*(void ***)*out)[3])(*out, &d, &e, &rd, &sid); }
    if (InterlockedIncrement(&k) <= 1000) { slog("[err] app GetRestrictedErrorInfo -> 0x%08lX error 0x%08lX \"%ls\"", (unsigned long)hr, (unsigned long)e, rd ? rd : (d ? d : L"")); log_callers("restricted error"); }
    if (d) SysFreeString(d); if (rd) SysFreeString(rd); if (sid) SysFreeString(sid); }
  return hr; }
__declspec(dllexport) BOOL WINAPI RoOriginateErrorW(HRESULT hr, UINT n, const WCHAR *msg)
{ typedef BOOL (WINAPI *F)(HRESULT, UINT, const WCHAR *); static F f;
  err_log("RoOriginateErrorW", hr, msg, msg ? (n ? n : (UINT)lstrlenW(msg)) : 0, __builtin_return_address(0)); if (!f) f = (F)err_real("RoOriginateErrorW"); return f ? f(hr, n, msg) : FALSE; }
/* kernel32 (Win10 1703+): custom crash-report metadata. Win7 WER has none; accepted and dropped. The Store P/Invokes it from a static
   constructor - as a missing export it became a TypeInitializationException in a dispatcher callback. */
__declspec(dllexport) HRESULT WINAPI WerRegisterCustomMetadata(const WCHAR *key, const WCHAR *value) { (void)value; return key ? S_OK : E_INVALIDARG; }
__declspec(dllexport) HRESULT WINAPI WerUnregisterCustomMetadata(const WCHAR *key) { return key ? S_OK : E_INVALIDARG; }
__declspec(dllexport) BOOL WINAPI RoOriginateLanguageException(HRESULT hr, void *msg, IUnknown *lang)
{ typedef BOOL (WINAPI *F)(HRESULT, void *, IUnknown *); static F f; UINT32 n = 0; const WCHAR *s = msg && hs_init() ? p_raw(msg, &n) : NULL;
  err_log("RoOriginateLanguageException", hr, s, n, __builtin_return_address(0));
  { HMODULE cm = NULL; WCHAR cp[MAX_PATH] = L""; if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)__builtin_return_address(0), &cm)
      && GetModuleFileNameW(cm, cp, MAX_PATH) && wcsstr(cp, L"\\WindowsApps\\") && !wcsstr(cp, L"Microsoft.UI.Xaml")) { static LONG k; if (InterlockedIncrement(&k) <= 20) { log_callers("language exception");
        /* .NET Native's exception object gives the stack where it was THROWN (ILanguageExceptionStackBackTrace); the stack above is often
           only the async continuation that reports it */
        static const GUID IID_ILangBT = { 0xCBE53FB5, 0xF967, 0x4258, { 0x8D, 0x34, 0x42, 0xF5, 0xE2, 0x58, 0x33, 0xDE } }; IUnknown *bt = NULL;
        if (lang && SUCCEEDED(lang->lpVtbl->QueryInterface(lang, &IID_ILangBT, (void **)&bt)) && bt) { ULONG_PTR ip[128]; ULONG c = 0, i; char line[900]; int p = 0;
          if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, ULONG, ULONG_PTR *, ULONG *))(*(void ***)bt)[3])(bt, 128, ip, &c)))
            for (i = 0; i < c && p < 860; i++) { HMODULE m = NULL; char nm[MAX_PATH] = "?"; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ip[i], &m);
              if (m) { GetModuleFileNameA(m, nm, MAX_PATH); { char *s2 = strrchr(nm, '\\'); if (s2) memmove(nm, s2 + 1, strlen(s2)); } }
              if (!m) { line[p < 899 ? p : 899] = 0; if (p) slog("[stack] thrown at:%s", line); p = 0; continue; }   /* one line per (re)throw segment */
              p += _snprintf(line + p, 900 - p, " %s+0x%lx", nm, (unsigned long)(ip[i] - (ULONG_PTR)m)); }
          line[p < 899 ? p : 899] = 0; if (p) slog("[stack] thrown at:%s", line); bt->lpVtbl->Release(bt); } } }
    { char w[16]; static LONG k2; if (GetEnvironmentVariableA("WXR_ERRSTACK", w, 16) && (HRESULT)strtoul(w, NULL, 16) == hr && InterlockedIncrement(&k2) <= 12) log_callers("language error"); } }
  if (!f) { HMODULE m = LoadLibraryA("api-ms-win-core-winrt-error-l1-1-1.dll"); f = m ? (F)GetProcAddress(m, "RoOriginateLanguageException") : NULL; }
  return f ? f(hr, msg, lang) : FALSE; }
