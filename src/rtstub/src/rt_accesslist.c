/* rt_accesslist.c - StorageApplicationPermissions.FutureAccessList / MostRecentlyUsedList (IStorageItemAccessList).
 * Win10 keeps them in its state-repository service (absent on Win7). Here: a token -> item table, persisted as token / kind / path
 * in %LOCALAPPDATA%\wxr\accesslist_<exe>.txt (UTF-16LE, one "token\tD|F\tpath" line per entry), so tokens survive restarts.
 * An entry loaded from disk is re-opened on first use through Win10's StorageFolder/StorageFile.Get*FromPathAsync.
 * Without persistence Media Player's "Recent media" entries (tokens from an earlier session) resolved to NULL and playback hung.
 * Both lists share one table (tokens are unique GUID strings). */
#include "rt_core.h"
#include <stdio.h>

typedef HRESULT (WINAPI *PRAW)(void *, UINT32 *);
static const GUID IID_IStorageItem_ = { 0x4207A996, 0xCA2F, 0x42F7, { 0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30 } };
static const GUID IID_IStorageFile_ = { 0xFA3F6186, 0x4214, 0x428C, { 0xA6, 0x4C, 0x14, 0xC9, 0xAC, 0x73, 0x15, 0xEA } };
static const GUID IID_IStorageFolder_ = { 0x72D1CB78, 0xB3EF, 0x4F75, { 0xA8, 0x0B, 0x6F, 0xD9, 0xDA, 0xE2, 0x94, 0x4B } };
static const GUID IID_IStorageFileStatics_a = { 0x5984C710, 0xDAF2, 0x43C8, { 0x8B, 0xB4, 0xA4, 0xD3, 0xEA, 0xCF, 0xD0, 0x3F } };
static const GUID IID_IStorageFolderStatics_a = { 0x08F327FF, 0x85D5, 0x48B9, { 0xAE, 0xE9, 0x28, 0x51, 0x1E, 0x33, 0x9F, 0x9F } };
HRESULT from_path(const WCHAR *cls, const GUID *iid, const WCHAR *path, IUnknown **op);   /* rt_pickers.c */
IUnknown *await_obj(IUnknown *op);

typedef struct { WCHAR token[64]; WCHAR *path; int dir; IUnknown *item; } Ent;   /* item NULL: loaded from disk, not opened yet */
static Ent *g_ent; static int g_n, g_cap, g_loaded; static SRWLOCK g_lk = SRWLOCK_INIT;

static const WCHAR *raw(void *hs)
{
    static const WCHAR *(WINAPI *p)(void *, UINT32 *);
    if (!hs) return L"";
    if (!p) { HMODULE m = GetModuleHandleA("wxrsys.dll"); if (!m) m = GetModuleHandleA("api-ms-win-core-winrt-string-l1-1-0.dll"); if (m) p = (const WCHAR *(WINAPI *)(void *, UINT32 *))GetProcAddress(m, "WindowsGetStringRawBuffer"); }
    return p ? p(hs, NULL) : L"";
}
static WCHAR *wdup(const WCHAR *s) { size_t n = lstrlenW(s) + 1; WCHAR *d = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR)); if (d) memcpy(d, s, n * sizeof(WCHAR)); return d; }
static void ent_free(Ent *e) { if (e->item) e->item->lpVtbl->Release(e->item); if (e->path) HeapFree(GetProcessHeap(), 0, e->path); e->item = NULL; e->path = NULL; }
static int find(const WCHAR *t) { int i; for (i = 0; i < g_n; i++) if (!lstrcmpiW(g_ent[i].token, t)) return i; return -1; }
static Ent *grow(void)
{
    if (g_n == g_cap) { int nc = g_cap ? g_cap * 2 : 16; Ent *ne = (Ent *)(g_ent ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_ent, nc * sizeof *ne) : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, nc * sizeof *ne));
        if (!ne) return NULL; g_ent = ne; g_cap = nc; }
    memset(&g_ent[g_n], 0, sizeof g_ent[0]); return &g_ent[g_n++];
}

/* ---- persistence ---- */
static BOOL store_path(WCHAR *p)
{
    WCHAR exe[MAX_PATH], *b, *dot; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH);
    if (!n || n >= MAX_PATH - 80) return FALSE;
    lstrcatW(p, L"\\wxr"); CreateDirectoryW(p, NULL);
    GetModuleFileNameW(NULL, exe, MAX_PATH); b = wcsrchr(exe, L'\\'); b = b ? b + 1 : exe; dot = wcsrchr(b, L'.'); if (dot) *dot = 0;
    if (lstrlenW(p) + lstrlenW(b) + 20 >= MAX_PATH) return FALSE;
    lstrcatW(p, L"\\accesslist_"); lstrcatW(p, b); lstrcatW(p, L".txt"); return TRUE;
}
static void save_locked(void)
{
    WCHAR p[MAX_PATH]; HANDLE h; int i; DWORD w;
    if (!store_path(p) || (h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) { rt_log("access list: cannot write %ls", p); return; }
    WriteFile(h, "\xFF\xFE", 2, &w, NULL);
    for (i = 0; i < g_n; i++) if (g_ent[i].path && g_ent[i].path[0]) {
        WriteFile(h, g_ent[i].token, lstrlenW(g_ent[i].token) * sizeof(WCHAR), &w, NULL);
        WriteFile(h, g_ent[i].dir ? L"\tD\t" : L"\tF\t", 3 * sizeof(WCHAR), &w, NULL);
        WriteFile(h, g_ent[i].path, lstrlenW(g_ent[i].path) * sizeof(WCHAR), &w, NULL);
        WriteFile(h, L"\r\n", 2 * sizeof(WCHAR), &w, NULL); }
    CloseHandle(h);
}
static void load_locked(void)
{
    WCHAR p[MAX_PATH], *buf, *line, *next; HANDLE h; DWORD sz, rd = 0; int n = 0;
    if (g_loaded) return; g_loaded = 1;
    if (!store_path(p) || (h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) return;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 4 * 1024 * 1024 || !(buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sz + 4))) { CloseHandle(h); return; }
    ReadFile(h, buf, sz, &rd, NULL); CloseHandle(h);
    line = (rd >= 2 && buf[0] == 0xFEFF) ? buf + 1 : buf;
    for (; line && *line; line = next) {
        WCHAR *t1, *t2, *eol = wcspbrk(line, L"\r\n"); Ent *e;
        next = eol ? eol + 1 : NULL; if (eol) *eol = 0;
        if (!(t1 = wcschr(line, L'\t')) || !(t2 = wcschr(t1 + 1, L'\t')) || t1 - line >= 64 || !t2[1]) continue;
        *t1 = 0; if (find(line) >= 0 || !(e = grow())) continue;
        lstrcpynW(e->token, line, 64); e->dir = t1[1] == L'D'; e->path = wdup(t2 + 1); n++;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    rt_log("access list: %d persisted entr%s loaded from %ls", n, n == 1 ? "y" : "ies", p);
}
/* path and kind of a storage item: IStorageItem slot 12 = get_Path, slot 13 = get_Attributes (FileAttributes.Directory = 0x10) */
static void item_info(IUnknown *it, WCHAR **path, int *dir)
{
    void *hs = NULL; UINT32 a = 0;
    *path = NULL; *dir = 0;
    HRESULT hp = ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)it)[12])(it, &hs), ha;
    rt_log("access list:   item %p get_Path -> 0x%08lX %p", (void *)it, (unsigned long)hp, hs);
    if (SUCCEEDED(hp) && hs) {
        typedef HRESULT (WINAPI *PDEL)(void *); HMODULE m = GetModuleHandleA("wxrsys.dll"); PDEL del = m ? (PDEL)GetProcAddress(m, "WindowsDeleteString") : NULL;
        *path = wdup(raw(hs)); if (del) del(hs); }
    if (SUCCEEDED(ha = ((HRESULT (STDMETHODCALLTYPE *)(void *, UINT32 *))(*(void ***)it)[13])(it, &a))) *dir = (a & 0x10) != 0;
    rt_log("access list:   get_Attributes -> 0x%08lX 0x%X", (unsigned long)ha, a);
}

static HRESULT put(const WCHAR *token, IUnknown *item)
{
    int i, dir; IUnknown *u = NULL; WCHAR *path; Ent *e;
    if (!item || FAILED(item->lpVtbl->QueryInterface(item, &IID_IStorageItem_, (void **)&u))) return E_INVALIDARG;
    item_info(u, &path, &dir);
    AcquireSRWLockExclusive(&g_lk); load_locked();
    if ((i = find(token)) >= 0) { ent_free(&g_ent[i]); e = &g_ent[i]; }
    else if (!(e = grow())) { ReleaseSRWLockExclusive(&g_lk); u->lpVtbl->Release(u); if (path) HeapFree(GetProcessHeap(), 0, path); return E_OUTOFMEMORY; }
    lstrcpynW(e->token, token, 64); e->item = u; e->path = path; e->dir = dir;
    save_locked();
    ReleaseSRWLockExclusive(&g_lk);
    rt_log("access list: %ls = %s %ls", token, dir ? "folder" : "file", path ? path : L"(no path)");
    return S_OK;
}
static IUnknown *get(const WCHAR *token)
{
    int i, dir = 0; IUnknown *u = NULL, *op = NULL; WCHAR *path = NULL;
    AcquireSRWLockExclusive(&g_lk); load_locked();
    if ((i = find(token)) >= 0) { if ((u = g_ent[i].item)) u->lpVtbl->AddRef(u); else if (g_ent[i].path) { path = wdup(g_ent[i].path); dir = g_ent[i].dir; } }
    ReleaseSRWLockExclusive(&g_lk);
    if (u || !path) return u;
    /* persisted entry: re-open it (outside the lock - the operation pumps messages) */
    if (SUCCEEDED(from_path(dir ? L"Windows.Storage.StorageFolder" : L"Windows.Storage.StorageFile", dir ? &IID_IStorageFolderStatics_a : &IID_IStorageFileStatics_a, path, &op)) && op) {
        IUnknown *r = await_obj(op); op->lpVtbl->Release(op);
        if (r) { r->lpVtbl->QueryInterface(r, &IID_IStorageItem_, (void **)&u); r->lpVtbl->Release(r); } }
    rt_log("access list: reopened %ls %ls -> %p", dir ? L"folder" : L"file", path, (void *)u);
    if (u) { AcquireSRWLockExclusive(&g_lk); if ((i = find(token)) >= 0 && !g_ent[i].item) { g_ent[i].item = u; u->lpVtbl->AddRef(u); } ReleaseSRWLockExclusive(&g_lk); }
    HeapFree(GetProcessHeap(), 0, path);
    return u;
}
static HRESULT add_new(IUnknown *item, void **token_out)
{
    GUID g; WCHAR t[64]; HRESULT hr;
    if (!token_out) return E_POINTER; *token_out = NULL;
    CoCreateGuid(&g);
    swprintf(t, 64, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    if (FAILED(hr = put(t, item))) return hr;
    *token_out = rt_hstring(t); return *token_out ? S_OK : E_OUTOFMEMORY;
}
/* completed IAsyncOperation<T> with the stored item, as T's default interface; unknown token: the operation still completes, result NULL */
static HRESULT get_async(void *token, const char *opname, const GUID *iid, void **out)
{
    IUnknown *u = get(raw(token)), *v = NULL; void *op;
    rt_log("access list: %s(%ls) [hstring %p] -> item %p", opname, raw(token), token, (void *)u);
    if (!out) return E_POINTER; *out = NULL;
    if (u) { u->lpVtbl->QueryInterface(u, iid, (void **)&v); u->lpVtbl->Release(u); }
    if (!v) rt_log("access list: no %s for token %ls", opname, raw(token));
    op = rt_new_async_result(rt_async_named(opname), v);
    if (v) v->lpVtbl->Release(v);
    *out = op; return op ? S_OK : E_OUTOFMEMORY;
}
#define OP_ITEM "Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>"
#define OP_FILE "Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>"
#define OP_FOLDER "Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>"

HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_Add(void *This, void *item, void *tok) { (void)This; return add_new((IUnknown *)item, (void **)tok); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_Add_2(void *This, void *item, void *meta, void *tok) { (void)This; (void)meta; return add_new((IUnknown *)item, (void **)tok); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_AddOrReplace(void *This, void *token, void *item) { (void)This; return put(raw(token), (IUnknown *)item); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_AddOrReplace_2(void *This, void *token, void *item, void *meta) { (void)This; (void)meta; return put(raw(token), (IUnknown *)item); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_GetItemAsync(void *This, void *token, void *out) { (void)This; return get_async(token, OP_ITEM, &IID_IStorageItem_, (void **)out); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_GetFileAsync(void *This, void *token, void *out) { (void)This; return get_async(token, OP_FILE, &IID_IStorageFile_, (void **)out); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_GetFolderAsync(void *This, void *token, void *out) { (void)This; return get_async(token, OP_FOLDER, &IID_IStorageFolder_, (void **)out); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_GetItemAsync_2(void *This, void *token, void *opt, void *out) { (void)This; (void)opt; return get_async(token, OP_ITEM, &IID_IStorageItem_, (void **)out); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_GetFileAsync_2(void *This, void *token, void *opt, void *out) { (void)This; (void)opt; return get_async(token, OP_FILE, &IID_IStorageFile_, (void **)out); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_GetFolderAsync_2(void *This, void *token, void *opt, void *out) { (void)This; (void)opt; return get_async(token, OP_FOLDER, &IID_IStorageFolder_, (void **)out); }
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_Remove(void *This, void *token)
{
    int i; (void)This; AcquireSRWLockExclusive(&g_lk); load_locked(); rt_log("access list: Remove(%ls)", raw(token));
    if ((i = find(raw(token))) >= 0) { ent_free(&g_ent[i]); g_ent[i] = g_ent[--g_n]; save_locked(); }
    ReleaseSRWLockExclusive(&g_lk); return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_ContainsItem(void *This, void *token, void *out)
{
    (void)This; if (!out) return E_POINTER; AcquireSRWLockExclusive(&g_lk); load_locked(); *(BYTE *)out = find(raw(token)) >= 0; ReleaseSRWLockExclusive(&g_lk);
    rt_log("access list: ContainsItem(%ls) [hstring %p] -> %d", raw(token), token, *(BYTE *)out); return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_Clear(void *This)
{
    int i; (void)This; AcquireSRWLockExclusive(&g_lk); load_locked();
    for (i = 0; i < g_n; i++) ent_free(&g_ent[i]); g_n = 0; save_locked();
    ReleaseSRWLockExclusive(&g_lk); return S_OK;
}
/* Win10's file/folder pickers add the picked item to the app's MostRecentlyUsedList (empty metadata); Media Player reads it back
   through Entries for "Recent media". Same path already listed: kept (one entry per item). */
void rt_mru_add_path(const WCHAR *path, int dir)
{
    int i; Ent *e; GUID g;
    if (!path || !path[0]) return;
    AcquireSRWLockExclusive(&g_lk); load_locked();
    for (i = 0; i < g_n; i++) if (g_ent[i].path && !lstrcmpiW(g_ent[i].path, path)) break;
    if (i == g_n && (e = grow())) {
        CoCreateGuid(&g);
        swprintf(e->token, 64, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        e->path = wdup(path); e->dir = dir; save_locked();
        rt_log("access list: picker added %ls = %s %ls", e->token, dir ? "folder" : "file", path); }
    ReleaseSRWLockExclusive(&g_lk);
}
HRESULT STDMETHODCALLTYPE S_IStorageItemMostRecentlyUsedList2_Add(void *This, void *item, void *meta, void *vis, void *tok) { (void)This; (void)meta; (void)vis; return add_new((IUnknown *)item, (void **)tok); }
HRESULT STDMETHODCALLTYPE S_IStorageItemMostRecentlyUsedList2_AddOrReplace(void *This, void *token, void *item, void *meta, void *vis) { (void)This; (void)meta; (void)vis; return put(raw(token), (IUnknown *)item); }

/* ---- Entries: AccessListEntryView = IVectorView<AccessListEntry> + IIterable<AccessListEntry> (AccessListEntry = { HSTRING Token, Metadata });
   a snapshot of the tokens, most recent first. IIDs: the pinterface signatures' SHA-1 IIDs. ---- */
static const GUID IID_VV_ALE = { 0x42B49B8A, 0x3014, 0x5D27, { 0x8F, 0x2C, 0x1E, 0xF5, 0xEE, 0x89, 0xEC, 0x00 } };
static const GUID IID_IT_ALE = { 0x4995C2B0, 0x736B, 0x588D, { 0xAE, 0x42, 0x6F, 0x69, 0xB0, 0x25, 0xB3, 0x88 } };
static const GUID IID_ITR_ALE = { 0xD1A0A6C4, 0x889D, 0x519B, { 0x85, 0x08, 0x26, 0x24, 0x1B, 0x32, 0x9B, 0x7E } };
static const GUID IID_IUnknown_a = { 0, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_a = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_a = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
typedef struct { void *h_token, *h_meta; } ALE;
typedef struct { const void *vt_view, *vt_able; LONG ref; int n; WCHAR (*tok)[64]; } EView;
typedef struct { const void *vt; LONG ref; EView *v; int pos; } EIter;
#define EV_FROM_ABLE(t) ((EView *)((char *)(t) - offsetof(EView, vt_able)))
static void ev_entry(EView *v, int i, ALE *o) { o->h_token = rt_hstring(v->tok[i]); o->h_meta = NULL; }
static ULONG ev_addref(EView *v) { return InterlockedIncrement(&v->ref); }
static ULONG ev_release(EView *v) { LONG r = InterlockedDecrement(&v->ref); if (!r) { HeapFree(GetProcessHeap(), 0, v->tok); HeapFree(GetProcessHeap(), 0, v); } return r; }
static HRESULT ev_qi(EView *v, REFIID r, void **o)
{
    if (IsEqualGUID(r, &IID_IUnknown_a) || IsEqualGUID(r, &IID_IInspectable_a) || IsEqualGUID(r, &IID_IAgileObject_a) || IsEqualGUID(r, &IID_VV_ALE)) *o = &v->vt_view;
    else if (IsEqualGUID(r, &IID_IT_ALE)) *o = &v->vt_able;
    else { *o = NULL; return E_NOINTERFACE; }
    ev_addref(v); return S_OK;
}
static HRESULT STDMETHODCALLTYPE insp_iids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE ev_name(void *t, void **hs) { (void)t; *hs = rt_hstring(L"Windows.Storage.AccessCache.AccessListEntryView"); return S_OK; }
static HRESULT STDMETHODCALLTYPE it_name(void *t, void **hs) { (void)t; *hs = rt_hstring(L"Windows.Foundation.Collections.IIterator`1<Windows.Storage.AccessCache.AccessListEntry>"); return S_OK; }
static HRESULT STDMETHODCALLTYPE insp_trust(void *t, int *l) { (void)t; if (l) *l = 0; return S_OK; }
/* IVectorView */
static HRESULT STDMETHODCALLTYPE vv_qi(void *t, REFIID r, void **o) { return ev_qi((EView *)t, r, o); }
static ULONG STDMETHODCALLTYPE vv_addref(void *t) { return ev_addref((EView *)t); }
static ULONG STDMETHODCALLTYPE vv_release(void *t) { return ev_release((EView *)t); }
static HRESULT STDMETHODCALLTYPE vv_getat(void *t, UINT32 i, ALE *o) { EView *v = (EView *)t; if (!o) return E_POINTER; if (i >= (UINT32)v->n) return E_BOUNDS; ev_entry(v, (int)i, o); return S_OK; }
static HRESULT STDMETHODCALLTYPE vv_size(void *t, UINT32 *o) { if (!o) return E_POINTER; *o = (UINT32)((EView *)t)->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE vv_indexof(void *t, ALE val, UINT32 *idx, BYTE *found)
{
    EView *v = (EView *)t; int i; const WCHAR *s = raw(val.h_token); if (!idx || !found) return E_POINTER; *idx = 0; *found = 0;
    for (i = 0; i < v->n; i++) if (!lstrcmpiW(v->tok[i], s)) { *idx = (UINT32)i; *found = 1; break; } return S_OK;
}
static HRESULT STDMETHODCALLTYPE vv_getmany(void *t, UINT32 start, UINT32 cap, ALE *items, UINT32 *got)
{
    EView *v = (EView *)t; UINT32 k = 0; if (!got) return E_POINTER;
    for (; start + k < (UINT32)v->n && k < cap; k++) ev_entry(v, (int)(start + k), &items[k]); *got = k; return S_OK;
}
static const void *const g_vt_vv[] = { (void *)vv_qi, (void *)vv_addref, (void *)vv_release, (void *)insp_iids, (void *)ev_name, (void *)insp_trust,
    (void *)vv_getat, (void *)vv_size, (void *)vv_indexof, (void *)vv_getmany };
/* IIterator */
static HRESULT STDMETHODCALLTYPE itr_qi(void *t, REFIID r, void **o)
{
    if (IsEqualGUID(r, &IID_IUnknown_a) || IsEqualGUID(r, &IID_IInspectable_a) || IsEqualGUID(r, &IID_IAgileObject_a) || IsEqualGUID(r, &IID_ITR_ALE)) { *o = t; InterlockedIncrement(&((EIter *)t)->ref); return S_OK; }
    *o = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE itr_addref(void *t) { return InterlockedIncrement(&((EIter *)t)->ref); }
static ULONG STDMETHODCALLTYPE itr_release(void *t) { EIter *i = (EIter *)t; LONG r = InterlockedDecrement(&i->ref); if (!r) { ev_release(i->v); HeapFree(GetProcessHeap(), 0, i); } return r; }
static HRESULT STDMETHODCALLTYPE itr_current(void *t, ALE *o) { EIter *i = (EIter *)t; if (!o) return E_POINTER; if (i->pos >= i->v->n) return E_BOUNDS; ev_entry(i->v, i->pos, o); return S_OK; }
static HRESULT STDMETHODCALLTYPE itr_has(void *t, BYTE *o) { EIter *i = (EIter *)t; if (!o) return E_POINTER; *o = i->pos < i->v->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE itr_next(void *t, BYTE *o) { EIter *i = (EIter *)t; if (i->pos < i->v->n) i->pos++; if (o) *o = i->pos < i->v->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE itr_getmany(void *t, UINT32 cap, ALE *items, UINT32 *got)
{
    EIter *i = (EIter *)t; UINT32 k = 0; if (!got) return E_POINTER;
    for (; i->pos < i->v->n && k < cap; k++, i->pos++) ev_entry(i->v, i->pos, &items[k]); *got = k; return S_OK;
}
static const void *const g_vt_itr[] = { (void *)itr_qi, (void *)itr_addref, (void *)itr_release, (void *)insp_iids, (void *)it_name, (void *)insp_trust,
    (void *)itr_current, (void *)itr_has, (void *)itr_next, (void *)itr_getmany };
/* IIterable */
static HRESULT STDMETHODCALLTYPE ab_qi(void *t, REFIID r, void **o) { return ev_qi(EV_FROM_ABLE(t), r, o); }
static ULONG STDMETHODCALLTYPE ab_addref(void *t) { return ev_addref(EV_FROM_ABLE(t)); }
static ULONG STDMETHODCALLTYPE ab_release(void *t) { return ev_release(EV_FROM_ABLE(t)); }
static HRESULT STDMETHODCALLTYPE ab_first(void *t, void **o)
{
    EView *v = EV_FROM_ABLE(t); EIter *i; if (!o) return E_POINTER;
    if (!(i = (EIter *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *i))) return E_OUTOFMEMORY;
    i->vt = g_vt_itr; i->ref = 1; i->v = v; ev_addref(v); *o = i; return S_OK;
}
static const void *const g_vt_ab[] = { (void *)ab_qi, (void *)ab_addref, (void *)ab_release, (void *)insp_iids, (void *)ev_name, (void *)insp_trust, (void *)ab_first };
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_get_Entries(void *This, void *out)
{
    EView *v; int i, n; (void)This;
    if (!out) return E_POINTER; *(void **)out = NULL;
    if (!(v = (EView *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *v))) return E_OUTOFMEMORY;
    v->vt_view = g_vt_vv; v->vt_able = g_vt_ab; v->ref = 1;
    AcquireSRWLockExclusive(&g_lk); load_locked();
    n = g_n; v->tok = (WCHAR (*)[64])HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (n ? n : 1) * sizeof *v->tok);
    if (v->tok) for (i = 0; i < n; i++) lstrcpynW(v->tok[i], g_ent[n - 1 - i].token, 64);   /* most recent first */
    v->n = v->tok ? n : 0;
    ReleaseSRWLockExclusive(&g_lk);
    rt_log("access list: Entries -> %d", v->n);
    *(void **)out = v; return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_CheckAccess(void *This, void *item, void *out) { (void)This; (void)item; if (!out) return E_POINTER; *(BYTE *)out = 1; return S_OK; }
