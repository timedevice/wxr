/* wxrpri.inc - standalone resources.pri string lookup for the shell side (wxr.exe): display names of apps ("ms-resource:AppName").
   The name schema follows the published PRI layout (PriTools); resource map, decisions and data items. */
static int g_pri_hnn, g_pri_hni;   /* diagnostics: node / item counts from the schema header */
typedef struct { DWORD off, len; char name[17]; } PSec;
typedef struct { char *path; char *upath; int node; } PItem;
typedef struct {
    char file[MAX_PATH]; unsigned char *d; DWORD n;
    PSec *sec; int nsec;
    PItem *items; int nitems;
    const unsigned char *rm; int rm_items, rm_cands;
    int ndec, nq, nsets; const unsigned short *w; int wn;
    int dec_idx, sets_idx, qa_idx, qb_idx, index_idx, pool_idx;
} PriFile;

static unsigned u16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned u32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static int ucmp(const void *a, const void *b) { return strcmp(((const PItem *)a)->upath, ((const PItem *)b)->upath); }

static const unsigned char *sec_body(PriFile *f, const char *prefix, DWORD *len) {
    int i;
    for (i = 0; i < f->nsec; i++)
        if (!strncmp(f->sec[i].name, prefix, strlen(prefix))) {
            if (len) *len = f->sec[i].len - 40;
            return f->d + f->sec[i].off + 32;
        }
    return NULL;
}

/* Hierarchical schema ([mrm_hschema] / [mrm_hschemaex]) read field by field, layout as documented by PriTools (PriFormat):
     u16 1, u16 uniqueNameLength, u16 nameLength, u16 0, [ex: 16 bytes "[def_hnamesx]  \0" or other], u16 major, u16 minor, u32 0,
     u32 checksum, u32 numScopes, u32 numItems, UTF-16 uniqueName\0, UTF-16 name\0, u16 0, u16 maxFullPathLength, u16 0,
     u32 numScopes+numItems, u32 numScopes, u32 numItems, u32 unicodeDataLength (chars), u32 ?, [hnamesx: u32 ?],
     (numScopes+numItems) x 12-byte nodes { u16 parent (0xFFFF = none), u16 fullPathLength, u16 uppercaseFirstChar, u8 nameLength,
       u8 flags (0x10 scope, 0x20 name in ASCII, low nibble = name offset bits 16..19), u16 nameOffset, u16 index },
     numScopes x 8-byte scope info, numItems x u16, UTF-16 name pool (unicodeDataLength chars), ASCII name pool.
   Items are listed with their full path ("Resources/AppName/Text") and their own index (what the resource map is indexed by). */
static int parse_schema(PriFile *f) {
    DWORD bl; const unsigned char *b, *recs, *uni, *asc, *endp; unsigned p, nsc, nit, tot, ulen, i, hx = 0, ex = 0; int s, k = 0;
    char **names; int *parent, *scope, *index; PItem *it;
    for (s = 0; s < f->nsec; s++) if (!strncmp(f->sec[s].name, "[mrm_hschema", 12)) { ex = f->sec[s].name[12] == 'e'; break; }
    b = sec_body(f, "[mrm_hschema", &bl);
    if (!b || bl < 64 || u16(b) != 1) return 0;
    endp = b + bl; p = 8;
    if (ex) { hx = !memcmp(b + 8, "[def_hnamesx]", 13); p += 16; }
    p += 12;                                  /* major, minor, 0, checksum */
    nsc = u32(b + p); nit = u32(b + p + 4); p += 8;
    for (i = 0; i < 2; i++) { while (p + 2 <= bl && (b[p] || b[p + 1])) p += 2; p += 2; }   /* unique name, name */
    p += 6;                                   /* u16 0, maxFullPathLength, u16 0 */
    if (p + 20 > bl) return 0;
    tot = u32(b + p); if (tot != nsc + nit || u32(b + p + 4) != nsc || u32(b + p + 8) != nit || tot > 400000) return 0;
    ulen = u32(b + p + 12); p += 20 + (hx ? 4 : 0);
    recs = b + p; uni = recs + tot * 12 + nsc * 8 + nit * 2; asc = uni + (size_t)ulen * 2;
    if (asc > endp) return 0;
    names = (char **)calloc(tot, sizeof(char *)); parent = (int *)calloc(tot, sizeof(int)); scope = (int *)calloc(tot, sizeof(int)); index = (int *)calloc(tot, sizeof(int));
    if (!names || !parent || !scope || !index) return 0;
    for (i = 0; i < tot; i++) {
        const unsigned char *r = recs + i * 12; unsigned par = u16(r), len = r[6], fl = r[7], off = u16(r + 8) | ((fl & 0xF) << 16); char *nm = (char *)malloc(len * 3 + 1);
        parent[i] = par == 0xFFFF || par >= tot ? -1 : (int)par; scope[i] = (fl & 0x10) != 0; index[i] = u16(r + 10);
        if (!nm) continue; nm[0] = 0;
        if (fl & 0x20) { if (asc + off + len <= endp) { memcpy(nm, asc + off, len); nm[len] = 0; } }
        else if (off + len <= ulen) { WCHAR w[256]; unsigned j; for (j = 0; j < len && j < 255; j++) w[j] = (WCHAR)u16(uni + (off + j) * 2); w[j] = 0;
            if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, nm, len * 3 + 1, NULL, NULL)) nm[0] = 0; }
        names[i] = nm;
    }
    it = (PItem *)calloc(nit + 1, sizeof(PItem));
    for (i = 0; i < tot && it && (unsigned)k < nit; i++) {
        char buf[1024]; int stack[64], d = 0, c, q; size_t o = 0;
        if (scope[i]) continue;
        for (q = (int)i; q >= 0 && d < 64; q = parent[q]) stack[d++] = q;
        for (c = d - 1; c >= 0; c--) { const char *n = names[stack[c]] ? names[stack[c]] : ""; size_t l = strlen(n);
            if (!l) continue;                 /* the root scope has no name */
            if (o + l + 2 >= sizeof buf) break; if (o) buf[o++] = '/'; memcpy(buf + o, n, l); o += l; }
        buf[o] = 0;
        it[k].path = _strdup(buf); it[k].upath = _strdup(buf); _strupr(it[k].upath); it[k].node = index[i]; k++;
    }
    for (i = 0; i < tot; i++) free(names[i]);
    free(names); free(parent); free(scope); free(index);
    if (!it) return 0;
    qsort(it, k, sizeof(PItem), ucmp);
    f->items = it; f->nitems = k;
    return k > 0;
}

static int parse_resmap(PriFile *f) {
    DWORD bl; const unsigned char *b = sec_body(f, "[mrm_res_map2_", &bl);
    if (!b) return 0;
    f->rm = b; f->rm_items = (int)u32(b + 16); f->rm_cands = (int)u32(b + 20);
    return f->rm_items >= f->nitems;   /* items whose names this reader cannot decode are missing from the list; indices are explicit */
}

static int parse_decn(PriFile *f) {
    DWORD bl; const unsigned char *b = sec_body(f, "[mrm_decn_info", &bl);
    int nqt, nsets, ndec, nidx;
    if (!b) return 0;
    f->wn = (int)(bl / 2); f->w = (const unsigned short *)b;
    nqt = f->w[0]; nsets = f->w[2]; ndec = f->w[3]; nidx = f->w[4];
    f->nq = nqt; f->nsets = nsets; f->ndec = ndec;
    f->dec_idx = 6; f->sets_idx = 6 + 2 * ndec; f->qa_idx = f->sets_idx + 2 * nsets; f->qb_idx = f->qa_idx + 4 * nqt;
    f->index_idx = f->qb_idx + 6 * nqt; f->pool_idx = f->index_idx + nidx;
    return f->pool_idx <= f->wn;
}
/* ------------------------------------------------------------------ candidate selection */
static void qual_value(PriFile *f, int q, char *out, int cap) {
    const unsigned short *w = f->w; unsigned off = w[f->qb_idx + 6 * q + 4]; int i = 0;
    while (f->pool_idx + off + i < (unsigned)f->wn && w[f->pool_idx + off + i] && i < cap - 1) { out[i] = (char)w[f->pool_idx + off + i]; i++; }
    out[i] = 0;
}

static char g_tags[4][40]; static int g_tags_init;
static void user_langs(void) {
    WCHAR loc[LOCALE_NAME_MAX_LENGTH]; int i;
    if (g_tags_init) return; g_tags_init = 1;
    memset(g_tags, 0, sizeof g_tags);
    if (GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH)) { WideCharToMultiByte(CP_ACP, 0, loc, -1, g_tags[0], 39, 0, 0); _strupr(g_tags[0]); }
    for (i = 0; g_tags[0][i] && g_tags[0][i] != '-' && i < 39; i++) g_tags[1][i] = g_tags[0][i];
    strcpy(g_tags[2], "EN-US"); strcpy(g_tags[3], "EN");
}

static int cand_score(PriFile *f, int dec, int j) {
    const unsigned short *w = f->w; int first, cnt, qs, f0, nq, k, score = 0, haslang = 0;
    if (dec >= f->ndec) return 0;
    first = w[f->dec_idx + 2 * dec]; cnt = w[f->dec_idx + 2 * dec + 1];
    if (j >= cnt) return -1000;
    user_langs();
    qs = w[f->index_idx + first + j]; f0 = w[f->sets_idx + 2 * qs]; nq = w[f->sets_idx + 2 * qs + 1];
    for (k = 0; k < nq; k++) {
        int q = w[f->index_idx + f0 + k]; int type = w[f->qb_idx + 6 * q + 1]; char v[64];
        if (type == 0) {
            size_t pl = strlen(g_tags[1]);
            qual_value(f, q, v, sizeof v); haslang = 1; _strupr(v);
            if (!strcmp(v, g_tags[0])) score += 100;
            else if (pl && !strncmp(v, g_tags[1], pl) && (v[pl] == '-' || !v[pl])) score += 80;
            else if (!strcmp(v, g_tags[2])) score += 30;
            else if (!strncmp(v, g_tags[3], 2)) score += 20;
        } else score -= 1;
    }
    if (!haslang) score += 10;
    return score;
}

static int find_item(PriFile *f, const char *path) {
    char up[1024]; int lo = 0, hi = f->nitems - 1;
    strncpy(up, path, sizeof up - 1); up[sizeof up - 1] = 0; _strupr(up);
    while (lo <= hi) { int mid = (lo + hi) / 2, c = strcmp(f->items[mid].upath, up); if (!c) return f->items[mid].node < f->rm_items ? f->items[mid].node : -1; if (c < 0) lo = mid + 1; else hi = mid - 1; }
    return -1;
}

static const unsigned char *item_data(PriFile *f, int k, DWORD *len, int *type) {
    const unsigned char *pr = f->rm + 0x60 + k * 4, *rows = f->rm + 0x60 + f->rm_items * 4;
    int dec = u16(pr), c0 = u16(pr + 2), c1, best = 0, bs = -100000, j, nc;
    c1 = (k + 1 < f->rm_items) ? (int)u16(pr + 6) : f->rm_cands;
    nc = c1 - c0; if (nc <= 0) return NULL;
    for (j = 0; j < nc; j++) { int s = cand_score(f, dec, j); if (s > bs) { bs = s; best = j; } }
    {
        const unsigned char *row = rows + (c0 + best) * 8; int t = row[1], item = u16(row + 4), sec = u16(row + 6);
        const unsigned char *b, *data; DWORD off, l; unsigned nstr, nblob;
        if (sec >= (unsigned)f->nsec) return NULL;
        b = f->d + f->sec[sec].off + 32;
        nstr = u16(b + 4); nblob = u16(b + 6);
        if ((unsigned)item >= nstr + nblob) return NULL;
        data = b + 12 + nstr * 4 + nblob * 8;
        if ((unsigned)item < nstr) { off = u16(b + 12 + item * 4); l = u16(b + 12 + item * 4 + 2); }
        else { const unsigned char *e = b + 12 + nstr * 4 + (item - nstr) * 8; off = u32(e); l = u32(e + 4); }
        if (len) *len = l;
        if (type) *type = t;
        return data + off;
    }
}

/* ---- single-file loader + string lookup ---- */
static int g_pri_stage, g_pri_rm, g_pri_ni;   /* diagnostics: how far pri_open got */
static void pri_free(PriFile *f) { int i; for (i = 0; i < f->nitems; i++) { free(f->items[i].path); free(f->items[i].upath); } free(f->items); free(f->sec); free(f->d); memset(f, 0, sizeof *f); }
static int pri_open(PriFile *f, const WCHAR *path) {
    HANDLE h; DWORD rd, sz; unsigned toc, sstart, nsec, i;
    memset(f, 0, sizeof *f);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0); if (h == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(h, 0); f->d = (unsigned char *)malloc(sz ? sz : 1); f->n = sz;
    if (!f->d || !ReadFile(h, f->d, sz, &rd, 0) || rd != sz || sz < 64 || memcmp(f->d, "mrm_pri2", 8)) { CloseHandle(h); free(f->d); f->d = NULL; return 0; }
    CloseHandle(h);
    toc = u32(f->d + 16); sstart = u32(f->d + 20); nsec = u16(f->d + 24);
    if (toc + nsec * 32 > sz) { pri_free(f); return 0; }
    f->sec = (PSec *)calloc(nsec, sizeof(PSec)); f->nsec = (int)nsec;
    for (i = 0; i < nsec; i++) { const unsigned char *e = f->d + toc + i * 32; int k;
        memcpy(f->sec[i].name, e, 16); f->sec[i].name[16] = 0;
        for (k = 15; k >= 0 && (f->sec[i].name[k] == ' ' || f->sec[i].name[k] == 0); k--) f->sec[i].name[k] = 0;
        f->sec[i].off = sstart + u32(e + 24); f->sec[i].len = u32(e + 28); }
    g_pri_stage = 1; if (!parse_schema(f)) { pri_free(f); return 0; } g_pri_stage = 2; if (!parse_resmap(f)) { g_pri_rm = f->rm ? (int)u32(f->rm + 16) : -1; g_pri_ni = f->nitems; pri_free(f); return 0; } g_pri_stage = 3; if (!parse_decn(f)) { pri_free(f); return 0; } g_pri_stage = 4;
    return 1; }
/* "ms-resource:AppName" / "ms-resource:///Resources/AppName" / "ms-resource://<pkg>/Resources/AppName" -> text from <dir>\resources.pri.
   A value without "ms-resource:" is returned as is. Returns 1 when out holds the text. */
static int wxr_pri_text(const WCHAR *dir, const char *value, WCHAR *out, int cap) {
    char path[600]; const char *r; PriFile f; WCHAR pri[MAX_PATH]; int k, ok = 0;
    out[0] = 0;
    if (_strnicmp(value, "ms-resource:", 12)) { MultiByteToWideChar(CP_UTF8, 0, value, -1, out, cap); return out[0] != 0; }
    r = value + 12;
    /* ms-resource://<authority>/path and ms-resource:///path are absolute; anything else is relative to the Resources map
       ("AppName" and "AppName/Text" -> Resources/AppName[/Text]) */
    if (!strncmp(r, "//", 2)) { r += 2; r = strchr(r, '/'); if (!r) return 0; r++; lstrcpynA(path, r, sizeof path); }
    else if (*r == '/') lstrcpynA(path, r + 1, sizeof path);
    else _snprintf(path, sizeof path, "Resources/%s", r);
    _snwprintf(pri, MAX_PATH, L"%ls\\resources.pri", dir);
    if (!pri_open(&f, pri)) return 0;
    if ((k = find_item(&f, path)) >= 0) { DWORD len = 0; int t = -1; const unsigned char *p = item_data(&f, k, &len, &t);
        if (p && t == 0) { int n = (int)(len / 2); if (n >= cap) n = cap - 1; memcpy(out, p, n * 2); out[n] = 0; while (n && !out[n - 1]) n--; out[n] = 0; ok = n > 0; }
        else if (p && (t == 3 || t == 4)) { char tmp[1024]; int n = (int)len < 1023 ? (int)len : 1023; memcpy(tmp, p, n); tmp[n] = 0; ok = MultiByteToWideChar(t == 4 ? CP_UTF8 : CP_ACP, 0, tmp, -1, out, cap) > 1; } }
    pri_free(&f); return ok; }
