/* wxrsetup - creates, inspects and purges a WXR environment root.
   usage: wxrsetup [--root <dir>] [--yes] <check | inspect | create | extract | components | register | install-layer | install-setup | status | purge>
   Root: --root, else %WXR_ROOT%, else C:\WXR.  Log: <root>\Logs\setup.log (console gets the same lines). */
#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdbool.h>
#include <shlobj.h>
#include <io.h>
#include <fcntl.h>
#include "udf.h"
#include "hive.h"
#include "wimlib.h"          /* types only: libwim-15.dll is loaded at run time (so "check" can report it missing) */

#define WXR_SETUP_VERSION L"0.2"
#define VXKEX_TESTED      L"1.2.1.2229"
#define WXR_MARKER        L"WXR environment root"
#define ROOT_MAX_LEN      32      /* Win7 MAX_PATH is 260 and package folder names are ~60 chars: keep the root short */

static WCHAR g_root[MAX_PATH];
static WCHAR g_ini[MAX_PATH];
static int   g_yes;
static FILE *g_log;
static char *g_pend; static size_t g_npend, g_cappend;   /* lines logged before the log file could be opened */
static int   g_warnings;
static int   g_lines;      /* --lines: console output is "<KIND>\t<text>" per line, UTF-8, flushed (for the GUI installer) */

/* ---------------- logging ----------------
   --lines kinds: INFO WARN ERROR (log lines), OK FAIL NOTE (check results; WARN too), PROGRESS\t<pct>\t<what>,
   DATA\t<key>\t<value> (facts for the caller, e.g. DATA\troot\texisting) */
static void log_raw(const char *utf8) {
    if (g_log) { fputs(utf8, g_log); fflush(g_log); return; }
    { size_t n = strlen(utf8);
      if (g_npend + n + 1 > g_cappend) { g_cappend = (g_npend + n + 1) * 2; g_pend = (char *)realloc(g_pend, g_cappend); }
      memcpy(g_pend + g_npend, utf8, n + 1); g_npend += n; }
}
static void logv(const WCHAR *lvl, const WCHAR *fmt, va_list ap) {
    WCHAR msg[2048], line[2200]; char u8[6600]; SYSTEMTIME t;
    _vsnwprintf(msg, 2047, fmt, ap); msg[2047] = 0;
    GetLocalTime(&t);
    _snwprintf(line, 2199, L"%04u-%02u-%02u %02u:%02u:%02u %-5ls %ls\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, lvl, msg); line[2199] = 0;
    if (g_lines) { wprintf(L"%ls\t%ls\n", lvl, msg); fflush(stdout); }
    else if (!wcscmp(lvl, L"INFO")) wprintf(L"%ls\n", msg); else wprintf(L"%ls: %ls\n", lvl, msg);
    WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof u8, NULL, NULL);
    log_raw(u8);
}
static void info(const WCHAR *fmt, ...) { va_list ap; va_start(ap, fmt); logv(L"INFO", fmt, ap); va_end(ap); }
static void warn(const WCHAR *fmt, ...) { va_list ap; va_start(ap, fmt); logv(L"WARN", fmt, ap); va_end(ap); g_warnings++; }
static void err (const WCHAR *fmt, ...) { va_list ap; va_start(ap, fmt); logv(L"ERROR", fmt, ap); va_end(ap); }
static void logk(const WCHAR *kind, const WCHAR *fmt, ...) { va_list ap; va_start(ap, fmt); logv(kind, fmt, ap); va_end(ap); }
static void data(const WCHAR *key, const WCHAR *val) { if (g_lines) { wprintf(L"DATA\t%ls\t%ls\n", key, val); fflush(stdout); } }
static void log_open(void) {
    WCHAR p[MAX_PATH];
    if (g_log) return;
    _snwprintf(p, MAX_PATH, L"%ls\\Logs\\setup.log", g_root);
    g_log = _wfopen(p, L"ab");
    if (g_log && g_npend) { fwrite(g_pend, 1, g_npend, g_log); fflush(g_log); g_npend = 0; }
}
static void log_close(void) { if (g_log) { fclose(g_log); g_log = NULL; } }

/* ---------------- paths and files ---------------- */
static void join(WCHAR *out, const WCHAR *a, const WCHAR *b) { _snwprintf(out, MAX_PATH, L"%ls\\%ls", a, b); out[MAX_PATH - 1] = 0; }
static int is_dir(const WCHAR *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static int exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static int mkdirs(const WCHAR *p) {                       /* create p and missing parents */
    WCHAR t[MAX_PATH]; WCHAR *s;
    if (is_dir(p)) return 1;
    lstrcpynW(t, p, MAX_PATH);
    for (s = t + 3; *s; s++) if (*s == '\\') { *s = 0; if (!is_dir(t) && !CreateDirectoryW(t, NULL)) return 0; *s = '\\'; }
    return CreateDirectoryW(t, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static int dir_empty(const WCHAR *p) {
    WCHAR pat[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; int empty = 1;
    join(pat, p, L"*"); h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return 1;
    do { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { empty = 0; break; } } while (FindNextFileW(h, &fd));
    FindClose(h); return empty;
}
/* long-path form for deep trees (package folders can pass MAX_PATH) */
static WCHAR *lp(const WCHAR *p) {
    size_t n = wcslen(p) + 8; WCHAR *r = (WCHAR *)malloc(n * sizeof(WCHAR));
    _snwprintf(r, n, L"\\\\?\\%ls", p); return r;
}
typedef struct { ULONGLONG bytes; unsigned files, dirs, links; } TreeStat;
static void tree_stat(const WCHAR *dirlp, TreeStat *st) {          /* dirlp is a \\?\ path; reparse points are not followed */
    size_t n = wcslen(dirlp) + 4; WCHAR *pat = (WCHAR *)malloc(n * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE h;
    _snwprintf(pat, n, L"%ls\\*", dirlp); h = FindFirstFileW(pat, &fd); free(pat);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { st->links++; continue; }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            size_t m = wcslen(dirlp) + wcslen(fd.cFileName) + 2; WCHAR *sub = (WCHAR *)malloc(m * sizeof(WCHAR));
            _snwprintf(sub, m, L"%ls\\%ls", dirlp, fd.cFileName); st->dirs++; tree_stat(sub, st); free(sub);
        } else { st->files++; st->bytes += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
typedef struct { unsigned files, dirs, failed; } RmStat;
static void rm_fail(RmStat *rs, const WCHAR *what, const WCHAR *p) {
    rs->failed++;
    if (rs->failed <= 10) warn(L"could not delete %ls %ls (error %lu)", what, p + 4, GetLastError());
    else if (rs->failed == 11) warn(L"more delete failures follow (not listed)");
}
/* delete everything inside dirlp (\\?\ path); skip = a top-level name to keep (or NULL); never follows junctions/symlinks */
static void rm_contents(const WCHAR *dirlp, const WCHAR *skip, RmStat *rs) {
    size_t n = wcslen(dirlp) + 4; WCHAR *pat = (WCHAR *)malloc(n * sizeof(WCHAR)); WIN32_FIND_DATAW fd; HANDLE h;
    _snwprintf(pat, n, L"%ls\\*", dirlp); h = FindFirstFileW(pat, &fd); free(pat);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        size_t m; WCHAR *sub;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (skip && !lstrcmpiW(fd.cFileName, skip)) continue;
        m = wcslen(dirlp) + wcslen(fd.cFileName) + 2; sub = (WCHAR *)malloc(m * sizeof(WCHAR));
        _snwprintf(sub, m, L"%ls\\%ls", dirlp, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(sub, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) rm_contents(sub, NULL, rs);   /* a link: remove the link only */
            if (RemoveDirectoryW(sub)) rs->dirs++; else rm_fail(rs, L"folder", sub);
        } else {
            if (DeleteFileW(sub)) rs->files++; else rm_fail(rs, L"file", sub);
        }
        free(sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void fmt_size(ULONGLONG b, WCHAR *out) {
    if (b >= (1ull << 30)) _snwprintf(out, 32, L"%.2f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) _snwprintf(out, 32, L"%.1f MB", b / 1048576.0);
    else _snwprintf(out, 32, L"%llu KB", (b + 1023) / 1024);
}

/* ---------------- environment.ini ---------------- */
static void ini_set(const WCHAR *sec, const WCHAR *key, const WCHAR *val) { WritePrivateProfileStringW(sec, key, val, g_ini); }
static void ini_get(const WCHAR *sec, const WCHAR *key, WCHAR *out, DWORD n) { GetPrivateProfileStringW(sec, key, L"", out, n, g_ini); }
static int is_wxr_root(void) { WCHAR m[64]; if (!exists(g_ini)) return 0; ini_get(L"wxr", L"marker", m, 64); return !wcscmp(m, WXR_MARKER); }
static void now_str(WCHAR *out) { SYSTEMTIME t; GetLocalTime(&t); _snwprintf(out, 32, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); }

/* ---------------- optional components: environment.ini [components] <name>=1|0 ----------------
   uwp  Windows' own XAML for UWP apps: the [uwp] section of data\files.txt, from the ISO
   gdk  the GDK game runtime (xgameruntime.dll & co.) from Microsoft's Gaming Services package, and each game's own GameInput
        redistributable: both through "wxrpkg gdk" (it downloads; setup never ships Microsoft files)
   msa  the Microsoft account sign-in tool (msa\wxrsignin.exe of the setup bundle -> Layer) and its Start menu entry; with msa=0 the
        layer does not use a signed-in account (rt_msa.c reads the same key) */
static const WCHAR *const k_comp[] = { L"uwp", L"gdk", L"msa" };
#define NCOMP 3
#define COMP_UWP 0
#define COMP_GDK 1
#define COMP_MSA 2
/* "uwp,gdk" / "none" / "" -> want[]; 0 = an unknown name */
static int parse_components(const WCHAR *list, int want[NCOMP]) {
    WCHAR b[256], *t, *ctx = NULL; int i;
    for (i = 0; i < NCOMP; i++) want[i] = 0;
    if (!list) return 1;
    lstrcpynW(b, list, 256);
    for (t = wcstok(b, L", ", &ctx); t; t = wcstok(NULL, L", ", &ctx)) {
        if (!_wcsicmp(t, L"none")) continue;
        for (i = 0; i < NCOMP && _wcsicmp(t, k_comp[i]); i++) ;
        if (i == NCOMP) { err(L"unknown component '%ls' (known: uwp, gdk, msa, none)", t); return 0; }
        want[i] = 1;
    }
    return 1;
}
static int uwp_present(void) { WCHAR p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls\\Windows\\System32\\Windows.UI.Xaml.dll", g_root); return exists(p); }
/* the recorded choice; -1 = not recorded (roots from before the split: uwp then follows the files, the others count as off) */
static int comp_recorded(int i) { WCHAR v[8]; ini_get(L"components", k_comp[i], v, 8); return v[0] ? v[0] == '1' : -1; }
static int comp_on(int i) { int r = comp_recorded(i); return r >= 0 ? r : i == COMP_UWP ? uwp_present() : 0; }

/* layout: name, tier (1 image, 2 layer, 3 state, 0 other) */
static const struct { const WCHAR *dir; int tier; const WCHAR *what; } k_layout[] = {
    { L"Windows",                   1, L"files from the Windows 10 ISO" },
    { L"Windows\\System32",         1, NULL },
    { L"Windows\\SystemResources",  1, NULL },
    { L"Windows\\Globalization",    1, NULL },
    { L"Windows\\Fonts",            1, NULL },
    { L"Windows\\Registry",         1, NULL },
    { L"Layer",                     2, L"WXR's own binaries" },
    { L"WindowsApps",               3, L"installed packages" },
    { L"ProgramData",               3, L"package database, footprint" },
    { L"Users",                     3, L"per-user app data" },
    { L"Logs",                      0, L"logs" },
    { L"Temp",                      0, L"scratch space" },
};
#define NLAYOUT (sizeof k_layout / sizeof k_layout[0])

/* ---------------- footprint: changes made outside the root ----------------
   ProgramData\footprint.txt, one change per line: <tier>\t<type>\t<data>   (tier = image|layer|state)
   types: REGKEY    HKCU\... or HKLM\...      (key is deleted with its subtree)
          REGVALUE  HKCU\...|<value name>     (only that value is deleted)
          FILE      full path                 (file is deleted)
          DIR       full path                 (folder is removed if empty)
          ENVPATH   <variable>|<entry>        (that entry is removed from the user variable HKCU\Environment\<variable>) */
static int env_get(const WCHAR *name, WCHAR *out, DWORD cap, DWORD *type);
static int env_set(const WCHAR *name, const WCHAR *val, DWORD type);
static int path_remove(WCHAR *list, const WCHAR *seg);
/*
   Purge undoes the entries of the purged tier in reverse order and keeps lines it could not undo. */
static int split_hive(const WCHAR *data, HKEY *hive, const WCHAR **sub) {
    if (!wcsncmp(data, L"HKCU\\", 5)) { *hive = HKEY_CURRENT_USER; *sub = data + 5; return **sub != 0; }
    if (!wcsncmp(data, L"HKLM\\", 5)) { *hive = HKEY_LOCAL_MACHINE; *sub = data + 5; return **sub != 0; }
    return 0;
}
static int is_elevated(void);
/* VxKex's own configuration tool: KexCfg.exe /EXE:"<path>" /ENABLE:0|1 (writes VxKex's IFEO entry for that exact path) */
static int kexcfg(const WCHAR *exe, int enable) {
    HKEY k; WCHAR dir[MAX_PATH] = L"", cmd[2 * MAX_PATH + 64]; DWORD sz = sizeof dir, code = 1; STARTUPINFOW si = { sizeof si }; PROCESS_INFORMATION pi;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VXsoft\\VxKex", 0, KEY_READ, &k) == ERROR_SUCCESS) { RegQueryValueExW(k, L"KexDir", NULL, NULL, (BYTE *)dir, &sz); RegCloseKey(k); }
    dir[MAX_PATH - 1] = 0;
    if (!dir[0]) { warn(L"VxKex is not installed (no KexDir)"); return 0; }
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\\KexCfg.exe\" /EXE:\"%ls\" /ENABLE:%d", dir, exe, enable ? 1 : 0);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { warn(L"cannot run %ls\\KexCfg.exe (error %lu)", dir, GetLastError()); return 0; }
    if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); warn(L"KexCfg.exe did not finish"); }
    GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    if (code) warn(L"KexCfg.exe /ENABLE:%d for %ls failed (exit code %lu%ls)", enable, exe, code, is_elevated() ? L"" : L"; run as administrator");
    return code == 0;
}
static int undo_entry(const WCHAR *type, const WCHAR *data) {
    if (!wcscmp(type, L"VXKEX")) {                                  /* VxKex enabled for an exe of ours */
        if (kexcfg(data, 0)) { info(L"  VxKex turned off for %ls", data); return 1; }
        return 0;
    }
    if (!wcscmp(type, L"REGVALUE")) {
        WCHAR key[1024], *bar; HKEY hive; const WCHAR *sub; LONG r;
        lstrcpynW(key, data, 1024); bar = wcschr(key, '|');
        if (!bar || !split_hive(key, &hive, &sub)) { warn(L"footprint: bad registry value '%ls'", data); return 0; }
        *bar = 0;
        r = RegDeleteKeyValueW(hive, sub, bar + 1);
        if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) { info(L"  removed registry value %ls", data); return 1; }
        warn(L"footprint: could not remove registry value %ls (error %ld%ls)", data, r, r == ERROR_ACCESS_DENIED ? L", run as administrator" : L"");
        return 0;
    }
    if (!wcscmp(type, L"REGKEY")) {
        HKEY hive = NULL; const WCHAR *sub = NULL; LONG r;
        if (!split_hive(data, &hive, &sub)) { warn(L"footprint: bad registry key '%ls'", data); return 0; }
        r = RegDeleteTreeW(hive, sub); if (r == ERROR_SUCCESS) r = RegDeleteKeyW(hive, sub);
        if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) { info(L"  removed registry key %ls", data); return 1; }
        warn(L"footprint: could not remove registry key %ls (error %ld%ls)", data, r, r == ERROR_ACCESS_DENIED ? L", run as administrator" : L"");
        return 0;
    }
    if (!wcscmp(type, L"ENVPATH")) {                                /* one entry WXR put into a user environment variable list */
        WCHAR name[64], *bar, cur[8192]; DWORD t;
        lstrcpynW(name, data, 64); bar = wcschr(name, '|'); if (!bar) { warn(L"footprint: bad ENVPATH '%ls'", data); return 0; }
        *bar = 0;
        if (env_get(name, cur, 8192, &t) && path_remove(cur, wcschr(data, '|') + 1) && !env_set(name, cur, t)) { warn(L"footprint: could not update %ls", name); return 0; }
        info(L"  removed %ls from your %ls", wcschr(data, '|') + 1, name); return 1;
    }
    if (!wcscmp(type, L"DIR")) {                                    /* a folder WXR created; left in place if something else is in it now */
        if (RemoveDirectoryW(data) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) { info(L"  removed folder %ls", data); return 1; }
        if (GetLastError() == ERROR_DIR_NOT_EMPTY) { info(L"  kept folder %ls (other files are in it)", data); return 1; }
        warn(L"footprint: could not remove folder %ls (error %lu)", data, GetLastError()); return 0;
    }
    if (!wcscmp(type, L"FILE")) {
        if (DeleteFileW(data) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) { info(L"  removed file %ls", data); return 1; }
        warn(L"footprint: could not remove file %ls (error %lu)", data, GetLastError()); return 0;
    }
    warn(L"footprint: unknown entry type '%ls' (kept)", type); return 0;
}
/* tier NULL = all tiers. returns the number of entries left (not undone) for that tier */
static int footprint_undo(const WCHAR *tier, int *count_only) {
    WCHAR fp[MAX_PATH], tmp[MAX_PATH]; FILE *f; char line[4096]; WCHAR **lines = NULL; int n = 0, cap = 0, i, left = 0, undone = 0;
    _snwprintf(fp, MAX_PATH, L"%ls\\ProgramData\\footprint.txt", g_root);
    f = _wfopen(fp, L"rb"); if (!f) { if (count_only) *count_only = 0; return 0; }
    while (fgets(line, sizeof line, f)) {
        WCHAR w[4096]; size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 4096);
        if (n == cap) { cap = cap ? cap * 2 : 32; lines = (WCHAR **)realloc(lines, cap * sizeof *lines); }
        lines[n++] = _wcsdup(w);
    }
    fclose(f);
    if (count_only) { int c = 0; for (i = 0; i < n; i++) if (lines[i][0] && lines[i][0] != '#') c++; *count_only = c; for (i = 0; i < n; i++) free(lines[i]); free(lines); return c; }
    for (i = n - 1; i >= 0; i--) {                               /* reverse order: undo the newest change first */
        WCHAR copy[4096], *t1, *t2;
        if (!lines[i][0] || lines[i][0] == '#') continue;
        lstrcpynW(copy, lines[i], 4096);
        t1 = wcschr(copy, '\t'); t2 = t1 ? wcschr(t1 + 1, '\t') : NULL;
        if (!t1 || !t2) { warn(L"footprint: malformed line kept: %ls", lines[i]); left++; continue; }
        *t1 = 0; *t2 = 0;
        if (tier && wcscmp(copy, tier)) continue;
        if (undo_entry(t1 + 1, t2 + 1)) { lines[i][0] = 0; undone++; } else left++;
    }
    _snwprintf(tmp, MAX_PATH, L"%ls.new", fp);
    f = _wfopen(tmp, L"wb");
    if (f) {
        for (i = 0; i < n; i++) if (lines[i][0]) { char u8[12288]; WideCharToMultiByte(CP_UTF8, 0, lines[i], -1, u8, sizeof u8, NULL, NULL); fprintf(f, "%s\r\n", u8); }
        fclose(f); MoveFileExW(tmp, fp, MOVEFILE_REPLACE_EXISTING);
    }
    for (i = 0; i < n; i++) free(lines[i]);
    free(lines);
    if (undone || left) info(L"footprint: %d change(s) undone, %d left", undone, left);
    return left;
}

/* ---------------- root selection and checks ---------------- */
static int under(const WCHAR *path, const WCHAR *envvar) {
    WCHAR d[MAX_PATH]; size_t n;
    if (!GetEnvironmentVariableW(envvar, d, MAX_PATH)) return 0;
    n = wcslen(d); while (n && d[n - 1] == '\\') d[--n] = 0;
    return n && !_wcsnicmp(path, d, n) && (path[n] == 0 || path[n] == '\\');
}
static int check_root_path(void) {
    WCHAR drive[4] = { g_root[0], ':', '\\', 0 }, fs[32]; const WCHAR *c; int ok = 1;
    if (!((g_root[0] >= 'A' && g_root[0] <= 'Z') || (g_root[0] >= 'a' && g_root[0] <= 'z')) || g_root[1] != ':' || g_root[2] != '\\') {
        err(L"the root must be a folder on a local drive (like C:\\WXR): %ls", g_root); return 0; }
    if (!g_root[3]) { err(L"the root cannot be a whole drive: %ls", g_root); return 0; }
    for (c = g_root; *c; c++) {
        if (*c == ' ') { err(L"the root path must not contain spaces: %ls", g_root); return 0; }
        if (*c > 126) { err(L"the root path must use plain ASCII characters: %ls", g_root); return 0; }
    }
    if (wcslen(g_root) > ROOT_MAX_LEN) { err(L"the root path is %u characters; keep it to %d or less (Win7 path limit is 260)", (unsigned)wcslen(g_root), ROOT_MAX_LEN); return 0; }
    if (under(g_root, L"SystemRoot") || under(g_root, L"ProgramFiles") || under(g_root, L"ProgramFiles(x86)") || under(g_root, L"ProgramW6432")) {
        err(L"the root must not be inside the Windows or Program Files folders: %ls", g_root); return 0; }
    if (GetDriveTypeW(drive) != DRIVE_FIXED) { err(L"drive %ls is not a fixed local disk", drive); return 0; }
    if (!GetVolumeInformationW(drive, NULL, 0, NULL, NULL, NULL, fs, 32)) { err(L"cannot read drive %ls (error %lu)", drive, GetLastError()); return 0; }
    if (wcscmp(fs, L"NTFS")) { err(L"drive %ls is %ls; WXR needs NTFS", drive, fs); return 0; }
    { ULARGE_INTEGER fr; WCHAR s[32];
      if (GetDiskFreeSpaceExW(drive, &fr, NULL, NULL)) { fmt_size(fr.QuadPart, s); info(L"Drive %ls: NTFS, %ls free", drive, s);
      } }
    return ok;
}
static int pick_root(const WCHAR *arg) {
    WCHAR src[MAX_PATH]; size_t n;
    if (arg) lstrcpynW(src, arg, MAX_PATH);
    else if (!GetEnvironmentVariableW(L"WXR_ROOT", src, MAX_PATH)) lstrcpyW(src, L"C:\\WXR");
    if (!GetFullPathNameW(src, MAX_PATH, g_root, NULL)) { err(L"bad root path: %ls", src); return 0; }
    n = wcslen(g_root); while (n > 3 && g_root[n - 1] == '\\') g_root[--n] = 0;
    join(g_ini, g_root, L"environment.ini");
    return 1;
}
static int need_root(void) {
    if (!is_dir(g_root)) { err(L"no WXR environment at %ls (run: wxrsetup create)", g_root); return 0; }
    if (!is_wxr_root()) { err(L"%ls is not a WXR environment (environment.ini with the WXR marker is missing)", g_root); return 0; }
    log_open(); return 1;
}
static int confirm(const WCHAR *what) {
    char ans[16];
    if (g_yes) return 1;
    wprintf(L"This will delete %ls in %ls.\nType YES to continue: ", what, g_root); fflush(stdout);
    if (!fgets(ans, sizeof ans, stdin)) return 0;
    return !strncmp(ans, "YES", 3);
}
static void user_dir(WCHAR *out) {
    WCHAR u[128]; DWORD n = 128;
    if (!GetUserNameW(u, &n)) lstrcpyW(u, L"user");
    _snwprintf(out, MAX_PATH, L"%ls\\Users\\%ls\\Packages", g_root, u);
}

/* ---------------- helpers for check / extract ---------------- */
static void exe_dir(WCHAR *out) { WCHAR *s; GetModuleFileNameW(NULL, out, MAX_PATH); s = wcsrchr(out, '\\'); if (s) *s = 0; }
static int file_version(const WCHAR *path, WORD v[4]) {
    DWORD h, n = GetFileVersionInfoSizeW(path, &h); BYTE *b; VS_FIXEDFILEINFO *fi; UINT l; int ok = 0;
    if (!n) return 0;
    b = (BYTE *)malloc(n);
    if (GetFileVersionInfoW(path, 0, n, b) && VerQueryValueW(b, L"\\", (void **)&fi, &l) && l >= sizeof *fi) {
        v[0] = HIWORD(fi->dwFileVersionMS); v[1] = LOWORD(fi->dwFileVersionMS); v[2] = HIWORD(fi->dwFileVersionLS); v[3] = LOWORD(fi->dwFileVersionLS); ok = 1; }
    free(b); return ok;
}
static int is_elevated(void) {
    HANDLE t; TOKEN_ELEVATION e; DWORD n; int r = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) { if (GetTokenInformation(t, TokenElevation, &e, sizeof e, &n)) r = e.TokenIsElevated != 0; CloseHandle(t); }
    return r;
}

/* wimlib, loaded from libwim-15.dll next to wxrsetup.exe */
static struct {
    HMODULE dll;
    int  (*global_init)(int);
    void (*global_cleanup)(void);
    int  (*set_print_errors)(bool);
    int  (*open_wim)(const wimlib_tchar *, int, WIMStruct **, wimlib_progress_func_t, void *);
    int  (*get_wim_info)(WIMStruct *, struct wimlib_wim_info *);
    const wimlib_tchar *(*get_image_property)(const WIMStruct *, int, const wimlib_tchar *);
    int  (*extract_paths)(WIMStruct *, int, const wimlib_tchar *, const wimlib_tchar * const *, size_t, int);
    void (*free_wim)(WIMStruct *);
    const wimlib_tchar *(*error_string)(enum wimlib_error_code);
} W;
static int wim_load(WCHAR *why) {
    WCHAR p[MAX_PATH];
    if (W.dll) return 1;
    exe_dir(p); wcscat(p, L"\\libwim-15.dll");
    W.dll = LoadLibraryExW(p, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!W.dll) { _snwprintf(why, 256, L"cannot load %ls (error %lu)", p, GetLastError()); return 0; }
#define WL(f, n) { FARPROC fp_ = GetProcAddress(W.dll, n); if (!fp_) { _snwprintf(why, 256, L"libwim-15.dll has no %hs", n); FreeLibrary(W.dll); W.dll = NULL; return 0; } memcpy(&W.f, &fp_, sizeof fp_); }
    WL(global_init, "wimlib_global_init") WL(global_cleanup, "wimlib_global_cleanup") WL(set_print_errors, "wimlib_set_print_errors")
    WL(open_wim, "wimlib_open_wim_with_progress") WL(get_wim_info, "wimlib_get_wim_info") WL(get_image_property, "wimlib_get_image_property")
    WL(extract_paths, "wimlib_extract_paths") WL(free_wim, "wimlib_free") WL(error_string, "wimlib_get_error_string")
#undef WL
    return 1;
}

/* data files next to the exe: data\files.txt, data\isos.txt. Returns lines (comments/blank removed).
   A line "[name]" starts the section of an optional component; the lines before the first one are section "core".
   sections: NULL = the lines of every section, else a list like L",core,uwp," (names between commas) */
static WCHAR **read_list(const WCHAR *name, const WCHAR *sections, int *count) {
    WCHAR p[MAX_PATH], sec[40] = L",core,"; FILE *f; char line[1024]; WCHAR **v = NULL; int n = 0, cap = 0;
    exe_dir(p); wcscat(p, L"\\data\\"); wcscat(p, name);
    *count = 0;
    if (!(f = _wfopen(p, L"rb"))) return NULL;
    while (fgets(line, sizeof line, f)) {
        WCHAR w[1024]; size_t l = strlen(line); char *s = line;
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ' || line[l - 1] == '\t')) line[--l] = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 1024);
        if (w[0] == '[') { WCHAR *e = wcschr(w, ']'); if (e) *e = 0; _snwprintf(sec, 40, L",%ls,", w + 1); sec[39] = 0; continue; }
        if (sections && !wcsstr(sections, sec)) continue;
        if (n == cap) { cap = cap ? cap * 2 : 64; v = (WCHAR **)realloc(v, cap * sizeof *v); }
        v[n++] = _wcsdup(w);
    }
    fclose(f); *count = n; return v;
}
static void free_list(WCHAR **v, int n) { int i; for (i = 0; i < n; i++) free(v[i]); free(v); }
static const WCHAR *arch_name(const WCHAR *a) { if (!a) return L"?"; if (!wcscmp(a, L"9")) return L"x64"; if (!wcscmp(a, L"0")) return L"x86"; if (!wcscmp(a, L"12")) return L"arm64"; return a; }

/* ---------------- commands ---------------- */
static int g_fail;
static void chk(int ok, const WCHAR *fmt, ...) {          /* ok: 1 pass, 0 fail, -1 warning, 2 note */
    WCHAR m[1024]; va_list ap; va_start(ap, fmt); _vsnwprintf(m, 1023, fmt, ap); m[1023] = 0; va_end(ap);
    if (ok == 0) g_fail++; else if (ok == -1) g_warnings++;
    if (g_lines) { static const WCHAR *k[] = { L"FAIL", L"OK", L"NOTE" }; logk(ok == -1 ? L"WARN" : k[ok], L"%ls", m); return; }
    if (ok == 1) info(L"[ OK ] %ls", m); else if (ok == 0) info(L"[FAIL] %ls", m); else if (ok == -1) info(L"[WARN] %ls", m); else info(L"[NOTE] %ls", m);
}
static int check_root(void);
/* check [system|root]: no argument = both */
static int cmd_check(const WCHAR *which) {
    OSVERSIONINFOEXW ov = { sizeof ov }; LONG (WINAPI *rtl_ver)(OSVERSIONINFOEXW *) = NULL; WCHAR p[MAX_PATH], sys[MAX_PATH], why[256]; WORD v[4]; BOOL wow = FALSE; int n;
    if (which && wcscmp(which, L"system") && wcscmp(which, L"root")) { err(L"check takes 'system', 'root' or nothing"); return 2; }
    if (is_wxr_root()) log_open();
    if (which && !wcscmp(which, L"root")) { check_root(); goto summary; }
    info(L"Checking this computer for WXR (setup %ls)", WXR_SETUP_VERSION);
    { FARPROC fp = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); memcpy(&rtl_ver, &fp, sizeof fp); }
    if (rtl_ver) rtl_ver(&ov);
    if (ov.dwMajorVersion == 6 && ov.dwMinorVersion == 1 && ov.wServicePackMajor >= 1)
        chk(1, L"Windows 7 Service Pack %u (6.1.%lu)", ov.wServicePackMajor, ov.dwBuildNumber);
    else chk(0, L"Windows %lu.%lu.%lu SP%u: WXR needs Windows 7 SP1", ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber, ov.wServicePackMajor);
    IsWow64Process(GetCurrentProcess(), &wow);
    chk(sizeof(void *) == 8 && !wow, L"64-bit Windows");
    GetSystemDirectoryW(sys, MAX_PATH);
    _snwprintf(p, MAX_PATH, L"%ls\\d3d11.dll", sys);
    if (file_version(p, v) && (v[0] > 6 || (v[0] == 6 && v[1] >= 2))) chk(1, L"Platform Update (d3d11.dll %u.%u.%u.%u)", v[0], v[1], v[2], v[3]);
    else chk(0, L"Platform Update for Windows 7 (KB2670838) is not installed: d3d11.dll is too old or missing");
    { HKEY k; WCHAR dir[MAX_PATH] = L""; DWORD sz = sizeof dir;
      _snwprintf(p, MAX_PATH, L"%ls\\KexDll.dll", sys);
      if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VXsoft\\VxKex", 0, KEY_READ, &k) == ERROR_SUCCESS) { RegQueryValueExW(k, L"KexDir", NULL, NULL, (BYTE *)dir, &sz); RegCloseKey(k); }
      if (dir[0] && file_version(p, v)) {
          WCHAR ver[64]; _snwprintf(ver, 64, L"%u.%u.%u.%u", v[0], v[1], v[2], v[3]);
          if (!wcscmp(ver, VXKEX_TESTED)) chk(1, L"VxKex %ls (%ls)", ver, dir);
          else chk(-1, L"VxKex %ls is installed; WXR was tested with %ls", ver, VXKEX_TESTED);
      } else chk(0, L"VxKex is not installed (https://github.com/vxiiduu/VxKex, version %ls)", VXKEX_TESTED);
    }
    if (is_elevated()) chk(1, L"running as administrator");
    else chk(2, L"not running as administrator: create, extract and status work; later steps (register) will need it");
    {   /* PowerShell execution policy (effective order: machine policy, user policy, CurrentUser, LocalMachine; default Restricted) */
        static const struct { HKEY h; const WCHAR *k, *scope; } src[] = {
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\PowerShell", L"machine group policy" },
            { HKEY_CURRENT_USER,  L"SOFTWARE\\Policies\\Microsoft\\Windows\\PowerShell", L"user group policy" },
            { HKEY_CURRENT_USER,  L"SOFTWARE\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell", L"CurrentUser" },
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell", L"LocalMachine" } };
        WCHAR pol[64] = L"Restricted"; const WCHAR *scope = L"default"; unsigned i;
        for (i = 0; i < 4; i++) { HKEY k; WCHAR v[64]; DWORD sz = sizeof v, t;
            if (RegOpenKeyExW(src[i].h, src[i].k, 0, KEY_READ, &k) == ERROR_SUCCESS) {
                int got = RegQueryValueExW(k, L"ExecutionPolicy", NULL, &t, (BYTE *)v, &sz) == ERROR_SUCCESS && t == REG_SZ && v[0]; RegCloseKey(k);
                if (got) { lstrcpynW(pol, v, 64); scope = src[i].scope; break; } } }
        if (!_wcsicmp(pol, L"Restricted") || !_wcsicmp(pol, L"AllSigned"))
            chk(2, L"PowerShell execution policy is %ls (%ls): the Appx cmdlets cannot load. Fix: Set-ExecutionPolicy -Scope CurrentUser RemoteSigned", pol, scope);
        else chk(1, L"PowerShell execution policy %ls (%ls)", pol, scope);
    }
    if (wim_load(why)) chk(1, L"wimlib (libwim-15.dll) loaded"); else chk(0, L"wimlib: %ls", why);
    { WCHAR **l = read_list(L"files.txt", NULL, &n); chk(n > 0, L"data\\files.txt: %d entries", n); free_list(l, n);
      l = read_list(L"isos.txt", NULL, &n); chk(n > 0, L"data\\isos.txt: %d supported image(s)", n); free_list(l, n); }
    if (!which) check_root();
summary:
    info(L"%d problem(s), %d warning(s)", g_fail, g_warnings);
    g_warnings = 0;
    return g_fail ? 1 : 0;
}

/* the install folder: DATA root = existing | new | empty | notempty | file | unusable */
#define NEED_FREE_GB 6       /* extraction copies install.esd (4-5 GB) into <root>\Temp before unpacking */
#define WANT_FREE_GB 10      /* room for apps */
static int check_root(void) {
    WCHAR drive[4] = { g_root[0], ':', '\\', 0 }, s[32], st[64] = L""; ULARGE_INTEGER fr; int extracted = 0;
    info(L"Checking the install folder %ls", g_root);
    data(L"path", g_root);
    if (exists(g_root) && !is_dir(g_root)) { data(L"root", L"file"); chk(0, L"%ls is a file, not a folder", g_root); return 0; }
    if (is_wxr_root()) {
        ini_get(L"image", L"state", st, 64); extracted = !wcscmp(st, L"extracted");
        data(L"root", L"existing"); data(L"image", st[0] ? st : L"none");
        { int i; for (i = 0; i < NCOMP; i++) { WCHAR k[32]; _snwprintf(k, 32, L"comp.%ls", k_comp[i]); data(k, comp_on(i) ? L"1" : L"0"); }
          data(L"uwpfiles", uwp_present() ? L"1" : L"0"); }
        chk(1, L"%ls holds a WXR installation (image: %ls): setup repairs or updates it", g_root, st[0] ? st : L"none");
    } else if (is_dir(g_root) && !dir_empty(g_root)) {
        data(L"root", L"notempty"); chk(0, L"%ls already exists and has other files in it: choose an empty or new folder", g_root); return 0;
    } else {
        int empty = is_dir(g_root);
        if (!check_root_path()) { data(L"root", L"unusable"); chk(0, L"%ls cannot be used as the install folder", g_root); return 0; }
        data(L"root", empty ? L"empty" : L"new");
        chk(1, empty ? L"%ls is empty and can be used" : L"%ls will be created", g_root);
    }
    if (GetDiskFreeSpaceExW(drive, &fr, NULL, NULL)) {
        ULONGLONG gb = fr.QuadPart >> 30; WCHAR n[32]; fmt_size(fr.QuadPart, s); _snwprintf(n, 32, L"%I64u", fr.QuadPart); data(L"free", n);
        if (!extracted && gb < NEED_FREE_GB) { chk(0, L"only %ls free on %ls; setup needs at least %d GB while it unpacks the Windows 10 files", s, drive, NEED_FREE_GB); return 0; }
        if (gb < WANT_FREE_GB) chk(-1, L"only %ls free on %ls; apps need room too (%d GB or more recommended)", s, drive, WANT_FREE_GB);
        else chk(1, L"%ls free on %ls", s, drive);
    }
    return 1;
}

/* ---------------- inspect: what is in an ISO / install.esd / install.wim, without extracting it ----------------
   Reads the WIM header and the XML metadata resource (uncompressed UTF-16 in WIM and ESD files) straight from the file,
   inside an ISO through the UDF reader: no 4 GB copy. */
typedef struct { Udf u; UdfFile f; HANDLE h; int iso; } Src;
static int src_read(Src *s, ULONGLONG off, void *buf, DWORD n) {
    if (s->iso) return udf_read(&s->u, &s->f, off, buf, n);
    { LARGE_INTEGER p; DWORD got = 0; p.QuadPart = (LONGLONG)off; return SetFilePointerEx(s->h, p, NULL, FILE_BEGIN) && ReadFile(s->h, buf, n, &got, NULL) && got == n; }
}
/* text of <tag>...</tag> inside [from, to) of the XML, or "" */
static void xml_tag(const WCHAR *from, const WCHAR *to, const WCHAR *tag, WCHAR *out, int cap) {
    WCHAR open[64], close[64]; const WCHAR *a, *b; int n;
    _snwprintf(open, 64, L"<%ls>", tag); _snwprintf(close, 64, L"</%ls>", tag); out[0] = 0;
    a = wcsstr(from, open); if (!a || a >= to) return; a += wcslen(open);
    b = wcsstr(a, close); if (!b || b > to) return;
    n = (int)(b - a); if (n >= cap) n = cap - 1; wcsncpy(out, a, n); out[n] = 0;
}
static int cmd_inspect(const WCHAR *path) {
    WCHAR p[MAX_PATH], why[256], **isos = NULL; const WCHAR *ext; Src s; BYTE hdr[208]; WCHAR *xml = NULL; int nisos = 0, rc = 1, i, pro = 0, best = 0;
    ULONGLONG xoff, xlen; WCHAR verdict_build[64] = L"";
    memset(&s, 0, sizeof s); s.h = INVALID_HANDLE_VALUE;
    if (!path) { err(L"inspect needs a file (.iso, .esd or .wim)"); return 2; }
    if (!GetFullPathNameW(path, MAX_PATH, p, NULL) || !exists(p) || is_dir(p)) { chk(0, L"file not found: %ls", path); return 1; }
    isos = read_list(L"isos.txt", NULL, &nisos);
    ext = wcsrchr(p, '.');
    if (ext && !_wcsicmp(ext, L".iso")) {
        static const WCHAR *cand[] = { L"\\sources\\install.esd", L"\\sources\\install.wim", L"\\x64\\sources\\install.esd", L"\\x64\\sources\\install.wim" };
        unsigned c;
        if (!udf_open(&s.u, p, why)) { chk(0, L"this is not a Windows installation ISO (%ls)", why); goto out; }
        s.iso = 1;
        for (c = 0; c < 4; c++) if (udf_find(&s.u, cand[c], &s.f) && !s.f.is_dir) break;
        if (c == 4) { chk(0, L"this ISO has no sources\\install.esd or install.wim: it is not a Windows installation ISO"); goto out; }
        info(L"Found %ls in the ISO", cand[c]);
    } else if (ext && (!_wcsicmp(ext, L".esd") || !_wcsicmp(ext, L".wim"))) {
        s.h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (s.h == INVALID_HANDLE_VALUE) { chk(0, L"cannot open %ls (error %lu)", p, GetLastError()); goto out; }
    } else { chk(0, L"%ls is not an .iso, .esd or .wim file", p); goto out; }
    if (!src_read(&s, 0, hdr, sizeof hdr) || memcmp(hdr, "MSWIM\0\0\0", 8)) { chk(0, L"the Windows image inside is not readable (no WIM header)"); goto out; }
    xlen = 0; memcpy(&xlen, hdr + 72, 7); memcpy(&xoff, hdr + 80, 8);       /* rhXmlData: 7-byte size, flags, offset */
    if (!xlen || xlen > (16u << 20) || (xlen & 1)) { chk(0, L"the Windows image has no readable description"); goto out; }
    xml = (WCHAR *)calloc((size_t)xlen / 2 + 1, sizeof(WCHAR));
    if (!src_read(&s, xoff, xml, (DWORD)xlen)) { chk(0, L"cannot read the image description"); goto out; }
    {   const WCHAR *cur = xml[0] == 0xFEFF ? xml + 1 : xml, *next;
        int n = 0;
        while ((cur = wcsstr(cur, L"<IMAGE INDEX=\"")) != NULL) {
            WCHAR ed[64], nm[128], b[16], sp[16], ar[8], id[48]; int known = 0, idx = _wtoi(cur + 14);
            next = wcsstr(cur + 1, L"<IMAGE INDEX=\""); if (!next) next = cur + wcslen(cur);
            xml_tag(cur, next, L"EDITIONID", ed, 64); xml_tag(cur, next, L"NAME", nm, 128); xml_tag(cur, next, L"BUILD", b, 16);
            xml_tag(cur, next, L"SPBUILD", sp, 16); xml_tag(cur, next, L"ARCH", ar, 8);
            _snwprintf(id, 48, L"%ls.%ls", b, sp);
            for (i = 0; i < nisos; i++) { WCHAR a[64], r[32]; if (swscanf(isos[i], L"%63ls %31ls", a, r) == 2 && !wcscmp(a, id) && !wcscmp(r, arch_name(ar))) known = 1; }
            info(L"  %d  %-22ls %ls, build %ls, %ls", idx, ed, nm, id, arch_name(ar));
            if (!_wcsicmp(ed, L"Professional")) { pro = 1; lstrcpynW(verdict_build, id, 64); best = known ? 2 : (!wcscmp(b, L"19041") && !wcscmp(arch_name(ar), L"x64") ? 1 : 0); }
            else if (!verdict_build[0]) { lstrcpynW(verdict_build, id, 64); best = known ? 2 : (!wcscmp(b, L"19041") && !wcscmp(arch_name(ar), L"x64") ? 1 : 0); }
            n++; cur = next;
        }
        if (!n) { chk(0, L"the image lists no Windows editions"); goto out; }
        data(L"build", verdict_build); data(L"edition", pro ? L"Professional" : L"first");
        if (best == 2) chk(1, L"Windows 10 build %ls x64: supported", verdict_build);
        else if (best == 1) { data(L"anybuild", L"1"); chk(-1, L"Windows 10 build %ls x64 is newer or older than the tested build; setup can try it", verdict_build); }
        else chk(0, L"build %ls is not a 64-bit Windows 10 version 2004-22H2 image (build 19041.x)", verdict_build);
        if (!pro && best) chk(-1, L"no Professional edition in the image; setup uses the first one");
        rc = best ? 0 : 1;
    }
out:
    free(xml); free_list(isos, nisos);
    if (s.iso) udf_close(&s.u);
    if (s.h != INVALID_HANDLE_VALUE) CloseHandle(s.h);
    return rc;
}

/* ---------------- register: changes outside the root ---------------- */
static void footprint_add(const WCHAR *tier, const WCHAR *type, const WCHAR *data) {
    WCHAR fp[MAX_PATH], line[2048]; char u8[6144]; FILE *f;
    _snwprintf(fp, MAX_PATH, L"%ls\\ProgramData\\footprint.txt", g_root);
    _snwprintf(line, 2048, L"%ls\t%ls\t%ls", tier, type, data);
    WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof u8, NULL, NULL);
    if ((f = _wfopen(fp, L"ab"))) { fprintf(f, "%s\r\n", u8); fclose(f); }
}
static int footprint_has(const WCHAR *type, const WCHAR *data) {
    WCHAR fp[MAX_PATH], w[4096], want[2100]; char line[4096]; FILE *f; int found = 0;
    _snwprintf(fp, MAX_PATH, L"%ls\\ProgramData\\footprint.txt", g_root);
    _snwprintf(want, 2100, L"\t%ls\t%ls", type, data);
    if (!(f = _wfopen(fp, L"rb"))) return 0;
    while (!found && fgets(line, sizeof line, f)) {
        size_t l = strlen(line); WCHAR *tab;
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 4096);
        if ((tab = wcschr(w, '\t')) && !_wcsicmp(tab, want)) found = 1;
    }
    fclose(f); return found;
}
/* WXR_REG_REDIRECT=HKCU\Software\X (testing): every target "HKLM\A" / "HKCU\A" becomes "HKCU\Software\X\HKLM\A" */
static int g_redirect;
static void reg_target(const WCHAR *want, WCHAR *out) {
    WCHAR r[512];
    if (GetEnvironmentVariableW(L"WXR_REG_REDIRECT", r, 512) && !wcsncmp(r, L"HKCU\\", 5)) { g_redirect = 1; _snwprintf(out, 1024, L"%ls\\%ls", r, want); }
    else lstrcpynW(out, want, 1024);
}
static int key_exists(const WCHAR *full) {
    HKEY hive, k; const WCHAR *sub;
    if (!split_hive(full, &hive, &sub) || RegOpenKeyExW(hive, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    RegCloseKey(k); return 1;
}
enum { REG_DONE, REG_ALREADY, REG_FOREIGN, REG_FAILED };
static const WCHAR *k_regres[] = { L"done", L"already registered", L"left alone", L"FAILED" };

/* empty marker key */
static int reg_marker(const WCHAR *want) {
    WCHAR t[1024]; HKEY hive, k; const WCHAR *sub; LONG r;
    reg_target(want, t);
    int ours = footprint_has(L"REGKEY", t);
    if (ours && key_exists(t)) return REG_ALREADY;
    if (!ours && key_exists(t)) { info(L"  %ls exists already and was not made by WXR: left as it is", t); return REG_FOREIGN; }
    split_hive(t, &hive, &sub);                            /* missing (or recorded but deleted since): create it */
    if ((r = RegCreateKeyExW(hive, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) != ERROR_SUCCESS) { warn(L"  cannot create %ls (error %ld)", t, r); return REG_FAILED; }
    RegCloseKey(k); if (!ours) footprint_add(L"image", L"REGKEY", t); info(L"  created %ls", t); return REG_DONE;
}
/* a key copied from the image's SOFTWARE hive */
static int reg_from_hive(Hive *hv, const WCHAR *hive_path, const WCHAR *want) {
    WCHAR t[1024]; HKEY hive, k; const WCHAR *sub; LONG r; DWORD nk; unsigned keys = 1, values = 0;
    reg_target(want, t);
    if (footprint_has(L"REGKEY", t) && key_exists(t)) return REG_ALREADY;
    if (key_exists(t)) { info(L"  %ls exists already and was not made by WXR: left as it is", t); return REG_FOREIGN; }
    if (!hive_find(hv, hive_path, &nk)) { warn(L"  %ls", hv->err); return REG_FAILED; }
    split_hive(t, &hive, &sub);
    if ((r = RegCreateKeyExW(hive, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) != ERROR_SUCCESS) {
        warn(L"  cannot create %ls (error %ld%ls)", t, r, r == ERROR_ACCESS_DENIED ? L": run as administrator" : L""); return REG_FAILED; }
    if (!footprint_has(L"REGKEY", t)) footprint_add(L"image", L"REGKEY", t);   /* recorded before copying: a half copy is still undone by purge */
    if (!hive_copy(hv, nk, k, &keys, &values)) { RegCloseKey(k); warn(L"  copying into %ls failed: %ls", t, hv->err); return REG_FAILED; }
    RegCloseKey(k); info(L"  copied %ls from the image (%u keys, %u values)", t, keys, values); return REG_DONE;
}
/* a string value */
static int reg_value(const WCHAR *want_key, const WCHAR *name, const WCHAR *data) {
    WCHAR t[1024], id[1300], cur[1024]; HKEY hive, k; const WCHAR *sub; LONG r; DWORD sz = sizeof cur, type;
    reg_target(want_key, t); _snwprintf(id, 1300, L"%ls|%ls", t, name);
    split_hive(t, &hive, &sub);
    cur[0] = 0;
    if (RegOpenKeyExW(hive, sub, 0, KEY_READ, &k) == ERROR_SUCCESS) { if (RegQueryValueExW(k, name, NULL, &type, (BYTE *)cur, &sz) != ERROR_SUCCESS) cur[0] = 0; else cur[1023] = 0; RegCloseKey(k); }
    if (cur[0] && footprint_has(L"REGVALUE", id) && !_wcsicmp(cur, data)) return REG_ALREADY;
    if (cur[0] && !footprint_has(L"REGVALUE", id)) { info(L"  '%ls' is already set to %ls (not by WXR): left as it is", name, cur); return REG_FOREIGN; }
    if ((r = RegCreateKeyExW(hive, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) != ERROR_SUCCESS) { warn(L"  cannot open %ls (error %ld)", t, r); return REG_FAILED; }
    r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)data, (DWORD)(wcslen(data) + 1) * sizeof(WCHAR)); RegCloseKey(k);
    if (r != ERROR_SUCCESS) { warn(L"  cannot set '%ls' (error %ld)", name, r); return REG_FAILED; }
    if (!footprint_has(L"REGVALUE", id)) footprint_add(L"image", L"REGVALUE", id);
    info(L"  set '%ls' = %ls", name, data); return REG_DONE;
}
static int cmd_register(void) {
    WCHAR st[64], p[MAX_PATH], font[MAX_PATH], rd[512]; Hive hv; int res[4], i, failed = 0, elevated = is_elevated();
    if (!need_root()) return 1;
    ini_get(L"image", L"state", st, 64);
    if (wcscmp(st, L"extracted")) { err(L"extract the Windows 10 image first (wxrsetup extract <iso>)"); return 1; }
    if (GetEnvironmentVariableW(L"WXR_REG_REDIRECT", rd, 512)) warn(L"WXR_REG_REDIRECT is set: all registry changes go under %ls (test mode)", rd);
    info(L"Registering WXR with Windows (changes outside %ls are recorded in ProgramData\\footprint.txt)", g_root);
    _snwprintf(p, MAX_PATH, L"%ls\\Windows\\System32\\config\\SOFTWARE", g_root);
    if (!hive_open(&hv, p)) { err(L"%ls", hv.err); return 1; }

    info(L"1. Media Foundation marker (Win10 mfplat's MFStartup checks for it)");
    res[0] = reg_marker(L"HKCU\\Software\\Classes\\CLSID\\{e79167d7-1b85-4d78-b603-798e0e1a4c67}");

    info(L"2. 'FileItemAPIs' folder type (Win10 file queries; Explorer never uses it)");
    reg_target(L"HKLM\\x", p);
    if (!elevated && !g_redirect && !key_exists(L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderTypes\\{B372207C-0011-438F-9151-098B2E36B887}")) {
        warn(L"  needs administrator rights: run 'wxrsetup register' again from an elevated command prompt"); res[1] = REG_FAILED; }
    else res[1] = reg_from_hive(&hv, L"Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderTypes\\{B372207C-0011-438F-9151-098B2E36B887}",
                                L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FolderTypes\\{B372207C-0011-438F-9151-098B2E36B887}");
    hive_close(&hv);

    info(L"3. Segoe MDL2 Assets icon font (per user)");
    _snwprintf(font, MAX_PATH, L"%ls\\Windows\\Fonts\\segmdl2.ttf", g_root);
    if (!exists(font)) { warn(L"  %ls is missing; extract the image again", font); res[2] = REG_FAILED; }
    else res[2] = reg_value(L"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", L"Segoe MDL2 Assets (TrueType)", font);

    info(L"4. VxKex for the WXR broker (apps inherit it from the broker)");
    _snwprintf(p, MAX_PATH, L"%ls\\Layer\\wxrsvc.exe", g_root);
    if (!exists(p)) { warn(L"  %ls is missing; install the layer first (wxrsetup install-layer)", p); res[3] = REG_FAILED; }
    else if (g_redirect) { info(L"  skipped (WXR_REG_REDIRECT test mode)"); res[3] = REG_ALREADY; }
    else if (!elevated) { warn(L"  needs administrator rights: run 'wxrsetup register' again from an elevated command prompt"); res[3] = REG_FAILED; }
    else if (kexcfg(p, 1)) { if (!footprint_has(L"VXKEX", p)) footprint_add(L"layer", L"VXKEX", p); info(L"  VxKex enabled for %ls", p); res[3] = REG_DONE; }
    else res[3] = REG_FAILED;

    info(L"Summary:");
    { static const WCHAR *names[] = { L"Media Foundation marker", L"FileItemAPIs folder type", L"Segoe MDL2 Assets font", L"VxKex for the broker" };
      for (i = 0; i < 4; i++) { info(L"  %-26ls %ls", names[i], k_regres[res[i]]); if (res[i] == REG_FAILED) failed++; } }
    ini_set(L"register", L"state", failed ? L"partial" : L"done");
    { WCHAR t[32]; now_str(t); ini_set(L"register", L"updated", t); }
    return failed ? 1 : 0;
}

/* ---------------- install-layer: our binaries into <root>\Layer ---------------- */
static int copy_tree(const WCHAR *src, const WCHAR *dst, unsigned *n) {
    WCHAR pat[MAX_PATH], s[MAX_PATH], d[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; int ok = 1;
    if (!mkdirs(dst)) { err(L"cannot create %ls (error %lu)", dst, GetLastError()); return 0; }
    join(pat, src, L"*"); h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        join(s, src, fd.cFileName); join(d, dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = copy_tree(s, d, n);
        else if (CopyFileW(s, d, FALSE)) (*n)++;
        else { err(L"cannot copy %ls (error %lu)", s, GetLastError()); ok = 0; }
    } while (ok && FindNextFileW(h, &fd));
    FindClose(h); return ok;
}
/* <root>\Layer\Modules goes to the front of the user's PSModulePath (HKCU\Environment), so PowerShell finds the Appx
   cmdlets by itself (module auto-loading). PowerShell 5.1 adds the system module folders back, even when a login hands it
   only the user value (tested). Footprint: tier "layer", type ENVPATH "PSModulePath|<folder>". */
static void env_broadcast(void) { DWORD_PTR r; SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, &r); }
static int env_get(const WCHAR *name, WCHAR *out, DWORD cap, DWORD *type) {
    HKEY k; DWORD sz = cap * sizeof(WCHAR); LONG r; out[0] = 0; *type = REG_EXPAND_SZ;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    r = RegQueryValueExW(k, name, NULL, type, (BYTE *)out, &sz); RegCloseKey(k);
    if (r != ERROR_SUCCESS || (*type != REG_SZ && *type != REG_EXPAND_SZ)) { out[0] = 0; *type = REG_EXPAND_SZ; return 0; }
    out[cap - 1] = 0; return 1;
}
static int env_set(const WCHAR *name, const WCHAR *val, DWORD type) {
    HKEY k; LONG r;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return 0;
    r = val[0] ? RegSetValueExW(k, name, 0, type, (const BYTE *)val, (DWORD)(wcslen(val) + 1) * sizeof(WCHAR)) : RegDeleteValueW(k, name);
    RegCloseKey(k); env_broadcast(); return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}
/* removes every ';'-separated entry equal to seg (case-insensitive, trailing '\' ignored) */
static int path_remove(WCHAR *list, const WCHAR *seg) {
    WCHAR out[8192] = L"", *tok, *ctx = NULL, copy[8192]; int removed = 0; size_t sl = wcslen(seg);
    lstrcpynW(copy, list, 8192);
    for (tok = wcstok(copy, L";", &ctx); tok; tok = wcstok(NULL, L";", &ctx)) {
        size_t tl = wcslen(tok); while (tl && tok[tl - 1] == '\\') tok[--tl] = 0;
        if (tl == sl && !_wcsicmp(tok, seg)) { removed = 1; continue; }
        if (!*tok) continue;
        if (out[0]) wcscat(out, L";");
        if (wcslen(out) + tl + 2 < 8192) wcscat(out, tok);
    }
    lstrcpyW(list, out); return removed;
}
static int ps_module_path(void) {
    WCHAR seg[MAX_PATH], cur[8192], nv[8192], id[MAX_PATH + 16]; DWORD type;
    _snwprintf(seg, MAX_PATH, L"%ls\\Layer\\Modules", g_root); _snwprintf(id, MAX_PATH + 16, L"PSModulePath|%ls", seg);
    env_get(L"PSModulePath", cur, 8192, &type);
    path_remove(cur, seg);                                         /* ours goes to the front (the newest environment wins) */
    _snwprintf(nv, 8192, L"%ls%ls%ls", seg, cur[0] ? L";" : L"", cur);
    if (!env_set(L"PSModulePath", nv, type)) { warn(L"cannot update your PSModulePath: the Appx cmdlets are not made available everywhere"); return 0; }
    if (!footprint_has(L"ENVPATH", id)) footprint_add(L"layer", L"ENVPATH", id);
    info(L"Appx cmdlets are now available in every new PowerShell window (added %ls to your PSModulePath)", seg);
    return 1;
}
static int tool_lnk(const WCHAR *name, const WCHAR *exe, const WCHAR *desc);   /* below */
static int cmd_install_layer(const WCHAR *src) {
    WCHAR full[MAX_PATH], p[MAX_PATH], ver[128] = L"", *l; RmStat rs = {0}; unsigned n = 0; FILE *f;
    if (!src) { err(L"install-layer needs the folder of a built layer (for example E:\\WXR\\out\\layer)"); return 2; }
    if (!need_root()) return 1;
    if (!GetFullPathNameW(src, MAX_PATH, full, NULL) || !is_dir(full)) { err(L"%ls is not a folder", src); return 1; }
    join(p, full, L"wxrpkg.exe"); if (!exists(p)) { err(L"%ls does not look like a WXR layer (no wxrpkg.exe)", full); return 1; }
    join(p, full, L"layer.txt");
    if ((f = _wfopen(p, L"rb"))) { char b[128] = ""; if (fgets(b, sizeof b, f)) { size_t k = strlen(b); while (k && (b[k - 1] == '\r' || b[k - 1] == '\n')) b[--k] = 0; MultiByteToWideChar(CP_UTF8, 0, b, -1, ver, 128); } fclose(f); }
    if (ver[0]) info(L"Installing the WXR layer from %ls (%ls)", full, ver); else info(L"Installing the WXR layer from %ls", full);
    join(p, g_root, L"Layer"); l = lp(p); rm_contents(l, NULL, &rs); free(l);
    if (rs.failed) { err(L"the old layer could not be removed completely (files in use?)"); return 1; }
    if (!copy_tree(full, p, &n)) return 1;
    ini_set(L"layer", L"state", L"installed"); ini_set(L"layer", L"version", ver); ini_set(L"layer", L"source", full);
    info(L"Layer installed: %u files in %ls", n, p);
    {   /* WXR's own tools in the Start menu (the sign-in tool is the msa component's: wxrsetup components) */
        WCHAR t[MAX_PATH]; join(t, p, L"wxruninstall.exe");
        if (exists(t)) { if (tool_lnk(L"Uninstall apps", t, L"Uninstall apps and packages installed in WXR")) info(L"Start menu: WXR > Tools > Uninstall apps"); else warn(L"could not create the Start menu entry for %ls", t); }
    }
    if (!ps_module_path()) info(L"Appx cmdlets: run %ls\\wxr-powershell.cmd (or Import-Module %ls\\Modules\\Appx)", p, p);
    return 0;
}

/* ---------------- install-setup: the installer itself into <root>\Setup + the Programs and Features entry ----------------
   <bundle> is the folder the GUI installer runs from (WxrInstaller.exe, wxrsetup.exe, libwim-15.dll, data\, licenses\; its
   layer\ subfolder is not copied: that is <root>\Layer already). Footprint: tier "setup", REGKEY of the uninstall entry. */
/* one entry per installation: Uninstall\WXR_<root with ":\" -> "_"> (several roots can be installed side by side) */
static void arp_key(WCHAR *out) {
    WCHAR r[MAX_PATH]; WCHAR *c; lstrcpynW(r, g_root, MAX_PATH);
    for (c = r; *c; c++) if (*c == ':' || *c == '\\' || *c == ' ') *c = '_';
    _snwprintf(out, 512, L"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WXR_%ls", r);
}
static int cmd_install_setup(const WCHAR *bundle) {
    WCHAR arp[512], full[MAX_PATH], dst[MAX_PATH], pat[MAX_PATH], s[MAX_PATH], d[MAX_PATH], exe[MAX_PATH], v[MAX_PATH * 2]; WIN32_FIND_DATAW fd; HANDLE h; unsigned n = 0;
    HKEY hive, k; const WCHAR *sub; LONG r; DWORD one = 1, kb;
    if (!bundle) { err(L"install-setup needs the folder of the setup program"); return 2; }
    if (!need_root()) return 1;
    if (!GetFullPathNameW(bundle, MAX_PATH, full, NULL) || !is_dir(full)) { err(L"%ls is not a folder", bundle); return 1; }
    join(exe, full, L"WxrInstaller.exe"); if (!exists(exe)) { err(L"%ls has no WxrInstaller.exe", full); return 1; }
    join(dst, g_root, L"Setup");
    if (_wcsicmp(full, dst)) {                                    /* running from <root>\Setup already (Modify): nothing to copy */
        if (!mkdirs(dst)) { err(L"cannot create %ls (error %lu)", dst, GetLastError()); return 1; }
        join(pat, full, L"*"); h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || !_wcsicmp(fd.cFileName, L"layer")) continue;
                join(s, full, fd.cFileName); join(d, dst, fd.cFileName);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!copy_tree(s, d, &n)) { FindClose(h); return 1; } }
                else if (CopyFileW(s, d, FALSE)) n++;
                else { err(L"cannot copy %ls (error %lu)", s, GetLastError()); FindClose(h); return 1; }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        info(L"Setup copied to %ls (%u files)", dst, n);
    }
    join(exe, dst, L"WxrInstaller.exe");
    arp_key(arp);
    split_hive(arp, &hive, &sub);
    if ((r = RegCreateKeyExW(hive, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL)) != ERROR_SUCCESS) {
        err(L"cannot add WXR to Programs and Features (error %ld%ls)", r, r == ERROR_ACCESS_DENIED ? L": run as administrator" : L""); return 1; }
    if (!footprint_has(L"REGKEY", arp)) footprint_add(L"setup", L"REGKEY", arp);
#define SETS(name, val) RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)(val), (DWORD)(wcslen(val) + 1) * sizeof(WCHAR))
    if (!_wcsicmp(g_root, L"C:\\WXR")) SETS(L"DisplayName", L"WXR"); else { _snwprintf(v, MAX_PATH * 2, L"WXR (%ls)", g_root); SETS(L"DisplayName", v); } SETS(L"DisplayVersion", WXR_SETUP_VERSION); SETS(L"Publisher", L"WXR project");
    SETS(L"InstallLocation", g_root); SETS(L"DisplayIcon", exe);
    _snwprintf(v, MAX_PATH * 2, L"\"%ls\" /uninstall /root \"%ls\"", exe, g_root); SETS(L"UninstallString", v);
    _snwprintf(v, MAX_PATH * 2, L"\"%ls\" /modify /root \"%ls\"", exe, g_root); SETS(L"ModifyPath", v);
#undef SETS
    RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, (const BYTE *)&one, sizeof one);   /* repair lives in Change */
    { TreeStat ts = {0}; WCHAR *l = lp(g_root); tree_stat(l, &ts); free(l); kb = (DWORD)(ts.bytes >> 10); RegSetValueExW(k, L"EstimatedSize", 0, REG_DWORD, (const BYTE *)&kb, sizeof kb); }
    RegCloseKey(k);
    info(L"WXR is listed in Programs and Features (Uninstall / Change run %ls)", exe);
    return 0;
}

static int g_last_pct = -1;
static void show_pct(const WCHAR *what, ULONGLONG done, ULONGLONG total) {
    static int console = -1; int pct = total ? (int)(done * 100 / total) : 100; DWORD mode;
    if (g_lines) { if (pct != g_last_pct) { g_last_pct = pct; wprintf(L"PROGRESS\t%d\t%ls\n", pct, what); fflush(stdout); } return; }
    if (console < 0) console = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) ? 1 : 0;
    if (!console) pct = pct / 25 * 25;                  /* redirected output: 0/25/50/75/100 on separate lines */
    if (pct == g_last_pct) return;
    g_last_pct = pct;
    if (console) { wprintf(L"\r  %ls: %3d%%", what, pct); if (pct == 100) wprintf(L"\n"); }
    else wprintf(L"  %ls: %3d%%\n", what, pct);
    fflush(stdout);
}
static void copy_progress(ULONGLONG done, ULONGLONG total, void *ctx) { (void)ctx; show_pct(L"copying from the ISO", done, total); }
static enum wimlib_progress_status wim_progress(enum wimlib_progress_msg m, union wimlib_progress_info *i, void *ctx) {
    (void)ctx;
    if (m == WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS) show_pct(L"extracting", i->extract.completed_bytes, i->extract.total_bytes);
    return WIMLIB_PROGRESS_STATUS_CONTINUE;
}
/* sections: which parts of data\files.txt (L",core,uwp," form). adding = 0: the whole image (state none -> extracted);
   adding = 1: more sections into an extracted image (a component turned on later), the image state stays as it is */
static int cmd_extract(const WCHAR *src, const WCHAR *edition, int keep_temp, int any_build, const WCHAR *sections, int adding) {
    WCHAR why[256], tmp[MAX_PATH] = L"", st[64], p[MAX_PATH], t[32], buildid[64], s1[32], ed0[64];
    WCHAR **files = NULL, **isos = NULL; int nfiles = 0, nisos = 0, i, image = 0, rc = 1, temp_made = 0, missing = 0;
    WIMStruct *wim = NULL; struct wimlib_wim_info wi; const WCHAR *ext; DWORD t0;
    if (!src) { err(L"extract needs the Windows 10 ISO (or an install.esd / install.wim)"); return 2; }
    if (!need_root()) return 1;
    ini_get(L"image", L"state", st, 64);
    if (!adding && !wcscmp(st, L"extracted")) { err(L"the image is already extracted; run 'wxrsetup purge image' first to extract again"); return 1; }
    if (adding && wcscmp(st, L"extracted")) { err(L"the image is not extracted yet (wxrsetup extract <iso>)"); return 1; }
    if (adding && !edition) { ini_get(L"image", L"edition", ed0, 64); if (ed0[0]) edition = ed0; }   /* the same edition as the rest */
    if (!wim_load(why)) { err(L"wimlib: %ls", why); return 1; }
    files = read_list(L"files.txt", sections, &nfiles); isos = read_list(L"isos.txt", NULL, &nisos);
    if (!nfiles || !nisos) { err(L"data\\files.txt or data\\isos.txt is missing next to wxrsetup.exe"); goto out; }
    if (!GetFullPathNameW(src, MAX_PATH, p, NULL) || !exists(p)) { err(L"cannot find %ls", src); goto out; }
    info(L"Extracting Windows 10 files into %ls\\Windows", g_root);
    info(L"Source: %ls", p);
    ext = wcsrchr(p, '.');
    if (ext && !_wcsicmp(ext, L".iso")) {
        static const WCHAR *cand[] = { L"\\sources\\install.esd", L"\\sources\\install.wim", L"\\x64\\sources\\install.esd", L"\\x64\\sources\\install.wim" };
        Udf u; UdfFile f; unsigned c; ULARGE_INTEGER fr; WCHAR drive[4] = { g_root[0], ':', '\\', 0 };
        if (!udf_open(&u, p, why)) { err(L"%ls: %ls", p, why); goto out; }
        for (c = 0; c < 4; c++) if (udf_find(&u, cand[c], &f) && !f.is_dir) break;
        if (c == 4) { err(L"no sources\\install.esd or install.wim in the ISO"); udf_close(&u); goto out; }
        fmt_size(f.size, s1); info(L"Found %ls in the ISO (%ls)", cand[c], s1);
        if (GetDiskFreeSpaceExW(drive, &fr, NULL, NULL) && fr.QuadPart < f.size + (1ull << 30)) { err(L"not enough free space on %ls to copy it out (need %ls + 1 GB)", drive, s1); udf_close(&u); goto out; }
        _snwprintf(tmp, MAX_PATH, L"%ls\\Temp\\%ls", g_root, wcsrchr(cand[c], '\\') + 1);
        t0 = GetTickCount(); g_last_pct = -1; temp_made = 1;
        if (!udf_copy(&u, &f, tmp, copy_progress, NULL, why)) { wprintf(L"\n"); err(L"%ls", why); udf_close(&u); goto out; }
        udf_close(&u);
        info(L"Copied to %ls in %lu s", tmp, (GetTickCount() - t0) / 1000);
    } else if (ext && (!_wcsicmp(ext, L".esd") || !_wcsicmp(ext, L".wim"))) {
        lstrcpynW(tmp, p, MAX_PATH);
    } else { err(L"%ls is not an .iso, .esd or .wim file", p); goto out; }

    W.global_init(WIMLIB_INIT_FLAG_DONT_ACQUIRE_PRIVILEGES);
    W.set_print_errors(true);
    if ((i = W.open_wim(tmp, 0, &wim, wim_progress, NULL)) != 0) { err(L"wimlib cannot open %ls: %ls", tmp, W.error_string((enum wimlib_error_code)i)); goto out; }
    W.get_wim_info(wim, &wi);
    info(L"The image file holds %u edition(s):", wi.image_count);
    for (i = 1; i <= (int)wi.image_count; i++) {
        const WCHAR *ed = W.get_image_property(wim, i, L"WINDOWS/EDITIONID"), *nm = W.get_image_property(wim, i, L"NAME");
        info(L"  %d  %-22ls %ls", i, ed ? ed : L"?", nm ? nm : L"");
        if (ed && !_wcsicmp(ed, edition ? edition : L"Professional")) image = i;
    }
    if (!image) {
        if (edition) { err(L"edition '%ls' is not in this image", edition); goto out; }
        image = 1; warn(L"no Professional edition; using edition 1");
    }
    { const WCHAR *b = W.get_image_property(wim, image, L"WINDOWS/VERSION/BUILD"), *sp = W.get_image_property(wim, image, L"WINDOWS/VERSION/SPBUILD"),
                  *ar = arch_name(W.get_image_property(wim, image, L"WINDOWS/ARCH")), *ed = W.get_image_property(wim, image, L"WINDOWS/EDITIONID");
      int known = 0;
      _snwprintf(buildid, 64, L"%ls.%ls", b ? b : L"?", sp ? sp : L"?");
      for (i = 0; i < nisos; i++) { WCHAR a[64], r[32]; if (swscanf(isos[i], L"%63ls %31ls", a, r) == 2 && !wcscmp(a, buildid) && !wcscmp(r, ar)) known = 1; }
      info(L"Using edition %d (%ls), build %ls, %ls", image, ed ? ed : L"?", buildid, ar);
      if (!known) {
          if (!any_build) { err(L"build %ls %ls is not one WXR supports (see data\\isos.txt); --any-build extracts it anyway", buildid, ar); goto out; }
          warn(L"build %ls %ls is not in data\\isos.txt; extracting anyway (--any-build)", buildid, ar);
      }
      if (adding) { WCHAR b0[64]; ini_get(L"image", L"build", b0, 64);
          if (b0[0] && wcscmp(b0, buildid)) { err(L"this image is build %ls, but the installed Windows 10 files are build %ls: use the same ISO", buildid, b0); goto out; } }
      else { ini_set(L"image", L"state", L"incomplete"); ini_set(L"image", L"source", p); ini_set(L"image", L"build", buildid); ini_set(L"image", L"edition", ed ? ed : L""); }
    }
    info(L"Extracting %d entries from data\\files.txt", nfiles);
    t0 = GetTickCount(); g_last_pct = -1;
    if ((i = W.extract_paths(wim, image, g_root, (const wimlib_tchar * const *)files, (size_t)nfiles, WIMLIB_EXTRACT_FLAG_NO_ACLS | WIMLIB_EXTRACT_FLAG_GLOB_PATHS)) != 0) {
        wprintf(L"\n"); err(L"extraction failed: %ls", W.error_string((enum wimlib_error_code)i));
        err(L"the image is incomplete: run 'wxrsetup purge image', then extract again"); goto out; }
    info(L"Extracted in %lu s", (GetTickCount() - t0) / 1000);
    for (i = 0; i < nfiles; i++) {                       /* entries without wildcards must exist now */
        if (wcspbrk(files[i], L"*?")) continue;
        _snwprintf(p, MAX_PATH, L"%ls%ls", g_root, files[i]);
        if (!exists(p)) { if (++missing <= 20) warn(L"not in the image: %ls", files[i]); }
    }
    if (missing > 20) warn(L"... %d entries in total were not in the image", missing);
    { TreeStat ts = {0}; WCHAR *l, n[16]; join(p, g_root, L"Windows"); l = lp(p); tree_stat(l, &ts); free(l); fmt_size(ts.bytes, s1);
      info(L"Windows\\ now holds %u files (%ls)", ts.files, s1);
      now_str(t); _snwprintf(n, 16, L"%u", ts.files);
      if (!adding) { ini_set(L"image", L"state", L"extracted"); ini_set(L"image", L"extracted", t); }
      ini_set(L"image", L"files", n); }
    if (wcsstr(sections, L",uwp,")) ini_set(L"components", L"uwp", L"1");
    else if (!adding) ini_set(L"components", L"uwp", L"0");
    rc = 0;
out:
    if (wim) W.free_wim(wim);
    if (W.dll) W.global_cleanup();
    if (temp_made && tmp[0]) {
        if (keep_temp && rc == 0) info(L"Kept %ls (--keep-temp)", tmp);
        else if (exists(tmp)) { if (DeleteFileW(tmp)) info(L"Removed the temporary copy %ls", tmp); else warn(L"could not remove %ls (error %lu)", tmp, GetLastError()); }
    }
    free_list(files, nfiles); free_list(isos, nisos);
    return rc;
}
/* ---------------- components: make the installed optional parts match a list ---------------- */
/* removes the files of one section of data\files.txt (a component turned off). Wildcard entries are not undone. */
static int remove_section(const WCHAR *section) {
    WCHAR sec[48], p[MAX_PATH], **files; int n, i; RmStat rs = {0};
    _snwprintf(sec, 48, L",%ls,", section); files = read_list(L"files.txt", sec, &n);
    for (i = 0; i < n; i++) {
        if (wcspbrk(files[i], L"*?")) continue;
        _snwprintf(p, MAX_PATH, L"%ls%ls%ls", g_root, files[i][0] == '\\' ? L"" : L"\\", files[i]);
        if (is_dir(p)) { WCHAR *l = lp(p); rm_contents(l, NULL, &rs); if (RemoveDirectoryW(l)) rs.dirs++; else rm_fail(&rs, L"folder", l); free(l); }
        else if (exists(p)) { SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL); if (DeleteFileW(p)) rs.files++; else { WCHAR *l = lp(p); rm_fail(&rs, L"file", l); free(l); } }
    }
    free_list(files, n);
    info(L"  removed %u file(s)", rs.files);
    if (rs.failed) { err(L"%u file(s) could not be deleted (in use? close running apps and try again)", rs.failed); return 0; }
    return 1;
}
/* runs a tool of the layer (wxrpkg) and logs its output lines; returns its exit code (-1 = could not start) */
static int run_logged(const WCHAR *exe, const WCHAR *args) {
    WCHAR cmd[2 * MAX_PATH + 256]; SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE }; HANDLE rd, wr; STARTUPINFOW si; PROCESS_INFORMATION pi;
    char buf[4096], line[2048]; DWORD n, code = (DWORD)-1; size_t ll = 0;
    if (!exists(exe)) { err(L"%ls is missing: install the layer first", exe); return -1; }
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" %ls", exe, args); cmd[sizeof cmd / sizeof cmd[0] - 1] = 0;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { err(L"cannot start %ls (error %lu)", exe, GetLastError()); CloseHandle(rd); CloseHandle(wr); return -1; }
    CloseHandle(wr);
    while (ReadFile(rd, buf, sizeof buf, &n, NULL) && n) {
        DWORD k; for (k = 0; k < n; k++) {
            if (buf[k] == '\n' || ll == sizeof line - 1) { WCHAR w[2048]; line[ll] = 0; if (ll && line[ll - 1] == '\r') line[--ll] = 0;
                if (ll && !strncmp(line, "@bytes ", 7)) { unsigned long long d = 0, t = 0; if (sscanf(line + 7, "%llu %llu", &d, &t) == 2 && t) show_pct(L"downloading", d, t); }
                else if (ll && line[0] != '@') { if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line, -1, w, 2048)) MultiByteToWideChar(CP_ACP, 0, line, -1, w, 2048); info(L"%ls", w); }
                ll = 0; if (buf[k] == '\n') continue; }
            line[ll++] = buf[k]; } }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
/* WXR's own tools in the Start menu: Programs\WXR\Tools\<name>.lnk (footprint, tier layer; the app entries beside Tools are wxr sync's) */
static void tool_lnk_path(const WCHAR *name, WCHAR *dir, WCHAR *lnk) {
    dir[0] = lnk[0] = 0;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, dir))) return;
    wcscat(dir, L"\\WXR\\Tools"); _snwprintf(lnk, MAX_PATH, L"%ls\\%ls.lnk", dir, name);
}
static int tool_lnk(const WCHAR *name, const WCHAR *exe, const WCHAR *desc) {
    WCHAR dir[MAX_PATH], lnk[MAX_PATH], parent[MAX_PATH], *s; IShellLinkW *sl = NULL; IPersistFile *pf = NULL; HRESULT hr;
    tool_lnk_path(name, dir, lnk); if (!lnk[0]) return 0;
    lstrcpyW(parent, dir); if ((s = wcsrchr(parent, '\\'))) *s = 0;
    if (CreateDirectoryW(parent, NULL) && !footprint_has(L"DIR", parent)) footprint_add(L"layer", L"DIR", parent);
    if (CreateDirectoryW(dir, NULL) && !footprint_has(L"DIR", dir)) footprint_add(L"layer", L"DIR", dir);
    CoInitialize(NULL);
    if (FAILED(hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) { CoUninitialize(); return 0; }
    sl->lpVtbl->SetPath(sl, exe); sl->lpVtbl->SetDescription(sl, desc);
    if (SUCCEEDED(hr = sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) { hr = pf->lpVtbl->Save(pf, lnk, TRUE); pf->lpVtbl->Release(pf); }
    sl->lpVtbl->Release(sl); CoUninitialize();
    if (SUCCEEDED(hr) && !footprint_has(L"FILE", lnk)) footprint_add(L"layer", L"FILE", lnk);
    return SUCCEEDED(hr);
}
/* components <list> [--iso <file>] [--bundle <dir>]: installs what the list names and removes what it leaves out.
   bundle = the folder of the setup program (default: wxrsetup's own folder), for msa\wxrsignin.exe. */
static int cmd_components(const WCHAR *list, const WCHAR *iso, const WCHAR *bundle, const WCHAR *edition, int any_build) {
    int want[NCOMP], i, failed = 0; WCHAR st[64], p[MAX_PATH], src[MAX_PATH], pkg[MAX_PATH], args[MAX_PATH + 32];
    if (!list) { err(L"components needs a list, e.g. 'uwp,gdk' or 'none' (known: uwp, gdk, msa)"); return 2; }
    if (!parse_components(list, want)) return 2;
    if (!need_root()) return 1;
    ini_get(L"image", L"state", st, 64);
    if (wcscmp(st, L"extracted")) { err(L"extract the Windows 10 image first (wxrsetup extract <iso>)"); return 1; }
    _snwprintf(pkg, MAX_PATH, L"%ls\\Layer\\wxrpkg.exe", g_root);
    _snwprintf(args, MAX_PATH + 32, L"--root \"%ls\" gdk --progress", g_root);

    /* uwp: files from the ISO */
    if (want[COMP_UWP] && !uwp_present()) {
        info(L"UWP apps: adding Windows' own XAML from the Windows 10 image");
        if (!iso) { err(L"adding UWP support needs the Windows 10 ISO (--iso <file>)"); failed++; }
        else if (cmd_extract(iso, edition, 0, any_build, L",uwp,", 1)) failed++;
    } else if (!want[COMP_UWP] && uwp_present()) {
        info(L"UWP apps: removing Windows' own XAML");
        if (!remove_section(L"uwp")) failed++;
    } else info(L"UWP apps: %ls", want[COMP_UWP] ? L"installed" : L"not installed");
    if (!failed) ini_set(L"components", L"uwp", want[COMP_UWP] ? L"1" : L"0");

    /* gdk: wxrpkg downloads / removes the runtime files */
    if (want[COMP_GDK]) {
        int r; info(L"GDK games: setting up the game runtime (downloads Microsoft's Gaming Services package when needed)"); g_last_pct = -1;
        ini_set(L"components", L"gdk", L"1");                  /* first: wxrpkg gdk reads it */
        if ((r = run_logged(pkg, args)) != 0) { err(L"the GDK game runtime could not be set up (wxrpkg exit code %d)", r); failed++; }
    } else if (comp_recorded(COMP_GDK) != 0) {
        int r; info(L"GDK games: removing the game runtime");
        wcscat(args, L" --remove");
        if ((r = run_logged(pkg, args)) != 0) { err(L"the GDK game runtime could not be removed (wxrpkg exit code %d)", r); failed++; }
        else ini_set(L"components", L"gdk", L"0");
    } else info(L"GDK games: not installed");

    /* msa: the sign-in tool + Start menu entry */
    _snwprintf(p, MAX_PATH, L"%ls\\Layer\\wxrsignin.exe", g_root);
    if (want[COMP_MSA]) {
        info(L"Microsoft account sign-in: installing WXR Sign-in");
        if (bundle) _snwprintf(src, MAX_PATH, L"%ls\\msa\\wxrsignin.exe", bundle); else { exe_dir(src); wcscat(src, L"\\msa\\wxrsignin.exe"); }
        if (!exists(src)) { err(L"%ls is missing from the setup files", src); failed++; }
        else if (!CopyFileW(src, p, FALSE)) { err(L"cannot copy %ls (error %lu)", src, GetLastError()); failed++; }
        else {
            if (tool_lnk(L"WXR Sign-in", p, L"Sign in to a Microsoft account for WXR's Microsoft Store")) info(L"  Start menu: WXR > Tools > WXR Sign-in");
            else warn(L"  could not create the Start menu entry; run %ls", p);
            ini_set(L"components", L"msa", L"1");
        }
    } else {
        WCHAR dir[MAX_PATH], lnk[MAX_PATH];
        if (comp_recorded(COMP_MSA) != 0 || exists(p)) {
            info(L"Microsoft account sign-in: removing WXR Sign-in and the signed-in account");
            DeleteFileW(p); tool_lnk_path(L"WXR Sign-in", dir, lnk); if (lnk[0]) DeleteFileW(lnk);
            _snwprintf(dir, MAX_PATH, L"%ls\\ProgramData\\Identity\\user.ini", g_root); DeleteFileW(dir);       /* the account; device.ini stays */
            _snwprintf(dir, MAX_PATH, L"%ls\\ProgramData\\Identity\\user.import", g_root); DeleteFileW(dir);
            if (exists(p)) { err(L"cannot delete %ls (in use?)", p); failed++; }
        } else info(L"Microsoft account sign-in: not installed");
        if (!exists(p)) ini_set(L"components", L"msa", L"0");
    }
    WritePrivateProfileStringW(L"components", L"winui3", NULL, g_ini);   /* no longer a component: always supported */
    for (i = 0; i < NCOMP; i++) { WCHAR k[32]; _snwprintf(k, 32, L"comp.%ls", k_comp[i]); data(k, comp_on(i) ? L"1" : L"0"); }
    if (failed) { err(L"%d component(s) could not be changed", failed); return 1; }
    info(L"Components done");
    return 0;
}
static int cmd_create(void) {
    WCHAR p[MAX_PATH], t[32]; unsigned i, made = 0; int fresh;
    info(L"Creating WXR environment at %ls", g_root);
    if (!check_root_path()) return 1;
    if (is_dir(g_root) && !is_wxr_root()) {
        if (!dir_empty(g_root)) { err(L"%ls exists and is not empty, but is not a WXR environment; choose another folder", g_root); return 1; }
    } else if (exists(g_root) && !is_dir(g_root)) { err(L"%ls exists and is a file", g_root); return 1; }
    fresh = !is_wxr_root();
    if (!fresh) info(L"An environment already exists here; adding any missing folders");
    if (!mkdirs(g_root)) { err(L"cannot create %ls (error %lu)", g_root, GetLastError()); return 1; }
    join(p, g_root, L"Logs"); mkdirs(p); log_open();
    for (i = 0; i < NLAYOUT; i++) {
        join(p, g_root, k_layout[i].dir);
        if (is_dir(p)) continue;
        if (!mkdirs(p)) { err(L"cannot create %ls (error %lu)", p, GetLastError()); return 1; }
        made++;
    }
    user_dir(p); if (!is_dir(p)) { if (!mkdirs(p)) { err(L"cannot create %ls (error %lu)", p, GetLastError()); return 1; } made++; }
    join(p, g_root, L"ProgramData\\footprint.txt");
    if (!exists(p)) { FILE *f = _wfopen(p, L"wb"); if (f) { fputs("# WXR footprint: changes made outside the root, undone by purge.  <tier>\\t<type>\\t<data>\r\n", f); fclose(f); } }
    if (fresh) {
        now_str(t);
        ini_set(L"wxr", L"marker", WXR_MARKER);
        ini_set(L"wxr", L"setup_version", WXR_SETUP_VERSION);
        ini_set(L"wxr", L"created", t);
        ini_set(L"image", L"state", L"none");
        ini_set(L"image", L"source", L"");
        ini_set(L"image", L"build", L"");
        ini_set(L"image", L"edition", L"");
        ini_set(L"layer", L"state", L"none");
        ini_set(L"layer", L"version", L"");
        if (!is_wxr_root()) { err(L"cannot write %ls (error %lu)", g_ini, GetLastError()); return 1; }
    }
    info(L"%ls: %u folder(s) created", fresh ? L"New environment" : L"Existing environment", made);
    info(L"Path budget: %u characters left for paths inside WindowsApps", (unsigned)(259 - wcslen(g_root) - 13));
    info(L"Next steps: wxrsetup extract <iso>, then wxrsetup register");
    return 0;
}
static int cmd_status(void) {
    WCHAR v[260], v2[260], v3[260], p[MAX_PATH], s[32]; unsigned i; int fp = 0;
    if (!need_root()) return 1;
    info(L"WXR environment: %ls", g_root);
    ini_get(L"wxr", L"created", v, 260); ini_get(L"wxr", L"setup_version", v2, 260);
    info(L"  created %ls by setup %ls", v, v2);
    ini_get(L"image", L"state", v, 260); ini_get(L"image", L"build", v2, 260); ini_get(L"image", L"edition", v3, 260);
    info(L"  image: %ls%ls%ls%ls%ls", v[0] ? v : L"?", v2[0] ? L", build " : L"", v2, v3[0] ? L", " : L"", v3);
    ini_get(L"image", L"source", v, 260); if (v[0]) info(L"         from %ls", v);
    ini_get(L"image", L"extracted", v, 260); ini_get(L"image", L"files", v2, 260); if (v[0]) info(L"         extracted %ls, %ls files", v, v2);
    ini_get(L"register", L"state", v, 260); ini_get(L"register", L"updated", v2, 260);
    info(L"  register: %ls%ls%ls", v[0] ? v : L"not run", v2[0] ? L", " : L"", v2);
    ini_get(L"layer", L"state", v, 260); ini_get(L"layer", L"version", v2, 260);
    info(L"  layer: %ls%ls%ls", v[0] ? v : L"?", v2[0] ? L", version " : L"", v2);
    info(L"  components: UWP apps %ls, GDK games %ls, Microsoft account sign-in %ls", comp_on(COMP_UWP) ? L"on" : L"off", comp_on(COMP_GDK) ? L"on" : L"off", comp_on(COMP_MSA) ? L"on" : L"off");
    for (i = 0; i < NLAYOUT; i++) {
        TreeStat st = {0}; WCHAR *l;
        if (!k_layout[i].what) continue;
        join(p, g_root, k_layout[i].dir);
        if (!is_dir(p)) { warn(L"  %-12ls missing (run: wxrsetup create)", k_layout[i].dir); continue; }
        l = lp(p); tree_stat(l, &st); free(l); fmt_size(st.bytes, s);
        info(L"  %-12ls %6u files %10ls   %ls%ls", k_layout[i].dir, st.files, s, k_layout[i].what, st.links ? L" (contains links)" : L"");
    }
    { WIN32_FIND_DATAW fd; HANDLE h; unsigned np = 0;
      _snwprintf(p, MAX_PATH, L"%ls\\WindowsApps\\*", g_root); h = FindFirstFileW(p, &fd);
      if (h != INVALID_HANDLE_VALUE) { do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') np++; while (FindNextFileW(h, &fd)); FindClose(h); }
      info(L"  packages: %u", np); }
    footprint_undo(NULL, &fp);
    info(L"  changes outside the root: %d", fp);
    return 0;
}
static int cmd_purge(const WCHAR *what) {
    RmStat rs = {0}; WCHAR p[MAX_PATH], *l; int left;
    if (!what) { err(L"purge needs a level: state, image, layer or all"); return 2; }
    if (wcscmp(what, L"state") && wcscmp(what, L"image") && wcscmp(what, L"layer") && wcscmp(what, L"all")) { err(L"unknown purge level '%ls' (state, image, layer, all)", what); return 2; }
    if (!need_root()) return 1;
    if (!wcscmp(what, L"state")) {
        if (!confirm(L"all installed packages and all app data")) { info(L"Cancelled"); return 1; }
        info(L"Purging state (packages, package database, app data)");
        left = footprint_undo(L"state", NULL);
        join(p, g_root, L"WindowsApps"); l = lp(p); rm_contents(l, NULL, &rs); free(l);
        join(p, g_root, L"Users");       l = lp(p); rm_contents(l, NULL, &rs); free(l);
        join(p, g_root, L"ProgramData"); l = lp(p); rm_contents(l, L"footprint.txt", &rs); free(l);
        user_dir(p); mkdirs(p);
    } else if (!wcscmp(what, L"image")) {
        if (!confirm(L"the extracted Windows 10 files")) { info(L"Cancelled"); return 1; }
        info(L"Purging image (Windows\\)");
        left = footprint_undo(L"image", NULL);
        join(p, g_root, L"Windows"); l = lp(p); rm_contents(l, NULL, &rs); free(l);
        { unsigned i; for (i = 0; i < NLAYOUT; i++) if (k_layout[i].tier == 1) { join(p, g_root, k_layout[i].dir); mkdirs(p); } }
        ini_set(L"image", L"state", L"none"); ini_set(L"image", L"source", L""); ini_set(L"image", L"build", L""); ini_set(L"image", L"edition", L"");
    } else if (!wcscmp(what, L"layer")) {
        if (!confirm(L"WXR's own binaries (Layer\\)")) { info(L"Cancelled"); return 1; }
        info(L"Purging layer (Layer\\)");
        left = footprint_undo(L"layer", NULL);
        join(p, g_root, L"Layer"); l = lp(p); rm_contents(l, NULL, &rs); free(l);
        ini_set(L"layer", L"state", L"none"); ini_set(L"layer", L"version", L"");
    } else {
        if (!confirm(L"the WHOLE environment")) { info(L"Cancelled"); return 1; }
        info(L"Purging everything");
        left = footprint_undo(NULL, NULL);
        if (left) { err(L"%d change(s) outside the root could not be undone; the root is kept so they are not forgotten (see ProgramData\\footprint.txt)", left); return 1; }
        info(L"Deleting %ls (the log ends here; the root is gone afterwards)", g_root);
        log_close();
        l = lp(g_root); rm_contents(l, NULL, &rs);
        if (!rs.failed && !RemoveDirectoryW(l)) rm_fail(&rs, L"folder", l);
        free(l);
        if (rs.failed) { err(L"%u item(s) could not be deleted (in use? close running apps and try again)", rs.failed); return 1; }
        info(L"Environment removed: %u files, %u folders", rs.files, rs.dirs);
        return 0;
    }
    info(L"Deleted %u files, %u folders", rs.files, rs.dirs);
    if (rs.failed) { err(L"%u item(s) could not be deleted (in use? close running apps and try again)", rs.failed); return 1; }
    if (left) { warn(L"%d change(s) outside the root were kept (see ProgramData\\footprint.txt)", left); }
    return 0;
}

static void usage(void) {
    wprintf(L"wxrsetup %ls - sets up a WXR environment (Windows 10 runtime files + packages on Windows 7)\n\n"
            L"usage: wxrsetup [--root <dir>] [--yes] <command>\n\n"
            L"commands:\n"
            L"  check [system|root]  check this computer (Windows 7 SP1 x64, Platform Update, VxKex, ...)\n"
            L"                    and/or the install folder (empty or WXR, local NTFS, free space)\n"
            L"  inspect <file>    show the Windows editions and build in an ISO / install.esd / install.wim\n"
            L"  create            create the environment folders (safe to run again)\n"
            L"  extract <file>    take the Windows 10 files from an ISO (or install.esd / install.wim)\n"
            L"  components <list> install the optional components named (uwp, gdk, msa; 'none' = none) and remove the others\n"
            L"  register          make the registry changes WXR needs outside its folder (some need administrator)\n"
            L"  install-layer <d> put WXR's own binaries (a built layer folder) into <root>\\Layer\n"
            L"  install-setup <d> copy the setup program into <root>\\Setup and list WXR in Programs and Features\n"
            L"  status            show what the environment contains\n"
            L"  purge state       remove all installed packages and app data\n"
            L"  purge image       remove the files extracted from the Windows 10 ISO\n"
            L"  purge layer       remove WXR's own binaries\n"
            L"  purge all         remove the whole environment, including changes made outside it\n\n"
            L"options:\n"
            L"  --root <dir>      environment folder (default: %%WXR_ROOT%%, else C:\\WXR)\n"
            L"  --yes             do not ask before deleting\n"
            L"  --lines           machine-readable output: <KIND>, a tab, the text; UTF-8 (for the setup program)\n"
            L"  --edition <id>    extract: edition to take files from (default Professional)\n"
            L"  --keep-temp       extract: keep the install.esd copied out of the ISO in <root>\\Temp\n"
            L"  --any-build       extract: accept an image build that is not in data\\isos.txt\n"
            L"  --components <l>  extract: the optional ISO parts to take (default: as recorded, else uwp)\n"
            L"  --iso <file>      components: the Windows 10 ISO, when a component needs files from it\n"
            L"  --bundle <dir>    components: the setup program's folder (default: wxrsetup's own)\n\n"
            L"components:\n"
            L"  uwp  Windows' own XAML for UWP apps (Calculator, Store, ...), from the ISO\n"
            L"  gdk  the runtime of GDK PC games, downloaded from Microsoft (Gaming Services) by wxrpkg\n"
            L"  msa  WXR Sign-in, for a Microsoft account in the Store (experimental)\n", WXR_SETUP_VERSION);
}
int wmain(int argc, WCHAR **argv) {
    const WCHAR *root = NULL, *cmd = NULL, *arg = NULL, *edition = NULL, *expect = NULL, *comps = NULL, *iso = NULL, *bundle = NULL; int i, rc, keep_temp = 0, any_build = 0;
    for (i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--root") && i + 1 < argc) root = argv[++i];
        else if (!wcscmp(argv[i], L"--components") && i + 1 < argc) comps = argv[++i];
        else if (!wcscmp(argv[i], L"--iso") && i + 1 < argc) iso = argv[++i];
        else if (!wcscmp(argv[i], L"--bundle") && i + 1 < argc) bundle = argv[++i];
        else if (!wcscmp(argv[i], L"--expect") && i + 1 < argc) expect = argv[++i];
        else if (!wcscmp(argv[i], L"--edition") && i + 1 < argc) edition = argv[++i];
        else if (!wcscmp(argv[i], L"--keep-temp")) keep_temp = 1;
        else if (!wcscmp(argv[i], L"--any-build")) any_build = 1;
        else if (!wcscmp(argv[i], L"--yes") || !wcscmp(argv[i], L"-y")) g_yes = 1;
        else if (!wcscmp(argv[i], L"--lines")) g_lines = 1;
        else if (!wcscmp(argv[i], L"--help") || !wcscmp(argv[i], L"-h") || !wcscmp(argv[i], L"/?")) { usage(); return 0; }
        else if (argv[i][0] == '-' && argv[i][1] == '-') { usage(); fwprintf(stderr, L"\nunknown option %ls\n", argv[i]); return 2; }
        else if (!cmd) cmd = argv[i];
        else if (!arg) arg = argv[i];
        else { usage(); return 2; }
    }
    if (!cmd) { usage(); return 2; }
    if (g_lines) _setmode(_fileno(stdout), _O_U8TEXT);   /* UTF-8 for the caller; only wide output functions from here on */
    if (!pick_root(root)) return 1;
    if (expect) {   /* safety net for callers that delete: the root wxrsetup resolved must be exactly this folder */
        WCHAR e[MAX_PATH]; size_t n;
        if (!GetFullPathNameW(expect, MAX_PATH, e, NULL)) { err(L"bad --expect path: %ls", expect); return 1; }
        n = wcslen(e); while (n > 3 && e[n - 1] == '\\') e[--n] = 0;
        if (_wcsicmp(e, g_root)) { err(L"refusing to run: the environment folder is %ls, but the caller expected %ls", g_root, e); return 1; }
    }
    if (!wcscmp(cmd, L"check")) rc = cmd_check(arg);
    else if (!wcscmp(cmd, L"inspect")) rc = cmd_inspect(arg);
    else if (!wcscmp(cmd, L"install-setup")) rc = cmd_install_setup(arg);
    else if (!wcscmp(cmd, L"extract")) {
        int want[NCOMP], uwp;
        if (comps) { if (!parse_components(comps, want)) return 2; uwp = want[COMP_UWP]; }
        else { uwp = is_wxr_root() ? comp_recorded(COMP_UWP) : -1; if (uwp < 0) uwp = 1; }
        rc = cmd_extract(arg, edition, keep_temp, any_build, uwp ? L",core,uwp," : L",core,", 0);
    }
    else if (!wcscmp(cmd, L"components")) rc = cmd_components(arg, iso, bundle, edition, any_build);
    else if (!wcscmp(cmd, L"register")) rc = cmd_register();
    else if (!wcscmp(cmd, L"install-layer")) rc = cmd_install_layer(arg);
    else if (!wcscmp(cmd, L"create")) rc = cmd_create();
    else if (!wcscmp(cmd, L"status")) rc = cmd_status();
    else if (!wcscmp(cmd, L"purge")) rc = cmd_purge(arg);
    else { usage(); fwprintf(stderr, L"\nunknown command %ls\n", cmd); return 2; }
    if (g_warnings && rc == 0 && !g_lines) wprintf(L"(%d warning(s), see %ls\\Logs\\setup.log)\n", g_warnings, g_root);
    log_close();
    return rc;
}
