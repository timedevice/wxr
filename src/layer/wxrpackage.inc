/* wxrpackage.inc - package identity and package graph for the process.  Included by wxrsys.c.
   The head package is the AppxManifest.xml next to the exe (or WXR_PACKAGE_MANIFEST).  Framework dependencies named in that manifest and the
   head package's resource packs ("split." resource ids) are looked up in the package store: WXR_PACKAGE_STORE, else <WXR root>\WindowsApps.
   A store entry is a folder "Name_Version_Arch_ResourceId_PublisherId" holding its own AppxManifest.xml.
   Implements GetCurrentPackage*, GetPackage*, ApplicationUserModelId, PackageIdFromFullName & co, GetStagedPackagePathByFullName2, the PRI context APIs,
   AppXGetOSMaxVersionTested and ntdll!RtlQueryPackageClaims. */
#include <wincrypt.h>

typedef struct { WCHAR name[128], publisher[300], resid[64], pubid[16], family[160], full[300], path[MAX_PATH]; USHORT arch; USHORT v[4]; UINT32 flags; } PKGENT_;
#define PKGF_FRAMEWORK 0x1
#define PKGF_RESOURCE  0x2
static PKGENT_ g_ent[24]; static int g_nent; static WCHAR g_appid[64], g_aumid[240], g_store[MAX_PATH]; static volatile LONG g_pkg_tried, g_pkg_busy;
static int g_app_fulltrust;
static PKGENT_ g_pcat[64]; static int g_npcat;

/* PublisherId = base32 ("0123456789abcdefghjkmnpqrstvwxyz") of the first 8 bytes of SHA-256(UTF-16LE publisher) */
static int pkg_publisher_id(const WCHAR *pub, WCHAR *out) {
  HCRYPTPROV p = 0; HCRYPTHASH h = 0; BYTE d[32]; DWORD dl = 32; int ok = 0, i; static const char al[] = "0123456789abcdefghjkmnpqrstvwxyz";
  if (!CryptAcquireContextW(&p, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return 0;
  if (CryptCreateHash(p, CALG_SHA_256, 0, 0, &h)) {
    if (CryptHashData(h, (const BYTE *)pub, (DWORD)(wcslen(pub) * 2), 0) && CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0)) {
      unsigned long long v = 0; for (i = 0; i < 8; i++) v = (v << 8) | d[i];
      for (i = 0; i < 13; i++) { int sh = 64 - 5 * (i + 1); unsigned idx = sh >= 0 ? (unsigned)((v >> sh) & 31) : (unsigned)((v << (-sh)) & 31); out[i] = (WCHAR)al[idx]; }
      out[13] = 0; ok = 1; }
    CryptDestroyHash(h); }
  CryptReleaseContext(p, 0); return ok;
}
static int xml_attr(const char *tag, const char *attr, char *out, size_t cap) {   /* tag = start of "<Element ...", finds attr="value" before the closing '>' */
  const char *end = strchr(tag, '>'), *p = tag; size_t al = strlen(attr); out[0] = 0;
  while ((p = strstr(p, attr)) && (!end || p < end)) { if ((p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t' || p[-1] == '\r') && p[al] == '=' && p[al + 1] == '"') { const char *q = p + al + 2, *e = strchr(q, '"'); if (!e) return 0; if ((size_t)(e - q) >= cap) return 0; memcpy(out, q, e - q); out[e - q] = 0; return 1; } p += al; }
  return 0;
}
static char *read_text(const WCHAR *path) {
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); char *buf = NULL; DWORD n, rd; if (f == INVALID_HANDLE_VALUE) return NULL;
  n = GetFileSize(f, NULL); if (n && n < 4000000) { buf = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1); if (buf && ReadFile(f, buf, n, &rd, NULL)) buf[rd] = 0; else { if (buf) HeapFree(GetProcessHeap(), 0, buf); buf = NULL; } }
  CloseHandle(f); return buf;
}
static const WCHAR *arch_str(USHORT a) { return a == 0 ? L"x86" : a == 5 ? L"arm" : a == 9 ? L"x64" : a == 12 ? L"arm64" : L"neutral"; }
static void ent_names(PKGENT_ *e) {
  _snwprintf(e->family, 160, L"%ls_%ls", e->name, e->pubid);
  _snwprintf(e->full, 300, L"%ls_%d.%d.%d.%d_%ls_%ls_%ls", e->name, e->v[3], e->v[2], e->v[1], e->v[0], arch_str(e->arch), e->resid, e->pubid);
}
/* fills e from the <Identity> of a manifest text; returns 1 on success */
static int ent_from_manifest(PKGENT_ *e, const char *buf, const WCHAR *dir) {
  const char *id = strstr(buf, "<Identity "); char a[400]; int maj = 0, min = 0, bld = 0, rev = 0;
  if (!id) return 0; memset(e, 0, sizeof *e);
  if (xml_attr(id, "Name", a, sizeof a)) MultiByteToWideChar(CP_UTF8, 0, a, -1, e->name, 128);
  if (xml_attr(id, "Publisher", a, sizeof a)) MultiByteToWideChar(CP_UTF8, 0, a, -1, e->publisher, 300);
  if (xml_attr(id, "ResourceId", a, sizeof a)) MultiByteToWideChar(CP_UTF8, 0, a, -1, e->resid, 64);
  if (xml_attr(id, "Version", a, sizeof a)) sscanf(a, "%d.%d.%d.%d", &maj, &min, &bld, &rev);
  e->v[0] = (USHORT)rev; e->v[1] = (USHORT)bld; e->v[2] = (USHORT)min; e->v[3] = (USHORT)maj; e->arch = 9;
  if (xml_attr(id, "ProcessorArchitecture", a, sizeof a)) { if (!_stricmp(a, "x86")) e->arch = 0; else if (!_stricmp(a, "arm")) e->arch = 5; else if (!_stricmp(a, "arm64")) e->arch = 12; else if (!_stricmp(a, "neutral")) e->arch = 11; }
  if (!e->name[0] || !e->publisher[0] || !pkg_publisher_id(e->publisher, e->pubid)) return 0;
  ent_names(e); wcsncpy(e->path, dir, MAX_PATH - 1); return 1;
}
static void pkg_scan_store(void) {
  WCHAR pat[MAX_PATH + 8]; WIN32_FIND_DATAW fd; HANDLE h;
  _snwprintf(pat, MAX_PATH + 8, L"%ls\\*", g_store); h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
  do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.' && g_npcat < 64) {
      WCHAR d[MAX_PATH], m[MAX_PATH + 20]; char *t; _snwprintf(d, MAX_PATH, L"%ls\\%ls", g_store, fd.cFileName); _snwprintf(m, MAX_PATH + 20, L"%ls\\AppxManifest.xml", d);
      if ((t = read_text(m))) { if (ent_from_manifest(&g_pcat[g_npcat], t, d)) g_npcat++; HeapFree(GetProcessHeap(), 0, t); } } } while (FindNextFileW(h, &fd));
  FindClose(h);
}
static int ver_ge(const USHORT *a, const USHORT *b) { int i; for (i = 3; i >= 0; i--) { if (a[i] != b[i]) return a[i] > b[i]; } return 1; }
static void pkg_load(void) {
  WCHAR mp[MAX_PATH], exe[MAX_PATH], *sl, dir[MAX_PATH]; char *buf, a[400], *ap, *dep; int i;
  if (g_pkg_tried) return; if (InterlockedExchange(&g_pkg_busy, 1)) { while (!g_pkg_tried) Sleep(1); return; }
  GetModuleFileNameW(NULL, exe, MAX_PATH); wcscpy(dir, exe); sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
  if (GetEnvironmentVariableW(L"WXR_PACKAGE_MANIFEST", mp, MAX_PATH) == 0) _snwprintf(mp, MAX_PATH, L"%ls\\AppxManifest.xml", dir);
  buf = read_text(mp);
  if (buf && ent_from_manifest(&g_ent[0], buf, dir)) {
    /* the head package path is the manifest's folder */
    wcscpy(g_ent[0].path, mp); sl = wcsrchr(g_ent[0].path, L'\\'); if (sl) *sl = 0; g_nent = 1;
    /* the <Application> whose Executable is this exe (WinStore.App.exe and StoreDesktopExtension.exe are the same file under two app
       ids); default: the first one. Full trust = EntryPoint Windows.FullTrustApplication (no AppContainer on Win10 either). */
    { char *first = strstr(buf, "<Application "), *cur; const WCHAR *eb = wcsrchr(exe, L'\\'); eb = eb ? eb + 1 : exe;
      for (cur = first, ap = NULL; cur; cur = strstr(cur + 12, "<Application ")) { char ex[MAX_PATH]; WCHAR wex[MAX_PATH]; const WCHAR *xb;
        if (!xml_attr(cur, "Executable", ex, sizeof ex)) continue; MultiByteToWideChar(CP_UTF8, 0, ex, -1, wex, MAX_PATH);
        for (i = 0; wex[i]; i++) if (wex[i] == L'/') wex[i] = L'\\'; xb = wcsrchr(wex, L'\\'); xb = xb ? xb + 1 : wex;
        if (!_wcsicmp(xb, eb)) { ap = cur; break; } }
      if (!ap) ap = first;
      if (ap && xml_attr(ap, "Id", a, sizeof a)) MultiByteToWideChar(CP_UTF8, 0, a, -1, g_appid, 64);
      g_app_fulltrust = ap && xml_attr(ap, "EntryPoint", a, sizeof a) && !strcmp(a, "Windows.FullTrustApplication"); }
    _snwprintf(g_aumid, 240, L"%ls!%ls", g_ent[0].family, g_appid[0] ? g_appid : L"App");
    /* package store */
    if (!GetEnvironmentVariableW(L"WXR_PACKAGE_STORE", g_store, MAX_PATH)) {   /* the WXR root's WindowsApps */
      if (g_wxr_root[0]) _snwprintf(g_store, MAX_PATH, L"%ls\\WindowsApps", g_wxr_root); else g_store[0] = 0; }
    if (g_store[0]) pkg_scan_store();
    /* framework dependencies: highest version >= MinVersion; then resource packs of the head package */
    for (dep = buf; (dep = strstr(dep, "<PackageDependency ")); dep += 10) {
      char nm[160], mv[64]; WCHAR wn[160]; USHORT mn[4] = {0, 0, 0, 0}; int maj = 0, min = 0, bld = 0, rev = 0, best = -1, j;
      if (!xml_attr(dep, "Name", nm, sizeof nm)) continue; MultiByteToWideChar(CP_UTF8, 0, nm, -1, wn, 160);
      if (xml_attr(dep, "MinVersion", mv, sizeof mv)) sscanf(mv, "%d.%d.%d.%d", &maj, &min, &bld, &rev); mn[0] = (USHORT)rev; mn[1] = (USHORT)bld; mn[2] = (USHORT)min; mn[3] = (USHORT)maj;
      for (j = 0; j < g_npcat; j++) if (!_wcsicmp(g_pcat[j].name, wn) && (g_pcat[j].arch == 9 || g_pcat[j].arch == 11) && ver_ge(g_pcat[j].v, mn) && (best < 0 || ver_ge(g_pcat[j].v, g_pcat[best].v))) best = j;
      if (best >= 0 && g_nent < 24) { g_ent[g_nent] = g_pcat[best]; g_ent[g_nent].flags = PKGF_FRAMEWORK; g_nent++; }
      else slog("[pkg] dependency %s (>= %s) not found in store %ls", nm, mv, g_store);
    }
    for (i = 0; i < g_npcat; i++) if (!_wcsicmp(g_pcat[i].name, g_ent[0].name) && g_pcat[i].resid[0] && g_nent < 24 && !_wcsicmp(g_pcat[i].pubid, g_ent[0].pubid)) { g_ent[g_nent] = g_pcat[i]; g_ent[g_nent].flags = PKGF_RESOURCE; g_nent++; }
    slog("[pkg] identity %ls (path %ls); graph of %d package(s), store %ls", g_ent[0].full, g_ent[0].path, g_nent, g_store[0] ? g_store : L"(none)");
    for (i = 1; i < g_nent; i++) slog("[pkg]   + %ls%ls", g_ent[i].full, (g_ent[i].flags & PKGF_RESOURCE) ? L" (resource)" : L"");
  } else slog("[pkg] no manifest at %ls: process stays unpackaged", mp);
  if (buf) HeapFree(GetProcessHeap(), 0, buf);
  g_pkg_tried = 1;
}
#define HEAD_ (&g_ent[0])
static LONG pkg_str(const WCHAR *s, UINT32 *len, PWSTR out) {
  UINT32 need = (UINT32)wcslen(s) + 1; if (!len) return ERROR_INVALID_PARAMETER;
  if (!out || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; }
  memcpy(out, s, need * 2); *len = need; return ERROR_SUCCESS;
}
#define PKG_OR_NONE(expr) do { pkg_load(); if (!g_nent) return ERROR_NO_PACKAGE_ID; return (expr); } while (0)
__declspec(dllexport) LONG WINAPI GetCurrentPackageFullName(UINT32 *len, PWSTR out) { PKG_OR_NONE(pkg_str(HEAD_->full, len, out)); }
__declspec(dllexport) LONG WINAPI GetCurrentPackageFamilyName(UINT32 *len, PWSTR out) { PKG_OR_NONE(pkg_str(HEAD_->family, len, out)); }
__declspec(dllexport) LONG WINAPI GetCurrentPackagePath(UINT32 *len, PWSTR out) { PKG_OR_NONE(pkg_str(HEAD_->path, len, out)); }
/* GetCurrentPackagePath2(PackagePathType, len, path): installed / mutable / effective are all the one install folder here */
__declspec(dllexport) LONG WINAPI GetCurrentPackagePath2(int type, UINT32 *len, PWSTR out) { (void)type; return GetCurrentPackagePath(len, out); }
__declspec(dllexport) LONG WINAPI GetCurrentApplicationUserModelId(UINT32 *len, PWSTR out) { PKG_OR_NONE(pkg_str(g_aumid, len, out)); }
static int pkg_is_self(HANDLE p) { return p == GetCurrentProcess() || p == (HANDLE)(LONG_PTR)-1 || GetProcessId(p) == GetCurrentProcessId(); }
__declspec(dllexport) LONG WINAPI GetPackageFullName(HANDLE p, UINT32 *len, PWSTR out) { if (!pkg_is_self(p)) return ERROR_NO_PACKAGE_ID; PKG_OR_NONE(pkg_str(HEAD_->full, len, out)); }
__declspec(dllexport) LONG WINAPI GetPackageFamilyName(HANDLE p, UINT32 *len, PWSTR out) { if (!pkg_is_self(p)) return ERROR_NO_PACKAGE_ID; PKG_OR_NONE(pkg_str(HEAD_->family, len, out)); }
__declspec(dllexport) LONG WINAPI GetApplicationUserModelId(HANDLE p, UINT32 *len, PWSTR out) { if (!pkg_is_self(p)) return ERROR_NO_PACKAGE_ID; PKG_OR_NONE(pkg_str(g_aumid, len, out)); }

/* PACKAGE_ID: reserved, processorArchitecture, version (UINT64: revision|build<<16|minor<<32|major<<48), name, publisher, resourceId, publisherId */
typedef struct { UINT32 reserved, arch; union { UINT64 version; USHORT p[4]; } v; PWSTR name, publisher, resid, pubid; } PKGID_T_;
static UINT32 pkg_fill_id(const PKGENT_ *e, PKGID_T_ *id, BYTE *tail) {   /* returns bytes of string data written/needed after the struct */
  const WCHAR *src[4]; PWSTR *dst[4]; int i; UINT32 off = 0;
  src[0] = e->name; src[1] = e->publisher; src[2] = e->resid; src[3] = e->pubid;
  if (id) { dst[0] = &id->name; dst[1] = &id->publisher; dst[2] = &id->resid; dst[3] = &id->pubid; id->reserved = 0; id->arch = e->arch; id->v.p[0] = e->v[0]; id->v.p[1] = e->v[1]; id->v.p[2] = e->v[2]; id->v.p[3] = e->v[3]; }
  for (i = 0; i < 4; i++) { UINT32 b = (UINT32)(wcslen(src[i]) + 1) * 2; if (id && tail) { memcpy(tail + off, src[i], b); *dst[i] = (PWSTR)(tail + off); } off += b; }
  return off;
}
__declspec(dllexport) LONG WINAPI GetCurrentPackageId(UINT32 *len, BYTE *buf) {
  UINT32 need; pkg_load(); if (!g_nent) return ERROR_NO_PACKAGE_ID; if (!len) return ERROR_INVALID_PARAMETER;
  need = (UINT32)sizeof(PKGID_T_) + pkg_fill_id(HEAD_, NULL, NULL); if (!buf || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; }
  pkg_fill_id(HEAD_, (PKGID_T_ *)buf, buf + sizeof(PKGID_T_)); *len = need; return ERROR_SUCCESS;
}
__declspec(dllexport) LONG WINAPI GetPackageId(HANDLE p, UINT32 *len, BYTE *buf) { if (!pkg_is_self(p)) return ERROR_NO_PACKAGE_ID; return GetCurrentPackageId(len, buf); }

/* PACKAGE_INFO: reserved, flags, path, packageFullName, packageFamilyName, PACKAGE_ID */
typedef struct { UINT32 reserved, flags; PWSTR path, full, family; PKGID_T_ id; } PKGINFO_T_;
static PKGENT_ *pkg_find_full(const WCHAR *full);
/* PACKAGE_INFO array of ents[0..n) */
static LONG pkg_info_of(const PKGENT_ *ents, UINT32 n, UINT32 *len, BYTE *buf, UINT32 *count) {
  UINT32 need = 0, total, i; if (!len) return ERROR_INVALID_PARAMETER;
  for (i = 0; i < n; i++) need += pkg_fill_id(&ents[i], NULL, NULL) + (UINT32)(wcslen(ents[i].path) + wcslen(ents[i].full) + wcslen(ents[i].family) + 3) * 2;
  total = (UINT32)(sizeof(PKGINFO_T_) * n) + need;
  if (count) *count = n;   /* the size pass must report the count too (MRM sizes its array from it) */
  if (!buf || *len < total) { *len = total; return ERROR_INSUFFICIENT_BUFFER; }
  { BYTE *t = buf + sizeof(PKGINFO_T_) * n; memset(buf, 0, sizeof(PKGINFO_T_) * n);
    for (i = 0; i < n; i++) { PKGINFO_T_ *pi = (PKGINFO_T_ *)buf + i; UINT32 b;
      pi->flags = ents[i].flags; t += pkg_fill_id(&ents[i], &pi->id, t);
      b = (UINT32)(wcslen(ents[i].path) + 1) * 2; pi->path = (PWSTR)t; memcpy(t, ents[i].path, b); t += b;
      b = (UINT32)(wcslen(ents[i].full) + 1) * 2; pi->full = (PWSTR)t; memcpy(t, ents[i].full, b); t += b;
      b = (UINT32)(wcslen(ents[i].family) + 1) * 2; pi->family = (PWSTR)t; memcpy(t, ents[i].family, b); t += b; } }
  *len = total; return ERROR_SUCCESS;
}
static LONG pkg_info(UINT32 flags, UINT32 *len, BYTE *buf, UINT32 *count) {
  slog("[pkg] GetCurrentPackageInfo flags=0x%x buf=%p", flags, (void *)buf); pkg_load(); if (!g_nent) { if (count) *count = 0; return ERROR_NO_PACKAGE_ID; }
  /* filters are OR-ed: HEAD alone = head package; HEAD|DIRECT|RESOURCE... = head + dependencies + resource packages */
  return pkg_info_of(g_ent, ((flags & 0xFF0) == 0x10) ? 1 : (UINT32)g_nent, len, buf, count);
}
/* package info by reference (OpenPackageInfoByFullName / GetPackageInfo / GetPackageInfo2 / ClosePackageInfo), and GetPackageFullNameFromToken.
   windows.storage resolves ms-appx: URIs with them (GetPackageInfoFromPackageFullNameAndPathType): the generic "returns 0" thunks reported
   success with no packages, the URI resolver failed (E_FAIL) and the WinUI 3 Gallery never got past loading its control list (no window). */
typedef struct { UINT32 magic; PKGENT_ *e; } PKGREF_;
#define PKGREF_MAGIC 0x52474B50u
__declspec(dllexport) LONG WINAPI OpenPackageInfoByFullName(PCWSTR full, UINT32 reserved, void **ref) {
  PKGENT_ *e; PKGREF_ *r; (void)reserved; if (!ref) return ERROR_INVALID_PARAMETER; *ref = NULL;
  e = pkg_find_full(full); slog("[pkg] OpenPackageInfoByFullName(%ls) -> %s", full ? full : L"(null)", e ? "found" : "NOT FOUND");
  if (!e) return ERROR_NOT_FOUND;
  r = (PKGREF_ *)HeapAlloc(GetProcessHeap(), 0, sizeof *r); if (!r) return ERROR_OUTOFMEMORY; r->magic = PKGREF_MAGIC; r->e = e; *ref = r; return ERROR_SUCCESS;
}
__declspec(dllexport) LONG WINAPI OpenPackageInfoByFullNameForUser(PSID sid, PCWSTR full, UINT32 reserved, void **ref) { (void)sid; return OpenPackageInfoByFullName(full, reserved, ref); }
__declspec(dllexport) LONG WINAPI ClosePackageInfo(void *ref) { PKGREF_ *r = (PKGREF_ *)ref; if (r && r->magic == PKGREF_MAGIC) { r->magic = 0; HeapFree(GetProcessHeap(), 0, r); } return ERROR_SUCCESS; }
__declspec(dllexport) LONG WINAPI GetPackageInfo(void *ref, UINT32 flags, UINT32 *len, BYTE *buf, UINT32 *count) {
  PKGREF_ *r = (PKGREF_ *)ref; if (!r || r->magic != PKGREF_MAGIC) return ERROR_INVALID_PARAMETER;
  if (r->e == HEAD_) return pkg_info(flags, len, buf, count);   /* the head package: its graph, as for the current package */
  return pkg_info_of(r->e, 1, len, buf, count);
}
__declspec(dllexport) LONG WINAPI GetPackageInfo2(void *ref, UINT32 flags, int pathType, UINT32 *len, BYTE *buf, UINT32 *count) { (void)pathType; return GetPackageInfo(ref, flags, len, buf, count); }
/* any token used in this process is taken as the process's own: its package */
__declspec(dllexport) LONG WINAPI GetPackageFullNameFromToken(HANDLE token, UINT32 *len, PWSTR out) { (void)token; PKG_OR_NONE(pkg_str(HEAD_->full, len, out)); }__declspec(dllexport) LONG WINAPI GetCurrentPackageInfo(UINT32 flags, UINT32 *len, BYTE *buf, UINT32 *count) { return pkg_info(flags, len, buf, count); }
/* GetCurrentPackageInfo2(flags, PackagePathType, &len, buf, &count): same data (we only have the install path) */
__declspec(dllexport) LONG WINAPI GetCurrentPackageInfo2(UINT32 flags, int pathType, UINT32 *len, BYTE *buf, UINT32 *count) { (void)pathType; return pkg_info(flags, len, buf, count); }
/* GetCurrentPackageInfo3(flags, PackageInfoType, &len, buf, &count): types 0x10 / 0x11 = package-graph generation counter, a single UINT32 */
__declspec(dllexport) LONG WINAPI GetCurrentPackageInfo3(UINT32 flags, UINT32 type, UINT32 *len, BYTE *buf, UINT32 *count) {
  pkg_load(); if (!g_nent) { if (count) *count = 0; return ERROR_NO_PACKAGE_ID; } if (!len) return ERROR_INVALID_PARAMETER;
  if (type == 0x10 || type == 0x11) { if (count) *count = 1; if (!buf || *len < 4) { *len = 4; return ERROR_INSUFFICIENT_BUFFER; } *(UINT32 *)buf = 1; *len = 4; return ERROR_SUCCESS; }
  slog("[pkg] GetCurrentPackageInfo3 type 0x%x not implemented", type); return pkg_info(flags, len, buf, count);
}

/* path / id lookups by full name: head, graph, then the store catalog */
static PKGENT_ *pkg_find_full(const WCHAR *full) {
  int i; pkg_load(); if (!full) return NULL;
  for (i = 0; i < g_nent; i++) if (!_wcsicmp(g_ent[i].full, full)) return &g_ent[i];
  for (i = 0; i < g_npcat; i++) if (!_wcsicmp(g_pcat[i].full, full)) return &g_pcat[i];
  return NULL;
}
__declspec(dllexport) LONG WINAPI GetPackagePathByFullName(PCWSTR full, UINT32 *len, PWSTR path) { PKGENT_ *e = pkg_find_full(full); slog("[pkg] GetPackagePathByFullName(%ls) -> %s", full ? full : L"(null)", e ? "found" : "NOT FOUND"); if (!e) return ERROR_NOT_FOUND; return pkg_str(e->path, len, path); }
__declspec(dllexport) LONG WINAPI GetPackagePathByFullName2(PCWSTR full, int type, UINT32 *len, PWSTR path) { (void)type; return GetPackagePathByFullName(full, len, path); }
__declspec(dllexport) LONG WINAPI GetStagedPackagePathByFullName(PCWSTR full, UINT32 *len, PWSTR path) { return GetPackagePathByFullName(full, len, path); }
__declspec(dllexport) LONG WINAPI GetStagedPackagePathByFullName2(PCWSTR full, int type, UINT32 *len, PWSTR path) { (void)type; return GetPackagePathByFullName(full, len, path); }
__declspec(dllexport) LONG WINAPI PackageFamilyNameFromFullName(PCWSTR full, UINT32 *len, PWSTR out) {
  /* works from the name alone: Name_Version_Arch_ResourceId_PublisherId -> Name_PublisherId */
  WCHAR fam[160], nm[128]; const WCHAR *u1, *last; size_t n; if (!full) return ERROR_INVALID_PARAMETER;
  u1 = wcschr(full, L'_'); last = wcsrchr(full, L'_'); if (!u1 || !last || u1 == last) return ERROR_INVALID_PARAMETER;
  n = (size_t)(u1 - full); if (n > 127) return ERROR_INVALID_PARAMETER; memcpy(nm, full, n * 2); nm[n] = 0; _snwprintf(fam, 160, L"%ls%ls", nm, last); return pkg_str(fam, len, out);
}
/* PackageNameAndPublisherIdFromFamilyName: "Name_PublisherId" -> packageName + packagePublisherId (two sized out buffers, chars incl. NUL).
   The GDK runtime (xgameruntime) probes for and calls it; the publisher id is just the tail after the last '_'. */
__declspec(dllexport) LONG WINAPI PackageNameAndPublisherIdFromFamilyName(PCWSTR family, UINT32 *nameLen, PWSTR name, UINT32 *pubLen, PWSTR pub) {
  const WCHAR *u; UINT32 nl, pl; int fits;
  if (!family || !nameLen || !pubLen) return ERROR_INVALID_PARAMETER;
  u = wcsrchr(family, L'_'); if (!u || u == family || !u[1]) return ERROR_INVALID_PARAMETER;
  nl = (UINT32)(u - family) + 1; pl = (UINT32)wcslen(u + 1) + 1;
  fits = name && pub && *nameLen >= nl && *pubLen >= pl;
  if (fits) { memcpy(name, family, (nl - 1) * sizeof(WCHAR)); name[nl - 1] = 0; wcscpy(pub, u + 1); }
  *nameLen = nl; *pubLen = pl;
  return fits ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER;
}
__declspec(dllexport) LONG WINAPI PackageIdFromFullName(PCWSTR full, UINT32 flags, UINT32 *len, BYTE *buf) {
  PKGENT_ *e = pkg_find_full(full), tmp; UINT32 need; (void)flags;
  if (!e) {   /* unknown package: split the name, publisher unknown */
    WCHAR f[300], *tok[5], *c; int k = 0; if (!full || wcslen(full) > 299) return ERROR_INVALID_PARAMETER; wcscpy(f, full);
    for (c = f, tok[k++] = f; *c && k < 5; c++) if (*c == L'_') { *c = 0; tok[k++] = c + 1; } if (k != 5) return ERROR_INVALID_PARAMETER;
    memset(&tmp, 0, sizeof tmp); wcsncpy(tmp.name, tok[0], 127); wcsncpy(tmp.resid, tok[3], 63); wcsncpy(tmp.pubid, tok[4], 15); wcscpy(tmp.publisher, L"CN=Unknown");
    { int a, b, cc, d; swscanf(tok[1], L"%d.%d.%d.%d", &a, &b, &cc, &d); tmp.v[3] = (USHORT)a; tmp.v[2] = (USHORT)b; tmp.v[1] = (USHORT)cc; tmp.v[0] = (USHORT)d; }
    tmp.arch = !_wcsicmp(tok[2], L"x86") ? 0 : !_wcsicmp(tok[2], L"x64") ? 9 : !_wcsicmp(tok[2], L"arm") ? 5 : !_wcsicmp(tok[2], L"arm64") ? 12 : 11; e = &tmp; }
  if (!len) return ERROR_INVALID_PARAMETER; need = (UINT32)sizeof(PKGID_T_) + pkg_fill_id(e, NULL, NULL);
  if (!buf || *len < need) { *len = need; return ERROR_INSUFFICIENT_BUFFER; } pkg_fill_id(e, (PKGID_T_ *)buf, buf + sizeof(PKGID_T_)); *len = need; return ERROR_SUCCESS;
}
__declspec(dllexport) LONG WINAPI GetPackagePath(const PKGID_T_ *id, UINT32 reserved, UINT32 *len, PWSTR path) {
  int i; (void)reserved; pkg_load(); if (!id || !id->name) return ERROR_INVALID_PARAMETER;
  for (i = 0; i < g_npcat + g_nent; i++) { PKGENT_ *e = i < g_nent ? &g_ent[i] : &g_pcat[i - g_nent];
    if (!_wcsicmp(e->name, id->name) && e->v[0] == id->v.p[0] && e->v[1] == id->v.p[1] && e->v[2] == id->v.p[2] && e->v[3] == id->v.p[3] && !_wcsicmp(e->resid, id->resid ? id->resid : L"")) return pkg_str(e->path, len, path); }
  return ERROR_NOT_FOUND;
}
__declspec(dllexport) LONG WINAPI FindPackagesByPackageFamily(PCWSTR family, UINT32 flags, UINT32 *count, PWSTR *fulls, UINT32 *bufLen, PWSTR buf, UINT32 *props) {
  int i, n = 0; UINT32 need = 0; (void)flags; pkg_load(); if (!family || !count || !bufLen) return ERROR_INVALID_PARAMETER;
  for (i = 0; i < g_npcat; i++) if (!_wcsicmp(g_pcat[i].family, family)) { n++; need += (UINT32)wcslen(g_pcat[i].full) + 1; }
  if (!n) { *count = 0; *bufLen = 0; return ERROR_SUCCESS; }
  if (!fulls || !buf || *count < (UINT32)n || *bufLen < need) { *count = n; *bufLen = need; return ERROR_INSUFFICIENT_BUFFER; }
  { UINT32 off = 0; int k = 0; for (i = 0; i < g_npcat; i++) if (!_wcsicmp(g_pcat[i].family, family)) { UINT32 l = (UINT32)wcslen(g_pcat[i].full) + 1; memcpy(buf + off, g_pcat[i].full, l * 2); fulls[k] = buf + off; if (props) props[k] = g_pcat[i].flags; k++; off += l; } *count = n; *bufLen = need; }
  return ERROR_SUCCESS;
}
__declspec(dllexport) LONG WINAPI GetPackagesByPackageFamily(PCWSTR family, UINT32 *count, PWSTR *fulls, UINT32 *bufLen, PWSTR buf) { return FindPackagesByPackageFamily(family, 0, count, fulls, bufLen, buf, NULL); }

/* the PRI "context" APIs take the output as the THIRD argument (reserved, reserved2, ctx); an opaque non-NULL handle is enough for now */
static LONG pkg_ctx(const char *nm, void **ctx) { pkg_load(); if (!g_nent) return ERROR_NO_PACKAGE_ID; if (!ctx) return ERROR_INVALID_PARAMETER; *ctx = (void *)HEAD_; slog("[pkg] %s -> context %p", nm, *ctx); return ERROR_SUCCESS; }
__declspec(dllexport) LONG WINAPI GetCurrentPackageResourcesContext(void *a, void *b, void **ctx) { (void)a; (void)b; return pkg_ctx("GetCurrentPackageResourcesContext", ctx); }
__declspec(dllexport) LONG WINAPI GetCurrentPackageApplicationResourcesContext(void *a, void *b, void **ctx) { (void)a; (void)b; return pkg_ctx("GetCurrentPackageApplicationResourcesContext", ctx); }
__declspec(dllexport) LONG WINAPI GetCurrentPackageGlobalizationContext(void *a, void *b, void **ctx) { (void)a; (void)b; return pkg_ctx("GetCurrentPackageGlobalizationContext", ctx); }
__declspec(dllexport) LONG WINAPI GetCurrentPackageContext(void *a, void *b, void **ctx) { (void)a; (void)b; return pkg_ctx("GetCurrentPackageContext", ctx); }
__declspec(dllexport) LONG WINAPI GetCurrentPackageApplicationContext(void *a, void *b, void **ctx) { (void)a; (void)b; return pkg_ctx("GetCurrentPackageApplicationContext", ctx); }

/* kernelbase: AppXGetOSMaxVersionTested(?, UINT64 *version): callers pre-set the result to 10.0.0.0 and compare it with the legacy 6.x values */
__declspec(dllexport) HRESULT WINAPI AppXGetOSMaxVersionTested(void *id, UINT64 *ver) { (void)id; (void)ver; return S_OK; }

/* ntdll: RtlQueryPackageClaims(token, fullName, &fullNameBytes, appId, &appIdBytes, dynamicId, PS_PKG_CLAIM{ULONG Flags; ULONG Origin}*)
   Flags bit0 = packaged application.  The XAML engine fail-fasts without it. */
__declspec(dllexport) NTSTATUS NTAPI RtlQueryPackageClaims(PVOID token, PWSTR full, SIZE_T *fullBytes, PWSTR appid, SIZE_T *appBytes, GUID *dyn, ULONG *claim) {
  (void)token; pkg_load(); if (!g_nent) return (NTSTATUS)0xC0000225;
  if (fullBytes) { SIZE_T need = (wcslen(HEAD_->full) + 1) * 2; if (!full || *fullBytes < need) { *fullBytes = need; if (full) return (NTSTATUS)0xC0000023; } else { memcpy(full, HEAD_->full, need); *fullBytes = need; } }
  if (appBytes) { SIZE_T need = (wcslen(g_appid) + 1) * 2; if (!appid || *appBytes < need) { *appBytes = need; if (appid) return (NTSTATUS)0xC0000023; } else { memcpy(appid, g_appid, need); *appBytes = need; } }
  if (dyn) memset(dyn, 0, sizeof *dyn);
  if (claim) { claim[0] = 0x11; claim[1] = 3; }
  return STATUS_SUCCESS;
}


/* appmodel-state: per-package state folders (LocalState / RoamingState / TempState) under %LOCALAPPDATA%\Packages\<family>. XAML's ApplicationDataProvider asks for them
   when it builds the resource manager. OpenState() returns a non-null cookie, GetStateFolder(state, kind 1|2|3, buf, *cch) returns TRUE and creates the folder. */
__declspec(dllexport) HANDLE WINAPI OpenState(void) { pkg_load(); if (!g_nent) { SetLastError(ERROR_NO_PACKAGE_ID); return NULL; } return (HANDLE)(ULONG_PTR)0x57A7E; }
__declspec(dllexport) BOOL WINAPI CloseState(HANDLE h) { (void)h; return TRUE; }
/* OpenStateExplicit(name) / OpenStateExplicitForUserSidString(a, b) (one is the package family, one the user SID): we have one package and one user */
/* the first argument is not always a string: Win10 windows.storage passes a token handle (0x95C) - logged as raw values, never dereferenced */
__declspec(dllexport) HANDLE WINAPI OpenStateExplicit(const void *a, const void *b) { slog("[state] OpenStateExplicit(%p, %p)", a, b); return OpenState(); }
__declspec(dllexport) HANDLE WINAPI OpenStateExplicitForUserSidString(const void *a, const void *b) { slog("[state] OpenStateExplicitForUserSidString(%p, %p)", a, b); return OpenState(); }

/* ---- appmodel state containers (ApplicationData.LocalSettings & co). Win10 keeps them in the package's settings.dat hive;
   here a key per locality under HKCU\Software\wxr\ApplicationData\<family>. Signatures from Windows.Storage.ApplicationData's call sites:
     HCONTAINER CreateStateContainer(HSTATE, ULONG locality 1..4, PCWSTR name (NULL = root), ULONG disposition) - roots always exist on Win10: opened or created
     HCONTAINER CreateStateSubcontainer(HCONTAINER, PCWSTR name, ULONG disposition): 1 = open or create (ApplicationDataCreateDisposition.Always,`r`n       Clock's CreateContainer), 2 = open existing (.Existing; missing: ERROR_OBJECT_NOT_FOUND)
     BOOL ReadStateContainerValue(HCONTAINER, PCWSTR, ULONG *type, void *buf, ULONG *cb)   missing: ERROR_OBJECT_NOT_FOUND, short: ERROR_INSUFFICIENT_BUFFER
     BOOL WriteStateContainerValue(HCONTAINER, PCWSTR, ULONG type, const void *data, ULONG cb, ...)
   Values keep ApplicationData's own type number and bytes, so they round-trip unchanged. */
typedef struct { DWORD magic; HKEY key; BOOL created; ULONG depth; } StateCont;
#define STATE_MAGIC 0x53434E54u
static StateCont *sc_of(HANDLE h) { StateCont *c = (StateCont *)h; if (!c || IsBadReadPtr(c, sizeof *c) || c->magic != STATE_MAGIC) return NULL; return c; }
static HANDLE sc_open(HKEY parent, const WCHAR *sub, ULONG disp, ULONG depth) {
  HKEY k = NULL; DWORD d = 0; LONG r; StateCont *c;
  if (disp == 2 && depth > 0) { r = RegOpenKeyExW(parent, sub, 0, KEY_ALL_ACCESS, &k); d = REG_OPENED_EXISTING_KEY; }
  else r = RegCreateKeyExW(parent, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &d);
  if (r) { SetLastError(r == ERROR_FILE_NOT_FOUND ? ERROR_OBJECT_NOT_FOUND : (DWORD)r); return NULL; }
  c = (StateCont *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *c); if (!c) { RegCloseKey(k); SetLastError(ERROR_OUTOFMEMORY); return NULL; }
  c->magic = STATE_MAGIC; c->key = k; c->created = d == REG_CREATED_NEW_KEY; c->depth = depth; return c; }
__declspec(dllexport) HANDLE WINAPI CreateStateContainer(HANDLE state, ULONG loc, const WCHAR *name, ULONG disp) {
  WCHAR p[400]; HANDLE c; (void)state; pkg_load(); if (!g_nent) { SetLastError(ERROR_NO_PACKAGE_ID); return NULL; }
  _snwprintf(p, 400, L"Software\\wxr\\ApplicationData\\%ls\\Locality%lu%ls%ls", g_ent[0].family, loc, name && *name ? L"\\" : L"", name ? name : L"");
  c = sc_open(HKEY_CURRENT_USER, p, 1, name && *name ? 1 : 0); slog("[state] CreateStateContainer(loc %lu, %ls, disp %lu) -> %p", loc, name ? name : L"(root)", disp, c); return c; }
__declspec(dllexport) HANDLE WINAPI CreateStateSubcontainer(HANDLE h, const WCHAR *name, ULONG disp) {
  StateCont *c = sc_of(h); HANDLE s; if (!c || !name) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
  s = sc_open(c->key, name, disp, c->depth + 1); slog("[state] CreateStateSubcontainer(%ls, disp %lu) -> %p", name, disp, s); return s; }
__declspec(dllexport) BOOL WINAPI CloseStateContainer(HANDLE h) { StateCont *c = sc_of(h); if (!c) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; } RegCloseKey(c->key); c->magic = 0; HeapFree(GetProcessHeap(), 0, c); return TRUE; }
__declspec(dllexport) HANDLE WINAPI DuplicateStateContainerHandle(HANDLE h) {
  StateCont *c = sc_of(h); HKEY k; StateCont *d; if (!c || DuplicateHandle(GetCurrentProcess(), (HANDLE)c->key, GetCurrentProcess(), (HANDLE *)&k, 0, FALSE, DUPLICATE_SAME_ACCESS) == 0) { SetLastError(ERROR_INVALID_HANDLE); return NULL; }
  d = (StateCont *)HeapAlloc(GetProcessHeap(), 0, sizeof *d); if (!d) { RegCloseKey(k); return NULL; } *d = *c; d->key = k; return d; }
__declspec(dllexport) BOOL WINAPI QueryStateContainerCreatedNew(HANDLE h, BOOL *created) { StateCont *c = sc_of(h); if (!c || !created) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *created = c->created; return TRUE; }
__declspec(dllexport) BOOL WINAPI GetStateContainerDepth(HANDLE h, ULONG *depth) { StateCont *c = sc_of(h); if (!c || !depth) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *depth = c->depth; return TRUE; }
__declspec(dllexport) BOOL WINAPI ReadStateContainerValue(HANDLE h, const WCHAR *name, ULONG *type, void *buf, ULONG *cb) {
  StateCont *c = sc_of(h); DWORD t = 0, n; LONG r; if (!c || !cb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  n = *cb; r = RegQueryValueExW(c->key, name, NULL, &t, (BYTE *)buf, &n);
  if (r == ERROR_FILE_NOT_FOUND) { SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; }
  if (type) *type = t; *cb = n;
  if (r == ERROR_MORE_DATA || (r == ERROR_SUCCESS && !buf && n)) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  if (r) { SetLastError((DWORD)r); return FALSE; }
  return TRUE; }
__declspec(dllexport) BOOL WINAPI WriteStateContainerValue(HANDLE h, const WCHAR *name, ULONG type, const void *data, ULONG cb) {
  StateCont *c = sc_of(h); LONG r; if (!c) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  r = RegSetValueExW(c->key, name, 0, type, (const BYTE *)data, cb); slog("[state] WriteStateContainerValue(%ls, type %lu, %lu bytes) -> %ld", name ? name : L"", type, cb, r);
  if (r) { SetLastError((DWORD)r); return FALSE; } return TRUE; }
__declspec(dllexport) BOOL WINAPI DeleteStateContainerValue(HANDLE h, const WCHAR *name) {
  StateCont *c = sc_of(h); LONG r; if (!c) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  r = RegDeleteValueW(c->key, name); if (r == ERROR_FILE_NOT_FOUND) { SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; } if (r) { SetLastError((DWORD)r); return FALSE; } return TRUE; }
/* item info: callers only test whether the value (or subcontainer) exists */
__declspec(dllexport) BOOL WINAPI QueryStateContainerItemInfo(HANDLE h, const WCHAR *name, void *info) {
  StateCont *c = sc_of(h); HKEY k; (void)info; if (!c) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (RegQueryValueExW(c->key, name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) return TRUE;
  if (RegOpenKeyExW(c->key, name, 0, KEY_READ, &k) == ERROR_SUCCESS) { RegCloseKey(k); return TRUE; }
  SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; }
/* EnumerateStateContainerItems(HCONTAINER, ULONG type, void *buf, ULONG *size) (Windows.Storage.ApplicationData StateABIHelpers::
   EnumerateApiSetItems): fills buf with a linked list of entries { ULONG_PTR, PCWSTR name, entry *next } (strings in the same buffer);
   a short buffer -> ERROR_INSUFFICIENT_BUFFER with *size = needed, nothing -> ERROR_NO_MORE_ITEMS (an "empty" TRUE left the caller
   walking an uninitialised buffer: Clock's ApplicationDataContainer.Containers crashed). type 1 = subcontainers; type 2 = values, whose
   entry layout is not known yet: reported as none (single values are still read and written by name). */
typedef struct StateItem { ULONG_PTR kind; const WCHAR *name; struct StateItem *next; } StateItem;
__declspec(dllexport) BOOL WINAPI EnumerateStateContainerItems(HANDLE h, ULONG type, void *buf, ULONG *size) {
  StateCont *c = sc_of(h); WCHAR nm[256]; DWORD i, n, cnt = 0, need = 0; StateItem *e, *prev = NULL; WCHAR *str;
  if (!c || !size) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (type != 1) { slog("[state] EnumerateStateContainerItems(type %lu): values not enumerated", type); SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
  for (i = 0; n = 256, RegEnumKeyExW(c->key, i, nm, &n, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; i++) { cnt++; need += sizeof(StateItem) + (n + 1) * sizeof(WCHAR); }
  if (!cnt) { SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
  if (!buf || *size < need) { *size = need; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  e = (StateItem *)buf; str = (WCHAR *)((BYTE *)buf + cnt * sizeof(StateItem));
  for (i = 0; i < cnt; i++) {
    n = 256; if (RegEnumKeyExW(c->key, i, nm, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
    memcpy(str, nm, (n + 1) * sizeof(WCHAR)); e[i].kind = 0; e[i].name = str; e[i].next = NULL; if (prev) prev->next = &e[i]; prev = &e[i]; str += n + 1;
  }
  slog("[state] EnumerateStateContainerItems: %lu subcontainers", cnt); return TRUE; }
/* ---- state atoms: ApplicationDataCompositeValue (Clock's settings). Signatures from Windows.Storage.ApplicationData's call sites:
     HATOM CreateStateAtom(void)                       HATOM OpenStateAtom(HCONTAINER, PCWSTR name)   BOOL CloseStateAtom(HATOM)
     BOOL CommitStateAtom(HCONTAINER, PCWSTR name, HATOM)   BOOL GetSerializedAtomBytes(HATOM, void *buf (NULL: size only), ULONG *cb)
     BOOL Write/Read/DeleteStateAtomValue, QueryStateAtomValueInfo: as the StateContainer ones, on the atom
   An atom is an in-memory list of typed values; stored in its container as one registry value of type 13 (PropertyType.Inspectable, the
   type ApplicationData gives composites; settings keep their PropertyType number) holding our own serialisation:
   "U7AT", then per value: u32 name length (chars), name, u32 type, u32 size, bytes. */
typedef struct { WCHAR name[256]; ULONG type, cb; BYTE *data; } AtomVal;
typedef struct { DWORD magic; int n, cap; AtomVal *v; } StateAtom;
#define ATOM_MAGIC 0x41544F4Du
#define ATOM_REG_TYPE 13
static StateAtom *atom_of(HANDLE h) { StateAtom *a = (StateAtom *)h; if (!a || IsBadReadPtr(a, sizeof *a) || a->magic != ATOM_MAGIC) return NULL; return a; }
static AtomVal *atom_find(StateAtom *a, const WCHAR *name) { int i; for (i = 0; i < a->n; i++) if (!lstrcmpiW(a->v[i].name, name)) return &a->v[i]; return NULL; }
static BOOL atom_put(StateAtom *a, const WCHAR *name, ULONG type, const void *data, ULONG cb) {
  AtomVal *v = atom_find(a, name); BYTE *d;
  if (!name || lstrlenW(name) >= 256) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (!v) { if (a->n == a->cap) { int nc = a->cap ? a->cap * 2 : 8; AtomVal *x = (AtomVal *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, nc * sizeof *x); if (!x) return FALSE;
      if (a->v) { memcpy(x, a->v, a->n * sizeof *x); HeapFree(GetProcessHeap(), 0, a->v); } a->v = x; a->cap = nc; }
    v = &a->v[a->n++]; lstrcpyW(v->name, name); v->data = NULL; }
  d = (BYTE *)HeapAlloc(GetProcessHeap(), 0, cb ? cb : 1); if (!d) return FALSE; if (cb) memcpy(d, data, cb);
  if (v->data) HeapFree(GetProcessHeap(), 0, v->data); v->data = d; v->type = type; v->cb = cb; return TRUE; }
static ULONG atom_size(StateAtom *a) { ULONG s = 4; int i; for (i = 0; i < a->n; i++) s += 12 + lstrlenW(a->v[i].name) * sizeof(WCHAR) + a->v[i].cb; return s; }
static void atom_write(StateAtom *a, BYTE *p) { int i; memcpy(p, "U7AT", 4); p += 4;
  for (i = 0; i < a->n; i++) { ULONG l = lstrlenW(a->v[i].name); memcpy(p, &l, 4); memcpy(p + 4, a->v[i].name, l * sizeof(WCHAR)); p += 4 + l * sizeof(WCHAR);
    memcpy(p, &a->v[i].type, 4); memcpy(p + 4, &a->v[i].cb, 4); memcpy(p + 8, a->v[i].data, a->v[i].cb); p += 8 + a->v[i].cb; } }
__declspec(dllexport) HANDLE WINAPI CreateStateAtom(void) { StateAtom *a = (StateAtom *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a); if (a) a->magic = ATOM_MAGIC; return a; }
__declspec(dllexport) BOOL WINAPI CloseStateAtom(HANDLE h) { StateAtom *a = atom_of(h); int i; if (!a) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
  for (i = 0; i < a->n; i++) HeapFree(GetProcessHeap(), 0, a->v[i].data); if (a->v) HeapFree(GetProcessHeap(), 0, a->v); a->magic = 0; HeapFree(GetProcessHeap(), 0, a); return TRUE; }
__declspec(dllexport) HANDLE WINAPI OpenStateAtom(HANDLE container, const WCHAR *name) {
  StateCont *c = sc_of(container); DWORD t = 0, n = 0; BYTE *b, *p, *e; StateAtom *a;
  if (!c || !name) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
  if (RegQueryValueExW(c->key, name, NULL, &t, NULL, &n) != ERROR_SUCCESS) { SetLastError(ERROR_OBJECT_NOT_FOUND); return NULL; }
  if (!(a = (StateAtom *)CreateStateAtom())) return NULL;
  if (n >= 4 && (b = (BYTE *)HeapAlloc(GetProcessHeap(), 0, n)) != NULL) {
    if (RegQueryValueExW(c->key, name, NULL, &t, b, &n) == ERROR_SUCCESS && !memcmp(b, "U7AT", 4))
      for (p = b + 4, e = b + n; p + 4 <= e; ) { ULONG l, ty, cb; WCHAR nm[256];
        memcpy(&l, p, 4); if (l >= 256 || p + 4 + l * sizeof(WCHAR) + 8 > e) break; memcpy(nm, p + 4, l * sizeof(WCHAR)); nm[l] = 0; p += 4 + l * sizeof(WCHAR);
        memcpy(&ty, p, 4); memcpy(&cb, p + 4, 4); p += 8; if (p + cb > e) break; atom_put(a, nm, ty, p, cb); p += cb; }
    HeapFree(GetProcessHeap(), 0, b); }
  slog("[state] OpenStateAtom(%ls) -> %d values", name, a->n); return a; }
__declspec(dllexport) BOOL WINAPI CommitStateAtom(HANDLE container, const WCHAR *name, HANDLE h) {
  StateCont *c = sc_of(container); StateAtom *a = atom_of(h); ULONG n; BYTE *b; LONG r;
  if (!c || !a || !name) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  n = atom_size(a); if (!(b = (BYTE *)HeapAlloc(GetProcessHeap(), 0, n))) { SetLastError(ERROR_OUTOFMEMORY); return FALSE; }
  atom_write(a, b); r = RegSetValueExW(c->key, name, 0, ATOM_REG_TYPE, b, n); HeapFree(GetProcessHeap(), 0, b);
  slog("[state] CommitStateAtom(%ls, %d values) -> %ld", name, a->n, r); if (r) { SetLastError((DWORD)r); return FALSE; } return TRUE; }
__declspec(dllexport) BOOL WINAPI GetSerializedAtomBytes(HANDLE h, void *buf, ULONG *cb) {
  StateAtom *a = atom_of(h); ULONG n; if (!a || !cb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  /* size query: TRUE only for an empty atom (size 0), otherwise ERROR_INSUFFICIENT_BUFFER (Serialize treats TRUE with a size as failure) */
  n = atom_size(a); if (!buf || *cb < n) { *cb = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  atom_write(a, (BYTE *)buf); *cb = n; return TRUE; }
__declspec(dllexport) BOOL WINAPI WriteStateAtomValue(HANDLE h, const WCHAR *name, ULONG type, const void *data, ULONG cb) {
  StateAtom *a = atom_of(h); if (!a) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; } return atom_put(a, name, type, data, cb); }
__declspec(dllexport) BOOL WINAPI ReadStateAtomValue(HANDLE h, const WCHAR *name, ULONG *type, void *buf, ULONG *cb) {
  StateAtom *a = atom_of(h); AtomVal *v; ULONG n; if (!a || !cb) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (!(v = atom_find(a, name))) { SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; }
  if (type) *type = v->type; n = *cb; *cb = v->cb;
  if (!buf || n < v->cb) { if (!v->cb && buf) return TRUE; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  memcpy(buf, v->data, v->cb); return TRUE; }
__declspec(dllexport) BOOL WINAPI QueryStateAtomValueInfo(HANDLE h, const WCHAR *name, void *info) {
  StateAtom *a = atom_of(h); (void)info; if (!a) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
  if (!atom_find(a, name)) { SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; } return TRUE; }
__declspec(dllexport) BOOL WINAPI DeleteStateAtomValue(HANDLE h, const WCHAR *name) {
  StateAtom *a = atom_of(h); AtomVal *v; if (!a) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
  if (!(v = atom_find(a, name))) { SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; }
  HeapFree(GetProcessHeap(), 0, v->data); *v = a->v[--a->n]; return TRUE; }
/* value enumeration (composite iteration): entry layout not known yet, reported as none, like a container's values */
__declspec(dllexport) BOOL WINAPI EnumerateStateAtomValues(HANDLE h, void *buf, ULONG *cb) { (void)h; (void)buf; (void)cb; slog("[state] EnumerateStateAtomValues: not enumerated"); SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }
/* roaming data never syncs here (ApplicationData.SetVersionAsync asks when roaming data last changed and re-stamps a time range) */
__declspec(dllexport) BOOL WINAPI GetRoamingLastObservedChangeTime(HANDLE state, FILETIME *t) { (void)state; if (t) { t->dwLowDateTime = t->dwHighDateTime = 0; } return TRUE; }
__declspec(dllexport) BOOL WINAPI OverrideRoamingDataModificationTimesInRange(HANDLE state, FILETIME *a, FILETIME *b, FILETIME *c) { (void)state; (void)a; (void)b; (void)c; return TRUE; }
/* DeleteStateContainer(HCONTAINER parent, PCWSTR name) (ApplicationDataContainer.DeleteContainer) */
__declspec(dllexport) BOOL WINAPI DeleteStateContainer(HANDLE h, const WCHAR *name) {
  StateCont *c = sc_of(h); LONG r; if (!c || !name) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  { typedef LONG (WINAPI *PDK)(HKEY, LPCWSTR); static PDK dk; if (!dk) { HMODULE m = LoadLibraryW(L"shlwapi.dll"); if (m) dk = (PDK)GetProcAddress(m, "SHDeleteKeyW"); }
    r = dk ? dk(c->key, name) : ERROR_PROC_NOT_FOUND; }
  if (r == ERROR_FILE_NOT_FOUND) { SetLastError(ERROR_OBJECT_NOT_FOUND); return FALSE; } if (r) { SetLastError((DWORD)r); return FALSE; } return TRUE; }
/* state locks (ApplicationData.SetVersionAsync: CreateStateLock(state) -> lock, AcquireStateLock(lock, 0, timeout ms), ReleaseStateLock,
   CloseStateLock; BOOL results). Win10 serialises settings migrations across the package's processes; one process here, a mutex is enough */
__declspec(dllexport) HANDLE WINAPI CreateStateLock(HANDLE state) { HANDLE m; (void)state; m = CreateMutexW(NULL, FALSE, NULL); slog("[state] CreateStateLock -> %p", m); return m; }
__declspec(dllexport) BOOL WINAPI AcquireStateLock(HANDLE lock, ULONG flags, ULONG ms) {
  DWORD r; (void)flags; r = WaitForSingleObject(lock, ms); if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) return TRUE;
  SetLastError(r == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError()); return FALSE; }
__declspec(dllexport) BOOL WINAPI ReleaseStateLock(HANDLE lock) { return ReleaseMutex(lock); }
__declspec(dllexport) BOOL WINAPI CloseStateLock(HANDLE lock) { return CloseHandle(lock); }
__declspec(dllexport) BOOL WINAPI GetStateVersion(HANDLE h, ULONG *ver, void *r) { (void)h; (void)r; if (ver) *ver = 0; return TRUE; }
__declspec(dllexport) BOOL WINAPI SetStateVersion(HANDLE h, ULONG ver, void *r) { (void)h; (void)r; slog("[state] SetStateVersion(%lu)", ver); return TRUE; }
/* change notifications: one process, one writer; nothing is ever signalled */
static int g_state_notif;
__declspec(dllexport) HANDLE WINAPI CreateStateChangeNotification(HANDLE state, ULONG a, ULONG b) { (void)state; (void)a; (void)b; return &g_state_notif; }
__declspec(dllexport) BOOL WINAPI SubscribeStateChangeNotification(HANDLE n, void *cb, void *ctx) { (void)n; (void)cb; (void)ctx; return TRUE; }
__declspec(dllexport) BOOL WINAPI UnsubscribeStateChangeNotification(HANDLE n) { (void)n; return TRUE; }
__declspec(dllexport) BOOL WINAPI CloseStateChangeNotification(HANDLE n) { (void)n; return TRUE; }
__declspec(dllexport) BOOL WINAPI PublishStateChangeNotification(HANDLE n) { (void)n; return TRUE; }
__declspec(dllexport) BOOL WINAPI SharedLocalIsEnabled(void) { return FALSE; }
__declspec(dllexport) BOOL WINAPI GetStateFolder(HANDLE h, int kind, WCHAR *buf, DWORD *cch) {
  /* kind 4 = LocalCache (ApplicationData.LocalCacheFolder; Media Player asks for it at startup) */
  static const WCHAR *const nm[] = { L"", L"LocalState", L"RoamingState", L"TempState", L"LocalCache" }; WCHAR base[MAX_PATH], p[MAX_PATH]; DWORD n;
  (void)h; if (kind < 1 || kind > 4 || !cch) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) { SetLastError(ERROR_PATH_NOT_FOUND); return FALSE; }
  _snwprintf(p, MAX_PATH, L"%ls\\Packages", base); CreateDirectoryW(p, NULL);
  _snwprintf(p, MAX_PATH, L"%ls\\Packages\\%ls", base, g_ent[0].family); CreateDirectoryW(p, NULL);
  _snwprintf(p, MAX_PATH, L"%ls\\Packages\\%ls\\%ls", base, g_ent[0].family, nm[kind]); CreateDirectoryW(p, NULL);
  n = (DWORD)wcslen(p) + 1; if (!buf || *cch < n) { *cch = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  memcpy(buf, p, n * 2); *cch = n; slog("[impl] GetStateFolder(%d) -> %ls", kind, p); return TRUE; }

/* GetPublisherCacheFolder(state, folderName, buf, &cch): ApplicationData.GetPublisherCacheFolder - a folder shared by all apps of one
   publisher, %LOCALAPPDATA%\Publishers\<publisher id>\<folderName> (publisher id = the family name's suffix). As a logging thunk it
   returned success with no path and the Store's launch hit a NullReferenceException. The manifest's publisherCacheFolders list is not
   checked. */
__declspec(dllexport) BOOL WINAPI GetPublisherCacheFolder(HANDLE h, const WCHAR *name, WCHAR *buf, DWORD *cch) {
  WCHAR base[MAX_PATH], p[MAX_PATH]; const WCHAR *pub; DWORD n; (void)h;
  if (!name || !*name || wcschr(name, L'\\') || wcschr(name, L'/') || !cch) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  pkg_load(); if (!g_nent || !(pub = wcsrchr(g_ent[0].family, L'_'))) { SetLastError(ERROR_NOT_FOUND); return FALSE; } pub++;
  if (!GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) { SetLastError(ERROR_PATH_NOT_FOUND); return FALSE; }
  _snwprintf(p, MAX_PATH, L"%ls\\Publishers", base); CreateDirectoryW(p, NULL);
  _snwprintf(p, MAX_PATH, L"%ls\\Publishers\\%ls", base, pub); CreateDirectoryW(p, NULL);
  _snwprintf(p, MAX_PATH, L"%ls\\Publishers\\%ls\\%ls", base, pub, name); CreateDirectoryW(p, NULL);
  n = (DWORD)wcslen(p) + 1; if (!buf || *cch < n) { *cch = n; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  memcpy(buf, p, n * 2); *cch = n; slog("[impl] GetPublisherCacheFolder(%ls) -> %ls", name, p); return TRUE; }

/* shcore #270: ( ISharedBuffer-like COM object whose slot 3 yields the data pointer, size, IStream **). MrmCoreR hands embedded resource data (App.xbf) to XAML this way. A copy in a memory stream is good enough. */
typedef IStream *(WINAPI *PSHCMS)(const BYTE *, UINT);
__declspec(dllexport) HRESULT WINAPI SHCreateMemoryStreamOnSharedBuffer(void *buf, DWORD size, IStream **out) {
  static PSHCMS f; void *p = NULL; HRESULT hr; if (out) *out = NULL; if (!buf || !out) return E_INVALIDARG;
  if (!f) f = (PSHCMS)GetProcAddress(LoadLibraryA("shlwapi.dll"), "SHCreateMemStream");
  hr = ((HRESULT (WINAPI *)(void *, void **))(*(void ***)buf)[3])(buf, &p);
  if (FAILED(hr) || !p || !f) { slog("[impl] SHCreateMemoryStreamOnSharedBuffer: buffer 0x%08lX p=%p f=%p", (unsigned long)hr, p, (void *)f); return FAILED(hr) ? hr : E_FAIL; }
  *out = f((const BYTE *)p, size); slog("[impl] SHCreateMemoryStreamOnSharedBuffer(%lu bytes) -> %p", (unsigned long)size, (void *)*out); return *out ? S_OK : E_OUTOFMEMORY; }

static int pkg_dep_path(int i, WCHAR *out) { pkg_load(); if (i < 1 || i >= g_nent || !g_ent[i].path[0]) return 0; lstrcpyW(out, g_ent[i].path); return 1; }



/* AppContainer as the app sees it. On Win10 a UWP app (an <Application> that is not Windows.FullTrustApplication) runs in an AppContainer;
   apps test it (the Store: WinStore.App.exe and StoreDesktopExtension.exe are one file and the token decides UI app vs desktop extension;
   with FALSE the UI process ran as its own extension and waited forever). Win7 has no AppContainers: the answer is TRUE only for a UWP
   app and only to the app's own code (caller inside the package folder) - Win10's system DLLs keep FALSE, their brokered paths need
   Win10 services. */
static int pkg_appcontainer_for(void *ret_addr) {
  HMODULE m = NULL; WCHAR mp[MAX_PATH]; size_t n;
  pkg_load(); if (!g_nent || g_app_fulltrust || !g_ent[0].path[0]) return 0;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ret_addr, &m) || !GetModuleFileNameW(m, mp, MAX_PATH)) return 0;
  n = wcslen(g_ent[0].path); return !_wcsnicmp(mp, g_ent[0].path, n) && (mp[n] == L'\\' || !mp[n]); }
/* S-1-15-2-x1..x7 from the package family name (stable per package; Win10 derives it with SHA-256, this only needs to be unique) */
static ULONG pkg_appcontainer_sid(BYTE *sid, ULONG cap) {
  ULONG need = 8 + 4 * 8, h = 2166136261u, k; const WCHAR *f; int i;
  if (cap < need) return need; pkg_load(); f = g_nent ? g_ent[0].family : L"";
  sid[0] = 1; sid[1] = 8; memset(sid + 2, 0, 5); sid[7] = 15;   /* revision, 8 subauthorities, authority 15 (APP_PACKAGE) */
  ((DWORD *)(sid + 8))[0] = 2;
  for (i = 1; i < 8; i++) { for (k = 0; f[k]; k++) h = (h ^ (towlower(f[k]) + i)) * 16777619u; ((DWORD *)(sid + 8))[i] = h; }
  return need; }
/* a packaged process whose <Application> is not Windows.FullTrustApplication (AppPolicyGetWindowingModel: Universal) */
static int pkg_is_uwp_app(void) { pkg_load(); return g_nent && !g_app_fulltrust; }
