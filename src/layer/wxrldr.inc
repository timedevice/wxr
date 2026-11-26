/* wxrldr.inc - the WXR loader layer: runs Microsoft's unmodified Windows 10 files from a WXR root.  Included by wxrsys.c.

   On by WXR_LOADER=1 (set by the launcher, wxrrun --wxr).  The launcher starts the app suspended and makes the exe's in-memory import
   directory name only wxrsys.dll, so this DLL is loaded first; its DllMain (wxr_start) then
     1. hooks ntdll!NtMapViewOfSection.  Every image mapped from <root>\Windows, <root>\WindowsApps or <root>\Layer\override (a "WXR module")
        gets its import directory emptied in memory (the Win7 loader then binds nothing) and its entry point pointed at a stub (wxr_entry);
     2. binds the exe's real imports (read from the exe file), then the loader continues.
   wxr_entry runs before the module's own DllMain: it hides the module's base name from the Win7 loader when Win7 has a DLL of that name
   (Win7 code importing dwrite.dll must keep getting Win7's), binds the module's imports and calls the real entry point.

   Binding rules:
     - an import name that is a file in the package graph, <root>\Layer\override or <root>\Windows\System32 (after the api-set -> sibling
       table) binds to that file, loaded by full path (never KernelBase & co.);
     - else the host: LoadLibrary(name) (Win7 + VxKex);
     - per function: names on the forced list go to wxrsys when it exports them; else the module's export (forwarders through the same
       rules); else wxrsys; else the same name in the classic Win7 DLLs; else a logging thunk (WxrUnimpl, returns 0).
     - ordinal imports are named with <root>\Layer\ordmap\<dll>.txt (data\ordmap, made by tools\genordmap.py).
   Delay loads: wxrsys's ResolveDelayLoadedAPI / ResolveDelayLoadsFromDll (forced) bind through the same rules.
   Logs: [ldr] lines in wxrsys.log; WXR_LDRLOG=1 logs every binding. */

typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } WXRUS_;
typedef struct WXRLDRE_ { LIST_ENTRY InLoad, InMem, InInit; void *DllBase, *EntryPoint; ULONG SizeOfImage; WXRUS_ FullDllName, BaseDllName; } WXRLDRE_;
typedef struct { BYTE *base; DWORD size, entry, imp, impsz; int done, ntls; DWORD tls[8]; WCHAR path[MAX_PATH]; } WxrMod_;
typedef struct { char name[96]; HMODULE m; int wxr, tried, imported; } WxrDll_;
typedef LONG (NTAPI *PFN_NtMapView_)(HANDLE, HANDLE, PVOID *, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, DWORD, ULONG, ULONG);
typedef LONG (NTAPI *PFN_NtQVM_)(HANDLE, PVOID, int, PVOID, SIZE_T, PSIZE_T);

static int g_wxr_on, g_wxr_log;
static WCHAR g_wxr_root[MAX_PATH], g_wxr_rootnt[MAX_PATH], g_wxr_img[MAX_PATH], g_wxr_ovr[MAX_PATH], g_wxr_layer[MAX_PATH], g_wxr_sys7[MAX_PATH];
static CRITICAL_SECTION g_wxr_cs;
static WxrMod_ g_wm[1024]; static int g_nwm;
static WxrDll_ g_wd[1024]; static int g_nwd;
static PFN_NtMapView_ g_map_orig; static PFN_NtQVM_ g_qvm;
static HMODULE (WINAPI *r_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);
static HMODULE (WINAPI *r_LoadLibraryA)(LPCSTR);
static HMODULE g_wxr_self;
static BYTE *g_near[64]; static DWORD g_near_used[64]; static int g_nnear;   /* stub chunks (64 KB) near module bases */
static BYTE *g_thk; static DWORD g_thk_used;                                  /* logging thunks */
static volatile LONG g_wxr_nthunk, g_wxr_nbind;

/* the forced list: wxrsys's version wins over the host's for WXR modules */
static const char *const g_wxr_forced[] = { "InternetSetOptionW", "HttpAddRequestHeadersA", "HttpAddRequestHeadersW", "CoCreateFreeThreadedMarshaler", "GetRestrictedErrorInfo", "AppPolicyGetWindowingModel", "PSLookupPropertyHandlerCLSIDAndPath", "PSLookupPropertyHandlerCLSID", "RoGetActivationFactory", "RoActivateInstance", "RegisterServiceCtrlHandlerExW", "SetServiceStatus", "StartServiceW",
  "OpenSCManagerW", "OpenServiceW", "CloseServiceHandle", "QueryServiceStatus", "RaiseFailFastException", "NtAlpcCreatePort", "NtAlpcConnectPort", "CreateDXGIFactory1", "CreateDXGIFactory2", "PowerSettingRegisterNotification", "PowerSettingUnregisterNotification", "CreateWindowExW", "LoadLibraryW", "DwmSetWindowAttribute", "SetWindowCompositionAttribute", "SetLayeredWindowAttributes", "WaitForMultipleObjectsEx", "OpenProcess", "CoCreateInstance", "CoCreateInstanceFromApp", "CoGetClassObject", "RoRegisterActivationFactories", "RoRevokeActivationFactories", "RoGetAgileReference", "CreateFileW", "RtlSubscribeWnfStateChangeNotification", "RtlUnsubscribeWnfStateChangeNotification", "RtlUnsubscribeWnfNotificationWaitForCompletion", "NtUpdateWnfStateData", "RtlGetDeviceFamilyInfoEnum", "ApiSetQueryApiSetPresence", "ApiSetQueryApiSetPresenceEx", "GetProcessMitigationPolicy", "SetProcessMitigationPolicy", "SetProcessValidCallTargets", "CoMarshalInterface", "CoUnmarshalInterface", "CoGetMarshalSizeMax", "CoGetInterfaceAndReleaseStream", "CoMarshalInterThreadInterfaceInStream", "CoGetStandardMarshal", "CoGetStdMarshalEx", "SHManagedCreateStreamOnFile",
  "GetProcAddress", "LoadLibraryExW", "GetModuleHandleW", "NtQueryInformationToken", "GetTokenInformation", "D3D11CreateDevice", "D3DKMTOpenAdapterFromLuid", "D3DKMTQueryAdapterInfo", "RegGetValueW", "CreateWindowExW", "CreateFileMappingW", "OpenFileMappingW", "RegOpenKeyExW", "WindowsPreallocateStringBuffer", "WindowsPromoteStringBuffer", "WindowsDeleteStringBuffer", "EnumSystemGeoID", "GetLocaleInfoEx", "RoReportUnhandledError", "RoGetMatchingRestrictedErrorInfo", "RtlQueryWnfStateData",
  "GetPointerInfo", "GetPointerType", "GetPointerCursorId", "GetPointerFrameInfo", "GetPointerInfoHistory", "GetPointerFrameInfoHistory", "SkipPointerFrameMessages",
  "EnableMouseInPointer", "IsMouseInPointerEnabled", "WindowsConcatString", "WindowsDuplicateString", "WindowsIsStringEmpty", "SHCoCreateInstance", "CoCreateInstanceEx", "CoWaitForMultipleHandles", "MFCreateAudioRenderer", "MFCreateAudioRendererActivate", "SetThreadpoolTimer", "LoadStringW", "FindResourceW", "FindResourceExW", "LoadResource", "SizeofResource", "GetSystemDirectoryW", "GetSystemWindowsDirectoryW", "ConvertStringSecurityDescriptorToSecurityDescriptorW", "CreateWaitableTimerExW", "EnumDynamicTimeZoneInformation", "GetDynamicTimeZoneInformationEffectiveYears", "GetCalendarInfoEx", "EnumCalendarInfoExEx", "EnumTimeFormatsEx", "RoOriginateError", "RoOriginateErrorW", "RoOriginateLanguageException", "GetPointerDevice", "GetPointerDevices", "GetPointerDeviceRects", "GetPointerInputTransform", "QuirkIsEnabled", "IsWow64Process2", "GetCurrentPackageFullName", "GetCurrentPackageFamilyName", "GetCurrentPackagePath", "GetCurrentPackageId", "GetCurrentPackageInfo",
  "GetCurrentApplicationUserModelId", "GetPackageFullName", "GetPackageFamilyName", "GetPackageId", "GetApplicationUserModelId", "RtlQueryPackageClaims", "GetCurrentPackageInfo2", "GetCurrentPackageInfo3", "GetPackagePathByFullName", "GetPackagePathByFullName2",
  "GetStagedPackagePathByFullName", "GetStagedPackagePathByFullName2", "PackageFamilyNameFromFullName", "PackageIdFromFullName", "GetPackagePath",
  "FindPackagesByPackageFamily", "GetPackagesByPackageFamily", "GetCurrentPackageContext", "GetCurrentPackageApplicationContext", "GetCurrentPackageResourcesContext",
  "GetCurrentPackageApplicationResourcesContext", "GetCurrentPackageGlobalizationContext", "AppXGetOSMaxVersionTested",
  /* new with the loader: delay loads of WXR modules bind through our rules (the host's would load the original names) */
  "ResolveDelayLoadedAPI", "ResolveDelayLoadsFromDll", "LoadLibraryExA", 0 };
static int wxr_forced(const char *n) { int i; for (i = 0; g_wxr_forced[i]; i++) if (g_wxr_forced[i][0] == n[0] && !strcmp(g_wxr_forced[i], n)) return 1; return 0; }
/* Win10-only api sets whose host DLL is in the image (Win7 has no schema entry for them) */
static const char *const g_wxr_apisib[][2] = { { "ext-ms-win-rometadata-dispenser-l1-1-0.dll", "rometadata.dll" }, { "api-ms-win-shell-namespace-l1-1-0.dll", "windows.storage.dll" },
  /* search folder: SearchFolder.dll imports the winrt set (17 functions: scopes, result sets, IsMSSearchEnabled ...); with
     logging thunks returning 0 every indexer query came back empty (Media Player library pages showed no tracks) */
  { "api-ms-win-winrt-search-folder-l1-1-0.dll", "windows.storage.search.dll" }, { "ext-ms-win-search-folder-l1-1-0.dll", "searchfolder.dll" } };
/* never bound to the image's copy: Win7's own core DLLs */
static const char *const g_wxr_never[] = { "kernelbase.dll", "kernel32.dll", "ntdll.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "msvcrt.dll", "ole32.dll", "oleaut32.dll", "rpcrt4.dll", "sechost.dll", "shell32.dll", "shlwapi.dll", "comctl32.dll", "wxrsys.dll", 0 };
static const char *const g_wxr_classic[] = { "user32.dll", "gdi32.dll", "kernel32.dll", "advapi32.dll", "shell32.dll", "shlwapi.dll", "ole32.dll", "oleaut32.dll", "imm32.dll",
  "uxtheme.dll", "dwmapi.dll", "comctl32.dll", "msctf.dll", "rpcrt4.dll", "ws2_32.dll", "version.dll", "winmm.dll", "urlmon.dll", "propsys.dll", "ntdll.dll", 0 };

static void wxr_lock(int on) { if (on) EnterCriticalSection(&g_wxr_cs); else LeaveCriticalSection(&g_wxr_cs); }

/* ---- small PE helpers (in-memory images) ---- */
static IMAGE_NT_HEADERS64 *wxr_nt(BYTE *b) { IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)b; if (!b || d->e_magic != IMAGE_DOS_SIGNATURE) return NULL; return (IMAGE_NT_HEADERS64 *)(b + d->e_lfanew); }
static void wxr_lower(const char *s, char *o, int cap) { int i; for (i = 0; s[i] && i < cap - 1; i++) o[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]; o[i] = 0; }

/* RVA -> string read from a file on disk (import DLL names: VxKex may rewrite them in memory) */
typedef struct { HANDLE f; IMAGE_SECTION_HEADER sec[96]; int ns; } WxrFile_;
static int wxr_file_open(WxrFile_ *wf, const WCHAR *path) {
  BYTE h[4096]; DWORD n; IMAGE_NT_HEADERS64 *nt; wf->ns = 0;
  wf->f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL); if (wf->f == INVALID_HANDLE_VALUE) return 0;
  if (!ReadFile(wf->f, h, sizeof h, &n, NULL) || n < 512 || (nt = wxr_nt(h)) == NULL || (BYTE *)nt - h > 3000) { CloseHandle(wf->f); wf->f = INVALID_HANDLE_VALUE; return 0; }
  wf->ns = nt->FileHeader.NumberOfSections; if (wf->ns > 96) wf->ns = 96;
  if ((BYTE *)IMAGE_FIRST_SECTION(nt) + wf->ns * sizeof(IMAGE_SECTION_HEADER) > h + n) { CloseHandle(wf->f); wf->f = INVALID_HANDLE_VALUE; return 0; }
  memcpy(wf->sec, IMAGE_FIRST_SECTION(nt), wf->ns * sizeof(IMAGE_SECTION_HEADER)); return 1; }
static int wxr_file_str(WxrFile_ *wf, DWORD rva, char *out, int cap) {
  int i; out[0] = 0; if (wf->f == INVALID_HANDLE_VALUE) return 0;
  for (i = 0; i < wf->ns; i++) { DWORD va = wf->sec[i].VirtualAddress, sz = wf->sec[i].SizeOfRawData; if (rva >= va && rva < va + sz) {
      LARGE_INTEGER o; DWORD n; o.QuadPart = wf->sec[i].PointerToRawData + (rva - va); if (!SetFilePointerEx(wf->f, o, NULL, FILE_BEGIN) || !ReadFile(wf->f, out, cap - 1, &n, NULL)) return 0;
      out[n] = 0; out[cap - 1] = 0; return out[0] != 0; } }
  return 0; }
static void wxr_file_close(WxrFile_ *wf) { if (wf->f != INVALID_HANDLE_VALUE) CloseHandle(wf->f); }

/* ---- ordinal maps (<root>\Layer\ordmap\<dll>.txt: "ordinal name") ---- */
static int wxr_ordmap(const char *dll, int ord, char *out, int cap, int *have) {
  WCHAR p[MAX_PATH]; char base[128], line[600], *dot; FILE *f; *have = 0;
  lstrcpynA(base, dll, 127); dot = strrchr(base, '.'); if (dot) *dot = 0;
  _snwprintf(p, MAX_PATH, L"%ls\\ordmap\\%hs.txt", g_wxr_layer, base); f = _wfopen(p, L"r"); if (!f) return 0; *have = 1;
  while (fgets(line, sizeof line, f)) { int o; char nm[500]; if (sscanf(line, "%d %499s", &o, nm) == 2 && o == ord) { fclose(f); if (nm[0] == '?' || nm[0] == '=') return 0; lstrcpynA(out, nm, cap); return 1; } }
  fclose(f); return 0; }

/* ---- logging thunks for functions nobody implements ---- */
static FARPROC wxr_thunk(const char *dll, const char *name, int ord) {
  BYTE *t; char *s; size_t l; FARPROC unimpl = GetProcAddress(g_wxr_self, "WxrUnimpl");
  char nm[200]; if (name) _snprintf(nm, sizeof nm - 1, "%s!%s", dll, name); else _snprintf(nm, sizeof nm - 1, "%s!#%d", dll, ord); nm[sizeof nm - 1] = 0;
  l = strlen(nm) + 1; s = (char *)HeapAlloc(GetProcessHeap(), 0, l); if (!s) return NULL; memcpy(s, nm, l);
  wxr_lock(1);
  if (!g_thk || g_thk_used + 32 > 0x10000) { g_thk = (BYTE *)VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE); g_thk_used = 0; }
  t = g_thk ? g_thk + g_thk_used : NULL; if (t) g_thk_used += 32;
  wxr_lock(0);
  if (!t) return NULL;
  t[0] = 0x49; t[1] = 0xBB; *(void **)(t + 2) = s;            /* mov r11, name */
  t[10] = 0x48; t[11] = 0xB8; *(void **)(t + 12) = (void *)unimpl; /* mov rax, WxrUnimpl */
  t[20] = 0xFF; t[21] = 0xE0;                                  /* jmp rax */
  InterlockedIncrement(&g_wxr_nthunk); slog("[ldr] THUNK %s (nobody implements it; returns 0)", nm);
  return (FARPROC)t; }

/* ---- module lookup ---- */
static int wxr_file_exists(const WCHAR *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static int wxr_find_file(const char *name, WCHAR *out) {
  WCHAR wn[128]; int i; MultiByteToWideChar(CP_ACP, 0, name, -1, wn, 128);
  _snwprintf(out, MAX_PATH, L"%ls\\%ls", g_wxr_ovr, wn); if (wxr_file_exists(out)) return 1;
  pkg_load();
  for (i = 0; i < g_nent; i++) if (g_ent[i].path[0]) { _snwprintf(out, MAX_PATH, L"%ls\\%ls", g_ent[i].path, wn); if (wxr_file_exists(out)) return 1; }
  _snwprintf(out, MAX_PATH, L"%ls\\%ls", g_wxr_img, wn); if (wxr_file_exists(out)) return 1;
  return 0; }
/* the module an import name binds to (cached) */
static WxrDll_ *wxr_dll(const char *name0) {
  char name[96], tgt[96]; int i, k; WxrDll_ *d = NULL; WCHAR path[MAX_PATH]; HMODULE m = NULL; int wxr = 0;
  wxr_lower(name0, name, sizeof name); { size_t l = strlen(name); if (l < 4 || strcmp(name + l - 4, ".dll")) lstrcatA(name, ".dll"); }
  wxr_lock(1);
  for (i = 0; i < g_nwd; i++) if (!strcmp(g_wd[i].name, name)) { d = &g_wd[i]; break; }
  if (!d && g_nwd < 1024) { d = &g_wd[g_nwd++]; memset(d, 0, sizeof *d); lstrcpynA(d->name, name, sizeof d->name); }
  wxr_lock(0);
  if (!d) return NULL;
  d->imported = 1;
  if (d->tried) return d;
  lstrcpyA(tgt, name);
  for (k = 0; k < (int)(sizeof g_wxr_apisib / sizeof g_wxr_apisib[0]); k++) if (!strcmp(g_wxr_apisib[k][0], name)) lstrcpyA(tgt, g_wxr_apisib[k][1]);
  for (k = 0; g_wxr_never[k]; k++) if (!strcmp(g_wxr_never[k], tgt)) break;
  if (!g_wxr_never[k] && wxr_find_file(tgt, path)) { m = r_LoadLibraryExW(path, NULL, 0); wxr = 1;
    if (!m) slog("[ldr] LoadLibrary %ls FAILED %lu", path, GetLastError()); }
  else m = r_LoadLibraryA(name);
  /* pin it: Windows pins statically imported DLLs, so a stray FreeLibrary is harmless there. WinUI 3's Microsoft.UI.Windowing does
     GetModuleHandleW("Microsoft.UI.Windowing.Core.dll") ... FreeLibrary(it) - with our dynamic load (count 1) that unloaded Windowing.Core
     under live objects (AV in AppWindow::GetTitleBar). */
  if (m) { HMODULE pinned; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)m, &pinned); }
  d->m = m; d->wxr = wxr && m; d->tried = 1;
  if (g_wxr_log || !m) slog("[ldr] %s -> %s %p", name, !m ? "NOT LOADABLE" : wxr ? "WXR" : "host", (void *)m);
  return d; }

static FARPROC wxr_proc(WxrDll_ *d, const char *name, int ord, int depth);
/* an export of a WXR module, forwarders bound through our rules */
static FARPROC wxr_export(HMODULE m, const char *name, int ord, int depth) {
  BYTE *b = (BYTE *)m; IMAGE_NT_HEADERS64 *nt = wxr_nt(b); IMAGE_DATA_DIRECTORY dd; IMAGE_EXPORT_DIRECTORY *e; DWORD *fn, *nm, rva = 0; WORD *no;
  if (!nt) return NULL; dd = nt->OptionalHeader.DataDirectory[0]; if (!dd.VirtualAddress) return NULL;
  e = (IMAGE_EXPORT_DIRECTORY *)(b + dd.VirtualAddress); fn = (DWORD *)(b + e->AddressOfFunctions); nm = (DWORD *)(b + e->AddressOfNames); no = (WORD *)(b + e->AddressOfNameOrdinals);
  if (!name) { if (ord < (int)e->Base || ord >= (int)(e->Base + e->NumberOfFunctions)) return NULL; rva = fn[ord - e->Base]; }
  else { int lo = 0, hi = (int)e->NumberOfNames - 1; while (lo <= hi) { int mid = (lo + hi) / 2, c = strcmp((const char *)(b + nm[mid]), name); if (!c) { rva = fn[no[mid]]; break; } if (c < 0) lo = mid + 1; else hi = mid - 1; } }
  if (!rva) return NULL;
  if (rva >= dd.VirtualAddress && rva < dd.VirtualAddress + dd.Size) {   /* forwarder "Module.Function" / "Module.#ord" */
    const char *s = (const char *)(b + rva), *dot = strrchr(s, '.'); char mod[96]; WxrDll_ *t;
    if (!dot || dot - s > 90 || depth > 8) return NULL; memcpy(mod, s, dot - s); mod[dot - s] = 0; lstrcatA(mod, ".dll");
    if (!(t = wxr_dll(mod))) return NULL;
    return dot[1] == '#' ? wxr_proc(t, NULL, atoi(dot + 2), depth + 1) : wxr_proc(t, dot + 1, 0, depth + 1); }
  return (FARPROC)(b + rva); }

/* one function of an imported DLL: see the binding rules at the top */
static FARPROC wxr_proc(WxrDll_ *d, const char *name, int ord, int depth) {
  char mn[160]; int have = 0, mapped = 0; FARPROC p = NULL; int k;
  if (!name && ord && d->wxr) {   /* the image's own DLL: its ordinals are right - unless the ordinal's name is on the forced list */
    if (wxr_ordmap(d->name, ord, mn, sizeof mn, &have) && mn[0] != '#' && wxr_forced(mn) && (p = GetProcAddress(g_wxr_self, mn)) != NULL) return p;
    p = wxr_export(d->m, NULL, ord, depth); return p ? p : wxr_thunk(d->name, NULL, ord); }
  if (!name && ord) { if (wxr_ordmap(d->name, ord, mn, sizeof mn, &have)) { if (mn[0] == '#') { if (d->m && !d->wxr) p = GetProcAddress(d->m, MAKEINTRESOURCEA(atoi(mn + 1))); if (p) return p; }
      else { name = mn; mapped = 1; } } }
  if (name && wxr_forced(name) && (p = GetProcAddress(g_wxr_self, name)) != NULL) return p;
  if (d->m) {
    if (d->wxr) p = name && !mapped ? wxr_export(d->m, name, 0, depth) : (!name ? wxr_export(d->m, NULL, ord, depth) : NULL);
    else if (name) p = GetProcAddress(d->m, name);
    else if (!have) p = GetProcAddress(d->m, MAKEINTRESOURCEA(ord));   /* a map that does not know this ordinal: the host's numbering would be wrong */
    if (p) return p; }
  if (name && (p = GetProcAddress(g_wxr_self, name)) != NULL) return p;
  if (name) for (k = 0; g_wxr_classic[k]; k++) { HMODULE hm = GetModuleHandleA(g_wxr_classic[k]); if (!hm) hm = r_LoadLibraryA(g_wxr_classic[k]); if (hm && (p = GetProcAddress(hm, name)) != NULL) return p; }
  return wxr_thunk(d->name, name, ord); }

/* binds the import descriptors at rva of the image at base; path = its file (DLL names are read from there) */
static void wxr_bind(BYTE *base, const WCHAR *path, DWORD rva) {
  IMAGE_IMPORT_DESCRIPTOR *id; WxrFile_ wf; int nd = 0, nf = 0;
  if (!rva) return; wxr_file_open(&wf, path);
  for (id = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); id->Name; id++) {
    char dn[128], fdn[128]; WxrDll_ *d; ULONGLONG *ilt, *iat; int n, i; DWORD old;
    lstrcpynA(dn, (const char *)(base + id->Name), sizeof dn);
    if (wxr_file_str(&wf, id->Name, fdn, sizeof fdn) && lstrcmpiA(fdn, dn)) { slog("[ldr] import name changed in memory: %s (file: %s) - using the file's", dn, fdn); lstrcpyA(dn, fdn); }
    d = wxr_dll(dn); if (!d) continue; nd++;
    ilt = (ULONGLONG *)(base + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk)); iat = (ULONGLONG *)(base + id->FirstThunk);
    for (n = 0; ilt[n]; n++) ;
    if (!VirtualProtect(iat, n * 8, PAGE_READWRITE, &old)) { slog("[ldr] cannot unprotect the IAT of %s in %ls", dn, path); continue; }
    for (i = 0; i < n; i++) { ULONGLONG v = ilt[i]; FARPROC p;
      if (v >> 63) p = wxr_proc(d, NULL, (int)(v & 0xFFFF), 0); else p = wxr_proc(d, (const char *)(base + (DWORD)v + 2), 0, 0);
      if (g_wxr_log) slog("[ldr]   %s!%s%s -> %p", dn, (v >> 63) ? "#" : (const char *)(base + (DWORD)v + 2), (v >> 63) ? "" : "", (void *)p);
      iat[i] = (ULONGLONG)p; nf++; }
    VirtualProtect(iat, n * 8, old, &old); }
  wxr_file_close(&wf); InterlockedExchangeAdd(&g_wxr_nbind, nf);
  slog("[ldr] bound %ls: %d DLLs, %d functions", wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path, nd, nf); }

/* ---- module records ---- */
static WxrMod_ *wxr_mod(BYTE *base) { int i; WxrMod_ *r = NULL; wxr_lock(1); for (i = 0; i < g_nwm; i++) if (g_wm[i].base == base) { r = &g_wm[i]; break; } wxr_lock(0); return r; }
/* Win7 binds static imports by base name: a WXR module whose name Win7 also has (dwrite, d2d1, propsys ...) is renamed in the loader's
   list ("w10_<name>") so that Win7 DLLs importing that name keep getting Win7's. Loads by full path still find it. */
static void wxr_hide_name(WxrMod_ *r) {
  BYTE *peb = (BYTE *)__readgsqword(0x60); LIST_ENTRY *head = (LIST_ENTRY *)(*(BYTE **)(peb + 0x18) + 0x10), *e; const WCHAR *bn = wcsrchr(r->path, L'\\'); WCHAR p[MAX_PATH];
  bn = bn ? bn + 1 : r->path; _snwprintf(p, MAX_PATH, L"%ls\\%ls", g_wxr_sys7, bn); if (!wxr_file_exists(p)) return;
  for (e = head->Flink; e != head; e = e->Flink) { WXRLDRE_ *le = (WXRLDRE_ *)e;
    if (le->DllBase == r->base) { int n = lstrlenW(bn) + 5; WCHAR *b = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR)); if (!b) return;
      _snwprintf(b, n, L"w10_%ls", bn); b[n - 1] = 0; le->BaseDllName.Buffer = b; le->BaseDllName.Length = (USHORT)(lstrlenW(b) * 2); le->BaseDllName.MaximumLength = (USHORT)(n * 2);
      slog("[ldr] %ls: Win7 has a DLL of this name; listed as %ls", bn, b); return; } } }
static void wxr_fixups(WxrMod_ *r);
static void wxr_note_delay(BYTE *b);
/* once bound, the module's import directory goes back into its header: the Win7 loader is done with it, and code that walks imports to
   patch them (wxrsys: windows.storage TryEnterCriticalSection, the mfcore/rtworkq scheduling hooks) must see a normal module */
static void wxr_restore_imports(WxrMod_ *r) {
  IMAGE_NT_HEADERS64 *h = wxr_nt(r->base); DWORD old;
  if (!h || !r->imp || !VirtualProtect(r->base, h->OptionalHeader.SizeOfHeaders, PAGE_READWRITE, &old)) return;
  h->OptionalHeader.DataDirectory[1].VirtualAddress = r->imp; h->OptionalHeader.DataDirectory[1].Size = r->impsz;
  VirtualProtect(r->base, h->OptionalHeader.SizeOfHeaders, old, &old); }
static void wxr_first_attach(WxrMod_ *r) { if (r->done) return; r->done = 1; wxr_hide_name(r); wxr_bind(r->base, r->path, r->imp); wxr_restore_imports(r); wxr_note_delay(r->base); wxr_fixups(r); }
static BOOL WINAPI wxr_entry(HINSTANCE h, DWORD reason, LPVOID res) {
  WxrMod_ *r = wxr_mod((BYTE *)h);
  if (!r) return TRUE;
  if (reason == DLL_PROCESS_ATTACH) wxr_first_attach(r);
  if (r->entry) return ((BOOL (WINAPI *)(HINSTANCE, DWORD, LPVOID))(r->base + r->entry))(h, reason, res);
  return TRUE; }
/* TLS callbacks run before the entry point: the module's first callback slot points here (the others at wxr_tls_nop); this binds the
   imports on process attach and then calls the module's real callbacks in order */
static VOID NTAPI wxr_tls_nop(PVOID h, DWORD reason, PVOID res) { (void)h; (void)reason; (void)res; }
static VOID NTAPI wxr_tls_cb(PVOID h, DWORD reason, PVOID res) {
  WxrMod_ *r = wxr_mod((BYTE *)h); int i;
  if (!r) return;
  if (reason == DLL_PROCESS_ATTACH) wxr_first_attach(r);
  for (i = 0; i < r->ntls; i++) ((PIMAGE_TLS_CALLBACK)(r->base + r->tls[i]))(h, reason, res); }
/* 16-byte stub `mov rax, wxr_entry; jmp rax` within 4 GB above base (AddressOfEntryPoint is an unsigned 32-bit RVA) */
static BYTE *wxr_near_stub(BYTE *base, DWORD size) {
  int i; BYTE *s = NULL;
  for (i = 0; i < g_nnear; i++) if (g_near[i] > base && (ULONG_PTR)(g_near[i] - base) < 0xFFF00000ull && g_near_used[i] + 16 <= 0x10000) { s = g_near[i] + g_near_used[i]; g_near_used[i] += 16; break; }
  if (!s && g_nnear < 64) { ULONG_PTR a = ((ULONG_PTR)base + size + 0xFFFF) & ~(ULONG_PTR)0xFFFF; int k;
    for (k = 0; k < 4096 && !s; k++, a += 0x10000) { BYTE *c = (BYTE *)VirtualAlloc((void *)a, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
      if (c) { if (c > base && (ULONG_PTR)(c - base) < 0xFFF00000ull) { g_near[g_nnear] = c; g_near_used[g_nnear] = 16; g_nnear++; s = c; } else VirtualFree(c, 0, MEM_RELEASE); } } }
  if (!s) return NULL;
  s[0] = 0x48; s[1] = 0xB8; *(void **)(s + 2) = (void *)wxr_entry; s[10] = 0xFF; s[11] = 0xE0; s[12] = s[13] = s[14] = s[15] = 0xCC;
  return s; }

/* a new image view: is it a WXR module? then take over its imports and entry point */
static void wxr_on_map(BYTE *base, LONG st) {
  MEMORY_BASIC_INFORMATION mbi; BYTE fnb[1200]; WXRUS_ *us = (WXRUS_ *)fnb; SIZE_T got = 0; WCHAR nt[MAX_PATH], rel[MAX_PATH], path[MAX_PATH];
  IMAGE_NT_HEADERS64 *h; int rl; DWORD old; BYTE *stub; WxrMod_ *r; int i;
  if (!VirtualQuery(base, &mbi, sizeof mbi) || mbi.Type != MEM_IMAGE) return;
  if (!g_qvm || g_qvm(GetCurrentProcess(), base, 2 /* MemoryMappedFilenameInformation */, fnb, sizeof fnb - 2, &got) < 0 || !us->Buffer) return;
  lstrcpynW(nt, us->Buffer, (us->Length / 2 + 1) < MAX_PATH ? us->Length / 2 + 1 : MAX_PATH);
  rl = lstrlenW(g_wxr_rootnt); if (_wcsnicmp(nt, g_wxr_rootnt, rl) || nt[rl] != L'\\') return;
  lstrcpynW(rel, nt + rl, MAX_PATH);
  /* <root>\Program Files\: Win10 components WXR installs into the root (GameInput's GameInputRedist.dll, found through the machine overlay
     HKLM\SOFTWARE\Microsoft\GameInput RedistDir and loaded by full path - Windows' own loader failed it on CheckTokenMembershipEx) */
  if (_wcsnicmp(rel, L"\\Windows\\", 9) && _wcsnicmp(rel, L"\\WindowsApps\\", 13) && _wcsnicmp(rel, L"\\Layer\\override\\", 16) && _wcsnicmp(rel, L"\\Program Files\\", 15)) return;
  if (!(h = wxr_nt(base)) || h->OptionalHeader.Magic != 0x20b || !(h->FileHeader.Characteristics & IMAGE_FILE_DLL)) return;
  _snwprintf(path, MAX_PATH, L"%ls%ls", g_wxr_root, rel);
  wxr_lock(1);
  for (r = NULL, i = 0; i < g_nwm; i++) if (g_wm[i].base == base) { r = &g_wm[i]; break; }   /* a view at the address of an unmapped one */
  if (!r && g_nwm < 1024) r = &g_wm[g_nwm++];
  stub = r ? wxr_near_stub(base, h->OptionalHeader.SizeOfImage) : NULL;
  if (r && stub) { memset(r, 0, sizeof *r); r->base = base; r->size = h->OptionalHeader.SizeOfImage; r->entry = h->OptionalHeader.AddressOfEntryPoint;
    r->imp = h->OptionalHeader.DataDirectory[1].VirtualAddress; r->impsz = h->OptionalHeader.DataDirectory[1].Size; lstrcpynW(r->path, path, MAX_PATH); }
  wxr_lock(0);
  if (!r || !stub) { slog("[ldr] no room to track %ls", path); return; }
  if (VirtualProtect(base, h->OptionalHeader.SizeOfHeaders, PAGE_READWRITE, &old)) {
    h->OptionalHeader.DataDirectory[1].VirtualAddress = 0; h->OptionalHeader.DataDirectory[1].Size = 0;
    h->OptionalHeader.DataDirectory[11].VirtualAddress = 0; h->OptionalHeader.DataDirectory[11].Size = 0;   /* bound imports */
    h->OptionalHeader.AddressOfEntryPoint = (DWORD)(stub - base);
    VirtualProtect(base, h->OptionalHeader.SizeOfHeaders, old, &old); }
  else slog("[ldr] cannot unprotect the headers of %ls", path);
  /* TLS callbacks: the array holds VAs. When the loader still has to relocate the image (STATUS_IMAGE_NOT_AT_BASE) the values are
     preferred-base VAs and will get the delta added, so ours are written minus that delta. */
  { IMAGE_DATA_DIRECTORY t = h->OptionalHeader.DataDirectory[9]; if (t.VirtualAddress) { IMAGE_TLS_DIRECTORY64 *td = (IMAGE_TLS_DIRECTORY64 *)(base + t.VirtualAddress);
      ULONGLONG pend = st == 0x40000003 ? (ULONGLONG)base - h->OptionalHeader.ImageBase : 0, orgb = st == 0x40000003 ? h->OptionalHeader.ImageBase : (ULONGLONG)base;
      ULONGLONG *cb = td->AddressOfCallBacks ? (ULONGLONG *)(td->AddressOfCallBacks + pend) : NULL; int n;
      if (cb && cb[0]) { for (n = 0; cb[n] && n < 8; n++) r->tls[n] = (DWORD)(cb[n] - orgb);
        if (cb[n]) slog("[ldr] WARNING %ls has more than 8 TLS callbacks", path);
        else if (VirtualProtect(cb, n * 8, PAGE_READWRITE, &old)) { int i; r->ntls = n; for (i = 0; i < n; i++) cb[i] = (i ? (ULONGLONG)wxr_tls_nop : (ULONGLONG)wxr_tls_cb) - pend; VirtualProtect(cb, n * 8, old, &old);
          if (g_wxr_log) slog("[ldr] %ls: %d TLS callback(s) wrapped", path, n); }
        else slog("[ldr] WARNING cannot unprotect the TLS callbacks of %ls", path); } } }
  if (g_wxr_log) slog("[ldr] mapped %ls at %p", path, (void *)base); }
static LONG NTAPI wxr_map_hook(HANDLE sec, HANDLE proc, PVOID *base, ULONG_PTR zb, SIZE_T commit, PLARGE_INTEGER off, PSIZE_T vsize, DWORD inh, ULONG at, ULONG prot) {
  LONG st = g_map_orig(sec, proc, base, zb, commit, off, vsize, inh, at, prot);
  if (st >= 0 && base && *base && (proc == GetCurrentProcess() || GetProcessId(proc) == GetCurrentProcessId())) { DWORD le = GetLastError(); wxr_on_map((BYTE *)*base, st); SetLastError(le); }
  return st; }
/* inline hook of the ntdll syscall stub (mov r10,rcx; mov eax,N; syscall; ret; 5-byte nop) or of a hook someone already put there */
static int wxr_hook_map(void) {
  BYTE *f = (BYTE *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtMapViewOfSection"), *o; DWORD old;
  if (!f) return 0;
  o = (BYTE *)VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE); if (!o) return 0;
  if (f[0] == 0x4C && f[1] == 0x8B && f[2] == 0xD1 && f[3] == 0xB8 && f[8] == 0x0F && f[9] == 0x05 && f[10] == 0xC3) memcpy(o, f, 11);   /* plain stub: copy it */
  else if (f[0] == 0x48 && f[1] == 0xB8 && f[10] == 0xFF && f[11] == 0xE0) memcpy(o, f, 12);                                           /* mov rax, x; jmp rax */
  else if (f[0] == 0xE9) { BYTE *t = f + 5 + *(INT32 *)(f + 1); o[0] = 0x48; o[1] = 0xB8; *(void **)(o + 2) = t; o[10] = 0xFF; o[11] = 0xE0; }
  else { slog("[ldr] NtMapViewOfSection has unknown code (%02x %02x %02x %02x): loader layer OFF", f[0], f[1], f[2], f[3]); return 0; }
  g_map_orig = (PFN_NtMapView_)o;
  if (!VirtualProtect(f, 12, PAGE_EXECUTE_READWRITE, &old)) return 0;
  f[0] = 0x48; f[1] = 0xB8; *(void **)(f + 2) = (void *)wxr_map_hook; f[10] = 0xFF; f[11] = 0xE0;
  VirtualProtect(f, 12, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 12);
  return 1; }

/* ---- delay loads (Win8+ ResolveDelayLoadedAPI), for WXR modules only (forced) ---- */
typedef struct { DWORD Attributes, DllNameRVA, ModuleHandleRVA, ImportAddressTableRVA, ImportNameTableRVA, BoundImportAddressTableRVA, UnloadInformationTableRVA, TimeDateStamp; } WXRDLY_;
static FARPROC wxr_delay_one(BYTE *base, const WXRDLY_ *dd, ULONGLONG *thunk) {
  WxrDll_ *d = wxr_dll((const char *)(base + dd->DllNameRVA)); HMODULE *slot = (HMODULE *)(base + dd->ModuleHandleRVA);
  ULONGLONG *iat = (ULONGLONG *)(base + dd->ImportAddressTableRVA), *intb = (ULONGLONG *)(base + dd->ImportNameTableRVA), v; FARPROC p; DWORD old;
  if (!d) return NULL;
  v = intb[thunk - iat];
  p = (v >> 63) ? wxr_proc(d, NULL, (int)(v & 0xFFFF), 0) : wxr_proc(d, (const char *)(base + (DWORD)v + 2), 0, 0);
  if (d->m && dd->ModuleHandleRVA && VirtualProtect(slot, 8, PAGE_READWRITE, &old)) { *slot = d->m; VirtualProtect(slot, 8, old, &old); }
  if (VirtualProtect(thunk, 8, PAGE_READWRITE, &old)) { *thunk = (ULONGLONG)p; VirtualProtect(thunk, 8, old, &old); }
  if (g_wxr_log) slog("[ldr] delay %s!%s -> %p", d->name, (v >> 63) ? "#ord" : (const char *)(base + (DWORD)v + 2), (void *)p);
  return p; }
__declspec(dllexport) FARPROC WINAPI ResolveDelayLoadedAPI(void *parent, const void *desc, void *fdll, void *fsys, void *thunk, ULONG flags) {
  (void)fdll; (void)fsys; (void)flags; return wxr_delay_one((BYTE *)parent, (const WXRDLY_ *)desc, (ULONGLONG *)thunk); }
__declspec(dllexport) LONG WINAPI ResolveDelayLoadsFromDll(void *parent, const char *target, ULONG flags) {
  BYTE *b = (BYTE *)parent; IMAGE_NT_HEADERS64 *nt = wxr_nt(b); const WXRDLY_ *dd; (void)flags;
  if (!nt || !nt->OptionalHeader.DataDirectory[13].VirtualAddress) return (LONG)0xC0000135;
  for (dd = (const WXRDLY_ *)(b + nt->OptionalHeader.DataDirectory[13].VirtualAddress); dd->DllNameRVA; dd++) if (!lstrcmpiA((const char *)(b + dd->DllNameRVA), target)) {
    ULONGLONG *iat = (ULONGLONG *)(b + dd->ImportAddressTableRVA), *intb = (ULONGLONG *)(b + dd->ImportNameTableRVA); int i;
    for (i = 0; intb[i]; i++) wxr_delay_one(b, dd, &iat[i]);
    return 0; }
  return (LONG)0xC0000135; }   /* STATUS_DLL_NOT_FOUND */

/* the module an api-set name stands for, for GetModuleHandleW (Win10 code probes api sets this way: CoreMessaging ties itself to a thread's
   message loop only when ext-ms-win-rtcore-ntuser-window-ext answers). As with shimgen's shim DLLs, a contract counts as present when a WXR
   module imports it (statically or delay-loaded); one Win7 cannot load is answered with wxrsys, whose GetProcAddress then also looks in the
   classic DLLs. Contracts nobody imports stay absent. */
static void wxr_note(const char *name0) {
  char name[96]; int i; wxr_lower(name0, name, sizeof name); { size_t l = strlen(name); if (l < 4 || strcmp(name + l - 4, ".dll")) lstrcatA(name, ".dll"); }
  wxr_lock(1);
  for (i = 0; i < g_nwd; i++) if (!strcmp(g_wd[i].name, name)) break;
  if (i == g_nwd && g_nwd < 1024) { memset(&g_wd[g_nwd], 0, sizeof g_wd[0]); lstrcpynA(g_wd[g_nwd].name, name, sizeof g_wd[0].name); g_wd[g_nwd].imported = 1; g_nwd++; }
  wxr_lock(0); }
static void wxr_note_delay(BYTE *b) {
  IMAGE_NT_HEADERS64 *nt = wxr_nt(b); const WXRDLY_ *dd;
  if (!nt || !nt->OptionalHeader.DataDirectory[13].VirtualAddress) return;
  for (dd = (const WXRDLY_ *)(b + nt->OptionalHeader.DataDirectory[13].VirtualAddress); dd->DllNameRVA; dd++) {
    const char *dn = (const char *)(b + dd->DllNameRVA); WxrDll_ *d;
    wxr_note(dn);
    /* a contract Win7 cannot load: bind its delay slots now (wxrsys / classic DLLs / ordmap / thunks, as shimgen's shim did). Left lazy,
       the CRT delay helper would get only a stand-in handle and could not resolve ordinals through it (0xC06D007F). */
    if ((!_strnicmp(dn, "api-", 4) || !_strnicmp(dn, "ext-", 4)) && (d = wxr_dll(dn)) != NULL && !d->m && dd->ImportNameTableRVA) {
      ULONGLONG *iat = (ULONGLONG *)(b + dd->ImportAddressTableRVA), *intb = (ULONGLONG *)(b + dd->ImportNameTableRVA); int i;
      for (i = 0; intb[i]; i++) wxr_delay_one(b, dd, &iat[i]);
      { HMODULE *slot = (HMODULE *)(b + dd->ModuleHandleRVA); DWORD old; if (dd->ModuleHandleRVA && VirtualProtect(slot, 8, PAGE_READWRITE, &old)) { *slot = g_wxr_self; VirtualProtect(slot, 8, old, &old); } }
      if (g_wxr_log) slog("[ldr] delay contract %s bound up front (%d functions)", dn, i); } } }
static HMODULE wxr_module_for(const char *name0) {
  char name[96]; int i, known = 0; WxrDll_ *d;
  wxr_lower(name0, name, sizeof name); { size_t l = strlen(name); if (l < 4 || strcmp(name + l - 4, ".dll")) lstrcatA(name, ".dll"); }
  wxr_lock(1); for (i = 0; i < g_nwd; i++) if (!strcmp(g_wd[i].name, name)) { known = g_wd[i].imported; break; } wxr_lock(0);
  if (!known || !(d = wxr_dll(name))) return NULL;
  return d->m ? d->m : g_wxr_self; }

/* plain-name LoadLibraryW/ExW of a DLL Win7's System32 does NOT have (e.g. WinUI 3's CoreMessagingXP: LoadLibraryW("CoreMessaging.dll")
   for the system DispatcherQueue exports - VxKex's DLL rewrite answered with Kx.dll): the image's copy through the binding rules.
   Names Win7 has keep Win7's behaviour (NULL = not handled here). */
static HMODULE wxr_plain_loadw(const WCHAR *n, HANDLE file, DWORD fl) {
  char a[96]; WCHAR sp[MAX_PATH]; UINT l; WxrDll_ *d;
  if (!g_wxr_on || !n || file || (fl & 0x6A) || wcschr(n, L'\\') || wcschr(n, L'/') || wcslen(n) > 90) return NULL;
  l = GetSystemDirectoryW(sp, MAX_PATH); if (!l || l > MAX_PATH - 100) return NULL; sp[l] = L'\\'; lstrcpyW(sp + l + 1, n);
  if (!wcschr(n, L'.')) lstrcatW(sp, L".dll");
  WideCharToMultiByte(CP_ACP, 0, n, -1, a, sizeof a, NULL, NULL);
  if (GetFileAttributesW(sp) != INVALID_FILE_ATTRIBUTES) {
    /* Win7 has one too: Win7's, unless the image copy is already loaded for Win10 modules (their static imports took it, "listed as
       w10_..."). Then a dynamic load from a Win10 module gets the same copy - AppxPackaging loads OpcServices.dll at run time and asked
       Win7's for ordinal 13 (the bundle reader failed with 0x80070002). */
    if ((d = wxr_dll(a)) == NULL || !d->m || !d->wxr) return NULL;
    { static volatile LONG nl2; if (InterlockedIncrement(&nl2) <= 20) slog("[ldr] LoadLibraryW(%ls) -> image copy %s (already bound for Win10 modules; not Win7's)", n, d->name); }
    return d->m; }
  if ((d = wxr_dll(a)) == NULL || !d->m || !d->wxr) return NULL;
  { static volatile LONG nl; if (InterlockedIncrement(&nl) <= 20) slog("[ldr] LoadLibraryW(%ls) -> image copy %s (Win7 has none)", n, d->name); }
  return d->m; }

/* LoadLibraryExA (forced, so only WXR modules get it): the C runtime's delay-load helper (__delayLoadHelper2, used by CoreMessaging & co.)
   loads its DLLs with the original import names. shimgen had renamed those; here plain module loads take the binding rules (image/package
   copy first, Win7's otherwise; a contract Win7 cannot load is answered with wxrsys like wxr_module_for). Data-file / special loads and
   paths go to Win7 unchanged. */
__declspec(dllexport) HMODULE WINAPI LoadLibraryExA(LPCSTR n, HANDLE file, DWORD fl) {
  static HMODULE (WINAPI *real)(LPCSTR, HANDLE, DWORD); HMODULE m; WxrDll_ *d;
  if (!real) real = (HMODULE (WINAPI *)(LPCSTR, HANDLE, DWORD))GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryExA");
  if (!g_wxr_on || !n || file || (fl & 0x6A) || strchr(n, '\\') || strchr(n, '/')) return real(n, file, fl);   /* 0x6A: datafile / image resource / as-is */
  if ((d = wxr_dll(n)) != NULL && (d->m || !_strnicmp(n, "api-", 4) || !_strnicmp(n, "ext-", 4))) {
    m = d->m ? d->m : g_wxr_self; if (g_wxr_log || !d->m) slog("[ldr] LoadLibraryExA(%s) -> %s", n, d->m ? d->name : "wxrsys (contract stand-in)"); return m; }
  return real(n, file, fl); }

/* ---- the app window as the Win10 shell shows it: title, icon and taskbar identity from <root>\ProgramData\Shell\<AUMID>.ini (wxr sync) ----
   Title = DisplayName (WXR_WINDOW_TITLE overrides), icon = the .ico made from the manifest logo, System.AppUserModel.ID = the AUMID (the
   taskbar groups the app's windows with its Start-menu shortcut and pins it), RelaunchCommand = wxr.exe activate <AUMID>. */
static void wxr_pv_set(void *ps, DWORD pid, const WCHAR *s) {
  static const GUID fmt = { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } };
  struct { GUID f; DWORD p; } key; PROPVARIANT v; key.f = fmt; key.p = pid; memset(&v, 0, sizeof v); v.vt = VT_LPWSTR; v.pwszVal = (LPWSTR)s;
  ((HRESULT (STDMETHODCALLTYPE *)(void *, const void *, const PROPVARIANT *))(*(void ***)ps)[6])(ps, &key, &v); }   /* IPropertyStore::SetValue */
static void wxr_window_decorate(HWND h) {
  WCHAR fn[400], ini[MAX_PATH], title[260] = L"", ico[MAX_PATH] = L"", *c; HICON big, small;
  if (!g_wxr_on || !h) return;
  pkg_load(); if (!g_aumid[0]) return;
  lstrcpynW(fn, g_aumid, 400); for (c = fn; *c; c++) if (wcschr(L"\\/:*?\"<>|", *c)) *c = L'_';
  _snwprintf(ini, MAX_PATH, L"%ls\\ProgramData\\Shell\\%ls.ini", g_wxr_root, fn);
  if (!GetEnvironmentVariableW(L"WXR_WINDOW_TITLE", title, 260)) GetPrivateProfileStringW(L"app", L"DisplayName", HEAD_->name, title, 260, ini);
  SetWindowTextW(h, title);
  GetPrivateProfileStringW(L"app", L"Icon", L"", ico, MAX_PATH, ini);
  if (ico[0]) {
    big = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    small = (HICON)LoadImageW(NULL, ico, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    if (big) SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)big); if (small) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)small); }
  {   /* taskbar identity (shell32 SHGetPropertyStoreForWindow, Win7+) */
    typedef HRESULT (WINAPI *PGPS)(HWND, REFIID, void **); static const GUID iid_ps = { 0x886d8eeb, 0x8cf2, 0x4446, { 0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99 } };
    typedef HRESULT (WINAPI *PSAI)(PCWSTR);
    HMODULE s32 = LoadLibraryA("shell32.dll"); PGPS gps = s32 ? (PGPS)GetProcAddress(s32, "SHGetPropertyStoreForWindow") : NULL; PSAI sai = s32 ? (PSAI)GetProcAddress(s32, "SetCurrentProcessExplicitAppUserModelID") : NULL; void *ps = NULL;
    if (sai) sai(g_aumid);
    if (gps && SUCCEEDED(gps(h, &iid_ps, &ps)) && ps) { WCHAR cmd[MAX_PATH + 450], icr[MAX_PATH + 4];
      _snwprintf(cmd, MAX_PATH + 450, L"\"%ls\\wxr.exe\" activate %ls", g_wxr_layer, g_aumid);
      wxr_pv_set(ps, 5, g_aumid);                                     /* System.AppUserModel.ID */
      wxr_pv_set(ps, 2, cmd);                                         /* RelaunchCommand */
      wxr_pv_set(ps, 4, title);                                       /* RelaunchDisplayNameResource */
      if (ico[0]) { _snwprintf(icr, MAX_PATH + 4, L"%ls,0", ico); wxr_pv_set(ps, 3, icr); }   /* RelaunchIconResource */
      ((ULONG (STDMETHODCALLTYPE *)(void *))(*(void ***)ps)[2])(ps); } }
  slog("[shell] window %p: \"%ls\", icon %ls, AUMID %ls", (void *)h, title, ico[0] ? ico : L"(none)", g_aumid); }

/* ---- splash screen: what Win10's shell shows from the manifest until the app's first frame ----
   An owned popup exactly over the CoreWindow's client area (a child window would clip the app's first frames away) paints SplashColor and the SplashImage (centred, at its logical size: the image's
   pixels / SplashScale%, times the screen scale). It goes away at the second frame dcomp presents after the app's activation returned
   (WxrFramePresented, called by dcomp after each Present), or after 20 s. */
static HWND g_splash, g_splash_parent; static volatile LONG g_splash_ready, g_splash_frames;
static void *g_splash_img; static UINT g_splash_iw, g_splash_ih; static int g_splash_scale = 100; static COLORREF g_splash_bg = RGB(0, 0x78, 0xD4);
typedef struct { UINT32 ver; void *cb; BOOL nobg, nocodecs; } WxrGdipIn_;
static int (WINAPI *p_GdipCreateFromHDC)(HDC, void **); static int (WINAPI *p_GdipDrawImageRectI)(void *, void *, int, int, int, int);
static int (WINAPI *p_GdipDeleteGraphics)(void *); static int (WINAPI *p_GdipSetInterpolationMode)(void *, int);
static LRESULT CALLBACK wxr_splash_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; HBRUSH br = CreateSolidBrush(g_splash_bg); void *g = NULL;
      GetClientRect(h, &rc); FillRect(dc, &rc, br); DeleteObject(br);
      if (g_splash_img && p_GdipCreateFromHDC && !p_GdipCreateFromHDC(dc, &g) && g) {
        float s = (float)GetDeviceCaps(dc, LOGPIXELSX) / 96.0f; int w = (int)(g_splash_iw * 100.0f / g_splash_scale * s), hh = (int)(g_splash_ih * 100.0f / g_splash_scale * s);
        if (w > rc.right) { hh = hh * rc.right / w; w = rc.right; }   /* narrow windows (Calculator): shrink to fit, keep the aspect */
        p_GdipSetInterpolationMode(g, 7 /* HighQualityBicubic */);
        p_GdipDrawImageRectI(g, g_splash_img, (rc.right - w) / 2, (rc.bottom - hh) / 2, w, hh); p_GdipDeleteGraphics(g); }
      EndPaint(h, &ps); return 0; }
  case WM_TIMER: case WM_CLOSE: DestroyWindow(h); return 0;
  case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
  case WM_DESTROY: if (g_splash == h) { g_splash = NULL; slog("[shell] splash screen removed"); } return 0; }
  return DefWindowProcW(h, m, wp, lp); }
static void wxr_splash_resize(HWND parent) { RECT rc; POINT o = { 0, 0 };   /* follows the owner's client area (move, size); shown once the owner is */
  if (g_splash && parent == g_splash_parent && GetClientRect(parent, &rc) && ClientToScreen(parent, &o))
    SetWindowPos(g_splash, NULL, o.x, o.y, rc.right, rc.bottom, SWP_NOACTIVATE | SWP_NOZORDER | (IsWindowVisible(parent) && !IsIconic(parent) ? SWP_SHOWWINDOW : 0)); }
static void wxr_splash_create(HWND parent) {
  WCHAR fn[400], ini[MAX_PATH], img[MAX_PATH] = L"", col[40] = L"", *c; WNDCLASSW wc; RECT rc;
  if (!g_wxr_on || !parent || g_splash) return;
  pkg_load(); if (!g_aumid[0]) return;
  lstrcpynW(fn, g_aumid, 400); for (c = fn; *c; c++) if (wcschr(L"\\/:*?\"<>|", *c)) *c = L'_';
  _snwprintf(ini, MAX_PATH, L"%ls\\ProgramData\\Shell\\%ls.ini", g_wxr_root, fn);
  GetPrivateProfileStringW(L"app", L"SplashImage", L"", img, MAX_PATH, ini); GetPrivateProfileStringW(L"app", L"SplashColor", L"", col, 40, ini);
  g_splash_scale = GetPrivateProfileIntW(L"app", L"SplashScale", 100, ini); if (g_splash_scale < 25) g_splash_scale = 100;
  if (!img[0] && !col[0]) return;
  if (col[0] == L'#' && lstrlenW(col) >= 7) { unsigned v = (unsigned)wcstoul(col + 1, NULL, 16); if (lstrlenW(col) >= 9) v &= 0xFFFFFF; g_splash_bg = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }
  if (img[0]) {   /* GDI+ (Win7's) decodes the PNG */
    HMODULE gp = LoadLibraryA("gdiplus.dll"); int (WINAPI *start)(ULONG_PTR *, const WxrGdipIn_ *, void *); int (WINAPI *load)(const WCHAR *, void **);
    int (WINAPI *gw)(void *, UINT *); int (WINAPI *gh)(void *, UINT *); ULONG_PTR tok; WxrGdipIn_ in = { 1, NULL, FALSE, FALSE };
    if (gp) { start = (void *)GetProcAddress(gp, "GdiplusStartup"); load = (void *)GetProcAddress(gp, "GdipLoadImageFromFile"); gw = (void *)GetProcAddress(gp, "GdipGetImageWidth"); gh = (void *)GetProcAddress(gp, "GdipGetImageHeight");
      p_GdipCreateFromHDC = (void *)GetProcAddress(gp, "GdipCreateFromHDC"); p_GdipDrawImageRectI = (void *)GetProcAddress(gp, "GdipDrawImageRectI");
      p_GdipDeleteGraphics = (void *)GetProcAddress(gp, "GdipDeleteGraphics"); p_GdipSetInterpolationMode = (void *)GetProcAddress(gp, "GdipSetInterpolationMode");
      if (start && load && gw && gh && p_GdipCreateFromHDC && p_GdipDrawImageRectI && p_GdipDeleteGraphics && p_GdipSetInterpolationMode && !start(&tok, &in, NULL) && !load(img, &g_splash_img) && g_splash_img) {
        gw(g_splash_img, &g_splash_iw); gh(g_splash_img, &g_splash_ih); }
      else g_splash_img = NULL; } }
  memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wxr_splash_proc; wc.hInstance = g_wxr_self; wc.lpszClassName = L"WXR.SplashScreen"; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_APPSTARTING);
  RegisterClassW(&wc);
  GetClientRect(parent, &rc); g_splash_parent = parent;
  g_splash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"WXR.SplashScreen", L"", WS_POPUP, 0, 0, rc.right, rc.bottom, parent /* owner */, NULL, g_wxr_self, NULL);
  if (g_splash) { wxr_splash_resize(parent); SetTimer(g_splash, 1, 20000, NULL); }
  slog("[shell] splash screen %ls (%ux%u @%d%%, colour %ls) -> %p", img[0] ? img : L"(none)", g_splash_iw, g_splash_ih, g_splash_scale, col, (void *)g_splash); }
static void wxr_splash_activated(void) { InterlockedExchange(&g_splash_ready, 1); }
/* dcomp, after every Present to a window: the splash leaves at the second frame after activation */
__declspec(dllexport) void WINAPI WxrFramePresented(HWND h) {
  HWND s = g_splash; static volatile LONG nall;
  if (InterlockedIncrement(&nall) <= 3) slog("[shell] frame presented to %p (splash %p, app activated %ld)", (void *)h, (void *)s, g_splash_ready);
  if (s && g_splash_ready && InterlockedIncrement(&g_splash_frames) == 2) { slog("[shell] app frames arrived: splash screen leaves"); PostMessageW(s, WM_CLOSE, 0, 0); } }

/* ---- start (wxrsys DllMain, process attach) ---- */
static void wxr_start(HINSTANCE self) {
  WCHAR v[16], dev[MAX_PATH], drv[3], exe[MAX_PATH], *s; BYTE *eb = (BYTE *)GetModuleHandleW(NULL); IMAGE_NT_HEADERS64 *ent = wxr_nt(eb);
  if (!GetEnvironmentVariableW(L"WXR_LOADER", v, 16) || v[0] != L'1') return;
  g_wxr_self = self; InitializeCriticalSection(&g_wxr_cs); g_wxr_log = GetEnvironmentVariableA("WXR_LDRLOG", NULL, 0) != 0;
  GetModuleFileNameW(self, g_wxr_layer, MAX_PATH); if ((s = wcsrchr(g_wxr_layer, L'\\'))) *s = 0;
  if (!GetEnvironmentVariableW(L"WXR_ROOT", g_wxr_root, MAX_PATH)) { lstrcpyW(g_wxr_root, g_wxr_layer); if ((s = wcsrchr(g_wxr_root, L'\\'))) *s = 0; }
  _snwprintf(g_wxr_img, MAX_PATH, L"%ls\\Windows\\System32", g_wxr_root); _snwprintf(g_wxr_ovr, MAX_PATH, L"%ls\\Layer\\override", g_wxr_root);
  { UINT (WINAPI *gsd)(LPWSTR, UINT) = (UINT (WINAPI *)(LPWSTR, UINT))GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemDirectoryW"); gsd(g_wxr_sys7, MAX_PATH); }
  drv[0] = g_wxr_root[0]; drv[1] = L':'; drv[2] = 0;
  if (g_wxr_root[1] != L':' || !QueryDosDeviceW(drv, dev, MAX_PATH)) { slog("[ldr] root %ls: no drive letter, loader layer OFF", g_wxr_root); return; }
  _snwprintf(g_wxr_rootnt, MAX_PATH, L"%ls%ls", dev, g_wxr_root + 2);
  r_LoadLibraryExW = (HMODULE (WINAPI *)(LPCWSTR, HANDLE, DWORD))GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryExW");
  r_LoadLibraryA = (HMODULE (WINAPI *)(LPCSTR))GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
  g_qvm = (PFN_NtQVM_)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryVirtualMemory");
  if (!wxr_hook_map()) { slog("[ldr] could not hook NtMapViewOfSection: loader layer OFF"); return; }
  g_wxr_on = 1;
  slog("[ldr] WXR loader layer on: root %ls (%ls), image %ls", g_wxr_root, g_wxr_rootnt, g_wxr_img);
  /* the exe: its real import directory is in the file (the launcher replaced the in-memory one with a single import of wxrsys) */
  GetModuleFileNameW(NULL, exe, MAX_PATH);
  if (ent) { HANDLE f = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL); BYTE hb[4096]; DWORD n = 0; IMAGE_NT_HEADERS64 *fnt;
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, hb, sizeof hb, &n, NULL); CloseHandle(f); }
    if (n >= 1024 && (fnt = wxr_nt(hb)) && (BYTE *)fnt - hb < 3000 && fnt->OptionalHeader.DataDirectory[1].VirtualAddress != ent->OptionalHeader.DataDirectory[1].VirtualAddress) {
      DWORD irva = fnt->OptionalHeader.DataDirectory[1].VirtualAddress, isz = fnt->OptionalHeader.DataDirectory[1].Size, old;
      wxr_bind(eb, exe, irva); wxr_note_delay(eb);
      if (VirtualProtect(eb, ent->OptionalHeader.SizeOfHeaders, PAGE_READWRITE, &old)) { ent->OptionalHeader.DataDirectory[1].VirtualAddress = irva; ent->OptionalHeader.DataDirectory[1].Size = isz; VirtualProtect(eb, ent->OptionalHeader.SizeOfHeaders, old, &old); } }
    else slog("[ldr] the exe's imports were not redirected by a launcher: only modules loaded from now on are WXR-bound"); }
  slog("[ldr] start done: %ld functions bound, %ld thunks", g_wxr_nbind, g_wxr_nthunk); }

/* module fixups that used to be file patches or fixed-address code elsewhere */
/* Windows.UI.Xaml 19041.3803: XAML alpha masks (TextBlock/Image/Shape.GetAlphaMask, used by apps for DropShadow.Mask - the Store does)
   rasterise the element with D2D into a WIC bitmap target on a WARP device (AlphaMask::Impl::UpdateIfAvailable -> EnsureBackingSurface,
   ConnectBackingSurface, RasterizeElement -> WinTextCore::CreateSharedD2DResources). Win7's WARP cannot do what Win10's D2D asks of it
   (0x88990024; a hardware device fails too), and AlphaMask::Impl::Ensure fail-fasts on any error -> the app dies whenever such content renders.
   Rasterising works for most content, and masks are not only for shadows: the Store shows its nav pane and search box through them, so
   an always-empty mask hides those (tried first; wrong). So UpdateIfAvailable (RVA 0x133D70) is wrapped: the original runs, and only a
   failure is turned into S_OK (that element's mask stays as it was, logged). Applied only when the prologues of UpdateIfAvailable and
   RasterizeElement (0x8C0110) match. */
typedef HRESULT (*PAMUPD_)(void *, void *, void *, void *);
static PAMUPD_ g_amupd_real;
static HRESULT am_update_hook(void *impl, void *elem, void *a3, void *a4) {
  static volatile LONG nlog; HRESULT hr = g_amupd_real(impl, elem, a3, a4);
  if (FAILED(hr)) { if (InterlockedIncrement(&nlog) <= 20) slog("[xaml] alpha mask of element %p failed 0x%08lX: mask left as is, fail-fast avoided", elem, (unsigned long)hr); return S_OK; }
  return hr; }
static void wxr_fix_alphamask(WxrMod_ *r) {
  static const BYTE upd[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57 }, ras[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x55 };
  BYTE *f = r->base + 0x133D70, *t; DWORD old;
  if (r->size < 0x8C0200 || memcmp(f, upd, sizeof upd) || memcmp(r->base + 0x8C0110, ras, sizeof ras)) { slog("[ldr] XAML alpha masks: unknown Windows.UI.Xaml build, not patched"); return; }
  /* trampoline: the 12 prologue bytes (no RIP-relative operands) + jmp to f+12 */
  t = (BYTE *)VirtualAlloc(NULL, 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE); if (!t) { slog("[ldr] XAML alpha masks: no trampoline memory"); return; }
  memcpy(t, upd, 12); t[12] = 0x48; t[13] = 0xB8; *(BYTE **)(t + 14) = f + 12; t[22] = 0xFF; t[23] = 0xE0;   /* mov rax, f+12; jmp rax */
  g_amupd_real = (PAMUPD_)t;
  if (!VirtualProtect(f, 12, PAGE_EXECUTE_READWRITE, &old)) { slog("[ldr] XAML alpha masks: VirtualProtect failed %lu", GetLastError()); return; }
  f[0] = 0x48; f[1] = 0xB8; *(void **)(f + 2) = (void *)am_update_hook; f[10] = 0xFF; f[11] = 0xE0;   /* mov rax, hook; jmp rax */
  VirtualProtect(f, 12, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 12);
  slog("[ldr] XAML alpha masks: AlphaMask::Impl::UpdateIfAvailable wrapped (failures -> S_OK, no fail-fast)");
  /* RasterizeElement's own fail-fast: CD2DRenderTarget<...>::EndDraw (0x62B910, XAML's wrapper of ID2D1RenderTarget::EndDraw) returns
     device-lost errors to the caller and fail-fasts on every other one (Win7 WARP: 0x88990024). Its `je` after IsDeviceLostError
     (0x62B945: 74 09 -> fail-fast block) now goes to the success exit (74 3B -> 0x62B982): device lost behaves as before, any other
     EndDraw error counts as drawn (that mask may stay partly empty). */
  { static const BYTE ed[] = { 0x8B, 0xD8, 0x85, 0xC0, 0x79, 0x46, 0x8B, 0xC8, 0xE8, 0x75, 0x02, 0xB8, 0xFF, 0x84, 0xC0, 0x74, 0x09, 0xE8 };
    BYTE *e = r->base + 0x62B936;
    if (memcmp(e, ed, sizeof ed) || r->base[0x62B982] != 0x48) slog("[ldr] XAML EndDraw: unexpected code, not patched");
    else if (VirtualProtect(e + 16, 1, PAGE_EXECUTE_READWRITE, &old)) { e[16] = 0x3B; VirtualProtect(e + 16, 1, old, &old); FlushInstructionCache(GetCurrentProcess(), e + 16, 1);
      slog("[ldr] XAML EndDraw: non-device-lost D2D errors no longer fail-fast"); } } }

/* generic inline hook: the first n prologue bytes (n >= 12, whole instructions, nothing RIP-relative) must equal `pro`; returns the
   trampoline that runs the original, or NULL (logged by the caller) */
static void *wxr_hook(BYTE *f, const BYTE *pro, int n, void *hook) {
  BYTE *t; DWORD old;
  if (n < 12 || memcmp(f, pro, n)) return NULL;
  t = (BYTE *)VirtualAlloc(NULL, 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE); if (!t) return NULL;
  memcpy(t, pro, n); t[n] = 0x48; t[n + 1] = 0xB8; *(BYTE **)(t + n + 2) = f + n; t[n + 10] = 0xFF; t[n + 11] = 0xE0;   /* mov rax, f+n; jmp rax */
  if (!VirtualProtect(f, 12, PAGE_EXECUTE_READWRITE, &old)) return NULL;
  f[0] = 0x48; f[1] = 0xB8; *(void **)(f + 2) = hook; f[10] = 0xFF; f[11] = 0xE0;
  VirtualProtect(f, 12, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 12); return t; }

/* twinapi.appcore 19041: CoreApplicationViewTitleBar::get_Height (RVA 0x18F00) returns its cached value or asks the frame host
   (ApplicationFrameHost on Win10) - there is none here, so 0. Apps that draw their own title bar row size it from this (the Store's
   "Microsoft Store" title text was laid out 0 wide). 0 -> 32, Win10's caption height at 100 %. */
typedef HRESULT (*PTBH_)(void *, double *);
static PTBH_ g_tbh_real;
static HRESULT tb_height_hook(void *self, double *out) {
  HRESULT hr = g_tbh_real(self, out);
  if (SUCCEEDED(hr) && out && *out <= 0.0) *out = 32.0;
  return hr; }
static void wxr_fix_titlebar(WxrMod_ *r) {
  static const BYTE pro[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA };
  if (r->size < 0x19000 || !(g_tbh_real = (PTBH_)wxr_hook(r->base + 0x18F00, pro, sizeof pro, (void *)tb_height_hook))) { slog("[ldr] twinapi.appcore: title bar height not patched (unknown build)"); return; }
  slog("[ldr] twinapi.appcore: CoreApplicationViewTitleBar.Height 0 -> 32"); }

/* Microsoft.UI.Input (WinAppSDK in the WinUI 3 Gallery, image size 0x186000): InputSiteWinRT::IsPointerInputOnUIThread (RVA 0x216D0)
   decides where mouse/pointer input is processed. It is true only when an InputSiteHwndWinRT is already attached, but
   PointerInputObserverWinRT::RuntimeClassInitialize asks during the input-site window's creation, before the attachment -> false ->
   input-thread mode, where Win10's composition input sinks deliver WM_POINTER to the input thread (no Win7 equivalent; clicks were
   ignored, only the wheel worked). True = UI-thread mode: the observer emulates mouse-in-pointer from the legacy mouse messages that
   wxrinput.inc forwards to the input site. Applied only on an exact prologue match. */
static void wxr_fix_winui_input(WxrMod_ *r) {
  static const BYTE pro[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xD9, 0xE8, 0x8D, 0xD7, 0xFF, 0xFF, 0x84 };
  static const BYTE ret1[] = { 0xB0, 0x01, 0xC3 };   /* mov al, 1; ret */
  BYTE *f = r->base + 0x216D0; DWORD old;
  if (r->size < 0x21700 || memcmp(f, pro, sizeof pro)) { slog("[ldr] Microsoft.UI.Input: unknown build, pointer input stays on the input thread (clicks will not work)"); return; }
  if (!VirtualProtect(f, sizeof ret1, PAGE_EXECUTE_READWRITE, &old)) return;
  memcpy(f, ret1, sizeof ret1); VirtualProtect(f, sizeof ret1, old, &old); FlushInstructionCache(GetCurrentProcess(), f, sizeof ret1);
  slog("[ldr] Microsoft.UI.Input: IsPointerInputOnUIThread -> true (mouse input emulated on the UI thread)"); }
/* module fixups that used to be file patches or fixed-address code elsewhere */
static void wxr_fixups(WxrMod_ *r) {
  const WCHAR *n = r->path, *p; for (p = r->path; *p; p++) if (*p == L'\\' || *p == L'/') n = p + 1;
  if (!lstrcmpiW(n, L"Windows.UI.Xaml.dll")) wxr_fix_alphamask(r);
  else if (!lstrcmpiW(n, L"twinapi.appcore.dll")) wxr_fix_titlebar(r);
  else if (!lstrcmpiW(n, L"Microsoft.UI.Input.dll")) wxr_fix_winui_input(r); }
