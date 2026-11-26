/* wxrmisc.inc - windowing model, the Media Foundation audio renderer, SDDL, locale and calendar info, error helpers. Included by wxrsys.c. */
/* AppPolicyGetWindowingModel: 1 Universal for a UWP app of the process's package, 2 ClassicDesktop otherwise (full trust, unpackaged) - as
   Win10 answers. VxKex's says ClassicDesktop, and the Store (WinStore.App.exe = StoreDesktopExtension.exe, one file) then ran as its own
   desktop extension and never showed a window. */
static int pkg_is_uwp_app(void);   /* wxrpackage.inc */
__declspec(dllexport) LONG WINAPI AppPolicyGetWindowingModel(HANDLE token, int *policy) {
  int u; (void)token; if (!policy) return ERROR_INVALID_PARAMETER;
  u = pkg_is_uwp_app(); *policy = u ? 1 : 2;
  { static LONG n; if (InterlockedIncrement(&n) <= 5) slog("[policy] AppPolicyGetWindowingModel -> %s", u ? "Universal" : "ClassicDesktop"); }
  return ERROR_SUCCESS; }
__declspec(dllexport) LONG WINAPI AppPolicyGetLifecycleManagement(HANDLE token, int *policy) { (void)token; if (policy) *policy = 1; return ERROR_SUCCESS; }   /* Unmanaged: no PLM here */
/* MFCreateAudioRenderer / MFCreateAudioRendererActivate: Win10 mfcore's streaming audio renderer, from the image (the plain name would
   bind to Win7's mf.dll, whose renderer the Win10 pipeline cannot drive) */
static FARPROC mfcore_fn(const char *name) {
  int d; HMODULE m = NULL;
  alock(1); act_init(); alock(0);
  for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"mfcore.dll")) { m = act_load(d); break; }
  return m ? GetProcAddress(m, name) : NULL; }
__declspec(dllexport) HRESULT WINAPI MFCreateAudioRenderer(void *attrs, void **sink)
{
  typedef HRESULT (WINAPI *PCAR)(void *, void **); static PCAR w10; HRESULT hr;
  if (!w10) w10 = (PCAR)mfcore_fn("MFCreateAudioRenderer");
  hr = w10 ? w10(attrs, sink) : E_NOTIMPL; slog("[audio] MFCreateAudioRenderer -> Win10 SAR 0x%08lX", (unsigned long)hr); return hr;
}
__declspec(dllexport) HRESULT WINAPI MFCreateAudioRendererActivate(void **act)
{
  typedef HRESULT (WINAPI *PCA)(void **); static PCA w10; HRESULT hr;
  if (!w10) w10 = (PCA)mfcore_fn("MFCreateAudioRendererActivate");
  hr = w10 ? w10(act) : E_NOTIMPL; slog("[audio] MFCreateAudioRendererActivate -> Win10 0x%08lX", (unsigned long)hr); return hr;
}
/* TpSetPoolWorkerThreadIdleTimeout (ntdll, Win8+): a tuning hint only. */
__declspec(dllexport) void WINAPI TpSetPoolWorkerThreadIdleTimeout(void *pool, void *timeout) { (void)pool; (void)timeout; }
/* ConvertStringSecurityDescriptorToSecurityDescriptorW: Win7's SDDL parser rejects the Win8 "AC" alias (ALL APPLICATION PACKAGES,
   ERROR_INVALID_SID) but takes its numeric form S-1-15-2-1. Win10's TextInputFramework builds its shared message port with
   "D:(A;;0x01;;;WD)(A;;0x01;;;AC)..." and fail-fasts when that fails (every TextBox, e.g. the CalendarDatePicker, killed the app). */
__declspec(dllexport) BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR s, DWORD rev, PSECURITY_DESCRIPTOR *sd, PULONG len) {
  typedef BOOL (WINAPI *F)(LPCWSTR, DWORD, PSECURITY_DESCRIPTOR *, PULONG); static F real; WCHAR *t, *o; const WCHAR *p; size_t n = 0; BOOL r; DWORD e;
  if (!real) real = (F)GetProcAddress(LoadLibraryA("advapi32.dll"), "ConvertStringSecurityDescriptorToSecurityDescriptorW");
  if (!real) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
  if (real(s, rev, sd, len)) return TRUE;
  e = GetLastError(); if (!s || !wcsstr(s, L";AC)")) { SetLastError(e); return FALSE; }
  for (p = s; (p = wcsstr(p, L";AC)")); p += 4) n++;
  if (!(t = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(s) + n * 8 + 1) * sizeof(WCHAR)))) { SetLastError(e); return FALSE; }
  for (p = s, o = t; *p; ) { if (!wcsncmp(p, L";AC)", 4)) { lstrcpyW(o, L";S-1-15-2-1)"); o += 12; p += 4; } else *o++ = *p++; } *o = 0;
  r = real(t, rev, sd, len); e = GetLastError(); slog("[sddl] AC -> S-1-15-2-1: %s (%lu)", r ? "ok" : "failed", r ? 0 : e);
  HeapFree(GetProcessHeap(), 0, t); SetLastError(r ? 0 : e); return r; }/* EnumDynamicTimeZoneInformation / GetDynamicTimeZoneInformationEffectiveYears (Win8+): from the registry, whose Time Zones layout Win7
   shares. Windows.Globalization's LegacyWindowsTimeZone::FindDynamicTimeZoneInformation enumerates every zone looking for a key name and
   throws when none matches (the Date calculator's Calendar.ChangeTimeZone("UTC") then failed with E_UNEXPECTED). */
#define TZ_ROOT L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones"
typedef struct { LONG Bias, StandardBias, DaylightBias; SYSTEMTIME StandardDate, DaylightDate; } RegTzi;
static int tz_fill(HKEY root, const WCHAR *name, DYNAMIC_TIME_ZONE_INFORMATION *d)
{
  HKEY k; RegTzi t; DWORD cb = sizeof t, cs; int ok = 0;
  if (RegOpenKeyExW(root, name, 0, KEY_READ, &k)) return 0;
  memset(d, 0, sizeof *d);
  if (!RegQueryValueExW(k, L"TZI", NULL, NULL, (BYTE *)&t, &cb) && cb >= sizeof t) {
    d->Bias = t.Bias; d->StandardBias = t.StandardBias; d->DaylightBias = t.DaylightBias; d->StandardDate = t.StandardDate; d->DaylightDate = t.DaylightDate;
    cs = sizeof d->StandardName - sizeof(WCHAR); RegQueryValueExW(k, L"Std", NULL, NULL, (BYTE *)d->StandardName, &cs);
    cs = sizeof d->DaylightName - sizeof(WCHAR); RegQueryValueExW(k, L"Dlt", NULL, NULL, (BYTE *)d->DaylightName, &cs);
    lstrcpynW(d->TimeZoneKeyName, name, 128); d->DynamicDaylightTimeDisabled = FALSE; ok = 1;
  }
  RegCloseKey(k); return ok;
}
__declspec(dllexport) DWORD WINAPI EnumDynamicTimeZoneInformation(DWORD index, DYNAMIC_TIME_ZONE_INFORMATION *d)
{
  HKEY root; WCHAR name[128]; DWORD n = 128, r;
  if (!d) return ERROR_INVALID_PARAMETER;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, TZ_ROOT, 0, KEY_READ, &root)) return ERROR_NO_MORE_ITEMS;
  r = RegEnumKeyExW(root, index, name, &n, NULL, NULL, NULL, NULL);
  if (r == ERROR_SUCCESS) r = tz_fill(root, name, d) ? ERROR_SUCCESS : ERROR_NO_MORE_ITEMS;
  else r = ERROR_NO_MORE_ITEMS;
  RegCloseKey(root); return r;
}
__declspec(dllexport) DWORD WINAPI GetDynamicTimeZoneInformationEffectiveYears(const DYNAMIC_TIME_ZONE_INFORMATION *d, DWORD *first, DWORD *last)
{
  HKEY k; WCHAR path[300]; DWORD cb, r;
  if (!d || !first || !last) return ERROR_INVALID_PARAMETER;
  wsprintfW(path, TZ_ROOT L"\\%s\\Dynamic DST", d->TimeZoneKeyName);
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k)) return ERROR_FILE_NOT_FOUND;   /* zone without dynamic DST data */
  cb = sizeof *first; r = RegQueryValueExW(k, L"FirstEntry", NULL, NULL, (BYTE *)first, &cb);
  if (!r) { cb = sizeof *last; r = RegQueryValueExW(k, L"LastEntry", NULL, NULL, (BYTE *)last, &cb); }
  RegCloseKey(k); return r ? ERROR_FILE_NOT_FOUND : ERROR_SUCCESS;
}
/* GetLocaleInfoEx: LCTYPEs added after Win7 fail there with ERROR_INVALID_FLAGS (Windows.Globalization's DecimalFormatter asks
   LOCALE_ICONSTRUCTEDLOCALE 0x7D). 0x7D -> 0 (a real locale); 0x7C relative long date -> LOCALE_SLONGDATE; 0x7E/0x7F shortest AM/PM -> AM/PM. */
static void strip_year(const WCHAR *in, WCHAR *out, int cap);
__declspec(dllexport) int WINAPI GetLocaleInfoEx(LPCWSTR loc, LCTYPE t, LPWSTR buf, int cch) {
  typedef int (WINAPI *F)(LPCWSTR, LCTYPE, LPWSTR, int); static F f; DWORD base = t & 0x0FFFFFFF & ~LOCALE_RETURN_NUMBER & ~LOCALE_NOUSEROVERRIDE & ~LOCALE_USE_CP_ACP, mods = t & ~base; int r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetLocaleInfoEx"); if (!f) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
  r = f(loc, t, buf, cch); if (r || GetLastError() != ERROR_INVALID_FLAGS) return r;
  if (base == 0x7D) { if (mods & LOCALE_RETURN_NUMBER) { if (cch < 2) { if (cch == 0) return 2; SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; } *(DWORD *)buf = 0; return 2; }
    if (cch == 0) return 2; if (cch < 2) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; } buf[0] = L'0'; buf[1] = 0; return 2; }
  if (base == 0x7C) {   /* relative long date = the long date without its year (see strip_year) */
    WCHAR full[256], o[256]; int n = f(loc, LOCALE_SLONGDATE | mods, full, 256); if (!n) return 0; strip_year(full, o, 256); n = lstrlenW(o) + 1;
    if (!cch) return n; if (cch < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; } lstrcpyW(buf, o); return n; }
  if (base == 0x7E) return f(loc, LOCALE_S1159 | mods, buf, cch);
  if (base == 0x7F) return f(loc, LOCALE_S2359 | mods, buf, cch);
  slog("[nls] GetLocaleInfoEx(%ls, 0x%lX) -> ERROR_INVALID_FLAGS (not mapped)", loc ? loc : L"(null)", (unsigned long)t);
  SetLastError(ERROR_INVALID_FLAGS); return 0; }
/* calendar info: types added after Win7 fail there with ERROR_INVALID_FLAGS. CAL_SRELATIVELONGDATE (0x3A, 8.1+: the long date without the
   year, used by Windows.Globalization's DateTimeFormatter, e.g. for the Date calculator) is derived from CAL_SLONGDATE with the year removed;
   anything else is logged. */
#define CAL_SRELATIVELONGDATE_ 0x3A
static void strip_year(const WCHAR *in, WCHAR *out, int cap)
{
  int n = 0, i; const WCHAR *p = in; WCHAR *s, *e;
  while (*p && n < cap - 1) {
    if (*p == L'\'') { do { out[n++] = *p++; } while (*p && *p != L'\'' && n < cap - 1); if (*p && n < cap - 1) out[n++] = *p++; continue; }   /* quoted literal */
    if (*p == L'y') { while (*p == L'y') p++; while (n > 0 && (out[n - 1] == L' ' || out[n - 1] == L',' || out[n - 1] == L'/' || out[n - 1] == L'-')) n--;
      if (!n) while (*p == L' ' || *p == L',' || *p == L'/' || *p == L'-' || *p == L'.') p++; continue; }
    out[n++] = *p++;
  }
  out[n] = 0;
  for (s = out; *s == L' ' || *s == L','; s++) ;                                   /* trim separators left at either end */
  for (e = s + lstrlenW(s); e > s && (e[-1] == L' ' || e[-1] == L','); e--) ; *e = 0;
  if (s != out) { for (i = 0; s[i]; i++) out[i] = s[i]; out[i] = 0; }
}
typedef struct { CALINFO_ENUMPROCEXEX proc; LPARAM lp; } RelCtx;
static __declspec(thread) RelCtx *t_rel;
static BOOL CALLBACK rel_enum_proc(LPWSTR s, CALID cal, LPWSTR reserved, LPARAM lp)
{ WCHAR o[256]; (void)lp; strip_year(s, o, 256); return t_rel ? t_rel->proc(o, cal, reserved, t_rel->lp) : FALSE; }
__declspec(dllexport) int WINAPI GetCalendarInfoEx(LPCWSTR loc, CALID cal, LPCWSTR res, CALTYPE type, LPWSTR buf, int cch, LPDWORD val) {
  typedef int (WINAPI *F)(LPCWSTR, CALID, LPCWSTR, CALTYPE, LPWSTR, int, LPDWORD); static F f; int r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetCalendarInfoEx"); if (!f) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0; }
  if ((type & 0xFFFF) == CAL_SRELATIVELONGDATE_ && !(type & CAL_RETURN_NUMBER)) {
    WCHAR full[256], o[256]; int n = f(loc, cal, res, (type & ~0xFFFFu) | CAL_SLONGDATE, full, 256, NULL);
    if (!n) return 0; strip_year(full, o, 256); n = lstrlenW(o) + 1;
    if (!cch) return n; if (cch < n) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; } lstrcpyW(buf, o); return n; }
  r = f(loc, cal, res, type, buf, cch, val);
  if (!r && GetLastError() == ERROR_INVALID_FLAGS) { slog("[nls] GetCalendarInfoEx(%ls, cal %lu, 0x%lX) -> ERROR_INVALID_FLAGS", loc ? loc : L"(null)", (unsigned long)cal, (unsigned long)type); SetLastError(ERROR_INVALID_FLAGS); }
  return r; }
__declspec(dllexport) BOOL WINAPI EnumCalendarInfoExEx(CALINFO_ENUMPROCEXEX proc, LPCWSTR loc, CALID cal, LPCWSTR res, CALTYPE type, LPARAM lp) {
  typedef BOOL (WINAPI *F)(CALINFO_ENUMPROCEXEX, LPCWSTR, CALID, LPCWSTR, CALTYPE, LPARAM); static F f; BOOL r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "EnumCalendarInfoExEx"); if (!f) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
  if ((type & 0xFFFF) == CAL_SRELATIVELONGDATE_ && proc) {
    RelCtx c, *saved = t_rel; c.proc = proc; c.lp = lp; t_rel = &c;
    r = f(rel_enum_proc, loc, cal, res, (type & ~0xFFFFu) | CAL_SLONGDATE, 0); t_rel = saved; return r; }
  r = f(proc, loc, cal, res, type, lp);
  if (!r && GetLastError() == ERROR_INVALID_FLAGS) { slog("[nls] EnumCalendarInfoExEx(%ls, cal %lu, 0x%lX) -> ERROR_INVALID_FLAGS", loc ? loc : L"(null)", (unsigned long)cal, (unsigned long)type); SetLastError(ERROR_INVALID_FLAGS); }
  return r; }
__declspec(dllexport) BOOL WINAPI EnumTimeFormatsEx(TIMEFMT_ENUMPROCEX proc, LPCWSTR loc, DWORD flags, LPARAM lp) {
  typedef BOOL (WINAPI *F)(TIMEFMT_ENUMPROCEX, LPCWSTR, DWORD, LPARAM); static F f; BOOL r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "EnumTimeFormatsEx"); if (!f) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
  r = f(proc, loc, flags, lp);
  if (!r && GetLastError() == ERROR_INVALID_FLAGS) { slog("[nls] EnumTimeFormatsEx(%ls, 0x%lX) -> ERROR_INVALID_FLAGS", loc ? loc : L"(null)", (unsigned long)flags); SetLastError(ERROR_INVALID_FLAGS); }
  return r; }
/* RoGetMatchingRestrictedErrorInfo(hr, out): Win10 always produces an error info for hr; VxKex fails when none was recorded (Windows.UI then
   fail-fasts instead of reporting). Fallback: a minimal IRestrictedErrorInfo carrying hr. */
typedef struct MinRei { const void *const *vt; LONG ref; HRESULT hr; } MinRei;
static const GUID IID_IRestrictedErrorInfo_ = {0x82BA7092, 0x4C88, 0x427D, {0xA7, 0xBC, 0x16, 0xDD, 0x93, 0xFE, 0xB6, 0x7E}};
static HRESULT WINAPI mrei_qi(MinRei *m, REFIID r, void **o) { static const GUID unk = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (IsEqualGUID(r, &unk) || IsEqualGUID(r, &IID_IRestrictedErrorInfo_)) { InterlockedIncrement(&m->ref); *o = m; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI mrei_ar(MinRei *m) { return (ULONG)InterlockedIncrement(&m->ref); }
static ULONG WINAPI mrei_rl(MinRei *m) { LONG n = InterlockedDecrement(&m->ref); if (!n) HeapFree(GetProcessHeap(), 0, m); return (ULONG)n; }
static HRESULT WINAPI mrei_details(MinRei *m, BSTR *d, HRESULT *e, BSTR *rd, BSTR *sid) { if (d) *d = NULL; if (e) *e = m->hr; if (rd) *rd = NULL; if (sid) *sid = NULL; return S_OK; }
static HRESULT WINAPI mrei_ref(MinRei *m, BSTR *r) { (void)m; if (r) *r = NULL; return S_OK; }
static const void *const g_mrei_vt[5] = { (void *)mrei_qi, (void *)mrei_ar, (void *)mrei_rl, (void *)mrei_details, (void *)mrei_ref };
__declspec(dllexport) HRESULT WINAPI RoGetMatchingRestrictedErrorInfo(HRESULT hrIn, void **out) {
  typedef HRESULT (WINAPI *F)(HRESULT, void **); static F f; static int looked; MinRei *m; HRESULT hr = E_FAIL;
  if (!out) return E_POINTER; *out = NULL;
  if (!looked) { HMODULE k = GetModuleHandleA("kxcom.dll"); if (!k) k = GetModuleHandleA("combase.dll"); if (k) f = (F)GetProcAddress(k, "RoGetMatchingRestrictedErrorInfo"); looked = 1; }
  if (f) { hr = f(hrIn, out); if (SUCCEEDED(hr) && *out) return hr; }
  m = (MinRei *)HeapAlloc(GetProcessHeap(), 0, sizeof *m); if (!m) return E_OUTOFMEMORY; m->vt = g_mrei_vt; m->ref = 1; m->hr = hrIn; *out = m; return S_OK; }
/* RoReportUnhandledError(IRestrictedErrorInfo*): Windows.UI reports errors escaping CoreDispatcher.RunAsync callbacks here; VxKex's fails, which
   becomes RoFailFastWithErrorContext without saying what went wrong. Log the error's details; treat it as reported (S_OK) so the run continues. */
__declspec(dllexport) HRESULT WINAPI RoReportUnhandledError(IUnknown *rei) {
  void *desc = NULL, *rdesc = NULL, *sid = NULL, *ri = NULL; HRESULT err = 0; HRESULT hr = E_POINTER;
  static const GUID iid_rei = {0x82ba7092, 0x4c88, 0x427d, {0xa7, 0xbc, 0x16, 0xdd, 0x93, 0xfe, 0xb6, 0x7e}}, iid_ei = {0x1cf2b120, 0x547d, 0x101b, {0x8e, 0x65, 0x08, 0x00, 0x2b, 0x2b, 0xd1, 0x19}};
  if (rei && SUCCEEDED(rei->lpVtbl->QueryInterface(rei, &iid_rei, &ri)) && ri) {
    hr = ((HRESULT (WINAPI *)(void *, void **, HRESULT *, void **, void **))(*(void ***)ri)[3])(ri, &desc, &err, &rdesc, &sid);   /* IRestrictedErrorInfo::GetErrorDetails (BSTRs) */
    ((IUnknown *)ri)->lpVtbl->Release((IUnknown *)ri); }
  else if (rei && SUCCEEDED(rei->lpVtbl->QueryInterface(rei, &iid_ei, &ri)) && ri) {
    hr = ((HRESULT (WINAPI *)(void *, void **))(*(void ***)ri)[5])(ri, &desc);   /* IErrorInfo::GetDescription */
    ((IUnknown *)ri)->lpVtbl->Release((IUnknown *)ri); }
  { HMODULE cm = NULL; WCHAR mp[MAX_PATH] = L"?"; void *ra = __builtin_return_address(0); const WCHAR *b;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &cm)) GetModuleFileNameW(cm, mp, MAX_PATH); b = wcsrchr(mp, L'\\');
    slog("[error] RoReportUnhandledError(%p) from %ls+0x%llx: 0x%08lX \"%ls\" / \"%ls\" (details 0x%08lX)", (void *)rei, b ? b + 1 : mp, (unsigned long long)((BYTE *)ra - (BYTE *)cm),
      (unsigned long)err, desc ? (WCHAR *)desc : L"", rdesc ? (WCHAR *)rdesc : L"", (unsigned long)hr); }
  if (desc) SysFreeString((BSTR)desc); if (rdesc) SysFreeString((BSTR)rdesc); if (sid) SysFreeString((BSTR)sid);
  return S_OK; }
/* IsWow64Process2 (Win10 1709+): a native x64 process on an x64 machine */
__declspec(dllexport) BOOL WINAPI IsWow64Process2(HANDLE p, USHORT *proc, USHORT *native) { (void)p; if (proc) *proc = 0; if (native) *native = 0x8664; return TRUE; }
static HRESULT try_dll(int d, void *hs, void **f) { HMODULE m = act_load(d); PGAF g; if (!m) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
  g = (PGAF)GetProcAddress(m, "DllGetActivationFactory"); if (!g) return REGDB_E_CLASSNOTREG; return g(hs, f); }
