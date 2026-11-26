/* wxruser.inc - user32 private / band APIs: a CoreWindow becomes an ordinary top-level window. Included by wxrsys.c. */
/* ---- user32 private / band APIs: CoreWindow becomes an ordinary top-level window ---- */
static void notify_dcomp_window(HWND h) {
  HMODULE mods[512]; DWORD need = 0, i; typedef void (WINAPI *PSet)(HWND); PSet f = NULL;
  { typedef BOOL (WINAPI *PEnum)(HANDLE, HMODULE *, DWORD, DWORD *); PEnum en = (PEnum)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules"); if (!en || !en(GetCurrentProcess(), mods, sizeof mods, &need)) return; }
  for (i = 0; i < need / sizeof(HMODULE) && !f; i++) f = (PSet)GetProcAddress(mods[i], "WxrDcomp_SetTargetWindow");
  if (f) { slog("[win] WxrDcomp_SetTargetWindow(%p)", (void *)h); f(h); } else slog("[win] dcomp target hook not found");
}
/* CreateFileMappingW / OpenFileMappingW (forced, exported through wxrsys.def): a NAMED section in Global\ needs
   SeCreateGlobalPrivilege (services/admins). On Win10 a service creates those first and apps only open them; on WXR that
   service does not run, so the app's own create fails with ERROR_ACCESS_DENIED (GDK: xgameruntime gamewnf\sharedmem.cpp
   creates Global\GameWnf_Memory[_<guid>] when GamingServices has not, -> InitializeApiImplEx2 E_ACCESSDENIED).
   Fallback: create it in Local\ (shared by every process of the session); opens of Global\X that find nothing try Local\X. */
static const WCHAR *fm_local(const WCHAR *n, WCHAR *buf, int cap) {
  if (!n || _wcsnicmp(n, L"Global\\", 7) || lstrlenW(n) + 1 > cap) return NULL;
  lstrcpyW(buf, L"Local\\"); lstrcatW(buf, n + 7); return buf; }
HANDLE WINAPI WxrCreateFileMappingW(HANDLE f, SECURITY_ATTRIBUTES *sa, DWORD prot, DWORD hi, DWORD lo, const WCHAR *name) {
  HANDLE h = CreateFileMappingW(f, sa, prot, hi, lo, name); WCHAR b[MAX_PATH]; const WCHAR *l; DWORD e;
  if (h || GetLastError() != ERROR_ACCESS_DENIED || !(l = fm_local(name, b, MAX_PATH))) return h;
  h = CreateFileMappingW(f, sa, prot, hi, lo, l); e = GetLastError();
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 10) slog("[sec] CreateFileMappingW(%ls): Global\\ denied -> %ls %s (%lu)", name, l, h ? "ok" : "FAILED", (unsigned long)e); }
  SetLastError(e); return h; }
HANDLE WINAPI WxrOpenFileMappingW(DWORD acc, BOOL inh, const WCHAR *name) {
  HANDLE h = OpenFileMappingW(acc, inh, name); WCHAR b[MAX_PATH]; const WCHAR *l; DWORD e;
  if (h || GetLastError() != ERROR_FILE_NOT_FOUND || !(l = fm_local(name, b, MAX_PATH))) return h;
  h = OpenFileMappingW(acc, inh, l); e = GetLastError(); if (h) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 10) slog("[sec] OpenFileMappingW(%ls) -> %ls", name, l); }
  SetLastError(h ? 0 : e); return h; }
/* RegOpenKeyExW (forced for WXR modules): machine-wide keys Win10 installers would write (HKLM, admin) that WXR provides per user
   instead: an HKLM\<path> that does not exist is opened as HKCU\Software\WXR\Machine\<path> when that exists. Only lookups that failed
   with "not found" are redirected, so a real HKLM key always wins. Bedrock reads HKLM\SOFTWARE\Microsoft\GameInput RedistDir (set by
   GameInputRedist.msi on Win10) to find GameInputRedist.dll. */
LONG WINAPI WxrRegOpenKeyExW(HKEY root, LPCWSTR sub, DWORD opt, REGSAM sam, PHKEY out) {
  LONG r = RegOpenKeyExW(root, sub, opt, sam, out); WCHAR p[512];
  if (r != ERROR_FILE_NOT_FOUND || root != HKEY_LOCAL_MACHINE || !sub || !*sub || lstrlenW(sub) > 470) return r;
  lstrcpyW(p, L"Software\\WXR\\Machine\\"); lstrcatW(p, sub);
  if (RegOpenKeyExW(HKEY_CURRENT_USER, p, 0, sam & ~(KEY_WOW64_32KEY | KEY_WOW64_64KEY), out) != ERROR_SUCCESS) return r;
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 20) slog("[reg] HKLM\\%ls -> HKCU\\%ls (WXR machine overlay)", sub, p); }
  return ERROR_SUCCESS; }

/* CreateWindowExW (forced, exported as "CreateWindowExW" through wxrsys.def): Win7 fails WS_EX_NOREDIRECTIONBITMAP (Win8+) with
   ERROR_INVALID_PARAMETER. WinUI 3 creates its input-site and content windows with it (Microsoft.UI.Input InputSiteHwndWinRT::CreateHwnd). */
static void in_install(const char *who);
HWND WINAPI WxrCreateWindowExW(DWORD ex, const WCHAR *cls, const WCHAR *name, DWORD style, int x, int y, int w, int h,
    HWND parent, HMENU menu, HINSTANCE inst, void *param) {
  if (ex & 0x00200000u) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 10) slog("[win] CreateWindowExW(class %S): WS_EX_NOREDIRECTIONBITMAP dropped (Win8+)", ((ULONG_PTR)cls > 0xffff && cls) ? cls : L"<atom>"); ex &= ~0x00200000u; }
  /* layered CHILD windows are Win8+ too (WinUI 3 InputNonClientPointerSourceWinRT::CreateTransparentChildWindow: a transparent
     overlay for caption hit-testing). Without WS_EX_LAYERED it is an ordinary child window: KNOWN GAP - it is not see-through. */
  if ((ex & WS_EX_LAYERED) && (style & WS_CHILD)) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 10) slog("[win] CreateWindowExW(class %S): WS_EX_LAYERED dropped on a child window (Win8+)", ((ULONG_PTR)cls > 0xffff && cls) ? cls : L"<atom>"); ex &= ~(DWORD)WS_EX_LAYERED; }
  if ((ULONG_PTR)cls > 0xffff && cls && !lstrcmpW(cls, L"InputSiteWindowClass")) in_install("InputSite");   /* WinUI 3 pointer input (wxrinput.inc) */
  return CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param); }
/* SetLayeredWindowAttributes (forced): the child windows that lost WS_EX_LAYERED above (Win7) - WinUI 3's caption-controls window
   (Windowing.Core CTitleBar::CreateCaptionControlsWindow fail-fasts when this fails). Accepted without effect for non-layered children. */
__declspec(dllexport) BOOL WINAPI WxrSetLayeredWindowAttributes(HWND h, COLORREF key, BYTE alpha, DWORD flags) {
  if (!(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYERED) && (GetWindowLongW(h, GWL_STYLE) & WS_CHILD)) return TRUE;
  return SetLayeredWindowAttributes(h, key, alpha, flags); }
static HWND g_corewnd; static HWND g_lastwnd_created;
__declspec(dllexport) HWND WINAPI CreateWindowInBandEx(DWORD ex, const WCHAR *cls, const WCHAR *name, DWORD style, int x, int y, int w, int h,
    HWND parent, HMENU menu, HINSTANCE inst, void *param, DWORD band, DWORD flags) {
  HWND r; DWORD ex0 = ex; ex &= ~0x00200000u;   /* WS_EX_NOREDIRECTIONBITMAP is Win8+ */
  r = CreateWindowExW(ex, cls, name, style, x, y, w, h, parent, menu, inst, param);
  if (r) g_lastwnd_created = r;
  slog("[win] CreateWindowInBandEx(ex=%lx class=%S band=%lu flags=%lx style=%lx %d,%d %dx%d parent=%p) -> %p err=%lu", ex0, ((ULONG_PTR)cls > 0xffff && cls) ? cls : L"<atom>", band, flags, style, x, y, w, h, (void *)parent, (void *)r, r ? 0 : GetLastError());
  return r;
}
#include "wxrinput.inc"   /* mouse -> WM_POINTER + GetPointerInfo & co. */
__declspec(dllexport) BOOL WINAPI SetCoreWindow(HWND h, void *p) { (void)p; g_corewnd = h; slog("[win] SetCoreWindow(%p)", (void *)h); notify_dcomp_window(h);
  if (h && GetWindowThreadProcessId(h, NULL) == GetCurrentThreadId()) in_install("SetCoreWindow"); return TRUE; }
__declspec(dllexport) BOOL WINAPI GetProcessUIContextInformation(HANDLE hp, DWORD *ctx) { (void)hp; if (!ctx) return FALSE; ctx[0] = 0; ctx[1] = 0; slog("[win] GetProcessUIContextInformation"); return TRUE; }
__declspec(dllexport) BOOL WINAPI IsShellFrameWindow(HWND h) { (void)h; return FALSE; }
__declspec(dllexport) BOOL WINAPI IsShellManagedWindow(HWND h) { (void)h; return FALSE; }
__declspec(dllexport) BOOL WINAPI IsCurrentProcessGdiScaledX(void) { return FALSE; }
/* PowerSettingRegisterNotification (Win8+, powrprof / api-ms-win-power-setting-l1-1-0), DEVICE_NOTIFY_CALLBACK (2): recipient =
   {callback(context, type, setting), context}. WinUI 3's lifted compositor (dwmcorei CComposition::RegisterMonitorPowerNotification)
   registers for the monitor/console display state and fails its engine start without it. Windows reports the current value right
   after registering: here once, "on" (1), from a worker thread. No later changes are reported. */
static void guid_str(const GUID *g, char *o);
typedef ULONG (CALLBACK *PPOWERCB_)(PVOID ctx, ULONG type, PVOID setting);
typedef struct { PPOWERCB_ cb; PVOID ctx; GUID g; } PowReg_;
static DWORD WINAPI pow_initial(LPVOID p) {
  PowReg_ *r = (PowReg_ *)p; struct { GUID g; DWORD len; DWORD data; } s;
  s.g = r->g; s.len = sizeof(DWORD); s.data = 1; r->cb(r->ctx, 0x8013 /* PBT_POWERSETTINGCHANGE */, &s); return 0; }
__declspec(dllexport) DWORD WINAPI PowerSettingRegisterNotification(const GUID *g, DWORD flags, HANDLE recipient, void **reg) {
  PowReg_ *r; char gs[48];
  if (!g || !reg) return ERROR_INVALID_PARAMETER; *reg = NULL; guid_str(g, gs);
  if (flags != 2 || !recipient) { slog("[power] PowerSettingRegisterNotification(%s, flags %lu): only callbacks are supported", gs, flags); return ERROR_INVALID_PARAMETER; }
  r = (PowReg_ *)HeapAlloc(GetProcessHeap(), 0, sizeof *r); if (!r) return ERROR_NOT_ENOUGH_MEMORY;
  r->cb = *(PPOWERCB_ *)recipient; r->ctx = ((PVOID *)recipient)[1]; r->g = *g;
  slog("[power] PowerSettingRegisterNotification(%s) -> %p (reports 'on' once)", gs, (void *)r);
  if (r->cb) QueueUserWorkItem(pow_initial, r, WT_EXECUTEDEFAULT);
  *reg = r; return ERROR_SUCCESS; }
/* DwmSetWindowAttribute (forced): Win7's DWM knows attributes 1..14 and fails the rest with E_INVALIDARG. Win8+/Win11 ones are
   appearance requests (17 USE_HOSTBACKDROPBRUSH - WinUI 3's Theming_TryEnableHostBackdropBrush, whose failure broke the Gallery's
   Window.SystemBackdrop assignment; 19/20 dark mode; 33 corner preference; 34-36 border/caption/text colour; 38 system backdrop):
   accepted and ignored. */
__declspec(dllexport) HRESULT WINAPI DwmSetWindowAttribute(HWND h, DWORD attr, const void *v, DWORD cb) {
  typedef HRESULT (WINAPI *F)(HWND, DWORD, const void *, DWORD); static F f;
  if (attr >= 15) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 10) slog("[dwm] DwmSetWindowAttribute(%p, %lu) ignored (newer than Win7)", (void *)h, attr); return S_OK; }
  if (!f) { HMODULE m = LoadLibraryW(L"dwmapi.dll"); f = m ? (F)GetProcAddress(m, "DwmSetWindowAttribute") : NULL; } if (!f) return E_NOTIMPL;
  return f(h, attr, v, cb); }
/* SetWindowCompositionAttribute (forced): Win7's user32 has it but not attribute 19 WCA_ACCENT_POLICY. WinUI 3's FrameworkUdk
   Theming_TryEnableHostBackdropBrush uses it below build 19041-ish (ACCENT_ENABLE_HOSTBACKDROP = 5) and turns FALSE into E_FAIL,
   which failed the Gallery's Window.SystemBackdrop. Accent requests are accepted without effect (backdrop = solid fallback). */
typedef struct { DWORD attr; PVOID data; SIZE_T size; } WCADATA_;
__declspec(dllexport) BOOL WINAPI SetWindowCompositionAttribute(HWND h, WCADATA_ *d) {
  typedef BOOL (WINAPI *F)(HWND, WCADATA_ *); static F f; BOOL r;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowCompositionAttribute");
  r = f ? f(h, d) : FALSE;
  if (!r && d && d->attr == 19) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 5) slog("[dwm] SetWindowCompositionAttribute(%p, ACCENT_POLICY %lu) accepted, no effect on Win7", (void *)h, d->data ? *(DWORD *)d->data : 0); return TRUE; }
  return r; }
/* ext-ms-win-windowing-internal-l1-1-0 Windowing_DllGetActivationFactory(HSTRING cls, IActivationFactory **out): the OS's own windowing
   classes (Windows 11). WinUI 3's Microsoft.UI.Windowing(.Core) asks it first (AppWindowTitleBar) and uses its own copy when the OS has
   none. The generic thunk answered 0 = S_OK with *out untouched -> AV in AppWindow::GetTitleBar. */
__declspec(dllexport) HRESULT WINAPI Windowing_DllGetActivationFactory(void *cls, void **out) { (void)cls; if (out) *out = NULL; return REGDB_E_CLASSNOTREG; }
/* win32u NtCreateCompositionInputSink(desc, HANDLE *out) (WinUI 3 InputStateManager SystemInputSinkHelpers::CreateInputSinkForHwnd,
   looked up at run time): on Win10 DWM routes input for the HWND through it. Here input reaches the app through wxrinput's hooks, so
   - like dcomp's #1019 - a closable handle of our own is enough. */
__declspec(dllexport) NTSTATUS NTAPI NtCreateCompositionInputSink(void *desc, HANDLE *out) {
  (void)desc; if (!out) return (NTSTATUS)0xC000000D; *out = CreateEventW(NULL, TRUE, FALSE, NULL);
  slog("[input] NtCreateCompositionInputSink -> %p (inert)", *out); return *out ? 0 : (NTSTATUS)0xC0000017; }
__declspec(dllexport) DWORD WINAPI PowerSettingUnregisterNotification(void *reg) { (void)reg; return ERROR_SUCCESS; }   /* r leaks: the initial report may still be queued */
__declspec(dllexport) BOOL WINAPI EnableMouseInPointerForThread(void) { in_install("EnableMouseInPointerForThread"); return TRUE; }__declspec(dllexport) BOOL WINAPI NtUserDisableImmersiveOwner(HWND h) { (void)h; return TRUE; }
__declspec(dllexport) BOOL WINAPI NtUserSetBrokeredForeground(HWND h) { (void)h; return TRUE; }
