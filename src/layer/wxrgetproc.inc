/* wxrgetproc.inc - dynamic lookups (GetProcAddress) and small exports reached that way. Included by wxrsys.c. */

/* Dynamic lookups: the image's DLLs GetProcAddress() Win8/10 functions at run time (not via the import table). Fall back to our own export of the same
   name, and log what is still missing so it can be implemented. */
/* exported as "GetProcAddress" through wxrsys.def (a function of that name would clash with the kernel32 declaration) */
FARPROC WINAPI WxrGetProcAddress(HMODULE m, LPCSTR name) {
  FARPROC r; static HMODULE self; static LONG nmiss;
  /* C++/WinRT (WinUI) resolves RoGetActivationFactory from combase.dll at run time: hand out ours so the pseudo registry sees the request */
  /* XAML also loads d3d11.dll itself: D3D11CreateDevice must be ours (IDXGIDevice3 tear-off for Win10 d2d1) */
  if (name && (ULONG_PTR)name >= 0x10000 && (!strcmp(name, "RoGetActivationFactory") || !strcmp(name, "RoActivateInstance") || !strcmp(name, "D3D11CreateDevice"))) {
    if (!self) GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)WxrGetProcAddress, &self);
    if (self && m != self && (r = GetProcAddress(self, name)) != NULL) { slog("[gpa] %s -> wxrsys (pseudo registry)", name); return r; } }
  /* WXR: a forced name asked for at run time gets ours as a static import would (CoreMessaging's RaiseFailFastException lookup - the old
     file patch of its api-ms-win-core-errorhandling string) */
  if (g_wxr_on && name && (ULONG_PTR)name >= 0x10000 && wxr_forced(name)) {
    if (!self) GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)WxrGetProcAddress, &self);
    if (self && m != self && (r = GetProcAddress(self, name)) != NULL) { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 30) slog("[gpa] %s -> wxrsys (forced)", name); return r; } }
  r = GetProcAddress(m, name); r = gi_wrap(m, name, r);

  /* run-time lookup BY ORDINAL of a Win10 private export Win7's DLL lacks (WinUI 3's InputStateManager: user32 #2561
     EnableMouseInPointerForThread, fail-fast when NULL): translate through <root>\Layer\ordmap like the static binder does */

  if (!r && g_wxr_on && m && (ULONG_PTR)name < 0x10000) { char mp[MAX_PATH] = "?", nm[200]; const char *bn = NULL, *sl; int have = 0, k;
    /* VxKex hands out its own DLLs for GetModuleHandle("user32.dll") etc.: use the name they stand in for */
    static const char *const kx[][2] = { {"kxuser.dll","user32.dll"}, {"kxbase.dll","kernelbase.dll"}, {"kxnt.dll","ntdll.dll"}, {"kxcom.dll","combase.dll"}, {0,0} };
    if (GetModuleFileNameA(m, mp, MAX_PATH) && (sl = strrchr(mp, '\\')) != NULL) { bn = sl + 1; for (k = 0; kx[k][0]; k++) if (!lstrcmpiA(bn, kx[k][0])) { bn = kx[k][1]; break; } }
    if (bn && wxr_ordmap(bn, (int)(ULONG_PTR)name, nm, sizeof nm, &have)) {
      if (!self) GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)WxrGetProcAddress, &self);
      r = GetProcAddress(m, nm); if (!r && self) r = GetProcAddress(self, nm);
      slog("[gpa] %s!#%u = %s -> %p", bn, (unsigned)(ULONG_PTR)name, nm, (void *)r); if (r) return r; }
    else slog("[gpa] MISS %s!#%u (no ordmap entry, map file %s)", mp, (unsigned)(ULONG_PTR)name, have ? "found" : "missing"); }
  if (r || !name || (ULONG_PTR)name < 0x10000) return r;
  if (!self) GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)WxrGetProcAddress, &self);
  if (self && m != self) { r = GetProcAddress(self, name); if (r) { slog("[gpa] %s -> wxrsys fallback", name); return r; } }
  if (g_wxr_on && self && m == self) { int k; for (k = 0; g_wxr_classic[k]; k++) { HMODULE hm = GetModuleHandleA(g_wxr_classic[k]); if (hm && (r = GetProcAddress(hm, name)) != NULL) return r; } return wxr_thunk("(contract stand-in)", name, 0); }   /* wxrsys standing in for an api set Win7 cannot load (wxr_module_for) */
  if (InterlockedIncrement(&nmiss) < 200) { char mn[MAX_PATH] = "?", *b; if (m) GetModuleFileNameA(m, mn, MAX_PATH); b = strrchr(mn, '\\'); slog("[gpa] MISS %s!%s", b ? b + 1 : mn, name); }
  SetLastError(ERROR_PROC_NOT_FOUND); return NULL;
}
/* ntdll feature staging (Win10): nothing is configured -> callers use their built-in defaults */
__declspec(dllexport) NTSTATUS NTAPI RtlQueryFeatureConfiguration(ULONG id, int type, ULONGLONG *stamp, void *cfg) { (void)id; (void)type; (void)cfg; if (stamp) *stamp = 0; return (NTSTATUS)0xC0000225; /* STATUS_NOT_FOUND */ }
__declspec(dllexport) NTSTATUS NTAPI RtlRegisterFeatureConfigurationChangeNotification(void *cb, void *ctx, ULONGLONG *stamp, void **sub) { (void)cb; (void)ctx; if (stamp) *stamp = 0; if (sub) *sub = (void *)(ULONG_PTR)1; return STATUS_SUCCESS; }
__declspec(dllexport) NTSTATUS NTAPI RtlUnregisterFeatureConfigurationChangeNotification(void *sub) { (void)sub; return STATUS_SUCCESS; }
__declspec(dllexport) NTSTATUS NTAPI RtlDisownModuleHeapAllocation(HANDLE heap, void *p) { (void)heap; (void)p; return STATUS_SUCCESS; }
/* kernelbase (Win8+): like CheckTokenMembership, plus flags (CTMF_INCLUDE_APPCONTAINER) which do not apply here */
__declspec(dllexport) BOOL WINAPI CheckTokenMembershipEx(HANDLE tok, PSID sid, DWORD flags, PBOOL is) { (void)flags; return CheckTokenMembership(tok, sid, is); }
/* WinRT apartment / error helpers used by the app-side contract stand-ins (wxr-winrt-l1-1-0.dll etc., real mode) */
__declspec(dllexport) HRESULT WINAPI RoGetApartmentIdentifier(unsigned long long *id) { if (!id) return E_INVALIDARG; *id = (unsigned long long)GetCurrentThreadId(); return S_OK; }
__declspec(dllexport) HRESULT WINAPI RoRegisterForApartmentShutdown(void *cb, unsigned long long *id, void **cookie) {
  (void)cb; if (id) *id = (unsigned long long)GetCurrentThreadId(); if (cookie) *cookie = (void *)(ULONG_PTR)1; slog("[impl] RoRegisterForApartmentShutdown (no-op)"); return S_OK; }
__declspec(dllexport) HRESULT WINAPI RoUnregisterForApartmentShutdown(void *cookie) { (void)cookie; return S_OK; }
/* RoParseTypeName lives in Win10's WinTypes.dll (the image's): XAML parses boxed types' names with it (markup extension ProvideValue results) */
__declspec(dllexport) HRESULT WINAPI RoParseTypeName(void *name, DWORD *n, void **parts) {
  typedef HRESULT (WINAPI *F)(void *, DWORD *, void **); static F f; int i;
  if (!f) { alock(1); act_init(); alock(0); for (i = 0; i < g_ndll; i++) if (!lstrcmpiW(g_dllname[i], L"WinTypes.dll")) { HMODULE m = act_load(i); if (m) f = (F)GetProcAddress(m, "RoParseTypeName"); break; } }
  if (!f) { if (n) *n = 0; slog("[stub] RoParseTypeName: WinTypes.dll not available -> E_NOTIMPL"); return E_NOTIMPL; }
  return f(name, n, parts); }
__declspec(dllexport) HRESULT WINAPI GetIntegratedDisplaySize(double *inches) { if (inches) *inches = 0; return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); }

/* combase private (ordinals 90 and 157 in 19041): HRESULT f(DWORD *designMode).  Not in design mode. */
__declspec(dllexport) HRESULT WINAPI RoGetDesignMode(DWORD *out) { if (!out) return E_INVALIDARG; *out = 0; return S_OK; }
__declspec(dllexport) HRESULT WINAPI RoGetDesignModeV2(DWORD *out) { if (!out) return E_INVALIDARG; *out = 0; return S_OK; }
