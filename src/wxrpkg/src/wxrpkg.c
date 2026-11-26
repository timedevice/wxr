/* wxrpkg - Appx/MSIX deployment engine of a WXR environment (Win10's AppXSvc + StateRepository, minus the sandbox).
     wxrpkg [--root <dir>] add <package|folder|AppxManifest.xml>... [--force-update] [--any-os]
     wxrpkg [--root <dir>] list [--all-users] [--json] [--type main,framework,resource|all] [--name <pattern>] [--publisher <pattern>]
     wxrpkg [--root <dir>] remove <PackageFullName> [--preserve-data]
     wxrpkg [--root <dir>] store <ProductId|PackageFamilyName> [--download-only] [--progress] [--force-update] [--any-os]   (store.inc)
     wxrpkg [--root <dir>] gdk [--remove] [--force-update]   the runtime of GDK PC games (gdk.inc)
   Store layout:
     WindowsApps\<PackageFullName>\           staged package, pristine (block map checked while staging)
     ProgramData\Packages\<PackageFullName>.ini  package record
     Users\<user>\packages.ini [registered]   packages registered for that user
     Users\<user>\Packages\<PackageFamilyName>\  app data (main packages)
   Root: --root, else the root this exe lives in (<root>\Layer\wxrpkg.exe), else %WXR_ROOT%, else C:\WXR. Log: <root>\Logs\deploy.log */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <bcrypt.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include "xml.h"
#include "miniz.h"

#define WXR_PKG_VERSION L"0.2"
#define BLOCK 65536

/* ================= basics ================= */
static WCHAR g_root[MAX_PATH], g_user[128];
static FILE *g_log; static int g_quiet;
static void logv(const WCHAR *lvl, const WCHAR *fmt, va_list ap) {
    WCHAR msg[2048]; char u8[6200]; SYSTEMTIME t;
    _vsnwprintf(msg, 2047, fmt, ap); msg[2047] = 0;
    if (!g_quiet || wcscmp(lvl, L"INFO")) { if (!wcscmp(lvl, L"INFO")) { wprintf(L"%ls\n", msg); fflush(stdout); } else fwprintf(stderr, L"%ls: %ls\n", lvl, msg); }
    if (g_log) { GetLocalTime(&t); WideCharToMultiByte(CP_UTF8, 0, msg, -1, u8, sizeof u8, NULL, NULL);
        fprintf(g_log, "%04u-%02u-%02u %02u:%02u:%02u %-5ls %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, lvl, u8); fflush(g_log); }
}
static void info(const WCHAR *f, ...) { va_list a; va_start(a, f); logv(L"INFO", f, a); va_end(a); }
static void warn(const WCHAR *f, ...) { va_list a; va_start(a, f); logv(L"WARN", f, a); va_end(a); }
static void err(const WCHAR *f, ...) { va_list a; va_start(a, f); logv(L"ERROR", f, a); va_end(a); }
static void dbg(const WCHAR *f, ...) { va_list a; WCHAR m[2048]; char u8[6200]; if (!g_log) return; va_start(a, f); _vsnwprintf(m, 2047, f, a); m[2047] = 0; va_end(a);
    WideCharToMultiByte(CP_UTF8, 0, m, -1, u8, sizeof u8, NULL, NULL); fprintf(g_log, "                    DEBUG %s\n", u8); fflush(g_log); }

static int is_dir(const WCHAR *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static int exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
static int mkdirs(const WCHAR *p) {
    WCHAR t[MAX_PATH]; WCHAR *s;
    if (is_dir(p)) return 1;
    lstrcpynW(t, p, MAX_PATH);
    for (s = t + 3; *s; s++) if (*s == '\\') { *s = 0; if (!is_dir(t) && !CreateDirectoryW(t, NULL)) return 0; *s = '\\'; }
    return CreateDirectoryW(t, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static void now_str(WCHAR *o) { SYSTEMTIME t; GetLocalTime(&t); _snwprintf(o, 32, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); }
static void rm_tree(const WCHAR *dir) {                              /* never follows junctions */
    WCHAR pat[MAX_PATH], sub[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h;
    _snwprintf(pat, MAX_PATH, L"%ls\\*", dir); h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            _snwprintf(sub, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(sub, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) rm_tree(sub); else RemoveDirectoryW(sub); }
            else if (!DeleteFileW(sub)) warn(L"could not delete %ls (error %lu)", sub, GetLastError());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}
/* version "a.b.c.d" -> 64-bit comparable */
static int parse_ver(const WCHAR *s, ULONGLONG *v) {
    unsigned p[4] = {0}; int n; if (!s) return 0;
    n = swscanf(s, L"%u.%u.%u.%u", &p[0], &p[1], &p[2], &p[3]);
    if (n < 1 || p[0] > 65535 || p[1] > 65535 || p[2] > 65535 || p[3] > 65535) return 0;
    *v = ((ULONGLONG)p[0] << 48) | ((ULONGLONG)p[1] << 32) | ((ULONGLONG)p[2] << 16) | p[3]; return 1;
}

/* ================= SHA-256 (BCrypt), base64, PublisherId ================= */
static BCRYPT_ALG_HANDLE g_sha;
static int sha256(const void *d, ULONG n, BYTE out[32]) {
    BCRYPT_HASH_HANDLE h; BYTE obj[1024]; DWORD ol = 0, got; int ok;
    if (!g_sha && BCryptOpenAlgorithmProvider(&g_sha, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return 0;
    BCryptGetProperty(g_sha, BCRYPT_OBJECT_LENGTH, (PUCHAR)&ol, sizeof ol, &got, 0);
    if (ol > sizeof obj || BCryptCreateHash(g_sha, &h, obj, ol, NULL, 0, 0) != 0) return 0;
    ok = BCryptHashData(h, (PUCHAR)d, n, 0) == 0 && BCryptFinishHash(h, out, 32, 0) == 0;
    BCryptDestroyHash(h); return ok;
}
static int b64dec(const WCHAR *s, BYTE *out, int cap) {
    int v = 0, bits = 0, n = 0;
    for (; *s; s++) {
        int c = *s, d;
        if (c >= 'A' && c <= 'Z') d = c - 'A'; else if (c >= 'a' && c <= 'z') d = c - 'a' + 26; else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62; else if (c == '/') d = 63; else if (c == '=') break; else continue;
        v = (v << 6) | d; bits += 6;
        if (bits >= 8) { bits -= 8; if (n < cap) out[n++] = (BYTE)(v >> bits); }
    }
    return n;
}
/* PublisherId = base32 ("0123456789abcdefghjkmnpqrstvwxyz") of the first 8 bytes of SHA-256(UTF-16LE publisher) */
static int publisher_id(const WCHAR *pub, WCHAR out[14]) {
    BYTE d[32]; unsigned long long v = 0; int i; static const char al[] = "0123456789abcdefghjkmnpqrstvwxyz";
    if (!sha256(pub, (ULONG)(wcslen(pub) * 2), d)) return 0;
    for (i = 0; i < 8; i++) v = (v << 8) | d[i];
    for (i = 0; i < 13; i++) { int sh = 64 - 5 * (i + 1); out[i] = (WCHAR)al[sh >= 0 ? (unsigned)((v >> sh) & 31) : (unsigned)((v << (-sh)) & 31)]; }
    out[13] = 0; return 1;
}

/* ================= ZIP (miniz) ================= */
typedef struct { mz_zip_archive z; FILE *f; unsigned n; WCHAR **names; } Zip;   /* names: decoded, '\' separated */
static void pct_decode(const char *in, WCHAR *out, int cap) {        /* zip names in packages are percent-encoded UTF-8 */
    char tmp[1024]; int k = 0;
    while (*in && k < 1023) {
        if (in[0] == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) { char h[3] = { in[1], in[2], 0 }; tmp[k++] = (char)strtol(h, NULL, 16); in += 3; }
        else tmp[k++] = *in++;
    }
    tmp[k] = 0;
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, cap);
    for (; *out; out++) if (*out == '/') *out = '\\';
}
static int zip_open(Zip *z, const WCHAR *path, WCHAR *why) {
    LARGE_INTEGER sz; HANDLE h; unsigned i;
    memset(z, 0, sizeof *z);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h, &sz)) { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); _snwprintf(why, 256, L"cannot open %ls (error %lu)", path, GetLastError()); return 0; }
    CloseHandle(h);
    if (!(z->f = _wfopen(path, L"rb"))) { _snwprintf(why, 256, L"cannot open %ls", path); return 0; }
    if (!mz_zip_reader_init_cfile(&z->z, z->f, (mz_uint64)sz.QuadPart, 0)) {
        _snwprintf(why, 256, L"%ls is not a valid package (zip: %hs)", path, mz_zip_get_error_string(mz_zip_get_last_error(&z->z))); fclose(z->f); z->f = NULL; return 0; }
    z->n = mz_zip_reader_get_num_files(&z->z);
    z->names = (WCHAR **)calloc(z->n + 1, sizeof(WCHAR *));
    for (i = 0; i < z->n; i++) { mz_zip_archive_file_stat st; WCHAR w[1024]; mz_zip_reader_file_stat(&z->z, i, &st); pct_decode(st.m_filename, w, 1024); z->names[i] = _wcsdup(w); }
    return 1;
}
static void zip_close(Zip *z) { unsigned i; if (z->f) { mz_zip_reader_end(&z->z); fclose(z->f); } for (i = 0; i < z->n; i++) free(z->names[i]); free(z->names); memset(z, 0, sizeof *z); }
static int zip_find(Zip *z, const WCHAR *name) { unsigned i; for (i = 0; i < z->n; i++) if (!_wcsicmp(z->names[i], name)) return (int)i; return -1; }
static void *zip_read(Zip *z, const WCHAR *name, size_t *len) { int i = zip_find(z, name); if (i < 0) return NULL; return mz_zip_reader_extract_to_heap(&z->z, (mz_uint)i, len, 0); }

/* ================= package model ================= */
typedef struct { WCHAR name[256], minver[32], publisher[512]; } Dep;
typedef struct { WCHAR id[128], exe[260], entry[260]; } App;
typedef struct Pkg {
    WCHAR name[256], publisher[512], pubid[16], version[32], arch[16], resid[128], fullname[400], family[300];
    ULONGLONG ver; int framework, resource;
    WCHAR os_min[32];                     /* highest Windows.* TargetDeviceFamily MinVersion */
    Dep deps[32]; int ndeps;
    App apps[16]; int napps;
    WCHAR langs[512];                     /* the package's <Resources> languages, ';' separated */
    WCHAR scales[64];                     /* the package's <Resources> scales, ';' separated */
    int kind;                             /* 0 = zip file, 1 = folder */
    WCHAR src[MAX_PATH];                  /* zip path or folder */
    WCHAR bundle[400];                    /* bundle full name, if it came from a bundle */
    WCHAR resolved[32][400]; int nresolved;   /* dependency full names after resolution */
    int skip;                             /* 1 already installed, 2 listed twice, 3 dependency not needed, 4 dependency of another architecture */
    int is_dep;                           /* came from -DependencyPath: installed only if something needs it */
    int missing;                          /* 1 + index of the first dependency that could not be resolved */
    WCHAR replaces[400];                  /* older version of the same family being updated */
} Pkg;
static void make_names(Pkg *p) {
    _snwprintf(p->fullname, 400, L"%ls_%ls_%ls_%ls_%ls", p->name, p->version, p->arch, p->resid, p->pubid);
    _snwprintf(p->family, 300, L"%ls_%ls", p->name, p->pubid);
}
static int parse_manifest(const void *data, size_t n, Pkg *p, WCHAR *why) {
    WCHAR e[256]; XNode *root = xml_parse(data, n, e), *id, *x, *props, *deps, *apps; const WCHAR *v;
    if (!root) { _snwprintf(why, 256, L"AppxManifest.xml: %ls", e); return 0; }
    if (wcscmp(root->name, L"Package")) { _snwprintf(why, 256, L"AppxManifest.xml: root element is <%ls>, not <Package>", root->name); xml_free(root); return 0; }
    id = xchild(root, L"Identity");
    if (!id || !xattr(id, L"Name") || !xattr(id, L"Publisher") || !xattr(id, L"Version")) { _snwprintf(why, 256, L"AppxManifest.xml: incomplete <Identity>"); xml_free(root); return 0; }
    lstrcpynW(p->name, xattr(id, L"Name"), 256); lstrcpynW(p->publisher, xattr(id, L"Publisher"), 512); lstrcpynW(p->version, xattr(id, L"Version"), 32);
    lstrcpynW(p->arch, (v = xattr(id, L"ProcessorArchitecture")) ? v : L"neutral", 16); CharLowerW(p->arch);
    lstrcpynW(p->resid, (v = xattr(id, L"ResourceId")) ? v : L"", 128);
    if (!parse_ver(p->version, &p->ver)) { _snwprintf(why, 256, L"bad version '%ls'", p->version); xml_free(root); return 0; }
    publisher_id(p->publisher, p->pubid);
    props = xchild(root, L"Properties");
    if ((x = xchild(props, L"Framework")) && x->text && !_wcsicmp(x->text, L"true")) p->framework = 1;
    if ((x = xchild(props, L"ResourcePackage")) && x->text && !_wcsicmp(x->text, L"true")) p->resource = 1;
    deps = xchild(root, L"Dependencies");
    for (x = deps ? deps->first : NULL; x; x = x->next) {
        if (!wcscmp(x->name, L"TargetDeviceFamily")) {
            const WCHAR *fam = xattr(x, L"Name"), *mv = xattr(x, L"MinVersion"); ULONGLONG a, b;
            if (fam && mv && !_wcsnicmp(fam, L"Windows.", 8) && (!_wcsicmp(fam, L"Windows.Universal") || !_wcsicmp(fam, L"Windows.Desktop")))
                if (!p->os_min[0] || (parse_ver(mv, &a) && parse_ver(p->os_min, &b) && a < b)) lstrcpynW(p->os_min, mv, 32);   /* the lowest of the families we can satisfy */
        } else if (!wcscmp(x->name, L"PackageDependency") && p->ndeps < 32) {
            Dep *d = &p->deps[p->ndeps++];
            lstrcpynW(d->name, (v = xattr(x, L"Name")) ? v : L"", 256); lstrcpynW(d->minver, (v = xattr(x, L"MinVersion")) ? v : L"0.0.0.0", 32);
            lstrcpynW(d->publisher, (v = xattr(x, L"Publisher")) ? v : L"", 512);
        }
    }
    apps = xchild(root, L"Applications");
    for (x = apps ? xchild(apps, L"Application") : NULL; x && p->napps < 16; x = xnext(x, L"Application")) {
        App *a = &p->apps[p->napps++];
        lstrcpynW(a->id, (v = xattr(x, L"Id")) ? v : L"", 128); lstrcpynW(a->exe, (v = xattr(x, L"Executable")) ? v : L"", 260);
        lstrcpynW(a->entry, (v = xattr(x, L"EntryPoint")) ? v : L"", 260);
    }
    if ((x = xchild(root, L"Resources"))) for (x = xchild(x, L"Resource"); x; x = xnext(x, L"Resource"))
        { if ((v = xattr(x, L"Language")) && wcslen(p->langs) + wcslen(v) + 2 < 512) { wcscat(p->langs, v); wcscat(p->langs, L";"); }
          if ((v = xattr(x, L"Scale")) && wcslen(p->scales) + wcslen(v) + 2 < 64) { wcscat(p->scales, v); wcscat(p->scales, L";"); } }
    if (!p->resid[0] && p->resource) warn(L"%ls is marked as a resource package but has no ResourceId", p->name);
    make_names(p);
    xml_free(root); return 1;
}

/* ================= block map ================= */
typedef struct { WCHAR name[1024]; ULONGLONG size; int nblocks; BYTE (*hash)[32]; int seen; } BmFile;
typedef struct { BmFile *f; int n; } BlockMap;
static int blockmap_parse(const void *data, size_t n, BlockMap *bm, WCHAR *why) {
    WCHAR e[256]; XNode *root = xml_parse(data, n, e), *f, *b; const WCHAR *hm;
    memset(bm, 0, sizeof *bm);
    if (!root) { _snwprintf(why, 256, L"AppxBlockMap.xml: %ls", e); return 0; }
    hm = xattr(root, L"HashMethod");
    if (hm && !wcsstr(hm, L"sha256")) { _snwprintf(why, 256, L"AppxBlockMap.xml uses hash method %ls (only SHA-256 is supported)", hm); xml_free(root); return 0; }
    for (f = xchild(root, L"File"); f; f = xnext(f, L"File")) bm->n++;
    bm->f = (BmFile *)calloc(bm->n ? bm->n : 1, sizeof(BmFile)); bm->n = 0;
    for (f = xchild(root, L"File"); f; f = xnext(f, L"File")) {
        BmFile *x = &bm->f[bm->n++]; const WCHAR *nm = xattr(f, L"Name"), *sz = xattr(f, L"Size"); int k = 0;
        lstrcpynW(x->name, nm ? nm : L"", 1024); x->size = sz ? _wcstoui64(sz, NULL, 10) : 0;
        for (b = xchild(f, L"Block"); b; b = xnext(b, L"Block")) x->nblocks++;
        x->hash = calloc(x->nblocks ? x->nblocks : 1, 32);
        for (b = xchild(f, L"Block"); b; b = xnext(b, L"Block")) { const WCHAR *h = xattr(b, L"Hash"); if (!h || b64dec(h, x->hash[k++], 32) != 32) { _snwprintf(why, 256, L"AppxBlockMap.xml: bad block hash for %ls", x->name); xml_free(root); return 0; } }
    }
    xml_free(root); return 1;
}
static void blockmap_free(BlockMap *bm) { int i; for (i = 0; i < bm->n; i++) free(bm->f[i].hash); free(bm->f); memset(bm, 0, sizeof *bm); }
static BmFile *blockmap_find(BlockMap *bm, const WCHAR *name) { int i; for (i = 0; i < bm->n; i++) if (!_wcsicmp(bm->f[i].name, name)) return &bm->f[i]; return NULL; }
/* files a package has that its block map does not (cannot) list */
static int is_footprint_file(const WCHAR *n) {
    return !_wcsicmp(n, L"[Content_Types].xml") || !_wcsicmp(n, L"AppxBlockMap.xml") || !_wcsicmp(n, L"AppxSignature.p7x") || !_wcsicmp(n, L"AppxMetadata\\CodeIntegrity.cat");
}

/* streaming writer: writes to a file and checks 64 KB block hashes as it goes */
typedef struct { HANDLE out; BmFile *bm; BYTE *buf; DWORD fill; int block; ULONGLONG total; int bad; } Sink;
static int sink_flush_block(Sink *s) {
    BYTE h[32];
    if (!s->bm) return 1;
    if (s->block >= s->bm->nblocks || !sha256(s->buf, s->fill, h) || memcmp(h, s->bm->hash[s->block], 32)) { s->bad = 1; return 0; }
    s->block++; s->fill = 0; return 1;
}
static size_t sink_write(void *op, mz_uint64 ofs, const void *data, size_t n) {
    Sink *s = (Sink *)op; const BYTE *d = (const BYTE *)data; size_t left = n; DWORD put; (void)ofs;
    if (s->out != INVALID_HANDLE_VALUE && (!WriteFile(s->out, data, (DWORD)n, &put, NULL) || put != n)) return 0;
    s->total += n;
    while (left && s->bm) {
        DWORD take = BLOCK - s->fill; if (take > left) take = (DWORD)left;
        memcpy(s->buf + s->fill, d, take); s->fill += take; d += take; left -= take;
        if (s->fill == BLOCK && !sink_flush_block(s)) return 0;
    }
    return n;
}
static int sink_finish(Sink *s) {
    if (!s->bm) return 1;
    if (s->fill && !sink_flush_block(s)) return 0;
    if (s->block != s->bm->nblocks || s->total != s->bm->size) { s->bad = 1; return 0; }
    return 1;
}
static HANDLE create_out(const WCHAR *path) {
    WCHAR d[MAX_PATH], *sl; lstrcpynW(d, path, MAX_PATH); if ((sl = wcsrchr(d, '\\'))) { *sl = 0; mkdirs(d); }
    return CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* ================= environment ================= */
static WCHAR g_osver[32] = L"10.0.19045.0";
static int pick_root(const WCHAR *arg) {
    WCHAR p[MAX_PATH], ini[MAX_PATH], m[64], *s; DWORD n = 128;
    if (arg) lstrcpynW(p, arg, MAX_PATH);
    else {
        GetModuleFileNameW(NULL, p, MAX_PATH); if ((s = wcsrchr(p, '\\'))) *s = 0;
        if ((s = wcsrchr(p, '\\')) && !_wcsicmp(s + 1, L"Layer")) *s = 0;            /* <root>\Layer\wxrpkg.exe */
        else if (!GetEnvironmentVariableW(L"WXR_ROOT", p, MAX_PATH)) lstrcpyW(p, L"C:\\WXR");
    }
    GetFullPathNameW(p, MAX_PATH, g_root, NULL);
    { size_t l = wcslen(g_root); while (l > 3 && g_root[l - 1] == '\\') g_root[--l] = 0; }
    _snwprintf(ini, MAX_PATH, L"%ls\\environment.ini", g_root);
    GetPrivateProfileStringW(L"wxr", L"marker", L"", m, 64, ini);
    if (wcscmp(m, L"WXR environment root")) { err(L"no WXR environment at %ls (create one with: wxrsetup create)", g_root); return 0; }
    if (!GetUserNameW(g_user, &n)) lstrcpyW(g_user, L"user");
    _snwprintf(p, MAX_PATH, L"%ls\\Logs\\deploy.log", g_root); g_log = _wfopen(p, L"ab");
    { WCHAR b[64]; unsigned bn = 0, sp = 0;                                         /* the Windows version the image provides */
      GetPrivateProfileStringW(L"image", L"build", L"", b, 64, ini);
      if (swscanf(b, L"%u.%u", &bn, &sp) == 2) _snwprintf(g_osver, 32, L"10.0.%u.%u", bn == 19041 ? 19045 : bn, sp); }   /* 19041 image + 22H2 enablement = 19045 */
    _snwprintf(p, MAX_PATH, L"%ls\\ProgramData\\Packages", g_root); mkdirs(p);
    _snwprintf(p, MAX_PATH, L"%ls\\Users\\%ls\\Packages", g_root, g_user); mkdirs(p);
    return 1;
}
static void rec_path(const WCHAR *full, WCHAR *out) { _snwprintf(out, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls.ini", g_root, full); }
static void user_ini(const WCHAR *user, WCHAR *out) { _snwprintf(out, MAX_PATH, L"%ls\\Users\\%ls\\packages.ini", g_root, user); }
static void rec_get(const WCHAR *full, const WCHAR *key, WCHAR *out, DWORD n) { WCHAR r[MAX_PATH]; rec_path(full, r); GetPrivateProfileStringW(L"package", key, L"", out, n, r); }
static int is_staged(const WCHAR *full) { WCHAR r[MAX_PATH], m[MAX_PATH]; rec_path(full, r); _snwprintf(m, MAX_PATH, L"%ls\\WindowsApps\\%ls\\AppxManifest.xml", g_root, full); return exists(r) && exists(m); }
/* the registered full names of a user (double-NUL list in buf) */
static WCHAR *user_list(const WCHAR *user) {
    WCHAR u[MAX_PATH]; DWORD cap = 1 << 16; WCHAR *buf = (WCHAR *)calloc(cap, sizeof(WCHAR));
    user_ini(user, u); GetPrivateProfileStringW(L"registered", NULL, L"", buf, cap, u); return buf;
}
static int user_has(const WCHAR *user, const WCHAR *full) { WCHAR *l = user_list(user), *s; int r = 0; for (s = l; *s; s += wcslen(s) + 1) if (!_wcsicmp(s, full)) r = 1; free(l); return r; }
static void user_set(const WCHAR *user, const WCHAR *full, int on) { WCHAR u[MAX_PATH], t[32]; user_ini(user, u); now_str(t); WritePrivateProfileStringW(L"registered", full, on ? t : NULL, u); }

/* ================= reading packages ================= */
static int load_zip_pkg(const WCHAR *path, Pkg *p, WCHAR *why) {
    Zip z; size_t n; void *m;
    if (!zip_open(&z, path, why)) return 0;
    m = zip_read(&z, L"AppxManifest.xml", &n);
    if (!m) { zip_close(&z); _snwprintf(why, 256, L"%ls has no AppxManifest.xml%ls", path, zip_find(&z, L"AppxMetadata\\AppxBundleManifest.xml") >= 0 ? L" (it is a bundle)" : L""); return 0; }
    memset(p, 0, sizeof *p);
    if (!parse_manifest(m, n, p, why)) { mz_free(m); zip_close(&z); return 0; }
    mz_free(m); zip_close(&z);
    p->kind = 0; lstrcpynW(p->src, path, MAX_PATH); return 1;
}
static int load_dir_pkg(const WCHAR *dir, Pkg *p, WCHAR *why) {
    WCHAR m[MAX_PATH]; HANDLE f; DWORD n, got; BYTE *b; int ok;
    _snwprintf(m, MAX_PATH, L"%ls\\AppxManifest.xml", dir);
    f = CreateFileW(m, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { _snwprintf(why, 256, L"no AppxManifest.xml in %ls", dir); return 0; }
    n = GetFileSize(f, NULL); b = (BYTE *)malloc(n + 1); ReadFile(f, b, n, &got, NULL); CloseHandle(f);
    memset(p, 0, sizeof *p);
    ok = parse_manifest(b, got, p, why); free(b);
    if (ok) { p->kind = 1; lstrcpynW(p->src, dir, MAX_PATH); }
    return ok;
}
static int user_scale(void) { HDC dc = GetDC(NULL); int dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc); return dpi * 100 / 96; }
static void user_lang(WCHAR *out) { if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), out, LOCALE_NAME_MAX_LENGTH, 0)) lstrcpyW(out, L"en-US"); }
static int lang_match(const WCHAR *a, const WCHAR *b, int exact) {   /* "en-us" vs "en-US"; not exact: same primary language */
    const WCHAR *da = wcschr(a, '-'), *db = wcschr(b, '-'); size_t la = da ? (size_t)(da - a) : wcslen(a), lb = db ? (size_t)(db - b) : wcslen(b);
    if (exact) return !_wcsicmp(a, b);
    return la == lb && !_wcsnicmp(a, b, la);
}
/* bundle: choose the x64 (or neutral) application package + resource packs for the user's language and scale, copy them to tmp */
static int load_bundle(const WCHAR *path, const WCHAR *tmpdir, Pkg *out, int *nout, int cap, WCHAR *why) {
    Zip z; size_t n; void *m; WCHAR e[256], lang[LOCALE_NAME_MAX_LENGTH], bname[256] = L"", bpub[512] = L"", bver[32] = L"", bpid[16], bfull[400];
    XNode *root, *pk, *app = NULL, *x; int scale = user_scale(), best_scale = 0, i; const WCHAR *v; XNode *pick[64]; int npick = 0;
    BlockMap bm; int base = *nout;
    if (!zip_open(&z, path, why)) return 0;
    if (!(m = zip_read(&z, L"AppxMetadata\\AppxBundleManifest.xml", &n))) { zip_close(&z); _snwprintf(why, 256, L"%ls: no AppxMetadata\\AppxBundleManifest.xml", path); return 0; }
    { size_t bn; void *bmx = zip_read(&z, L"AppxBlockMap.xml", &bn); BmFile *bf; Sink s;            /* the bundle's block map covers its manifest */
      if (bmx && blockmap_parse(bmx, bn, &bm, why)) {
          if ((bf = blockmap_find(&bm, L"AppxMetadata\\AppxBundleManifest.xml"))) {
              memset(&s, 0, sizeof s); s.out = INVALID_HANDLE_VALUE; s.bm = bf; s.buf = (BYTE *)malloc(BLOCK);
              sink_write(&s, 0, m, n); if (!sink_finish(&s)) { free(s.buf); blockmap_free(&bm); mz_free(bmx); mz_free(m); zip_close(&z); _snwprintf(why, 256, L"%ls: the bundle manifest does not match the block map (damaged file)", path); return 0; }
              free(s.buf); }
          blockmap_free(&bm); }
      if (bmx) mz_free(bmx); }
    root = xml_parse(m, n, e); mz_free(m);
    if (!root) { zip_close(&z); _snwprintf(why, 256, L"AppxBundleManifest.xml: %ls", e); return 0; }
    if ((x = xchild(root, L"Identity"))) { lstrcpynW(bname, (v = xattr(x, L"Name")) ? v : L"", 256); lstrcpynW(bpub, (v = xattr(x, L"Publisher")) ? v : L"", 512); lstrcpynW(bver, (v = xattr(x, L"Version")) ? v : L"", 32); }
    publisher_id(bpub, bpid); _snwprintf(bfull, 400, L"%ls_%ls_neutral_~_%ls", bname, bver, bpid);
    user_lang(lang);
    info(L"Bundle %ls: your language %ls, display scale %d", bfull, lang, scale);
    pk = xchild(root, L"Packages");
    /* Stub packages (b5:IsStub="true", AppxMetadata\Stub\...: a tiny launcher Win10 installs first and swaps for the full app on first
       use) are not installable apps here: taken out of the selection by blanking their Type. Seen in Windows Clock 11.2607.20. */
    for (x = pk ? xchild(pk, L"Package") : NULL; x; x = xnext(x, L"Package")) {
        const WCHAR *st = xattr(x, L"IsStub"), *fn = xattr(x, L"FileName"); WCHAR *ty = (WCHAR *)xattr(x, L"Type");
        if (ty && ((st && !_wcsicmp(st, L"true")) || (fn && !_wcsnicmp(fn, L"AppxMetadata\\Stub\\", 18)))) { dbg(L"bundle: stub package %ls left out", fn ? fn : L"?"); ty[0] = 0; }
    }
    for (x = pk ? xchild(pk, L"Package") : NULL; x; x = xnext(x, L"Package")) {      /* application package: x64, else neutral */
        const WCHAR *t = xattr(x, L"Type"), *a = xattr(x, L"Architecture");
        if (t && !_wcsicmp(t, L"application") && a) { if (!_wcsicmp(a, L"x64")) { app = x; break; } if (!_wcsicmp(a, L"neutral") && !app) app = x; }
    }
    if (!app) {
        WCHAR archs[128] = L"";
        for (x = pk ? xchild(pk, L"Package") : NULL; x; x = xnext(x, L"Package")) if ((v = xattr(x, L"Type")) && !_wcsicmp(v, L"application") && (v = xattr(x, L"Architecture")) && wcslen(archs) + wcslen(v) < 120) { wcscat(archs, v); wcscat(archs, L" "); }
        xml_free(root); zip_close(&z); _snwprintf(why, 256, L"the bundle has no x64 or neutral application package (it has: %ls)", archs[0] ? archs : L"none"); return 0; }
    pick[npick++] = app;
    { int have_lang = 0, have_scale = 0, exact_lang = 0; XNode *r;                    /* does the application package already cover them? */
      for (r = xchild(xchild(app, L"Resources"), L"Resource"); r; r = xnext(r, L"Resource")) {
          if ((v = xattr(r, L"Language")) && lang_match(v, lang, 0)) have_lang = 1;
          if ((v = xattr(r, L"Scale")) && _wtoi(v) == scale) have_scale = 1; }
      for (x = xchild(pk, L"Package"); x; x = xnext(x, L"Package")) {                 /* exact language match available? */
          if (!(v = xattr(x, L"Type")) || _wcsicmp(v, L"resource")) continue;
          for (r = xchild(xchild(x, L"Resources"), L"Resource"); r; r = xnext(r, L"Resource")) if ((v = xattr(r, L"Language")) && lang_match(v, lang, 1)) exact_lang = 1; }
      for (x = xchild(pk, L"Package"); x; x = xnext(x, L"Package")) {
          int s;
          if (!(v = xattr(x, L"Type")) || _wcsicmp(v, L"resource")) continue;
          for (r = xchild(xchild(x, L"Resources"), L"Resource"); r; r = xnext(r, L"Resource")) {
              if (!have_lang && (v = xattr(r, L"Language")) && lang_match(v, lang, exact_lang) && npick < 64) { pick[npick++] = x; break; }
              if (!have_scale && (v = xattr(r, L"Scale")) && (s = _wtoi(v)) > 0) {                 /* closest scale, prefer larger */
                  if (!best_scale || (abs(s - scale) < abs(best_scale - scale)) || (abs(s - scale) == abs(best_scale - scale) && s > best_scale)) best_scale = s; }
          } }
      if (!have_scale && best_scale) for (x = xchild(pk, L"Package"); x; x = xnext(x, L"Package")) {
          if (!(v = xattr(x, L"Type")) || _wcsicmp(v, L"resource")) continue;
          for (r = xchild(xchild(x, L"Resources"), L"Resource"); r; r = xnext(r, L"Resource")) if ((v = xattr(r, L"Scale")) && _wtoi(v) == best_scale && npick < 64) { pick[npick++] = x; break; } }
    }
    for (i = 0; i < npick; i++) {
        const WCHAR *fn = xattr(pick[i], L"FileName"); WCHAR dst[MAX_PATH], w2[256]; int idx; Sink s; Pkg *p;
        if (*nout >= cap) break;
        if (!fn || (idx = zip_find(&z, fn)) < 0) { xml_free(root); zip_close(&z); _snwprintf(why, 256, L"the bundle lists %ls but does not contain it", fn ? fn : L"?"); return 0; }
        _snwprintf(dst, MAX_PATH, L"%ls\\%ls", tmpdir, fn);
        memset(&s, 0, sizeof s); s.out = create_out(dst);
        if (s.out == INVALID_HANDLE_VALUE || !mz_zip_reader_extract_to_callback(&z.z, (mz_uint)idx, sink_write, &s, 0)) { if (s.out != INVALID_HANDLE_VALUE) CloseHandle(s.out); xml_free(root); zip_close(&z); _snwprintf(why, 256, L"cannot unpack %ls from the bundle", fn); return 0; }
        CloseHandle(s.out);
        p = &out[*nout];
        if (!load_zip_pkg(dst, p, why)) { xml_free(root); zip_close(&z); return 0; }
        lstrcpynW(p->bundle, bfull, 400); (*nout)++;
        lstrcpynW(w2, fn, 256);
        info(L"  chose %ls%ls", w2, i == 0 ? L" (application)" : L"");
    }
    xml_free(root); zip_close(&z);
    return *nout > base;
}

/* ================= staging ================= */
static int stage_from_zip(Pkg *p, const WCHAR *dir, WCHAR *why) {
    Zip z; size_t n; void *bmx; BlockMap bm; unsigned i; int ok = 1, nfiles = 0; BYTE *buf = (BYTE *)malloc(BLOCK);
    if (!zip_open(&z, p->src, why)) { free(buf); return 0; }
    if (!(bmx = zip_read(&z, L"AppxBlockMap.xml", &n))) { zip_close(&z); free(buf); _snwprintf(why, 256, L"the package has no AppxBlockMap.xml"); return 0; }
    if (!blockmap_parse(bmx, n, &bm, why)) { mz_free(bmx); zip_close(&z); free(buf); return 0; }
    mz_free(bmx);
    for (i = 0; i < z.n && ok; i++) {
        WCHAR dst[MAX_PATH + 8]; Sink s; BmFile *bf; const WCHAR *nm = z.names[i];
        if (mz_zip_reader_is_file_a_directory(&z.z, i)) continue;
        if (!_wcsicmp(nm, L"[Content_Types].xml")) continue;                          /* packaging-only file, not installed (as on Win10) */
        bf = blockmap_find(&bm, nm);
        if (!bf && !is_footprint_file(nm)) { _snwprintf(why, 256, L"%ls is not listed in the block map (damaged or tampered package)", nm); ok = 0; break; }
        if (wcsstr(nm, L"..")) { _snwprintf(why, 256, L"unsafe file name in the package: %ls", nm); ok = 0; break; }
        if (wcslen(dir) + 1 + wcslen(nm) >= MAX_PATH) { _snwprintf(why, 256, L"path too long for Windows 7 (%u characters): %ls\\%ls", (unsigned)(wcslen(dir) + 1 + wcslen(nm)), dir, nm); ok = 0; break; }
        _snwprintf(dst, MAX_PATH, L"%ls\\%ls", dir, nm);
        memset(&s, 0, sizeof s); s.bm = bf; s.buf = buf; s.out = create_out(dst);
        if (s.out == INVALID_HANDLE_VALUE) { _snwprintf(why, 256, L"cannot create %ls (error %lu)", dst, GetLastError()); ok = 0; break; }
        if (!mz_zip_reader_extract_to_callback(&z.z, i, sink_write, &s, 0) || !sink_finish(&s)) {
            CloseHandle(s.out);
            if (s.bad) _snwprintf(why, 256, L"%ls does not match its block map hash (damaged or tampered package)", nm);
            else _snwprintf(why, 256, L"cannot unpack %ls (zip: %hs)", nm, mz_zip_get_error_string(mz_zip_get_last_error(&z.z)));
            ok = 0; break; }
        CloseHandle(s.out); if (bf) bf->seen = 1; nfiles++;
    }
    if (ok) for (i = 0; i < (unsigned)bm.n; i++) if (!bm.f[i].seen) { _snwprintf(why, 256, L"the block map lists %ls but the package does not contain it", bm.f[i].name); ok = 0; break; }
    if (ok) dbg(L"staged %d files from %ls (block map checked)", nfiles, p->src);
    blockmap_free(&bm); zip_close(&z); free(buf); return ok;
}
/* folder packages (an already installed copy, e.g. from WindowsApps): copied file by file, checked against its block map if it has one.
   Files the block map does not list were added after install (by hand or by other tools) and are left out, so the staged copy is the package. */
static int g_extra;
static int copy_dir_verified(const WCHAR *src, const WCHAR *dst, const WCHAR *rel, BlockMap *bm, BYTE *buf, int *nfiles, WCHAR *why) {
    WCHAR pat[MAX_PATH], s[MAX_PATH], d[MAX_PATH], r[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; int ok = 1;
    _snwprintf(pat, MAX_PATH, L"%ls%ls%ls\\*", src, rel[0] ? L"\\" : L"", rel); h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 1;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        _snwprintf(r, MAX_PATH, L"%ls%ls%ls", rel, rel[0] ? L"\\" : L"", fd.cFileName);
        _snwprintf(s, MAX_PATH, L"%ls\\%ls", src, r); _snwprintf(d, MAX_PATH, L"%ls\\%ls", dst, r);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (!rel[0] && !_wcsicmp(fd.cFileName, L"microsoft.system.package.metadata")) { dbg(L"skipped %ls (written by Windows after install, not part of the package)", s); continue; }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { ok = copy_dir_verified(src, dst, r, bm, buf, nfiles, why); continue; }
        { BmFile *bf = bm ? blockmap_find(bm, r) : NULL; HANDLE in; Sink sk; DWORD got;
          if (bm && !bf && !is_footprint_file(r)) {                                 /* added to the folder after install: not part of the package */
              g_extra++; dbg(L"left out %ls (not in the block map)", r); continue; }
          if (wcslen(d) >= MAX_PATH - 1) { _snwprintf(why, 256, L"path too long for Windows 7: %ls", d); ok = 0; break; }
          in = CreateFileW(s, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
          if (in == INVALID_HANDLE_VALUE) { _snwprintf(why, 256, L"cannot read %ls (error %lu)", s, GetLastError()); ok = 0; break; }
          memset(&sk, 0, sizeof sk); sk.bm = bf; sk.buf = buf; sk.out = create_out(d);
          if (sk.out == INVALID_HANDLE_VALUE) { CloseHandle(in); _snwprintf(why, 256, L"cannot create %ls (error %lu)", d, GetLastError()); ok = 0; break; }
          { static BYTE io[1 << 20]; while (ReadFile(in, io, sizeof io, &got, NULL) && got) if (!sink_write(&sk, 0, io, got)) break; }
          CloseHandle(in); CloseHandle(sk.out);
          if (!sink_finish(&sk)) { _snwprintf(why, 256, L"%ls does not match its block map hash", r); ok = 0; break; }
          if (bf) bf->seen = 1;
          (*nfiles)++; }
    } while (ok && FindNextFileW(h, &fd));
    FindClose(h); return ok;
}
static int stage_from_dir(Pkg *p, const WCHAR *dir, WCHAR *why) {
    WCHAR bmp[MAX_PATH]; HANDLE f; BlockMap bm, *pbm = NULL; BYTE *buf = (BYTE *)malloc(BLOCK); int ok, nfiles = 0, i;
    _snwprintf(bmp, MAX_PATH, L"%ls\\AppxBlockMap.xml", p->src);
    if ((f = CreateFileW(bmp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL)) != INVALID_HANDLE_VALUE) {
        DWORD n = GetFileSize(f, NULL), got; BYTE *b = (BYTE *)malloc(n + 1); ReadFile(f, b, n, &got, NULL); CloseHandle(f);
        if (!blockmap_parse(b, got, &bm, why)) { free(b); free(buf); return 0; }
        free(b); pbm = &bm;
    } else warn(L"%ls has no AppxBlockMap.xml: its files are copied without checking", p->src);
    g_extra = 0;
    ok = copy_dir_verified(p->src, dir, L"", pbm, buf, &nfiles, why);
    if (ok && g_extra) warn(L"%ls: %d file(s) in the folder are not part of the package (not in its block map) and were left out; see Logs\\deploy.log", p->fullname, g_extra);
    if (ok && pbm) for (i = 0; i < bm.n; i++) if (!bm.f[i].seen) { _snwprintf(why, 256, L"the block map lists %ls but the folder does not contain it", bm.f[i].name); ok = 0; break; }
    if (ok) dbg(L"staged %d files from folder %ls%ls", nfiles, p->src, pbm ? L" (block map checked)" : L"");
    if (pbm) blockmap_free(pbm);
    free(buf); return ok;
}
static void write_record(Pkg *p) {
    WCHAR r[MAX_PATH], loc[MAX_PATH], t[32], k[16]; int i;
    rec_path(p->fullname, r); DeleteFileW(r);
    _snwprintf(loc, MAX_PATH, L"%ls\\WindowsApps\\%ls", g_root, p->fullname); now_str(t);
#define W(key, val) WritePrivateProfileStringW(L"package", key, val, r)
    W(L"Name", p->name); W(L"Publisher", p->publisher); W(L"PublisherId", p->pubid); W(L"Version", p->version); W(L"Architecture", p->arch);
    W(L"ResourceId", p->resid); W(L"PackageFullName", p->fullname); W(L"PackageFamilyName", p->family); W(L"InstallLocation", loc);
    W(L"IsFramework", p->framework ? L"True" : L"False"); W(L"IsResourcePackage", p->resource ? L"True" : L"False");
    W(L"Bundle", p->bundle); W(L"Source", p->src); W(L"Staged", t); W(L"SignatureKind", L"None"); W(L"TargetMinVersion", p->os_min);
#undef W
    for (i = 0; i < p->nresolved; i++) { _snwprintf(k, 16, L"%d", i); WritePrivateProfileStringW(L"dependencies", k, p->resolved[i], r); }
    for (i = 0; i < p->napps; i++) { WCHAR v[600]; _snwprintf(v, 600, L"%ls|%ls", p->apps[i].exe, p->apps[i].entry); WritePrivateProfileStringW(L"applications", p->apps[i].id, v, r); }
}
static int stage(Pkg *p) {
    WCHAR fin[MAX_PATH], part[MAX_PATH], why[256] = L""; int ok;
    _snwprintf(fin, MAX_PATH, L"%ls\\WindowsApps\\%ls", g_root, p->fullname);
    if (is_staged(p->fullname)) { dbg(L"%ls is already staged", p->fullname); return 1; }
    _snwprintf(part, MAX_PATH, L"%ls.partial", fin);
    if (is_dir(part)) rm_tree(part);
    if (is_dir(fin)) rm_tree(fin);                                                 /* folder without a record: an interrupted install */
    if (!mkdirs(part)) { err(L"cannot create %ls (error %lu)", part, GetLastError()); return 0; }
    info(L"  staging %ls", p->fullname);
    ok = p->kind == 0 ? stage_from_zip(p, part, why) : stage_from_dir(p, part, why);
    if (!ok) { err(L"%ls: %ls", p->fullname, why); rm_tree(part); return 0; }
    {   /* a virus scanner (or the indexer) may still hold a just-written file open: retry for up to ~10 s */
        int tries; DWORD e = 0;
        for (tries = 0; tries < 40; tries++) { if (MoveFileExW(part, fin, 0)) break; e = GetLastError(); if (e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) break; Sleep(250); }
        if (tries) dbg(L"rename of %ls needed %d retries (last error %lu)", part, tries, e);
        if (!is_dir(fin)) { err(L"cannot rename %ls (error %lu)", part, e); rm_tree(part); return 0; }
    }
    write_record(p); return 1;
}

/* ================= resolution ================= */
static int arch_ok(const WCHAR *a) { return !_wcsicmp(a, L"x64") || !_wcsicmp(a, L"neutral"); }
/* best package satisfying d: from the batch, else staged in the store. Returns full name in out */
static int resolve_dep(const Dep *d, Pkg *batch, int nb, WCHAR *out) {
    WCHAR pid[16], pat[MAX_PATH]; ULONGLONG minv = 0, best = 0; int i; WIN32_FIND_DATAW fd; HANDLE h;
    publisher_id(d->publisher, pid); parse_ver(d->minver, &minv); out[0] = 0;
    for (i = 0; i < nb; i++) if (!_wcsicmp(batch[i].name, d->name) && !wcscmp(batch[i].pubid, pid) && !batch[i].resource && arch_ok(batch[i].arch) && batch[i].ver >= minv && batch[i].ver >= best) { best = batch[i].ver; lstrcpynW(out, batch[i].fullname, 400); }
    _snwprintf(pat, MAX_PATH, L"%ls\\ProgramData\\Packages\\%ls_*.ini", g_root, d->name);
    if ((h = FindFirstFileW(pat, &fd)) != INVALID_HANDLE_VALUE) {
        do { WCHAR full[400], nm[256], ar[16], vs[32], pi[16], rs[8]; ULONGLONG v;
             lstrcpynW(full, fd.cFileName, 400); full[wcslen(full) - 4] = 0;
             rec_get(full, L"Name", nm, 256); rec_get(full, L"Architecture", ar, 16); rec_get(full, L"Version", vs, 32); rec_get(full, L"PublisherId", pi, 16); rec_get(full, L"IsResourcePackage", rs, 8);
             if (!_wcsicmp(nm, d->name) && !wcscmp(pi, pid) && wcscmp(rs, L"True") && arch_ok(ar) && parse_ver(vs, &v) && v >= minv && v > best && is_staged(full)) { best = v; lstrcpynW(out, full, 400); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out[0] != 0;
}
/* registered package of the same family and kind (same ResourceId) for this user */
static int registered_same_family(const Pkg *p, WCHAR *out) {
    WCHAR *l = user_list(g_user), *s; int r = 0;
    for (s = l; *s; s += wcslen(s) + 1) { WCHAR fam[300], rid[128]; rec_get(s, L"PackageFamilyName", fam, 300); rec_get(s, L"ResourceId", rid, 128);
        if (!_wcsicmp(fam, p->family) && !_wcsicmp(rid, p->resid) && _wcsicmp(s, p->fullname)) { lstrcpynW(out, s, 400); r = 1; break; } }
    free(l); return r;
}

/* ================= add ================= */
static void make_data_dirs(const WCHAR *family) {
    static const WCHAR *sub[] = { L"AC", L"AppData", L"LocalCache", L"LocalState", L"RoamingState", L"Settings", L"SystemAppData", L"TempState" };
    WCHAR p[MAX_PATH]; unsigned i;
    for (i = 0; i < sizeof sub / sizeof *sub; i++) { _snwprintf(p, MAX_PATH, L"%ls\\Users\\%ls\\Packages\\%ls\\%ls", g_root, g_user, family, sub[i]); mkdirs(p); }
}
static void gc_store(void);
static int unregister_pkg(const WCHAR *full, int delete_data);
#define MAXB 256
/* a resource pack from -DependencyPath is used only if it is for the user's UI language or the display scale (as a bundle would pick) */
static int resource_fits(const Pkg *p) {
    WCHAR lang[LOCALE_NAME_MAX_LENGTH], l[512], *tok, *ctx = NULL; int scale = user_scale();
    user_lang(lang); lstrcpynW(l, p->langs, 512);
    for (tok = wcstok(l, L";", &ctx); tok; tok = wcstok(NULL, L";", &ctx)) if (lang_match(tok, lang, 0)) return 1;
    return p->scales[0] && _wtoi(p->scales) == scale;
}
/* loads one input into the batch. dep = from -DependencyPath: problems there skip the file instead of failing the install */
static int load_input(const WCHAR *full, DWORD a, int dep, const WCHAR *tmp, Pkg *b, int *nb, WCHAR *why) {
    const WCHAR *ext = wcsrchr(full, '.'); int before = *nb, ok, i;
    if (*nb >= MAXB - 70) { _snwprintf(why, 256, L"too many packages in one install"); return 0; }
    if (a & FILE_ATTRIBUTE_DIRECTORY) { ok = load_dir_pkg(full, &b[*nb], why); if (ok) (*nb)++; }
    else if (ext && (!_wcsicmp(ext, L".appxbundle") || !_wcsicmp(ext, L".msixbundle"))) ok = load_bundle(full, tmp, b, nb, MAXB, why);
    else if (ext && (!_wcsicmp(ext, L".appx") || !_wcsicmp(ext, L".msix"))) { ok = load_zip_pkg(full, &b[*nb], why); if (ok) (*nb)++; }
    else if (ext && (!_wcsicmp(ext, L".eappx") || !_wcsicmp(ext, L".emsix") || !_wcsicmp(ext, L".eappxbundle") || !_wcsicmp(ext, L".emsixbundle"))) { _snwprintf(why, 256, L"%ls is an encrypted Store package; WXR cannot install those (they need the Store's licensing)", full); ok = 0; }
    else { _snwprintf(why, 256, L"%ls is not a package (.appx, .msix, .appxbundle, .msixbundle, a package folder or its AppxManifest.xml)", full); ok = 0; }
    for (i = before; i < *nb; i++) b[i].is_dep = dep;
    return ok;
}
static int is_pkg_file(const WCHAR *n) { const WCHAR *e = wcsrchr(n, '.'); return e && (!_wcsicmp(e, L".appx") || !_wcsicmp(e, L".msix") || !_wcsicmp(e, L".appxbundle") || !_wcsicmp(e, L".msixbundle")); }
static void st_shell_sync(void);   /* store.inc: wxr sync after an install */
static int gdk_on(void); static int gdk_gameinput(const WCHAR *pkgdir);   /* gdk.inc: a GDK game's GameInput */
static int cmd_add(WCHAR **paths, int npaths, WCHAR **dpaths, int ndpaths, int force_update, int any_os) {
    Pkg *b = (Pkg *)calloc(MAXB, sizeof(Pkg)); int nb = 0, i, j, rc = 1, order[MAXB], no = 0, pass, changed, nskip = 0; WCHAR tmp[MAX_PATH], why[256];
    char *needed = (char *)calloc(MAXB, 1);
    ULONGLONG osv = 0; parse_ver(g_osver, &osv);
    _snwprintf(tmp, MAX_PATH, L"%ls\\Temp\\deploy-%lu", g_root, GetCurrentProcessId()); mkdirs(tmp);
    /* 1. open every input: dependencies first (a folder without AppxManifest.xml = every package file in it), then the packages */
    for (pass = 0; pass < 2; pass++) for (i = 0; i < (pass ? npaths : ndpaths); i++) {
        const WCHAR *in = pass ? paths[i] : dpaths[i]; WCHAR full[MAX_PATH], *ext, man[MAX_PATH]; DWORD a;
        if (!GetFullPathNameW(in, MAX_PATH, full, NULL) || (a = GetFileAttributesW(full)) == INVALID_FILE_ATTRIBUTES) { err(L"cannot find %ls", in); goto out; }
        ext = wcsrchr(full, '.');
        if (!(a & FILE_ATTRIBUTE_DIRECTORY) && ext && !_wcsicmp(ext, L".xml")) { WCHAR *sl = wcsrchr(full, '\\'); *sl = 0; a = FILE_ATTRIBUTE_DIRECTORY; }   /* ...\AppxManifest.xml */
        _snwprintf(man, MAX_PATH, L"%ls\\AppxManifest.xml", full);
        if (!pass && (a & FILE_ATTRIBUTE_DIRECTORY) && !exists(man)) {                   /* a folder of dependency packages */
            WCHAR pat[MAX_PATH], f[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; int cnt = 0;
            info(L"Reading the packages in %ls", full);
            _snwprintf(pat, MAX_PATH, L"%ls\\*", full);
            if ((h = FindFirstFileW(pat, &fd)) != INVALID_HANDLE_VALUE) {
                do { WCHAR sub[MAX_PATH];
                     if (fd.cFileName[0] == '.') continue;
                     _snwprintf(f, MAX_PATH, L"%ls\\%ls", full, fd.cFileName); _snwprintf(sub, MAX_PATH, L"%ls\\AppxManifest.xml", f);
                     if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? !exists(sub) : !is_pkg_file(fd.cFileName)) continue;   /* package files and package folders */
                     cnt++;
                     if (!load_input(f, fd.dwFileAttributes, 1, tmp, b, &nb, why)) { info(L"  skipped %ls: %ls", fd.cFileName, why); nskip++; }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (!cnt) warn(L"%ls contains no packages", full);
            continue;
        }
        info(L"Reading %ls", full);
        if (!load_input(full, a, !pass, tmp, b, &nb, why)) { if (pass) { err(L"%ls", why); goto out; } info(L"  skipped %ls: %ls", full, why); nskip++; }
    }
    /* 2. check each package */
    for (i = 0; i < nb; i++) {
        Pkg *p = &b[i];
        if (!arch_ok(p->arch)) {
            if (p->is_dep) { p->skip = 4; dbg(L"%ls: %ls package, not usable here", p->fullname, p->arch); continue; }
            err(L"%ls is a %ls package; WXR runs x64 and neutral packages only", p->fullname, p->arch); goto out; }
        for (j = 0; j < i; j++) if (!b[j].skip && !_wcsicmp(b[j].fullname, p->fullname)) {   /* listed twice: the copy given as a package wins */
            if (b[j].is_dep && !p->is_dep) b[j].skip = 2; else { p->skip = 2; break; } }
        if (!p->skip && p->is_dep && p->resource && !resource_fits(p)) { p->skip = 3; dbg(L"%ls: language/scale not used here", p->fullname); }
    }
    for (i = 0; i < nb; i++) {                                                      /* resolve; failures are reported only for packages that are needed */
        Pkg *p = &b[i];
        if (p->skip) continue;
        for (j = 0; j < p->ndeps; j++) {
            if (!resolve_dep(&p->deps[j], b, nb, p->resolved[p->nresolved])) { if (!p->missing) p->missing = j + 1; continue; }
            dbg(L"%ls: dependency %ls -> %ls", p->fullname, p->deps[j].name, p->resolved[p->nresolved]);
            p->nresolved++;
        }
    }
    for (i = 0; i < nb; i++) if (!b[i].skip && !b[i].is_dep) needed[i] = 1;         /* what the packages (transitively) need */
    do { changed = 0;
         for (i = 0; i < nb; i++) { if (!needed[i]) continue;
             for (j = 0; j < b[i].nresolved; j++) { int k; for (k = 0; k < nb; k++) if (!needed[k] && !b[k].skip && !_wcsicmp(b[k].fullname, b[i].resolved[j])) { needed[k] = 1; changed = 1; } } }
         for (i = 0; i < nb; i++) if (!needed[i] && !b[i].skip && b[i].resource) { int k; for (k = 0; k < nb; k++) if (needed[k] && !b[k].resource && !_wcsicmp(b[k].family, b[i].family) && b[k].ver == b[i].ver) { needed[i] = 1; changed = 1; } }
    } while (changed);
    { int unused = 0, other = 0;
      for (i = 0; i < nb; i++) { if (b[i].skip == 4) other++; else if (!b[i].skip && !needed[i]) { b[i].skip = 3; unused++; dbg(L"not needed: %ls", b[i].fullname); } }
      if (unused || other) info(L"  %d dependency package(s) not needed and %d for other architectures left out (details in Logs\\deploy.log)", unused, other); }
    for (i = 0; i < nb; i++) {
        Pkg *p = &b[i];
        if (p->skip) continue;
        info(L"  %ls%ls", p->fullname, p->framework ? L" (framework)" : p->resource ? L" (resource)" : L"");
        if (p->os_min[0] && !p->resource) { ULONGLONG mv = 0; parse_ver(p->os_min, &mv);
            if (mv > osv) { if (any_os) warn(L"%ls needs Windows %ls; this environment provides %ls (installing anyway: --any-os)", p->fullname, p->os_min, g_osver);
                            else { err(L"%ls needs Windows %ls or newer; this environment provides %ls (--any-os installs it anyway)", p->fullname, p->os_min, g_osver); goto out; } } }
    }
    for (i = 0; i < nb; i++) {
        Pkg *p = &b[i]; WCHAR old[400];
        if (p->skip) continue;
        if (p->missing) { Dep *d = &p->deps[p->missing - 1];
            err(L"%ls needs %ls (version %ls or newer, x64 or neutral), which is not installed. Add it with -DependencyPath.", p->fullname, d->name, d->minver); goto out; }
        if (p->resource) {                                                        /* a resource pack needs its main package (in the batch or registered) */
            int found = 0; WCHAR *l = user_list(g_user), *s;
            for (j = 0; j < nb; j++) if (!b[j].resource && !_wcsicmp(b[j].family, p->family) && b[j].ver == p->ver) found = 1;
            for (s = l; !found && *s; s += wcslen(s) + 1) { WCHAR fam[300], vs[32]; ULONGLONG v; rec_get(s, L"PackageFamilyName", fam, 300); rec_get(s, L"Version", vs, 32); if (!_wcsicmp(fam, p->family) && parse_ver(vs, &v) && v == p->ver) found = 1; }
            free(l);
            if (!found) { err(L"%ls is a resource package; install it together with its main package (same version)", p->fullname); goto out; }
        }
        if (user_has(g_user, p->fullname) && is_staged(p->fullname)) { p->skip = 1; info(L"  %ls is already installed", p->fullname); continue; }
        if (!p->framework && registered_same_family(p, old)) {             /* frameworks live side by side, as on Win10 */
            WCHAR vs[32]; ULONGLONG v = 0; rec_get(old, L"Version", vs, 32); parse_ver(vs, &v);
            if (v > p->ver && !force_update) { err(L"a newer version is installed (%ls); -ForceUpdateFromAnyVersion installs %ls anyway", old, p->version); goto out; }
            lstrcpynW(p->replaces, old, 400);
        }
    }
    /* 3. stage + register: frameworks first, then main packages, then resource packs */
    for (pass = 0; pass < 3; pass++) for (i = 0; i < nb; i++) { int k = b[i].framework ? 0 : b[i].resource ? 2 : 1; if (k == pass) order[no++] = i; }
    for (i = 0; i < no; i++) { Pkg *p = &b[order[i]]; if (p->skip) continue; if (!stage(p)) goto out; }
    for (i = 0; i < no; i++) {
        Pkg *p = &b[order[i]];
        if (p->skip) continue;
        user_set(g_user, p->fullname, 1);
        if (!p->framework && !p->resource) make_data_dirs(p->family);
        if (p->replaces[0]) { info(L"  %ls replaces %ls", p->fullname, p->replaces); unregister_pkg(p->replaces, 0); }
        info(L"Installed %ls", p->fullname);
        if (!p->framework && !p->resource && gdk_on()) { WCHAR d[MAX_PATH]; _snwprintf(d, MAX_PATH, L"%ls\\WindowsApps\\%ls", g_root, p->fullname); gdk_gameinput(d); }
    }
    /* a new app: start menu entry, icon and display name (wxr sync). Without it the app was installed but neither in the start menu nor
       launchable through Package.GetAppListEntries (App Installer's "Launch when ready" did nothing). */
    { int app = 0; for (i = 0; i < no; i++) { Pkg *p = &b[order[i]]; if (!p->skip && !p->framework && !p->resource) app = 1; } if (app) st_shell_sync(); }
    rc = 0;
out:
    gc_store();                                       /* also drops what a failed install had staged already */
    rm_tree(tmp);
    free(b); free(needed); (void)nskip;
    return rc;
}

/* ================= remove / gc ================= */
static int depends_on(const WCHAR *pkg, const WCHAR *dep) {
    WCHAR r[MAX_PATH], buf[8192], *k; int found = 0; rec_path(pkg, r);
    GetPrivateProfileSectionW(L"dependencies", buf, 8192, r);
    for (k = buf; *k; k += wcslen(k) + 1) { WCHAR *eq = wcschr(k, '='); if (eq && !_wcsicmp(eq + 1, dep)) found = 1; }
    return found;
}
static int unregister_pkg(const WCHAR *full, int delete_data) {
    WCHAR fam[300], fw[8], rs[8], *l, *s;
    rec_get(full, L"PackageFamilyName", fam, 300); rec_get(full, L"IsFramework", fw, 8); rec_get(full, L"IsResourcePackage", rs, 8);
    user_set(g_user, full, 0);
    if (wcscmp(fw, L"True") && wcscmp(rs, L"True")) {                             /* a main package: its resource packs go with it */
        WCHAR vs[32]; rec_get(full, L"Version", vs, 32);
        l = user_list(g_user);
        for (s = l; *s; s += wcslen(s) + 1) { WCHAR f2[300], r2[8], v2[32]; rec_get(s, L"PackageFamilyName", f2, 300); rec_get(s, L"IsResourcePackage", r2, 8); rec_get(s, L"Version", v2, 32);
            if (!_wcsicmp(f2, fam) && !wcscmp(r2, L"True") && !wcscmp(v2, vs)) { user_set(g_user, s, 0); dbg(L"unregistered resource package %ls", s); } }
        free(l);
        if (delete_data) { WCHAR d[MAX_PATH]; _snwprintf(d, MAX_PATH, L"%ls\\Users\\%ls\\Packages\\%ls", g_root, g_user, fam); if (is_dir(d)) { rm_tree(d); info(L"  removed app data %ls", d); } }
    }
    return 1;
}
/* staged packages no user has and no registered package depends on are deleted */
static void gc_store(void) {
    WCHAR pat[MAX_PATH], users[MAX_PATH]; WIN32_FIND_DATAW fd, ud; HANDLE h, hu; int changed = 1, pass = 0;
    while (changed && pass++ < 8) {
        changed = 0;
        _snwprintf(pat, MAX_PATH, L"%ls\\ProgramData\\Packages\\*.ini", g_root);
        if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return;
        do {
            WCHAR full[400]; int used = 0;
            lstrcpynW(full, fd.cFileName, 400); full[wcslen(full) - 4] = 0;
            _snwprintf(users, MAX_PATH, L"%ls\\Users\\*", g_root);
            if ((hu = FindFirstFileW(users, &ud)) != INVALID_HANDLE_VALUE) {
                do { WCHAR *l, *s; if (!(ud.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || ud.cFileName[0] == '.') continue;
                     l = user_list(ud.cFileName);
                     for (s = l; *s && !used; s += wcslen(s) + 1) if (!_wcsicmp(s, full) || depends_on(s, full)) used = 1;
                     free(l);
                } while (!used && FindNextFileW(hu, &ud));
                FindClose(hu);
            }
            if (!used) {
                WCHAR dir[MAX_PATH], r[MAX_PATH];
                _snwprintf(dir, MAX_PATH, L"%ls\\WindowsApps\\%ls", g_root, full); rec_path(full, r);
                if (is_dir(dir)) rm_tree(dir);
                DeleteFileW(r); info(L"  removed %ls from the store (no longer used)", full); changed = 1;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
static int cmd_remove(const WCHAR *full, int preserve) {
    WCHAR fw[8], *l, *s;
    if (!user_has(g_user, full)) { err(L"%ls is not installed for %ls", full, g_user); return 1; }
    rec_get(full, L"IsFramework", fw, 8);
    if (!wcscmp(fw, L"True")) {
        l = user_list(g_user);
        for (s = l; *s; s += wcslen(s) + 1) if (depends_on(s, full)) { err(L"%ls is used by %ls; remove that first", full, s); free(l); return 1; }
        free(l);
    }
    info(L"Removing %ls", full);
    unregister_pkg(full, !preserve);
    gc_store();
    st_shell_sync();                                  /* its Start menu entries go with it */
    info(L"Removed %ls", full);
    return 0;
}

/* ================= list ================= */
static void json_str(FILE *o, const WCHAR *s) {
    fputc('"', o);
    for (; *s; s++) { if (*s == '"' || *s == '\\') fprintf(o, "\\%c", (char)*s); else if (*s < 32) fprintf(o, "\\u%04x", *s); else if (*s < 128) fputc((char)*s, o);
        else { char u8[8]; WCHAR w[3] = { s[0], 0, 0 }; if (*s >= 0xD800 && *s < 0xDC00 && s[1]) { w[1] = s[1]; s++; } WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, 8, NULL, NULL); fputs(u8, o); } }
    fputc('"', o);
}
static int type_wanted(const WCHAR *types, const WCHAR *full) {
    WCHAR fw[8], rs[8]; const WCHAR *k;
    if (!_wcsicmp(types, L"all")) return 1;
    rec_get(full, L"IsFramework", fw, 8); rec_get(full, L"IsResourcePackage", rs, 8);
    k = !wcscmp(fw, L"True") ? L"framework" : !wcscmp(rs, L"True") ? L"resource" : L"main";
    return StrStrIW(types, k) != NULL;
}
static int cmd_list(int all_users, int json, const WCHAR *types, const WCHAR *name, const WCHAR *pub) {
    WCHAR pat[MAX_PATH]; WIN32_FIND_DATAW ud; HANDLE hu; int first = 1, count = 0;
    WCHAR *names[256]; WCHAR *owners[256]; int n = 0, i;
    if (all_users) {
        _snwprintf(pat, MAX_PATH, L"%ls\\Users\\*", g_root);
        if ((hu = FindFirstFileW(pat, &ud)) != INVALID_HANDLE_VALUE) {
            do { WCHAR *l, *s; if (!(ud.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || ud.cFileName[0] == '.') continue;
                 l = user_list(ud.cFileName);
                 for (s = l; *s && n < 256; s += wcslen(s) + 1) { for (i = 0; i < n; i++) if (!_wcsicmp(names[i], s)) break;
                     if (i == n) { names[n] = _wcsdup(s); owners[n] = _wcsdup(ud.cFileName); n++; } else { size_t L = wcslen(owners[i]) + wcslen(ud.cFileName) + 2; owners[i] = realloc(owners[i], L * 2); wcscat(owners[i], L";"); wcscat(owners[i], ud.cFileName); } }
                 free(l);
            } while (FindNextFileW(hu, &ud));
            FindClose(hu);
        }
    } else { WCHAR *l = user_list(g_user), *s; for (s = l; *s && n < 256; s += wcslen(s) + 1) { names[n] = _wcsdup(s); owners[n] = _wcsdup(g_user); n++; } free(l); }
    if (json) fputs("[", stdout);
    for (i = 0; i < n; i++) {
        WCHAR v[18][600], r[MAX_PATH], buf[8192], *k; static const WCHAR *keys[] = { L"Name", L"Publisher", L"Architecture", L"ResourceId", L"Version", L"PackageFullName", L"InstallLocation", L"IsFramework", L"PackageFamilyName", L"PublisherId", L"IsResourcePackage", L"Bundle", L"SignatureKind", L"Staged" }; int j;
        if (!type_wanted(types, names[i])) continue;
        for (j = 0; j < 14; j++) rec_get(names[i], keys[j], v[j], 600);
        if (name && !PathMatchSpecW(v[0], name)) continue;
        if (pub && !PathMatchSpecW(v[1], pub)) continue;
        count++;
        rec_path(names[i], r); GetPrivateProfileSectionW(L"dependencies", buf, 8192, r);
        if (json) {
            if (!first) fputs(",", stdout);
            first = 0;
            fputs("\n{", stdout);
            for (j = 0; j < 14; j++) { if (j) fputs(",", stdout); json_str(stdout, keys[j]); fputs(":", stdout);
                if (j == 7 || j == 10) fputs(!wcscmp(v[j], L"True") ? "true" : "false", stdout); else json_str(stdout, v[j]); }
            fputs(",\"Dependencies\":[", stdout);
            { int d = 0; for (k = buf; *k; k += wcslen(k) + 1) { WCHAR *eq = wcschr(k, '='); if (!eq) continue; if (d++) fputs(",", stdout); json_str(stdout, eq + 1); } }
            fputs("],\"Users\":", stdout); json_str(stdout, owners[i]);
            fputs(",\"Status\":\"Ok\"}", stdout);
        } else {
            wprintf(L"\nName              : %ls\nPublisher         : %ls\nArchitecture      : %ls\nResourceId        : %ls\nVersion           : %ls\nPackageFullName   : %ls\nInstallLocation   : %ls\n"
                    L"IsFramework       : %ls\nPackageFamilyName : %ls\nPublisherId       : %ls\nIsResourcePackage : %ls\nDependencies      :",
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
            for (k = buf; *k; k += wcslen(k) + 1) { WCHAR *eq = wcschr(k, '='); if (eq) wprintf(L" %ls", eq + 1); }
            wprintf(L"\n");
            if (all_users) wprintf(L"Users             : %ls\n", owners[i]);
        }
    }
    if (json) fputs("\n]\n", stdout); else if (!count) wprintf(L"(no packages)\n");
    for (i = 0; i < n; i++) { free(names[i]); free(owners[i]); }
    return 0;
}

#include "store.inc"   /* wxrpkg store: packages from Microsoft's delivery service */
#include "gdk.inc"     /* wxrpkg gdk: the runtime of GDK PC games */

/* ================= main ================= */
static void usage(void) {
    wprintf(L"wxrpkg %ls - installs Appx/MSIX packages into a WXR environment\n\n"
            L"usage:\n"
            L"  wxrpkg [--root <dir>] add <package>... [--dependency <package|folder>]... [--force-update] [--any-os]\n"
            L"        package = .appx .msix .appxbundle .msixbundle, a package folder, or its AppxManifest.xml.\n"
            L"        --dependency: a package, or a folder of package files; only what the packages need is installed\n"
            L"        (other architectures, versions and unused frameworks are left out).\n"
            L"  wxrpkg [--root <dir>] list [--all-users] [--json] [--type main,framework,resource|all] [--name <pattern>] [--publisher <pattern>]\n"
            L"  wxrpkg [--root <dir>] remove <PackageFullName> [--preserve-data]\n"
            L"  wxrpkg [--root <dir>] store <ProductId|PackageFamilyName> [--download-only | --check] [--progress]\n"
            L"        downloads a free Store app (e.g. 9WZDNCRFHVN5 or Microsoft.WindowsCalculator_8wekyb3d8bbwe) and the\n"
            L"        frameworks it still needs from Microsoft's delivery service and installs them. No account is used;\n"
            L"        paid or encrypted packages cannot be installed.\n"
            L"  wxrpkg [--root <dir>] gdk [--remove] [--force-update]\n"
            L"        sets up the runtime of GDK PC games: three files of Microsoft's Gaming Services package (downloaded) and\n"
            L"        GameInput from each installed game's own redistributable. --remove deletes them again.\n", WXR_PKG_VERSION);
}
int wmain(int argc, WCHAR **argv) {
    const WCHAR *root = NULL, *cmd = NULL, *types = L"main,framework", *name = NULL, *pub = NULL; WCHAR **pos = (WCHAR **)calloc(argc + 1, sizeof(WCHAR *)); int npos = 0, i, rc;
    WCHAR **dep = (WCHAR **)calloc(argc + 1, sizeof(WCHAR *)); int ndep = 0;
    int force_update = 0, any_os = 0, all_users = 0, json = 0, preserve = 0, download_only = 0, remove = 0;
    for (i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--root") && i + 1 < argc) root = argv[++i];
        else if (!wcscmp(argv[i], L"--type") && i + 1 < argc) types = argv[++i];
        else if (!wcscmp(argv[i], L"--dependency") && i + 1 < argc) dep[ndep++] = argv[++i];
        else if (!wcscmp(argv[i], L"--name") && i + 1 < argc) name = argv[++i];
        else if (!wcscmp(argv[i], L"--publisher") && i + 1 < argc) pub = argv[++i];
        else if (!wcscmp(argv[i], L"--force-update")) force_update = 1;
        else if (!wcscmp(argv[i], L"--any-os")) any_os = 1;
        else if (!wcscmp(argv[i], L"--all-users")) all_users = 1;
        else if (!wcscmp(argv[i], L"--json")) json = 1;
        else if (!wcscmp(argv[i], L"--preserve-data")) preserve = 1;
        else if (!wcscmp(argv[i], L"--remove")) remove = 1;
        else if (!wcscmp(argv[i], L"--download-only")) download_only = 1;
        else if (!wcscmp(argv[i], L"--check")) download_only = 2;          /* store: only say what would happen */
        else if (!wcscmp(argv[i], L"--progress")) g_progress = 1;
        else if (!wcscmp(argv[i], L"--help") || !wcscmp(argv[i], L"/?")) { usage(); return 0; }
        else if (argv[i][0] == '-' && argv[i][1] == '-') { usage(); fwprintf(stderr, L"\nunknown option %ls\n", argv[i]); return 2; }
        else if (!cmd) cmd = argv[i]; else pos[npos++] = argv[i];
    }
    if (!cmd) { usage(); return 2; }
    if (json) g_quiet = 1;
    if (!pick_root(root)) return 1;
    if (!wcscmp(cmd, L"add")) { if (!npos) { usage(); return 2; } rc = cmd_add(pos, npos, dep, ndep, force_update, any_os); }
    else if (!wcscmp(cmd, L"list")) rc = cmd_list(all_users, json, types, name, pub);
    else if (!wcscmp(cmd, L"remove")) { if (npos != 1) { usage(); return 2; } rc = cmd_remove(pos[0], preserve); }
    else if (!wcscmp(cmd, L"store")) { if (npos != 1) { usage(); return 2; } rc = cmd_store(pos[0], download_only, force_update, any_os); }
    else if (!wcscmp(cmd, L"gdk")) { if (npos) { usage(); return 2; } rc = cmd_gdk(remove, force_update); }
    else { usage(); return 2; }
    if (g_log) fclose(g_log);
    return rc;
}
