/* wxr.exe - starts a WXR app the way Windows 10's shell does: by AUMID, through the broker.
     wxr activate <PackageFamilyName!AppId> [arguments]
   Starts the broker (wxrsvc.exe --wxr <root>, next to this exe) when it is not running yet, sends the request and exits.
   Root: WXR_ROOT, else the parent of this exe's folder when that holds environment.ini (<root>\Layer\wxr.exe), else C:\WXR.
   Errors are shown in a message box (no console). Exit code 0 = started. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void fail(const char *m) { MessageBoxA(NULL, m, "WXR", MB_ICONWARNING); }
#include <objbase.h>
#include <shlobj.h>
#include <stdlib.h>
#include "wxrpri.inc"

/* ======== wxr sync: shell integration of the registered apps ========
   For every application of every registered main package:
     <root>\ProgramData\Shell\<AUMID>.ini   [app] Aumid, DisplayName (resolved from resources.pri), Icon, Package
     <root>\ProgramData\Shell\<AUMID>.ico   from Square44x44Logo's targetsize PNGs (altform-unplated preferred, as Win10's taskbar)
     Start menu: %APPDATA%\Microsoft\Windows\Start Menu\Programs\WXR\<DisplayName>.lnk -> wxr.exe activate <AUMID>,
                 with System.AppUserModel.ID = AUMID so the taskbar groups/pins it with the app's windows.
   Entries of apps no longer registered are removed. Paths outside the root go to footprint.txt (tier "state"). */
static int xml_attr(const char *tag, const char *attr, char *out, size_t cap) {
  const char *end = strchr(tag, '>'), *p = tag; size_t al = strlen(attr); out[0] = 0;
  while ((p = strstr(p, attr)) && (!end || p < end)) { if ((p[-1] == ' ' || p[-1] == '\n' || p[-1] == '\t' || p[-1] == '\r') && p[al] == '=' && p[al + 1] == '"') {
      const char *q = p + al + 2, *e = strchr(q, '"'); if (!e || (size_t)(e - q) >= cap) return 0; memcpy(out, q, e - q); out[e - q] = 0; return 1; } p += al; }
  return 0; }
static char *read_all(const WCHAR *p, DWORD *n) {
  HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); char *b; DWORD sz, rd;
  if (f == INVALID_HANDLE_VALUE) return NULL; sz = GetFileSize(f, NULL); b = (char *)malloc(sz + 1);
  if (!b || !ReadFile(f, b, sz, &rd, NULL)) { CloseHandle(f); free(b); return NULL; } CloseHandle(f); b[rd] = 0; if (n) *n = rd; return b; }
static void footprint(const WCHAR *root, const char *type, const WCHAR *data) {
  WCHAR fp[MAX_PATH]; char line[1200], dn[1000]; char *all; HANDLE f; DWORD w;
  WideCharToMultiByte(CP_UTF8, 0, data, -1, dn, sizeof dn, NULL, NULL); _snprintf(line, sizeof line, "state\t%s\t%s\r\n", type, dn);
  _snwprintf(fp, MAX_PATH, L"%ls\\ProgramData\\footprint.txt", root);
  if ((all = read_all(fp, NULL)) && strstr(all, line)) { free(all); return; } free(all);
  f = CreateFileW(fp, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL); if (f == INVALID_HANDLE_VALUE) return;
  WriteFile(f, line, (DWORD)strlen(line), &w, NULL); CloseHandle(f); }
/* the best PNG of a logo for a pixel size: <base>.targetsize-N_altform-unplated.png, then <base>.targetsize-N.png, in the given folders */
static int logo_png(const WCHAR dirs[][MAX_PATH], int nd, const WCHAR *base, int size, WCHAR *out) {
  static const WCHAR *const fmt[] = { L"%ls\\%ls.targetsize-%d_altform-unplated.png", L"%ls\\%ls.targetsize-%d.png" }; int i, d;
  for (i = 0; i < 2; i++) for (d = 0; d < nd; d++) { _snwprintf(out, MAX_PATH, fmt[i], dirs[d], base, size); if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return 1; }
  return 0; }
/* .ico made of PNG frames (supported since Vista) */
static int write_ico(const WCHAR *ico, const WCHAR dirs[][MAX_PATH], int nd, const WCHAR *base) {
  static const int sizes[] = { 16, 24, 32, 48, 256 }; char *png[5]; DWORD len[5]; int n = 0, i; HANDLE f; DWORD w, off;
  for (i = 0; i < 5; i++) { WCHAR p[MAX_PATH]; if (logo_png(dirs, nd, base, sizes[i], p) && (png[n] = read_all(p, &len[n])) != NULL) n++; }
  if (!n) return 0;
  f = CreateFileW(ico, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); if (f == INVALID_HANDLE_VALUE) { for (i = 0; i < n; i++) free(png[i]); return 0; }
  { WORD h[3] = { 0, 1, (WORD)n }; WriteFile(f, h, 6, &w, NULL); }
  off = 6 + 16 * n;
  for (i = 0; i < n; i++) { BYTE e[16]; unsigned pw = 0, ph = 0; const BYTE *b = (const BYTE *)png[i];
    if (len[i] > 24) { pw = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19]; ph = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23]; }   /* IHDR */
    memset(e, 0, sizeof e); e[0] = pw >= 256 ? 0 : (BYTE)pw; e[1] = ph >= 256 ? 0 : (BYTE)ph; e[4] = 1; e[6] = 32;
    memcpy(e + 8, &len[i], 4); memcpy(e + 12, &off, 4); WriteFile(f, e, 16, &w, NULL); off += len[i]; }
  for (i = 0; i < n; i++) { WriteFile(f, png[i], len[i], &w, NULL); free(png[i]); }
  CloseHandle(f); return 1; }
static const GUID FMTID_AppUserModel_ = { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } };
static const GUID IID_IPropertyStore_ = { 0x886d8eeb, 0x8cf2, 0x4446, { 0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99 } };
typedef struct { GUID fmtid; DWORD pid; } PKEY_;
typedef struct { void **vt; } PStore_;
static int make_lnk(const WCHAR *lnk, const WCHAR *target, const WCHAR *args, const WCHAR *ico, const WCHAR *aumid, const WCHAR *desc) {
  IShellLinkW *sl = NULL; IPersistFile *pf = NULL; PStore_ *ps = NULL; HRESULT hr;
  if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return 0;
  sl->lpVtbl->SetPath(sl, target); sl->lpVtbl->SetArguments(sl, args); sl->lpVtbl->SetDescription(sl, desc);
  if (ico && ico[0]) sl->lpVtbl->SetIconLocation(sl, ico, 0);
  if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPropertyStore_, (void **)&ps)) && ps) {
    PKEY_ k = { FMTID_AppUserModel_, 5 }; PROPVARIANT v; memset(&v, 0, sizeof v); v.vt = VT_LPWSTR; v.pwszVal = (LPWSTR)aumid;
    ((HRESULT (STDMETHODCALLTYPE *)(void *, const PKEY_ *, const PROPVARIANT *))ps->vt[6])(ps, &k, &v);   /* IPropertyStore::SetValue */
    ((HRESULT (STDMETHODCALLTYPE *)(void *))ps->vt[7])(ps);                                                /* Commit */
    ((ULONG (STDMETHODCALLTYPE *)(void *))ps->vt[2])(ps); }
  hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf);
  if (SUCCEEDED(hr)) { hr = pf->lpVtbl->Save(pf, lnk, TRUE); pf->lpVtbl->Release(pf); }
  sl->lpVtbl->Release(sl); return SUCCEEDED(hr); }
/* a scale-qualified asset ("Assets\X.png" -> Assets\X.scale-NNN.png, plain variant) for this screen's DPI: the smallest scale >= the
   screen's, else the largest one there is; out = full path, *scale = its scale (100 = 96 DPI) */
static int scaled_asset(const WCHAR dirs[][MAX_PATH], int nd, const char *rel8, WCHAR *out, int *scale) {
  static const int sc[] = { 100, 125, 150, 200, 400 }; WCHAR rel[MAX_PATH], *dot; HDC dc = GetDC(NULL); int want = dc ? GetDeviceCaps(dc, LOGPIXELSX) * 100 / 96 : 100, i, d, best = -1; WCHAR bestp[MAX_PATH];
  if (dc) ReleaseDC(NULL, dc);
  MultiByteToWideChar(CP_UTF8, 0, rel8, -1, rel, MAX_PATH); dot = wcsrchr(rel, L'.'); if (!dot) return 0; *dot = 0;
  for (i = 0; i < 5; i++) for (d = 0; d < nd; d++) { WCHAR p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls\\%ls.scale-%d.png", dirs[d], rel, sc[i]);
      if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
      if (best < 0 || (sc[best] < want && sc[i] > sc[best]) || (sc[i] >= want && sc[i] < sc[best])) { best = i; lstrcpyW(bestp, p); } }
  if (best < 0) { for (d = 0; d < nd; d++) { _snwprintf(out, MAX_PATH, L"%ls\\%ls.png", dirs[d], rel); if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) { *scale = 100; return 1; } } return 0; }
  lstrcpyW(out, bestp); *scale = sc[best]; return 1; }
static void clean_name(WCHAR *s) { for (; *s; s++) if (wcschr(L"\\/:*?\"<>|", *s)) *s = L'_'; }
static int do_sync(const char *root8, const char *self8) {
  WCHAR root[MAX_PATH], self[MAX_PATH], user[128], regf[MAX_PATH], shelld[MAX_PATH], menu[MAX_PATH], listf[MAX_PATH]; WCHAR *keys; DWORD un = 128; WCHAR *k;
  WCHAR keep[200][160]; int nkeep = 0, napps = 0;
  MultiByteToWideChar(CP_ACP, 0, root8, -1, root, MAX_PATH); MultiByteToWideChar(CP_ACP, 0, self8, -1, self, MAX_PATH);
  GetUserNameW(user, &un); _snwprintf(regf, MAX_PATH, L"%ls\\Users\\%ls\\packages.ini", root, user);
  _snwprintf(shelld, MAX_PATH, L"%ls\\ProgramData\\Shell", root); CreateDirectoryW(shelld, NULL);
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, menu))) return 0;
  lstrcatW(menu, L"\\WXR"); if (CreateDirectoryW(menu, NULL)) footprint(root, "DIR", menu);
  keys = (WCHAR *)malloc(65536 * sizeof(WCHAR)); if (!keys) return 0;
  GetPrivateProfileStringW(L"registered", NULL, L"", keys, 65536, regf);
  CoInitialize(NULL);
  for (k = keys; *k; k += lstrlenW(k) + 1) {
    WCHAR rec[MAX_PATH], is[16], loc[MAX_PATH], fam[200], name[200], man[MAX_PATH], apps[4096], *a; char *mx;
    _snwprintf(rec, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls.ini", root, k);
    GetPrivateProfileStringW(L"package", L"IsFramework", L"", is, 16, rec); if (!lstrcmpiW(is, L"True")) continue;
    GetPrivateProfileStringW(L"package", L"IsResourcePackage", L"", is, 16, rec); if (!lstrcmpiW(is, L"True")) continue;
    GetPrivateProfileStringW(L"package", L"InstallLocation", L"", loc, MAX_PATH, rec);
    GetPrivateProfileStringW(L"package", L"PackageFamilyName", L"", fam, 200, rec);
    GetPrivateProfileStringW(L"package", L"Name", L"", name, 200, rec);
    if (!loc[0] || !fam[0]) continue;
    _snwprintf(man, MAX_PATH, L"%ls\\AppxManifest.xml", loc); if (!(mx = read_all(man, NULL))) continue;
    GetPrivateProfileStringW(L"applications", NULL, L"", apps, 4096, rec);
    for (a = apps; *a; a += lstrlenW(a) + 1) {
      char id8[100], *ap = mx, dn[400] = "", logo[400] = ""; WCHAR aumid[400], disp[260], ico[MAX_PATH], ini[MAX_PATH], lnk[MAX_PATH], args[500], base[MAX_PATH], dirs[8][MAX_PATH]; int nd = 0, have_ico;
      WideCharToMultiByte(CP_UTF8, 0, a, -1, id8, sizeof id8, NULL, NULL);
      while ((ap = strstr(ap, "<Application ")) != NULL) { char v[100]; if (xml_attr(ap, "Id", v, sizeof v) && !strcmp(v, id8)) break; ap += 12; }
      if (ap) { char *ve = strstr(ap, "VisualElements "), *end = strstr(ap, "</Application>"); if (ve && (!end || ve < end)) { xml_attr(ve, "DisplayName", dn, sizeof dn); xml_attr(ve, "Square44x44Logo", logo, sizeof logo); }
        { char *ve = strstr(ap, "VisualElements "), *end = strstr(ap, "</Application>"), le[16];   /* AppListEntry sits on VisualElements: not in the app list (Clock's widget server) */
          if (ve && (!end || ve < end) && xml_attr(ve, "AppListEntry", le, sizeof le) && !_stricmp(le, "none")) continue; } }
      _snwprintf(aumid, 400, L"%ls!%ls", fam, a);
      if (!dn[0] || !wxr_pri_text(loc, dn, disp, 260)) {   /* unresolved: "Microsoft.WindowsAlarms" -> "Windows Alarms" */
        const WCHAR *last = wcsrchr(name, L'.'); int o = 0; last = last ? last + 1 : name;
        for (; *last && o < 250; last++) { if (o && *last >= L'A' && *last <= L'Z' && last[-1] >= L'a' && last[-1] <= L'z') disp[o++] = L' '; disp[o++] = *last; } disp[o] = 0; }
      /* icon folders: the package, then its resource packs (same name and version, registered) */
      lstrcpyW(dirs[nd++], loc);
      { WCHAR *k2; for (k2 = keys; *k2 && nd < 8; k2 += lstrlenW(k2) + 1) { WCHAR r2[MAX_PATH], l2[MAX_PATH], is2[16]; size_t nl = lstrlenW(name);
          if (_wcsnicmp(k2, name, nl) || k2[nl] != L'_' || !lstrcmpiW(k2, k)) continue;
          _snwprintf(r2, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls.ini", root, k2); GetPrivateProfileStringW(L"package", L"IsResourcePackage", L"", is2, 16, r2);
          if (lstrcmpiW(is2, L"True")) continue; GetPrivateProfileStringW(L"package", L"InstallLocation", L"", l2, MAX_PATH, r2); if (l2[0]) lstrcpyW(dirs[nd++], l2); } }
      MultiByteToWideChar(CP_UTF8, 0, logo, -1, base, MAX_PATH); { WCHAR *dot = wcsrchr(base, L'.'); if (dot) *dot = 0; }
      { WCHAR fn[400]; lstrcpynW(fn, aumid, 400); clean_name(fn); _snwprintf(ico, MAX_PATH, L"%ls\\%ls.ico", shelld, fn); _snwprintf(ini, MAX_PATH, L"%ls\\%ls.ini", shelld, fn); }
      {   /* "Assets\CalculatorAppList" -> folder part goes onto every package folder, file part is the logo's base name */
        WCHAR bd[8][MAX_PATH], sub[MAX_PATH] = L"", leaf[MAX_PATH], *sl = wcsrchr(base, L'\\'); int d;
        if (sl) { *sl = 0; lstrcpyW(sub, base); lstrcpyW(leaf, sl + 1); } else lstrcpyW(leaf, base);
        for (d = 0; d < nd; d++) { if (sub[0]) _snwprintf(bd[d], MAX_PATH, L"%ls\\%ls", dirs[d], sub); else lstrcpyW(bd[d], dirs[d]); }
        have_ico = logo[0] && write_ico(ico, (const WCHAR (*)[MAX_PATH])bd, nd, leaf); }
      if (!have_ico) ico[0] = 0;
      WritePrivateProfileStringW(L"app", L"Aumid", aumid, ini); WritePrivateProfileStringW(L"app", L"DisplayName", disp, ini);
      WritePrivateProfileStringW(L"app", L"Icon", ico, ini); WritePrivateProfileStringW(L"app", L"Package", k, ini);
      {   /* splash screen (drawn by wxrsys until the app's first frame, as Win10's shell does): <uap:SplashScreen Image BackgroundColor>,
             colour falling back to the tile's BackgroundColor; "transparent" = the accent colour (Win10's default #0078D4) */
        char img[400] = "", bg[40] = "", vbg[40] = ""; WCHAR sp[MAX_PATH] = L"", bgw[40], scw[16]; int scale = 100;
        if (ap) { char *end = strstr(ap, "</Application>"), *ve = strstr(ap, "VisualElements "), *ss = strstr(ap, "SplashScreen ");
          if (ve && (!end || ve < end)) xml_attr(ve, "BackgroundColor", vbg, sizeof vbg);
          if (ss && (!end || ss < end)) { xml_attr(ss, "Image", img, sizeof img); xml_attr(ss, "BackgroundColor", bg, sizeof bg); } }
        if (!bg[0]) lstrcpyA(bg, vbg);
        if (!bg[0] || !_stricmp(bg, "transparent")) lstrcpyA(bg, "#0078D4");
        if (img[0]) {
          WCHAR bd[8][MAX_PATH], sub[MAX_PATH] = L"", leaf[MAX_PATH], relw[MAX_PATH], *sl; char leaf8[400]; int d;
          MultiByteToWideChar(CP_UTF8, 0, img, -1, relw, MAX_PATH); sl = wcsrchr(relw, L'\\');
          if (sl) { *sl = 0; lstrcpyW(sub, relw); lstrcpyW(leaf, sl + 1); } else lstrcpyW(leaf, relw);
          for (d = 0; d < nd; d++) { if (sub[0]) _snwprintf(bd[d], MAX_PATH, L"%ls\\%ls", dirs[d], sub); else lstrcpyW(bd[d], dirs[d]); }
          WideCharToMultiByte(CP_UTF8, 0, leaf, -1, leaf8, sizeof leaf8, NULL, NULL);
          if (!scaled_asset((const WCHAR (*)[MAX_PATH])bd, nd, leaf8, sp, &scale)) sp[0] = 0; }
        MultiByteToWideChar(CP_UTF8, 0, bg, -1, bgw, 40); _snwprintf(scw, 16, L"%d", scale);
        WritePrivateProfileStringW(L"app", L"SplashImage", sp, ini); WritePrivateProfileStringW(L"app", L"SplashScale", scw, ini); WritePrivateProfileStringW(L"app", L"SplashColor", bgw, ini); }
      { WCHAR dn2[260]; lstrcpyW(dn2, disp); clean_name(dn2); _snwprintf(lnk, MAX_PATH, L"%ls\\%ls.lnk", menu, dn2); }
      { WCHAR prev[MAX_PATH]; if (GetPrivateProfileStringW(L"app", L"Shortcut", L"", prev, MAX_PATH, ini) && lstrcmpiW(prev, lnk)) DeleteFileW(prev); }   /* renamed app: drop its old shortcut */
      _snwprintf(args, 500, L"activate %ls", aumid);
      if (make_lnk(lnk, self, args, ico, aumid, disp)) { footprint(root, "FILE", lnk); WritePrivateProfileStringW(L"app", L"Shortcut", lnk, ini); }
      if (nkeep < 200) { lstrcpynW(keep[nkeep++], ini, 160); } napps++;
    }
    free(mx);
  }
  /* entries of apps that are gone */
  { WIN32_FIND_DATAW fd; HANDLE h; WCHAR pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*.ini", shelld); h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) { do { WCHAR p[MAX_PATH], lnk[MAX_PATH], ico[MAX_PATH]; int i, kept = 0; _snwprintf(p, MAX_PATH, L"%ls\\%ls", shelld, fd.cFileName);
        for (i = 0; i < nkeep; i++) if (!lstrcmpiW(keep[i], p)) kept = 1;
        if (kept) continue;
        GetPrivateProfileStringW(L"app", L"Shortcut", L"", lnk, MAX_PATH, p); GetPrivateProfileStringW(L"app", L"Icon", L"", ico, MAX_PATH, p);
        if (lnk[0]) DeleteFileW(lnk); if (ico[0]) DeleteFileW(ico); DeleteFileW(p); } while (FindNextFileW(h, &fd)); FindClose(h); } }
  CoUninitialize(); free(keys);
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
  return napps; }
static void pipe_name(char *out, size_t cap) { DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid); _snprintf(out, cap, "\\\\.\\pipe\\wxr.broker.%lu", (unsigned long)sid); }

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show)
{
    char self[MAX_PATH], dir[MAX_PATH], root[MAX_PATH], pn[100], req[4096], reply[1024], msg[1400], *s; DWORD n = 0; int i, tries;
    (void)hi; (void)hp; (void)cl; (void)show;
    GetModuleFileNameA(NULL, self, MAX_PATH); lstrcpyA(dir, self); if ((s = strrchr(dir, '\\'))) *s = 0;
    if (!GetEnvironmentVariableA("WXR_ROOT", root, MAX_PATH)) {
        char ini[MAX_PATH]; lstrcpyA(root, dir); if ((s = strrchr(root, '\\'))) *s = 0;
        _snprintf(ini, MAX_PATH, "%s\\environment.ini", root); if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) lstrcpyA(root, "C:\\WXR"); }
    if (__argc >= 4 && !_stricmp(__argv[1], "pri")) {   /* diagnostic: wxr pri <package dir> <ms-resource:value> */
        WCHAR d[MAX_PATH], pri[MAX_PATH], out[400]; PriFile f; int ok;
        MultiByteToWideChar(CP_ACP, 0, __argv[2], -1, d, MAX_PATH); _snwprintf(pri, MAX_PATH, L"%ls\\resources.pri", d);
        ok = pri_open(&f, pri); _snprintf(msg, sizeof msg, "open: %s, items %d (failed at stage %d)", ok ? "ok" : "FAILED", ok ? f.nitems : 0, ok ? 0 : g_pri_stage);
        if (ok) { int k; int shown = 0; for (k = 0; k < f.nitems && shown < 8; k++) if (strstr(f.items[k].upath, "APPNAME")) { strncat(msg, "\n", sizeof msg - strlen(msg) - 1); strncat(msg, f.items[k].path, sizeof msg - strlen(msg) - 1); shown++; } pri_free(&f); }
        ok = wxr_pri_text(d, __argv[3], out, 400);
        { char o8[800]; WideCharToMultiByte(CP_UTF8, 0, out, -1, o8, sizeof o8, NULL, NULL); strncat(msg, ok ? "\nresult: " : "\nresult: (none) ", sizeof msg - strlen(msg) - 1); strncat(msg, o8, sizeof msg - strlen(msg) - 1); }
        { HANDLE o = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL); FILE *fo = fopen(__argc > 4 ? __argv[4] : "NUL", "w"); if (fo) { fputs(msg, fo); fclose(fo); } if (o != INVALID_HANDLE_VALUE) CloseHandle(o); }
        return 0; }
    if (__argc >= 2 && !_stricmp(__argv[1], "sync")) {   /* shell integration (Start menu, icons, names); quiet unless "sync /v" */
        int n = do_sync(root, self);
        if (__argc > 2 && !_stricmp(__argv[2], "/v")) { _snprintf(msg, sizeof msg, "%d app(s) in the Start menu (WXR folder).", n); MessageBoxA(NULL, msg, "WXR", MB_ICONINFORMATION); }
        return n >= 0 ? 0 : 1; }
    if (__argc < 3 || _stricmp(__argv[1], "activate")) { fail("usage:\n  wxr activate <PackageFamilyName!AppId> [arguments]\n  wxr sync [/v]"); return 2; }
    _snprintf(req, sizeof req, "activate\t%s\t", __argv[2]);
    for (i = 3; i < __argc; i++) { strncat(req, i > 3 ? " \"" : "\"", sizeof req - strlen(req) - 1); strncat(req, __argv[i], sizeof req - strlen(req) - 1); strncat(req, "\"", sizeof req - strlen(req) - 1); }
    pipe_name(pn, sizeof pn);
    for (tries = 0; tries < 2; tries++) {
        if (CallNamedPipeA(pn, req, (DWORD)strlen(req), reply, sizeof reply - 1, &n, 3000)) break;
        if (tries) break;
        {   /* no broker yet: start it and wait until its endpoint answers (it first brings up the CoreMessaging registrar) */
            char cmd[MAX_PATH * 2 + 40]; STARTUPINFOA si; PROCESS_INFORMATION pi; int w;
            _snprintf(cmd, sizeof cmd, "\"%s\\wxrsvc.exe\" --wxr \"%s\"", dir, root);
            memset(&si, 0, sizeof si); si.cb = sizeof si;
            if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, dir, &si, &pi)) { _snprintf(msg, sizeof msg, "Cannot start the WXR broker (%s\\wxrsvc.exe), error %lu.", dir, GetLastError()); fail(msg); return 3; }
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            for (w = 0; w < 100 && !WaitNamedPipeA(pn, 100); w++) Sleep(100);
        }
    }
    if (!n) { fail("The WXR broker did not answer."); return 4; }
    reply[n] = 0;
    if (strncmp(reply, "ok", 2)) { _snprintf(msg, sizeof msg, "Could not start %s:\n\n%s", __argv[2], !strncmp(reply, "error ", 6) ? reply + 6 : reply); fail(msg); return 5; }
    return 0;
}
