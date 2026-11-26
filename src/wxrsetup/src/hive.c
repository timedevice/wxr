/* Read-only regf parser: base block (4 KB), then hbins; cell offsets are relative to 0x1000; a cell starts with its
   (negative = allocated) int32 size. Records used: nk (key), vk (value), lf/lh/li/ri (subkey lists), db (big data). */
#include "hive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define BASE 0x1000
static const BYTE *cell(Hive *h, DWORD off, DWORD need) {                 /* cell data (after the size field) */
    ULONGLONG at = (ULONGLONG)BASE + off;
    if (off == 0xFFFFFFFF || at + 4 + need > h->n) return NULL;
    return h->b + at + 4;
}
static WORD w16(const BYTE *p) { return *(const WORD *)p; }
static DWORD w32(const BYTE *p) { return *(const DWORD *)p; }

int hive_open(Hive *h, const WCHAR *path) {
    HANDLE f; DWORD got = 0; LARGE_INTEGER sz;
    memset(h, 0, sizeof *h);
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) { _snwprintf(h->err, 256, L"cannot open %ls (error %lu)", path, GetLastError()); return 0; }
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart < BASE + 32 || sz.QuadPart > 0x40000000) { CloseHandle(f); _snwprintf(h->err, 256, L"%ls has an unexpected size", path); return 0; }
    h->n = (DWORD)sz.QuadPart; h->b = (BYTE *)malloc(h->n);
    if (!h->b || !ReadFile(f, h->b, h->n, &got, NULL) || got != h->n) { CloseHandle(f); hive_close(h); _snwprintf(h->err, 256, L"cannot read %ls", path); return 0; }
    CloseHandle(f);
    if (memcmp(h->b, "regf", 4) || w32(h->b + 20) != 1) { hive_close(h); _snwprintf(h->err, 256, L"%ls is not a registry hive", path); return 0; }
    if (w32(h->b + 4) != w32(h->b + 8)) { hive_close(h); _snwprintf(h->err, 256, L"%ls was not closed cleanly (needs its .LOG files)", path); return 0; }
    h->root = w32(h->b + 0x24);
    { const BYTE *r = cell(h, h->root, 0x50); if (!r || memcmp(r, "nk", 2)) { hive_close(h); _snwprintf(h->err, 256, L"%ls: root key not found", path); return 0; } }
    return 1;
}
void hive_close(Hive *h) { free(h->b); h->b = NULL; h->n = 0; }

static void nk_name(const BYTE *nk, WCHAR *out, int cap) {
    WORD len = w16(nk + 0x48), fl = w16(nk + 2); int i, k = 0;
    if (fl & 0x20) for (i = 0; i < len && k < cap - 1; i++) out[k++] = nk[0x4C + i];         /* compressed (Latin-1) name */
    else for (i = 0; i + 1 < len && k < cap - 1; i += 2) out[k++] = *(const WCHAR *)(nk + 0x4C + i);
    out[k] = 0;
}
/* calls fn for every subkey of nk; stops when fn returns nonzero */
typedef int (*SubFn)(Hive *h, DWORD sub, void *ctx);
static int walk_list(Hive *h, DWORD list, SubFn fn, void *ctx, int depth) {
    const BYTE *l = cell(h, list, 4); WORD n, i;
    if (!l || depth > 4) return 0;
    n = w16(l + 2);
    if (!memcmp(l, "lf", 2) || !memcmp(l, "lh", 2)) { if (!cell(h, list, 4 + n * 8u)) return 0; for (i = 0; i < n; i++) { int r = fn(h, w32(l + 4 + i * 8), ctx); if (r) return r; } }
    else if (!memcmp(l, "li", 2)) { if (!cell(h, list, 4 + n * 4u)) return 0; for (i = 0; i < n; i++) { int r = fn(h, w32(l + 4 + i * 4), ctx); if (r) return r; } }
    else if (!memcmp(l, "ri", 2)) { if (!cell(h, list, 4 + n * 4u)) return 0; for (i = 0; i < n; i++) { int r = walk_list(h, w32(l + 4 + i * 4), fn, ctx, depth + 1); if (r) return r; } }
    return 0;
}
static int walk_subkeys(Hive *h, DWORD nk, SubFn fn, void *ctx) {
    const BYTE *k = cell(h, nk, 0x50);
    if (!k || memcmp(k, "nk", 2) || !w32(k + 0x14)) return 0;
    return walk_list(h, w32(k + 0x1C), fn, ctx, 0);
}
typedef struct { const WCHAR *name; DWORD found; } FindCtx;
static int find_fn(Hive *h, DWORD sub, void *ctx) {
    FindCtx *c = (FindCtx *)ctx; const BYTE *k = cell(h, sub, 0x50); WCHAR nm[260];
    if (!k || memcmp(k, "nk", 2)) return 0;
    nk_name(k, nm, 260);
    if (!_wcsicmp(nm, c->name)) { c->found = sub; return 1; }
    return 0;
}
int hive_find(Hive *h, const WCHAR *path, DWORD *nk) {
    WCHAR comp[260]; const WCHAR *p = path; DWORD cur = h->root;
    while (*p) {
        int k = 0; FindCtx c;
        while (*p == '\\') p++;
        if (!*p) break;
        while (*p && *p != '\\' && k < 259) comp[k++] = *p++;
        comp[k] = 0; c.name = comp; c.found = 0;
        if (!walk_subkeys(h, cur, find_fn, &c)) { _snwprintf(h->err, 256, L"key not found in the hive: %ls", path); return 0; }
        cur = c.found;
    }
    *nk = cur; return 1;
}
/* value data; big data ("db") is joined into a malloc'd buffer (*own = 1) */
static const BYTE *vk_data(Hive *h, const BYTE *vk, DWORD *len, int *own) {
    DWORD sz = w32(vk + 4), off = w32(vk + 8); const BYTE *d;
    *own = 0;
    if (sz & 0x80000000) { *len = sz & 0x7FFFFFFF; if (*len > 4) *len = 4; return vk + 8; }   /* data stored in the offset field */
    *len = sz;
    if (!sz) return vk;
    d = cell(h, off, 2);
    if (!d) return NULL;
    if (sz > 16344 && !memcmp(d, "db", 2)) {
        WORD segs = w16(d + 2), i; const BYTE *list = cell(h, w32(d + 4), segs * 4u); BYTE *out; DWORD at = 0;
        if (!list) return NULL;
        out = (BYTE *)malloc(sz);
        for (i = 0; i < segs && at < sz; i++) {
            DWORD take = sz - at < 16344 ? sz - at : 16344; const BYTE *s = cell(h, w32(list + i * 4), take);
            if (!s) { free(out); return NULL; }
            memcpy(out + at, s, take); at += take;
        }
        *own = 1; return out;
    }
    return cell(h, off, sz);
}
typedef struct { HKEY dst; unsigned *keys, *values; int ok; } CopyCtx;
static int copy_into(Hive *h, DWORD nk, HKEY dst, unsigned *keys, unsigned *values);
static int copy_fn(Hive *h, DWORD sub, void *ctx) {
    CopyCtx *c = (CopyCtx *)ctx; const BYTE *k = cell(h, sub, 0x50); WCHAR nm[260]; HKEY nd; LONG r;
    if (!k || memcmp(k, "nk", 2)) return 0;
    nk_name(k, nm, 260);
    r = RegCreateKeyExW(c->dst, nm, 0, NULL, 0, KEY_WRITE, NULL, &nd, NULL);
    if (r != ERROR_SUCCESS) { _snwprintf(h->err, 256, L"cannot create key %ls (error %ld)", nm, r); c->ok = 0; return 1; }
    (*c->keys)++;
    if (!copy_into(h, sub, nd, c->keys, c->values)) { c->ok = 0; RegCloseKey(nd); return 1; }
    RegCloseKey(nd); return 0;
}
static int copy_into(Hive *h, DWORD nk, HKEY dst, unsigned *keys, unsigned *values) {
    const BYTE *k = cell(h, nk, 0x50); DWORD nv, i; const BYTE *vl; CopyCtx c;
    if (!k || memcmp(k, "nk", 2)) { _snwprintf(h->err, 256, L"bad key record"); return 0; }
    nv = w32(k + 0x24);
    if (nv && (vl = cell(h, w32(k + 0x28), nv * 4))) {
        for (i = 0; i < nv; i++) {
            const BYTE *vk = cell(h, w32(vl + i * 4), 0x14); WCHAR nm[1024]; WORD nl; int j, q = 0, own; DWORD len; const BYTE *d; LONG r;
            if (!vk || memcmp(vk, "vk", 2)) continue;
            nl = w16(vk + 2);
            if (!cell(h, w32(vl + i * 4), 0x14 + nl)) continue;
            if (w16(vk + 0x10) & 1) for (j = 0; j < nl && q < 1023; j++) nm[q++] = vk[0x14 + j];
            else for (j = 0; j + 1 < nl && q < 1023; j += 2) nm[q++] = *(const WCHAR *)(vk + 0x14 + j);
            nm[q] = 0;
            d = vk_data(h, vk, &len, &own);
            if (!d) { _snwprintf(h->err, 256, L"bad data for value '%ls'", nm); return 0; }
            r = RegSetValueExW(dst, nm, 0, w32(vk + 0xC), d, len);
            if (own) free((void *)d);
            if (r != ERROR_SUCCESS) { _snwprintf(h->err, 256, L"cannot write value '%ls' (error %ld)", nm, r); return 0; }
            (*values)++;
        }
    }
    c.dst = dst; c.keys = keys; c.values = values; c.ok = 1;
    walk_subkeys(h, nk, copy_fn, &c);
    return c.ok;
}
int hive_copy(Hive *h, DWORD nk, HKEY dst, unsigned *keys, unsigned *values) { return copy_into(h, nk, dst, keys, values); }
