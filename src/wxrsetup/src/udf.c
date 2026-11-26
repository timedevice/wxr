/* Minimal read-only UDF reader (UDF 1.02 as used by Microsoft's ISOs, whose ISO9660 tree is an empty stub).
   Finds a file by path and copies it out. Port of setup\udfls.py. */
#include "udf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define SEC 2048

static int rd(Udf *u, ULONGLONG lba, void *buf, DWORD n) {
    LARGE_INTEGER o; DWORD got = 0;
    o.QuadPart = (LONGLONG)(lba * SEC);
    if (!SetFilePointerEx(u->h, o, NULL, FILE_BEGIN)) return 0;
    return ReadFile(u->h, buf, n, &got, NULL) && got == n;
}
static WORD tag(const BYTE *d) { return *(const WORD *)d; }
static DWORD u32(const BYTE *d) { return *(const DWORD *)d; }

int udf_open(Udf *u, const WCHAR *path, WCHAR *msg) {
    BYTE d[SEC]; DWORD vds_len, vds_loc, i, fsd_lb = 0; int have_part = 0;
    memset(u, 0, sizeof *u);
    u->h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (u->h == INVALID_HANDLE_VALUE) { _snwprintf(msg, 256, L"cannot open %ls (error %lu)", path, GetLastError()); return 0; }
    if (!rd(u, 256, d, SEC) || tag(d) != 2) { _snwprintf(msg, 256, L"not a UDF image (no anchor at sector 256)"); udf_close(u); return 0; }
    vds_len = u32(d + 16); vds_loc = u32(d + 20);
    for (i = 0; i < vds_len / SEC; i++) {
        if (!rd(u, vds_loc + i, d, SEC)) break;
        if (tag(d) == 5) { u->part_start = u32(d + 188); have_part = 1; }   /* partition descriptor */
        if (tag(d) == 6) fsd_lb = u32(d + 252);                              /* logical volume descriptor: FSD long_ad */
        if (tag(d) == 8) break;                                              /* terminating descriptor */
    }
    if (!have_part || !rd(u, u->part_start + fsd_lb, d, SEC) || tag(d) != 256) { _snwprintf(msg, 256, L"UDF volume structure not understood"); udf_close(u); return 0; }
    u->root_lb = u32(d + 404);                                               /* root directory ICB */
    return 1;
}
void udf_close(Udf *u) { if (u->h && u->h != INVALID_HANDLE_VALUE) CloseHandle(u->h); u->h = NULL; }

/* reads a file entry: size + extents (or embedded data) */
static int fentry(Udf *u, DWORD lb, UdfFile *f) {
    BYTE d[SEC]; DWORD off, lea, lad, j; int adtype;
    memset(f, 0, sizeof *f);
    if (!rd(u, u->part_start + lb, d, SEC)) return 0;
    if (tag(d) == 261) { off = 176; lea = u32(d + 168); lad = u32(d + 172); }
    else if (tag(d) == 266) { off = 216; lea = u32(d + 208); lad = u32(d + 212); }   /* extended file entry */
    else return 0;
    if (off + lea + lad > SEC) return 0;
    f->size = *(const ULONGLONG *)(d + 56);
    f->is_dir = d[27] == 4;
    adtype = *(const WORD *)(d + 34) & 7;
    if (adtype == 3) { f->embedded = 1; f->emb_len = lad < sizeof f->emb ? lad : sizeof f->emb; memcpy(f->emb, d + off + lea, f->emb_len); return 1; }
    for (j = 0; j < lad && f->next < UDF_MAX_EXT; j += adtype == 0 ? 8 : 16) {
        DWORD ln = u32(d + off + lea + j), pos = u32(d + off + lea + j + 4);
        if ((ln >> 30) == 3) return 0;                                       /* continuation of descriptors: not needed for our ISOs */
        if ((ln & 0x3FFFFFFF) == 0) break;
        f->ext[f->next].lb = pos; f->ext[f->next].len = ln & 0x3FFFFFFF; f->ext[f->next].recorded = (ln >> 30) == 0; f->next++;
    }
    return adtype == 0 || adtype == 1;
}
static BYTE *read_dir(Udf *u, UdfFile *f, DWORD *len) {
    BYTE *b; DWORD i, at = 0; size_t cap = SEC;
    if (f->embedded) { b = (BYTE *)calloc(1, SEC); memcpy(b, f->emb, f->emb_len); *len = f->emb_len; return b; }
    if (f->size > 16u << 20) return NULL;
    for (i = 0; i < f->next; i++) cap += (f->ext[i].len + SEC - 1) / SEC * SEC;
    b = (BYTE *)calloc(1, cap);
    for (i = 0; i < f->next; i++) {                     /* extents are concatenated by length; each read is whole sectors */
        DWORD n = (f->ext[i].len + SEC - 1) / SEC * SEC;
        if (f->ext[i].recorded && n && !rd(u, u->part_start + f->ext[i].lb, b + at, n)) { free(b); return NULL; }
        at += f->ext[i].len;
    }
    *len = at < f->size ? at : (DWORD)f->size; return b;
}
static void dname(const BYTE *p, int n, WCHAR *out, int cap) {        /* OSTA compressed unicode */
    int i, k = 0;
    if (n < 1) { out[0] = 0; return; }
    if (p[0] == 16) for (i = 1; i + 1 < n && k < cap - 1; i += 2) out[k++] = (WCHAR)((p[i] << 8) | p[i + 1]);
    else for (i = 1; i < n && k < cap - 1; i++) out[k++] = p[i];
    out[k] = 0;
}
/* look up one name in a directory; returns the ICB of the entry */
static int lookup(Udf *u, DWORD dir_lb, const WCHAR *name, DWORD *out_lb) {
    UdfFile f; BYTE *b; DWORD len, i = 0; int found = 0;
    if (!fentry(u, dir_lb, &f) || !f.is_dir || !(b = read_dir(u, &f, &len))) return 0;
    while (i + 38 <= len && tag(b + i) == 257) {
        BYTE ch = b[i + 18], lfi = b[i + 19]; WORD liu = *(const WORD *)(b + i + 36); WCHAR nm[260];
        if (!(ch & 8) && !(ch & 4)) {                                       /* not parent, not deleted */
            dname(b + i + 38 + liu, lfi, nm, 260);
            if (!_wcsicmp(nm, name)) { *out_lb = u32(b + i + 24); found = 1; break; }
        }
        i += (38 + liu + lfi + 3) & ~3u;
    }
    free(b); return found;
}
int udf_find(Udf *u, const WCHAR *path, UdfFile *out) {
    WCHAR comp[260]; const WCHAR *p = path; DWORD lb = u->root_lb;
    while (*p) {
        int k = 0;
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        while (*p && *p != '\\' && *p != '/' && k < 259) comp[k++] = *p++;
        comp[k] = 0;
        if (!lookup(u, lb, comp, &lb)) return 0;
    }
    return fentry(u, lb, out);
}
int udf_read(Udf *u, const UdfFile *f, ULONGLONG offset, void *buf, DWORD n) {
    ULONGLONG base = 0; DWORD i; BYTE *out = (BYTE *)buf;
    if (offset + n > f->size) return 0;
    for (i = 0; i < f->next && n; i++) {                 /* extents are concatenated by length */
        ULONGLONG len = f->ext[i].len;
        if (offset < base + len) {
            ULONGLONG in = offset - base; DWORD take = (DWORD)(len - in < n ? len - in : n);
            if (f->ext[i].recorded) {
                LARGE_INTEGER pos; DWORD got = 0;
                pos.QuadPart = (LONGLONG)(((ULONGLONG)u->part_start + f->ext[i].lb) * SEC + in);
                if (!SetFilePointerEx(u->h, pos, NULL, FILE_BEGIN) || !ReadFile(u->h, out, take, &got, NULL) || got != take) return 0;
            } else memset(out, 0, take);
            out += take; offset += take; n -= take;
        }
        base += len;
    }
    return n == 0;
}
int udf_copy(Udf *u, const UdfFile *f, const WCHAR *dest, void (*progress)(ULONGLONG done, ULONGLONG total, void *ctx), void *ctx, WCHAR *msg) {
    HANDLE o; BYTE *buf; ULONGLONG left = f->size, done = 0; DWORD i; const DWORD CH = 4u << 20;
    o = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (o == INVALID_HANDLE_VALUE) { _snwprintf(msg, 256, L"cannot create %ls (error %lu)", dest, GetLastError()); return 0; }
    buf = (BYTE *)malloc(CH);
    for (i = 0; i < f->next && left; i++) {
        ULONGLONG ext_left = f->ext[i].len < left ? f->ext[i].len : left; ULONGLONG lba = (ULONGLONG)u->part_start + f->ext[i].lb;
        LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)(lba * SEC);
        if (f->ext[i].recorded && !SetFilePointerEx(u->h, pos, NULL, FILE_BEGIN)) break;
        while (ext_left) {
            DWORD n = ext_left < CH ? (DWORD)ext_left : CH, got = 0, put = 0;
            if (f->ext[i].recorded) { if (!ReadFile(u->h, buf, n, &got, NULL) || got != n) { _snwprintf(msg, 256, L"read error in the image (error %lu)", GetLastError()); goto fail; } }
            else memset(buf, 0, n);
            if (!WriteFile(o, buf, n, &put, NULL) || put != n) { _snwprintf(msg, 256, L"write error on %ls (error %lu)", dest, GetLastError()); goto fail; }
            ext_left -= n; left -= n; done += n;
            if (progress) progress(done, f->size, ctx);
        }
    }
    if (left) { _snwprintf(msg, 256, L"the image ended before the file did"); goto fail; }
    free(buf); CloseHandle(o); return 1;
fail:
    free(buf); CloseHandle(o); DeleteFileW(dest); return 0;
}
