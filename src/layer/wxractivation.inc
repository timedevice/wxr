/* wxractivation.inc - WinRT activation: the class catalog of the image and the package graph. Included by wxrsys.c. */
/* RoGetActivationFactory / RoActivateInstance of the image's DLLs come here (wxrbuiltin.inc): a "registry in code".
   Resolution order: built-in stand-ins -> wxrstub.dll -> the catalog built by scanning the image's DLLs (and the app's manifests) for
   the class names of DLLs that export DllGetActivationFactory -> the host's own implementation (VxKex). */
typedef struct { WCHAR cls[160]; int dll; } CatEnt;
static CatEnt *g_cat; static int g_ncat, g_capcat; static WCHAR g_dllname[256][64], g_dllpath[256][MAX_PATH]; static int g_ndll; static HMODULE g_dllmod[256];
/* WXR loader layer state (wxrldr.inc) */
static int g_wxr_on; static WCHAR g_wxr_root[MAX_PATH], g_wxr_img[MAX_PATH], g_wxr_ovr[MAX_PATH];
static HMODULE wxr_module_for(const char *name); static HMODULE wxr_plain_loadw(const WCHAR *n, HANDLE file, DWORD fl); static int wxr_forced(const char *n); static const char *const g_wxr_classic[]; static FARPROC wxr_thunk(const char *dll, const char *name, int ord);
static WCHAR g_dir[MAX_PATH]; static int g_actinit; static CRITICAL_SECTION g_acs; static LONG g_ainit; static HMODULE g_self;   /* this (wxrsys) module */
typedef HRESULT (WINAPI *PGAF)(void *hs, void **f);

static void alock(int on) { if (!g_ainit) { if (InterlockedCompareExchange(&g_ainit, 1, 0) == 0) { InitializeCriticalSection(&g_acs); g_ainit = 2; } while (g_ainit != 2) Sleep(0); } if (on) EnterCriticalSection(&g_acs); else LeaveCriticalSection(&g_acs); }
static void cat_add(const WCHAR *c, int dll) { int i; for (i = 0; i < g_ncat; i++) if (g_cat[i].dll == dll && !lstrcmpiW(g_cat[i].cls, c)) return;
  if (g_ncat == g_capcat) { g_capcat = g_capcat ? g_capcat * 2 : 2048; g_cat = (CatEnt *)realloc(g_cat, g_capcat * sizeof(CatEnt)); }
  lstrcpynW(g_cat[g_ncat].cls, c, 160); g_cat[g_ncat].dll = dll; g_ncat++; }
static void scan_file(const WCHAR *path, int dll) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); DWORD n, got; BYTE *b; DWORD j;
  if (f == INVALID_HANDLE_VALUE) return; n = GetFileSize(f, NULL); b = (BYTE *)malloc(n + 4); if (!b) { CloseHandle(f); return; }
  if (!ReadFile(f, b, n, &got, NULL)) got = 0; CloseHandle(f);
  for (j = 0; j + 40 < got; j += 2) {
    if ((b[j] == 'W' || b[j] == 'M') && !b[j + 1]) { const WCHAR *w = (const WCHAR *)(b + j); const WCHAR *e; WCHAR tmp[160]; int k = 0, dots = 0;
      if (wcsncmp(w, L"Windows.", 8) && wcsncmp(w, L"Microsoft.", 10)) continue;
      for (e = w; (const BYTE *)e + 2 <= b + got && *e && k < 158; e++, k++) { WCHAR c = *e; if (c == '.') dots++; else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) break; tmp[k] = c; }
      if (*e == 0 && k > 12 && dots >= 2) { tmp[k] = 0; cat_add(tmp, dll); j += (DWORD)(k * 2); } } }
  free(b);
}
/* in-process WinRT servers declared by the app's own manifest: <InProcessServer><Path>X.dll</Path><ActivatableClass ActivatableClassId="..."/> */
static int pkg_dep_path(int i, WCHAR *out);   /* wxrpackage.inc: install path of package-graph entry i (>= 1 = dependencies); 0 when there is none */
static void act_manifest_dir(const WCHAR *dir0) {
  WCHAR ep[MAX_PATH], dir[MAX_PATH]; HANDLE f; DWORD n, got; char *b, *p; int count = 0;
  lstrcpyW(dir, dir0);
  _snwprintf(ep, MAX_PATH, L"%s\\AppxManifest.xml", dir);
  f = CreateFileW(ep, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); if (f == INVALID_HANDLE_VALUE) return;
  n = GetFileSize(f, NULL); b = (char *)malloc(n + 2); if (!b) { CloseHandle(f); return; } if (!ReadFile(f, b, n, &got, NULL)) got = 0; CloseHandle(f); b[got] = 0;
  p = b;
  while ((p = strstr(p, "<InProcessServer>")) != NULL) {
    char *end = strstr(p, "</InProcessServer>"), *path = strstr(p, "<Path>"), *pe; char name[160] = {0}; int d;
    if (!end) break; if (!path || path > end) { p = end; continue; }
    path += 6; pe = strstr(path, "</Path>"); if (!pe || pe > end) { p = end; continue; }
    if (pe - path < 150) { memcpy(name, path, pe - path); name[pe - path] = 0; }
    if (g_ndll >= 256 || !name[0]) { p = end; continue; }
    d = g_ndll++; MultiByteToWideChar(CP_UTF8, 0, name, -1, g_dllname[d], 64); _snwprintf(g_dllpath[d], MAX_PATH, L"%s\\%s", dir, g_dllname[d]);

    { char *c = pe; while ((c = strstr(c, "ActivatableClassId=\"")) != NULL && c < end) { char *q; WCHAR w[160]; c += 20; q = strchr(c, '"'); if (!q || q > end) break;
        if (q - c < 150) { char tmp[160]; memcpy(tmp, c, q - c); tmp[q - c] = 0; MultiByteToWideChar(CP_UTF8, 0, tmp, -1, w, 160); cat_add(w, d); count++; } c = q; } }
    p = end;
  }
  free(b); slog("[act] manifest %ls: %d activatable classes from in-process servers", dir, count);
}
/* Reg-free WinRT (Win10 1903+): the exe's embedded side-by-side manifest (RT_MANIFEST #1) lists
   <asmv3:file name="X.dll"><winrtv1:activatableClass name="..."/></asmv3:file>. Self-contained Windows App SDK / WinUI 3 apps register
   all of Microsoft.UI.* this way (no AppxManifest entries). X.dll is relative to the exe. Read before the image scan so these win. */
static void act_sxs_exe(void) {
  WCHAR dir[MAX_PATH], *s; HRSRC r; HGLOBAL g; DWORD sz; const char *m, *p, *e; char *b; int count = 0, files = 0;
  GetModuleFileNameW(NULL, dir, MAX_PATH); s = wcsrchr(dir, '\\'); if (s) *s = 0;
  r = FindResourceW(NULL, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24)); if (!r) return;
  g = LoadResource(NULL, r); sz = SizeofResource(NULL, r); m = g ? (const char *)LockResource(g) : NULL; if (!m || !sz) return;
  b = (char *)malloc(sz + 1); if (!b) return; memcpy(b, m, sz); b[sz] = 0;
  for (p = b; (p = strstr(p, "file name=\"")) != NULL; ) {
    const char *lt = p, *nm = p + 11, *q = strchr(nm, '"'); char name[160]; int d, ok = 1;
    while (lt > b && lt[-1] != '<') { char c = lt[-1]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':')) { ok = 0; break; } lt--; }
    if (!q) break; p = q;
    if (!ok || lt == b || q - nm >= 150 || g_ndll >= 256) continue;
    memcpy(name, nm, q - nm); name[q - nm] = 0;
    e = strstr(q, "file>"); if (!e) e = b + sz;   /* </asmv3:file> */
    d = g_ndll++; MultiByteToWideChar(CP_UTF8, 0, name, -1, g_dllname[d], 64); _snwprintf(g_dllpath[d], MAX_PATH, L"%s\\%s", dir, g_dllname[d]); files++;
    { const char *c = q; while ((c = strstr(c, "activatableClass name=\"")) != NULL && c < e) { const char *z; WCHAR w[160]; c += 23; z = strchr(c, '"'); if (!z || z > e) break;
        if (z - c < 150) { char tmp[160]; memcpy(tmp, c, z - c); tmp[z - c] = 0; MultiByteToWideChar(CP_UTF8, 0, tmp, -1, w, 160); cat_add(w, d); count++; } c = z; } }
    p = e;
  }
  free(b); if (files) slog("[act] exe manifest (reg-free WinRT): %d classes in %d DLLs", count, files);
}
static void act_manifest(void) {
  WCHAR ep[MAX_PATH], dir[MAX_PATH]; int i;
  GetModuleFileNameW(NULL, ep, MAX_PATH); lstrcpyW(dir, ep); { WCHAR *s = wcsrchr(dir, '\\'); if (s) *s = 0; }
  act_manifest_dir(dir);
  for (i = 1; pkg_dep_path(i, ep); i++) act_manifest_dir(ep);   /* framework packages (Microsoft.UI.Xaml, VCLibs, ...) */
}
static void act_init(void) {
  WCHAR p[MAX_PATH]; HMODULE self = NULL; int i;
  if (g_actinit) return; g_actinit = 1;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)act_init, &self);
  GetModuleFileNameW(self, g_dir, MAX_PATH); { WCHAR *s = wcsrchr(g_dir, '\\'); if (s) *s = 0; }
  /* WXR (loader layer): the catalog is the image's own files under their original names - <root>\Layer\override first (our dcomp.dll),
     then <root>\Windows\System32. KernelBase.dll is Win7's. */
  if (g_wxr_on) { const WCHAR *dirs[2]; int k; dirs[0] = g_wxr_ovr; dirs[1] = g_wxr_img;
    for (k = 0; k < 2; k++) { WIN32_FIND_DATAW fd; HANDLE h; _snwprintf(p, MAX_PATH, L"%ls\\*.dll", dirs[k]); h = FindFirstFileW(p, &fd); if (h == INVALID_HANDLE_VALUE) continue;
      do { int j, dup = 0; if (!lstrcmpiW(fd.cFileName, L"KernelBase.dll")) continue; for (j = 0; j < g_ndll; j++) if (!lstrcmpiW(g_dllname[j], fd.cFileName)) dup = 1;
        if (!dup && g_ndll < 256) { lstrcpynW(g_dllname[g_ndll], fd.cFileName, 64); _snwprintf(g_dllpath[g_ndll], MAX_PATH, L"%ls\\%ls", dirs[k], fd.cFileName); g_ndll++; } } while (FindNextFileW(h, &fd));
      FindClose(h); } }
  { int n0 = g_ndll; act_sxs_exe(); for (i = 0; i < n0; i++) scan_file(g_dllpath[i], i); }
  act_manifest();
  slog("[act] catalog: %d dlls, %d class names", g_ndll, g_ncat);
}
static HMODULE act_load(int d) { if (!g_dllmod[d]) { g_dllmod[d] = LoadLibraryExW(g_dllpath[d], NULL, LOAD_WITH_ALTERED_SEARCH_PATH); if (!g_dllmod[d]) slog("[act] LoadLibrary %ls failed %lu", g_dllpath[d], GetLastError()); } return g_dllmod[d]; }
/* LoadLibraryW (forced; exported as "LoadLibraryW" through wxrsys.def so wxrsys's own calls keep kernel32's): the same rules as below */
HMODULE WINAPI WxrLoadLibraryW(LPCWSTR n) { HMODULE m = wxr_plain_loadw(n, NULL, 0); return m ? m : LoadLibraryExW(n, NULL, 0); }
__declspec(dllexport) HMODULE WINAPI LoadLibraryExW(LPCWSTR n, HANDLE file, DWORD fl) {
  typedef HMODULE (WINAPI *F)(LPCWSTR, HANDLE, DWORD); static F f; HMODULE m; int i; void *ra = __builtin_return_address(0);
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryExW");
  /* XAML asks DWriteCreateFactory for IDWriteFactory4 (Win10 1607+); Win7's dwrite 6.2 stops at IDWriteFactory1, so dynamic loads of
     dwrite.dll get the image's Win10 copy (Win7's stays loaded for d2d1 & co under its own name). XAML loads dcomp.dll the same way
     (LOAD_LIBRARY_SEARCH_SYSTEM32); Win7 has none, ours is in <root>\Layer\override. */
  /* msftedit.dll: XAML's TextBox needs Win10's (RichEdit 8); once anything has loaded Win7's by name, a plain load returns that one and the
     next TextBox failed (ShutdownTextServices missing, E_POINTER) */
  if (n && (!lstrcmpiW(n, L"dwrite.dll") || !lstrcmpiW(n, L"dcomp.dll") || !lstrcmpiW(n, L"d2d1.dll") || !lstrcmpiW(n, L"msftedit.dll"))) {   /* d2d1: Win7's 6.2 crashes on Win10 DWrite objects */ alock(1); act_init(); alock(0);
    for (i = 0; i < g_ndll; i++) if (!lstrcmpiW(g_dllname[i], n)) { m = f(g_dllpath[i], file, fl & ~0x1F00u); { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 6) slog("[ldr] LoadLibraryExW(%ls) -> %ls %p", n, g_dllpath[i], (void *)m); } if (m) return m; break; } }
  if ((m = wxr_plain_loadw(n, file, fl)) != NULL) return m;
  m = f(n, file, fl);
  /* data-file loads of an image DLL's own name (XAML reads its error strings from LoadLibraryExW("windows.ui.xaml.dll", AS_DATAFILE)):
     Win7 has no such file in System32, so hand out the image's en-US\<name>.mui, which carries the string tables */
  if (!m && n && (fl & 0x62) && !wcschr(n, L'\\')) { WCHAR p[MAX_PATH]; int h = 0; alock(1); act_init(); alock(0);
    for (i = 0; i < g_ndll; i++) if (!lstrcmpiW(g_dllname[i], n)) h = 1;   /* an image DLL: its real file (resources XAML reads, e.g. Windows.UI.Xaml.Controls.dll) wins */
    _snwprintf(p, MAX_PATH, L"%ls\\en-US\\%ls.mui", g_wxr_img, n);
    if (!h) if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) { m = f(p, file, fl & ~0x1F00u); slog("[ldr] LoadLibraryExW(%ls, 0x%lx) -> %ls %p", n, fl, p, (void *)m); if (m) return m; } }
  /* any other bare name of an image DLL that the system cannot find (XAML loads Windows.UI.Xaml.Controls.dll - TextCommandBarFlyout,
     RevealBackgroundBrush - and its theme DLLs, Windows.UI.Xaml.Resources.*.dll, as data files from System32): the image's file */
  if (m || !n || wcschr(n, L'\\') || wcschr(n, L'/')) return m;
  alock(1); act_init(); alock(0);
  { DWORD le = GetLastError();
    for (i = 0; i < g_ndll; i++) if (!lstrcmpiW(g_dllname[i], n)) { m = f(g_dllpath[i], file, fl & ~0x1F00u); slog("[ldr] LoadLibraryExW(%ls, 0x%lx) -> %ls %p", n, fl, g_dllpath[i], (void *)m); return m; }
    /* a packaged app's DLL loading a SIBLING by bare name (WinUI 3's Microsoft.UI.Xaml.dll loads Microsoft.UI.Xaml.Resources.19H1.dll as an
       image-resource datafile; the Store/winget do likewise): on Windows the package directory is on the DLL search path, under WXR it is not,
       so resolve the name against the CALLER module's own directory. Only taken when the normal load already failed and the file is really there. */
    { HMODULE cm = NULL; WCHAR cp[MAX_PATH], *sl;
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &cm)
          && cm != g_self && GetModuleFileNameW(cm, cp, MAX_PATH) && (sl = wcsrchr(cp, L'\\')) && (int)(sl - cp) + lstrlenW(n) + 2 < MAX_PATH) {
        lstrcpyW(sl + 1, n);
        if (GetFileAttributesW(cp) != INVALID_FILE_ATTRIBUTES && (m = f(cp, file, fl & ~0x1F00u))) { slog("[ldr] LoadLibraryExW(%ls, 0x%lx) -> caller dir %ls %p", n, fl, cp, (void *)m); return m; } } }
    { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 30) slog("[ldr] LoadLibraryExW(%ls, 0x%lx) failed, not in the image", n, fl); }
    SetLastError(le); return NULL; } }
/* Win10 code probes api-set names with GetModuleHandleW (CoreMessaging's IsCurrentThreadUserInitialized asks for
   ext-ms-win-rtcore-ntuser-window-ext-l1-1-0.dll and only then ties itself to the thread's message loop): the module the loader layer
   binds that api set to (wxr_module_for: loaded now if nothing bound it yet; wxrsys for a contract Win7 cannot load). */
__declspec(dllexport) HMODULE WINAPI GetModuleHandleW(LPCWSTR n) {
  typedef HMODULE (WINAPI *F)(LPCWSTR); static F f; HMODULE m; char a[72];
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetModuleHandleW");
  m = f(n);
  /* win32u.dll (Win10's syscall stubs) does not exist on Win7: wxrsys stands in, so GetProcAddress finds the few Nt* we implement
     (NtCreateCompositionInputSink for WinUI 3's InputStateManager) and returns NULL for the rest, as a missing export would */
  if (!m && n && g_wxr_on && !lstrcmpiW(n, L"win32u.dll")) { HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)GetModuleHandleW, &self);
    { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 5) slog("[ldr] GetModuleHandleW(win32u.dll) -> wxrsys stand-in"); } return self; }
  if (m || !n || !g_wxr_on || (_wcsnicmp(n, L"api-", 4) && _wcsnicmp(n, L"ext-", 4))) return m;
  WideCharToMultiByte(CP_ACP, 0, n, -1, a, sizeof a, NULL, NULL); m = wxr_module_for(a);
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 20) slog("[ldr] GetModuleHandleW(%ls) -> %p", n, (void *)m); }
  if (!m) SetLastError(ERROR_MOD_NOT_FOUND); return m; }
/* Win8 token pseudo-handles: GetCurrentProcessToken() = -4, GetCurrentThreadToken() = -5, GetCurrentThreadEffectiveToken() = -6.
   Win7's kernel rejects them (STATUS_INVALID_HANDLE; ApplicationData.Current queries TokenUser on -4). */
static HANDLE token_real(HANDLE h, int *own) {
  HANDLE t = NULL; *own = 0;
  if (h != (HANDLE)-4 && h != (HANDLE)-5 && h != (HANDLE)-6) return h;
  if (h != (HANDLE)-4 && OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_QUERY_SOURCE, TRUE, &t)) { *own = 1; return t; }
  if (h == (HANDLE)-5) return NULL;   /* no impersonation token */
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_QUERY_SOURCE, &t)) { *own = 1; return t; }
  return h; }
static int pkg_appcontainer_for(void *ret_addr); static ULONG pkg_appcontainer_sid(BYTE *sid, ULONG cap);   /* wxrpackage.inc */
/* Win8 token classes Win7 does not know: 29 TokenIsAppContainer, 30 TokenCapabilities,
   31 TokenAppContainerSid, 32 TokenAppContainerNumber. Returns 0 = handled, 1 = buffer too small, -1 = not one of them */
static int token_win8_class(int cls, void *buf, ULONG len, ULONG *ret, void *caller) {
  ULONG need; int ac; if (cls < 29 || cls > 32) return -1;
  ac = pkg_appcontainer_for(caller);   /* the app's own code in a UWP app: AppContainer (wxrpackage.inc) */
  if (cls == 31 && ac) { BYTE s[64]; ULONG sl = pkg_appcontainer_sid(s, sizeof s); need = 16 + sl; if (ret) *ret = need; if (len < need || !buf) return 1;
    memcpy((BYTE *)buf + 16, s, sl); *(void **)buf = (BYTE *)buf + 16; return 0; }   /* TOKEN_APPCONTAINER_INFORMATION + the SID behind it */
  need = cls == 29 || cls == 32 ? 4 : 8; if (ret) *ret = need; if (len < need || !buf) return 1;
  memset(buf, 0, need); if ((cls == 29 || cls == 32) && ac) *(DWORD *)buf = 1;
  { static LONG n; if (cls == 29 && InterlockedIncrement(&n) <= 10) slog("[token] TokenIsAppContainer -> %d", ac); }
  return 0; }
__declspec(dllexport) LONG WINAPI NtQueryInformationToken(HANDLE h, int cls, void *buf, ULONG len, ULONG *ret) {
  typedef LONG (WINAPI *F)(HANDLE, int, void *, ULONG, ULONG *); static F f; int own, w; HANDLE t; LONG st;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationToken");
  if ((w = token_win8_class(cls, buf, len, ret, __builtin_return_address(0))) >= 0) return w ? (LONG)0xC0000023 : 0;   /* STATUS_BUFFER_TOO_SMALL */
  t = token_real(h, &own); if (!t) return (LONG)0xC000007C;   /* STATUS_NO_TOKEN */
  st = f(t, cls, buf, len, ret); if (own) CloseHandle(t); return st; }
__declspec(dllexport) BOOL WINAPI GetTokenInformation(HANDLE h, TOKEN_INFORMATION_CLASS cls, LPVOID buf, DWORD len, PDWORD ret) {
  typedef BOOL (WINAPI *F)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD); static F f; int own; HANDLE t; BOOL ok; DWORD le;
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("advapi32.dll"), "GetTokenInformation");
  { int w = token_win8_class((int)cls, buf, len, (ULONG *)ret, __builtin_return_address(0)); if (w >= 0) { if (w) SetLastError(ERROR_INSUFFICIENT_BUFFER); return !w; } }
  t = token_real(h, &own); if (!t) { SetLastError(ERROR_NO_TOKEN); return FALSE; }
  ok = f(t, cls, buf, len, ret); le = GetLastError(); if (own) CloseHandle(t);
  { static LONG n; if (InterlockedIncrement(&n) <= 40) { HMODULE m = NULL; WCHAR mn[MAX_PATH] = L"?"; void *ra = __builtin_return_address(0);
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &m); if (m) GetModuleFileNameW(m, mn, MAX_PATH);
      slog("[token] GetTokenInformation(class %d, len %lu) -> %d from %ls+0x%Ix", (int)cls, len, ok, wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, (ULONG_PTR)ra - (ULONG_PTR)m); } }
  SetLastError(le); return ok; }
