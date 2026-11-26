/* wxruninstall.exe - WXR's "Uninstall apps" (Start menu, WXR > Tools): lists what is installed and removes what the user picks.
 *
 * Two lists, numbered through: first the APPS (main packages with an app entry, by their Start menu names), then ALL PACKAGES
 * (apps, frameworks such as VCLibs or the Windows App Runtime, resource packs), with what each framework is used by.
 * The removal itself is "wxrpkg remove" (beside this exe): it unregisters the package, deletes its app data unless asked to keep it,
 * deletes what no package uses any more, and updates the Start menu (wxr sync). An app that is running is not removed.
 * Reads the package database directly: <root>\Users\<user>\packages.ini [registered], <root>\ProgramData\Packages\<full>.ini,
 * <root>\ProgramData\Shell\<AUMID>.ini (display names). Root: the parent of this exe's folder, else %WXR_ROOT%. */
#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>

typedef struct { WCHAR full[400], name[300], family[300], version[32], loc[MAX_PATH]; int kind; ULONGLONG size, data; } Item;   /* kind 0 app, 1 framework, 2 resource, 3 main without app */
static WCHAR g_root[MAX_PATH], g_user[128], g_layer[MAX_PATH];
static Item *g_it; static int g_n;

static void out(const WCHAR *fmt, ...) {
    WCHAR b[2048]; va_list a; DWORD w, m; HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    va_start(a, fmt); _vsnwprintf(b, 2047, fmt, a); b[2047] = 0; va_end(a);
    if (GetConsoleMode(h, &m)) WriteConsoleW(h, b, (DWORD)wcslen(b), &w, NULL);
    else { char u[6200]; WideCharToMultiByte(CP_UTF8, 0, b, -1, u, sizeof u, NULL, NULL); fputs(u, stdout); fflush(stdout); }
}
static void ask(const WCHAR *prompt, WCHAR *ans, int cap) {
    WCHAR *e; out(L"%ls", prompt); ans[0] = 0;
    if (!fgetws(ans, cap, stdin)) { ans[0] = 0; return; }
    for (e = ans + wcslen(ans); e > ans && (e[-1] == L'\n' || e[-1] == L'\r' || e[-1] == L' '); ) *--e = 0;
}
static int yes(const WCHAR *prompt, int def) { WCHAR a[16]; ask(prompt, a, 16); if (!a[0]) return def; return a[0] == L'y' || a[0] == L'Y'; }

static int is_root(const WCHAR *dir) {
    WCHAR ini[MAX_PATH], m[64]; _snwprintf(ini, MAX_PATH, L"%ls\\environment.ini", dir); ini[MAX_PATH - 1] = 0;
    GetPrivateProfileStringW(L"wxr", L"marker", L"", m, 64, ini); return !wcscmp(m, L"WXR environment root");
}
static int find_root(void) {
    WCHAR *s; DWORD n = 128;
    GetModuleFileNameW(NULL, g_layer, MAX_PATH);
    if ((s = wcsrchr(g_layer, L'\\'))) *s = 0;                     /* <root>\Layer */
    lstrcpyW(g_root, g_layer);
    if ((s = wcsrchr(g_root, L'\\'))) *s = 0;                      /* <root> */
    if (!is_root(g_root)) {
        if (!GetEnvironmentVariableW(L"WXR_ROOT", g_root, MAX_PATH) || !is_root(g_root)) return 0;
        _snwprintf(g_layer, MAX_PATH, L"%ls\\Layer", g_root); }
    if (!GetUserNameW(g_user, &n)) lstrcpyW(g_user, L"user");
    return 1;
}
static void rec(const WCHAR *full, const WCHAR *key, WCHAR *o, DWORD n) {
    WCHAR r[MAX_PATH]; _snwprintf(r, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls.ini", g_root, full); GetPrivateProfileStringW(L"package", key, L"", o, n, r);
}
static ULONGLONG tree_size(const WCHAR *dir) {
    WCHAR pat[MAX_PATH], sub[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; ULONGLONG t = 0;
    _snwprintf(pat, MAX_PATH, L"%ls\\*", dir); if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do { if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
         if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { _snwprintf(sub, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName); t += tree_size(sub); }
         else t += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; } while (FindNextFileW(h, &fd));
    FindClose(h); return t;
}
static void fmt_size(ULONGLONG b, WCHAR *o) {
    if (b >= (1ull << 30)) _snwprintf(o, 16, L"%.1f GB", b / 1073741824.0); else if (b >= (1ull << 20)) _snwprintf(o, 16, L"%.0f MB", b / 1048576.0);
    else if (b) _snwprintf(o, 16, L"%llu KB", (b + 1023) / 1024); else lstrcpyW(o, L"-");
}
/* the Start menu names of a package's apps (ProgramData\Shell\*.ini, Package=<full>), joined with " / " */
static int display_names(const WCHAR *full, WCHAR *o, int cap) {
    WCHAR pat[MAX_PATH], ini[MAX_PATH], pk[400], dn[260]; WIN32_FIND_DATAW fd; HANDLE h; int n = 0; o[0] = 0;
    _snwprintf(pat, MAX_PATH, L"%ls\\ProgramData\\Shell\\*.ini", g_root);
    if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do { _snwprintf(ini, MAX_PATH, L"%ls\\ProgramData\\Shell\\%ls", g_root, fd.cFileName);
         GetPrivateProfileStringW(L"app", L"Package", L"", pk, 400, ini); if (_wcsicmp(pk, full)) continue;
         GetPrivateProfileStringW(L"app", L"DisplayName", L"", dn, 260, ini); if (!dn[0]) continue;
         if (n++ && (int)(wcslen(o) + 3) < cap) wcscat(o, L" / ");
         if ((int)(wcslen(o) + wcslen(dn)) < cap - 1) wcscat(o, dn); } while (FindNextFileW(h, &fd));
    FindClose(h); return n;
}
static int by_name(const void *a, const void *b) { const Item *x = (const Item *)a, *y = (const Item *)b; if (x->kind != y->kind) return x->kind - y->kind; return _wcsicmp(x->name, y->name); }
static void load(void) {
    WCHAR ini[MAX_PATH], *keys, *k; int cap = 64;
    free(g_it); g_n = 0; g_it = (Item *)calloc(cap, sizeof(Item));
    _snwprintf(ini, MAX_PATH, L"%ls\\Users\\%ls\\packages.ini", g_root, g_user);
    keys = (WCHAR *)malloc(65536 * sizeof(WCHAR)); GetPrivateProfileStringW(L"registered", NULL, L"", keys, 65536, ini);
    for (k = keys; *k; k += wcslen(k) + 1) {
        Item *it; WCHAR fw[8], rs[8], apps[64], recf[MAX_PATH], dd[MAX_PATH];
        if (g_n == cap) { cap *= 2; g_it = (Item *)realloc(g_it, cap * sizeof(Item)); }
        it = &g_it[g_n]; memset(it, 0, sizeof *it); lstrcpynW(it->full, k, 400);
        rec(k, L"IsFramework", fw, 8); rec(k, L"IsResourcePackage", rs, 8); rec(k, L"Version", it->version, 32);
        rec(k, L"PackageFamilyName", it->family, 300); rec(k, L"InstallLocation", it->loc, MAX_PATH);
        if (!it->loc[0]) continue;                                   /* no record: not a package wxrpkg knows */
        _snwprintf(recf, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls.ini", g_root, k);
        GetPrivateProfileSectionW(L"applications", apps, 64, recf);
        it->kind = !wcscmp(fw, L"True") ? 1 : !wcscmp(rs, L"True") ? 2 : apps[0] ? 0 : 3;
        if (it->kind != 0 || !display_names(k, it->name, 300)) rec(k, L"Name", it->name, 300);
        if (it->kind == 0 && wcschr(it->name, L'.')) {   /* no Start menu name: "Microsoft.GamingServices" -> "Gaming Services", as wxr sync names it */
            WCHAR t[300]; const WCHAR *l = wcsrchr(it->name, L'.') + 1; int o = 0;
            for (; *l && o < 290; l++) { if (o && *l >= L'A' && *l <= L'Z' && l[-1] >= L'a' && l[-1] <= L'z') t[o++] = L' '; t[o++] = *l; }
            t[o] = 0; lstrcpyW(it->name, t); }
        it->size = tree_size(it->loc);
        if (it->kind == 0 || it->kind == 3) { _snwprintf(dd, MAX_PATH, L"%ls\\Users\\%ls\\Packages\\%ls", g_root, g_user, it->family); it->data = tree_size(dd); }
        g_n++;
    }
    free(keys);
    qsort(g_it, g_n, sizeof(Item), by_name);
}
/* the registered packages that depend on `full` (record [dependencies]) */
static int used_by(const WCHAR *full, WCHAR *o, int cap) {
    int i, n = 0; o[0] = 0;
    for (i = 0; i < g_n; i++) { WCHAR r[MAX_PATH], buf[8192], *k;
        _snwprintf(r, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls.ini", g_root, g_it[i].full);
        GetPrivateProfileSectionW(L"dependencies", buf, 8192, r);
        for (k = buf; *k; k += wcslen(k) + 1) { WCHAR *eq = wcschr(k, L'='); if (eq && !_wcsicmp(eq + 1, full)) {
            if (n++ && (int)wcslen(o) + 2 < cap) wcscat(o, L", ");
            if ((int)(wcslen(o) + wcslen(g_it[i].name)) < cap - 1) wcscat(o, g_it[i].name);
            break; } } }
    return n;
}
static void show(void) {
    int i, num = 0, apps = 0; WCHAR s1[16], s2[16], u[600];
    static const WCHAR *kinds[] = { L"app", L"framework", L"resources", L"package" };
    out(L"\nAPPS\n");
    for (i = 0; i < g_n; i++) if (g_it[i].kind == 0) { fmt_size(g_it[i].size, s1); fmt_size(g_it[i].data, s2);
        out(L"  %3d  %-44ls %9ls  data %ls\n", ++num, g_it[i].name, s1, s2); apps++; }
    if (!apps) out(L"  (no apps installed)\n");
    out(L"\nALL PACKAGES\n");
    for (i = 0; i < g_n; i++) { fmt_size(g_it[i].size, s1);
        out(L"  %3d  %-10ls %ls  %ls\n", ++num, kinds[g_it[i].kind], g_it[i].full, s1);
        if (g_it[i].kind == 1) { if (used_by(g_it[i].full, u, 600)) out(L"                  used by %ls\n", u); else out(L"                  not used by any installed app\n"); } }
}
/* number -> item (apps are listed first, then every package) */
static Item *pick(int num) {
    int i, k = 0;
    for (i = 0; i < g_n; i++) if (g_it[i].kind == 0 && ++k == num) return &g_it[i];
    if (num > k && num - k <= g_n) return &g_it[num - k - 1];
    return NULL;
}
static int running(const Item *it, WCHAR *name, int cap) {
    DWORD pids[4096], cb, i; size_t ll = wcslen(it->loc);
    if (!EnumProcesses(pids, sizeof pids, &cb)) return 0;
    for (i = 0; i < cb / sizeof(DWORD); i++) { HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]); WCHAR p[MAX_PATH]; DWORD n = MAX_PATH;
        if (!h) continue;
        if (QueryFullProcessImageNameW(h, 0, p, &n) && !_wcsnicmp(p, it->loc, ll) && p[ll] == L'\\') { const WCHAR *b = wcsrchr(p, L'\\'); lstrcpynW(name, b ? b + 1 : p, cap); CloseHandle(h); return 1; }
        CloseHandle(h); }
    return 0;
}
static int remove_item(Item *it) {
    WCHAR exe[MAX_PATH], cmd[2 * MAX_PATH + 500], proc[MAX_PATH], s[16]; STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD code = 1; int keep = 0;
    if (running(it, proc, MAX_PATH)) { out(L"\n%ls is running (%ls). Close it first.\n", it->name, proc); return 1; }
    fmt_size(it->size, s);
    out(L"\n%ls %ls (%ls)\n", it->name, it->version, s);
    if (it->kind == 1) { WCHAR u[600]; if (used_by(it->full, u, 600)) { out(L"It is used by %ls: uninstall those first.\n", u); return 1; } }
    if (!yes(L"Uninstall it? [y/N] ", 0)) { out(L"Nothing was changed.\n"); return 1; }
    if ((it->kind == 0 || it->kind == 3) && it->data) { WCHAR q[128]; fmt_size(it->data, s);
        _snwprintf(q, 128, L"Also delete its data (settings, saves: %ls)? [Y/n] ", s); keep = !yes(q, 1); }
    _snwprintf(exe, MAX_PATH, L"%ls\\wxrpkg.exe", g_layer);
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" --root \"%ls\" remove %ls%ls", exe, g_root, it->full, keep ? L" --preserve-data" : L"");
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    out(L"\n");
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { out(L"Cannot start %ls (error %lu).\n", exe, GetLastError()); return 1; }
    WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    out(code ? L"\nUninstalling failed (see above).\n" : keep ? L"\nUninstalled; its data was kept.\n" : L"\nUninstalled.\n");
    return (int)code;
}

/* frameworks no installed package uses any more (left behind by apps uninstalled before) */
static int unused_frameworks(void) { int i, n = 0; WCHAR u[600]; for (i = 0; i < g_n; i++) if (g_it[i].kind == 1 && !used_by(g_it[i].full, u, 600)) n++; return n; }
static void remove_unused(void) {
    int i, n = unused_frameworks(); WCHAR u[600], q[96];
    _snwprintf(q, 96, L"\nUninstall the %d framework(s) no app uses? [y/N] ", n);
    if (!yes(q, 0)) { out(L"Nothing was changed.\n"); return; }
    for (i = 0; i < g_n; i++) if (g_it[i].kind == 1 && !used_by(g_it[i].full, u, 600)) {
        WCHAR exe[MAX_PATH], cmd[2 * MAX_PATH + 500]; STARTUPINFOW si; PROCESS_INFORMATION pi;
        _snwprintf(exe, MAX_PATH, L"%ls\\wxrpkg.exe", g_layer);
        _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" --root \"%ls\" remove %ls", exe, g_root, g_it[i].full);
        memset(&si, 0, sizeof si); si.cb = sizeof si;
        if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        else out(L"Cannot start %ls (error %lu).\n", exe, GetLastError()); }
}

int wmain(int argc, WCHAR **argv) {
    WCHAR a[32]; DWORD procs[4];
    (void)argv;
    SetConsoleTitleW(L"WXR - Uninstall apps");
    out(L"WXR - Uninstall apps\n");
    if (argc > 1) { out(L"usage: wxruninstall   (interactive)\n"); return 2; }
    if (!find_root()) { out(L"This program belongs in the Layer folder of a WXR installation (or set WXR_ROOT).\n"); goto done; }
    for (;;) {
        Item *it; int num;
        out(L"\nReading what is installed in %ls...\n", g_root);
        load(); show();
        if (unused_frameworks()) out(L"\n  0  uninstall the frameworks no app uses\n");
        ask(L"\nNumber to uninstall, or Enter to close: ", a, 32);
        if (!a[0]) break;
        num = _wtoi(a);
        if (!wcscmp(a, L"0") && unused_frameworks()) { remove_unused(); continue; }
        if (num <= 0 || !(it = pick(num))) { out(L"There is no number %ls.\n", a); continue; }
        remove_item(it);
    }
    return 0;
done:
    if (GetConsoleProcessList(procs, 4) <= 1) { out(L"\nPress Enter to close."); getchar(); }
    return 1;
}
