/* rt_install.c - Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager: the Store's install queue, hand-written.
 *
 * Win10's AppInstallManager is a proxy to the InstallService; here the queue lives in this process (one per Store process) and every
 * item is a child process of the deployment engine:   <root>\Layer\wxrpkg.exe store <ProductId | PackageFamilyName> --progress
 * (pkg\src\store.inc: catalogue + Microsoft's delivery service, anonymous, free unencrypted packages; installs through wxrpkg add).
 * Its "@state ..." / "@bytes done total" lines become the item's AppInstallStatus and the StatusChanged / Completed events.
 *
 *   StartAppInstallAsync / StartProductInstallAsync / UpdateAppByPackageFamilyNameAsync   -> a queue item, started at once
 *   SearchForUpdatesAsync(product)        -> `wxrpkg store <id> --check`: an item when the product can be updated, else null
 *   SearchForAllUpdatesAsync              -> the same check for every main package registered for the user (not the Store itself:
 *                                            it cannot replace itself while running), items for those with an update
 *   AppInstallItems[WithGroupSupport]     -> the items that are not finished
 *   Cancel                                -> ends the child process (state Canceled). Pause / Restart: accepted, not implemented.
 * Search operations are real pending IAsyncOperations (rt_new_async_pending) completed from a worker thread; events are raised on the
 * worker threads too, as Win10 raises them on thread-pool threads.
 * Objects: AppInstallItem (one COM object per queue item, the same one every time), AppInstallStatus (a snapshot),
 * AppInstallManagerItemEventArgs - generated hidden classes (classes.txt "~"), state through rt_user.
 * Not here: the purchase / entitlement step that precedes Get in the Store UI (needs sign-in), per-user and volume options. */
#include "rt_core.h"
#include <stdio.h>
#include <stdlib.h>

#define NS "Windows.ApplicationModel.Store.Preview.InstallControl."
enum { ST_PENDING = 0, ST_STARTING, ST_LICENSE, ST_DOWNLOADING, ST_RESTORING, ST_INSTALLING, ST_COMPLETED, ST_CANCELED, ST_PAUSED, ST_ERROR };   /* AppInstallState */
#define MAXQ 256
#define MAXH 8
typedef struct QItem {
    int used, finished, type;                 /* type: AppInstallType Install 0 / Update 1 / Repair 2 */
    WCHAR what[220], product[40], family[200];
    int state; UINT64 total, done; double pct; HRESULT err;
    void *obj;                                /* the AppInstallItem (a reference held for the life of the process) */
    HANDLE proc; int cancel;
    IUnknown *hStatus[MAXH], *hDone[MAXH];    /* the item's own StatusChanged / Completed handlers */
} QItem;
typedef struct { int state; UINT64 total, done; double pct; HRESULT err; } QStatus;
static QItem g_q[MAXQ]; static int g_nq;
static struct { IUnknown *h; void *sender; } g_mStatus[32], g_mDone[32];   /* AppInstallManager.ItemStatusChanged / ItemCompleted */
static CRITICAL_SECTION g_cs; static LONG g_cs_init;
static void lock(void) { if (InterlockedCompareExchange(&g_cs_init, 1, 0) == 0) { InitializeCriticalSection(&g_cs); InterlockedExchange(&g_cs_init, 2); } while (g_cs_init != 2) Sleep(0); EnterCriticalSection(&g_cs); }
static void unlock(void) { LeaveCriticalSection(&g_cs); }

static const WCHAR *raw(void *hs)
{
    static const WCHAR *(WINAPI *p)(void *, UINT32 *);
    if (!hs) return L"";
    if (!p) p = (const WCHAR *(WINAPI *)(void *, UINT32 *))rt_winrt_fn("WindowsGetStringRawBuffer");
    return p ? p(hs, NULL) : L"";
}
static int wxr_root(WCHAR *out)
{
    HMODULE m = NULL; WCHAR *b;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)wxr_root, &m) || !GetModuleFileNameW(m, out, MAX_PATH)) return 0;
    if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0;
    if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0;
    return 1;
}
static QItem *item_of(void *This) { int *i = (int *)rt_user(This); return (i && *i >= 0 && *i < g_nq) ? &g_q[*i] : NULL; }

/* ---- events ---- */
static void fire(QItem *q, int completed)
{
    IUnknown *ih[MAXH], *mh[32]; void *ms[32]; int ni = 0, nm = 0, i; void *args = NULL; int cls = rt_class_index(L"" NS "AppInstallManagerItemEventArgs");
    lock();
    for (i = 0; i < MAXH; i++) { IUnknown *h = completed ? q->hDone[i] : q->hStatus[i]; if (h) { h->lpVtbl->AddRef(h); ih[ni++] = h; } }
    for (i = 0; i < 32; i++) { IUnknown *h = completed ? g_mDone[i].h : g_mStatus[i].h; if (h) { h->lpVtbl->AddRef(h); mh[nm] = h; ms[nm++] = completed ? g_mDone[i].sender : g_mStatus[i].sender; } }
    unlock();
    /* TypedEventHandler<TSender, TArgs>::Invoke(sender, args) = slot 3 */
    for (i = 0; i < ni; i++) { ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))(*(void ***)ih[i])[3])(ih[i], q->obj, NULL); ih[i]->lpVtbl->Release(ih[i]); }
    if (nm && cls >= 0 && (args = rt_new_object(cls, NULL))) { int *u = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int)); if (u) { *u = (int)(q - g_q); rt_set_user(args, u); } }
    for (i = 0; i < nm; i++) { if (args) ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))(*(void ***)mh[i])[3])(mh[i], ms[i], args); mh[i]->lpVtbl->Release(mh[i]); }
    if (args) rt_Release(args);
}
static void set_state(QItem *q, int st) { int ch; lock(); ch = q->state != st; q->state = st; unlock(); if (ch) fire(q, 0); }

/* ---- running the engine ---- */
/* runs wxrpkg store <what> [--check] --progress; calls line(ctx, text) for every output line; returns the exit code, -1 if it did not start */
static int run_engine(const WCHAR *what, int check, HANDLE *procOut, void (*line)(void *, const char *), void *ctx)
{
    WCHAR root[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH + 400]; SECURITY_ATTRIBUTES sa; HANDLE rd = NULL, wr = NULL; STARTUPINFOW si; PROCESS_INFORMATION pi; char buf[4096], ln[1024]; DWORD n, code = (DWORD)-1; int ll = 0; const WCHAR *c;
    for (c = what; *c; c++) if (!(iswalnum(*c) || *c == L'.' || *c == L'_' || *c == L'-')) { rt_log("install: refused id '%ls'", what); return -1; }   /* ids and family names only: it goes on a command line */
    if (!wxr_root(root)) return -1;
    _snwprintf(exe, MAX_PATH, L"%s\\Layer\\wxrpkg.exe", root); _snwprintf(cmd, MAX_PATH + 399, L"\"%s\" store %s %s--progress", exe, what, check ? L"--check " : L"");
    sa.nLength = sizeof sa; sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = NULL;
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { rt_log("install: cannot start %ls (error %lu)", cmd, GetLastError()); CloseHandle(rd); CloseHandle(wr); return -1; }
    CloseHandle(wr); CloseHandle(pi.hThread); if (procOut) *procOut = pi.hProcess;
    while (ReadFile(rd, buf, sizeof buf, &n, NULL) && n) { DWORD i;
        for (i = 0; i < n; i++) { if (buf[i] == '\n' || ll == sizeof ln - 1) { ln[ll] = 0; if (ll && ln[ll - 1] == '\r') ln[ll - 1] = 0; if (ln[0]) line(ctx, ln); ll = 0; } else ln[ll++] = buf[i]; } }
    if (ll) { ln[ll] = 0; line(ctx, ln); }
    CloseHandle(rd); WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &code);
    if (procOut) { lock(); *procOut = NULL; unlock(); } CloseHandle(pi.hProcess);
    return (int)code;
}
static void item_line(void *ctx, const char *l)
{
    QItem *q = (QItem *)ctx; static DWORD last;
    if (!strncmp(l, "@state ", 7)) { const char *s = l + 7;
        set_state(q, !strcmp(s, "lookup") || !strcmp(s, "sync") ? ST_STARTING : !strcmp(s, "download") ? ST_DOWNLOADING : !strcmp(s, "install") ? ST_INSTALLING : q->state); }
    else if (!strncmp(l, "@bytes ", 7)) { unsigned long long d = 0, t = 0;
        if (sscanf(l + 7, "%llu %llu", &d, &t) == 2) { lock(); q->done = d; q->total = t; q->pct = t ? d * 100.0 / t : 0; unlock(); if (GetTickCount() - last > 400) { last = GetTickCount(); fire(q, 0); } } }
    else if (!strncmp(l, "@product ", 9)) { lock(); MultiByteToWideChar(CP_UTF8, 0, l + 9, -1, q->product, 40); unlock(); }
    else if (!strncmp(l, "@family ", 8)) { lock(); MultiByteToWideChar(CP_UTF8, 0, l + 8, -1, q->family, 200); unlock(); }
    else if (!strncmp(l, "@size ", 6)) { lock(); q->total = _strtoui64(l + 6, NULL, 10); unlock(); }
    else if (l[0] != '@') rt_log("install [%ls]: %.300s", q->what, l);
}
static DWORD WINAPI item_thread(void *p)
{
    QItem *q = (QItem *)p; int rc;
    rt_log("install: starting %ls (%s)", q->what, q->type == 1 ? "update" : "install");
    set_state(q, ST_STARTING);
    rc = run_engine(q->what, 0, &q->proc, item_line, q);
    lock(); if (q->cancel) q->state = ST_CANCELED; else if (rc == 0) { q->state = ST_COMPLETED; q->pct = 100; if (q->total) q->done = q->total; } else { q->state = ST_ERROR; q->err = (HRESULT)0x80004005L; } q->finished = 1; unlock();
    rt_log("install: %ls finished, exit code %d -> state %d", q->what, rc, q->state);
    fire(q, 0); fire(q, 1);
    return 0;
}
/* the queue item for `what` (a product id or a package family name): the running one, else a new one that starts now */
static QItem *enqueue(const WCHAR *what, int type)
{
    QItem *q = NULL; int i, cls = rt_class_index(L"" NS "AppInstallItem"), *u; HANDLE t;
    if (!what[0] || cls < 0) return NULL;
    lock();
    for (i = 0; i < g_nq; i++) if (!g_q[i].finished && (!lstrcmpiW(g_q[i].what, what) || !lstrcmpiW(g_q[i].product, what) || !lstrcmpiW(g_q[i].family, what))) { q = &g_q[i]; break; }
    if (!q && g_nq < MAXQ) {
        q = &g_q[g_nq]; memset(q, 0, sizeof *q); q->used = 1; q->type = type; lstrcpynW(q->what, what, 220);
        if (wcschr(what, L'_')) lstrcpynW(q->family, what, 200); else lstrcpynW(q->product, what, 40);
        if ((q->obj = rt_new_object(cls, NULL)) && (u = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int)))) { *u = g_nq; rt_set_user(q->obj, u); g_nq++; } else q = NULL;
        unlock();
        if (q) { t = CreateThread(NULL, 0, item_thread, q, 0, NULL); if (t) CloseHandle(t); }
        return q;
    }
    unlock(); return q;
}
static void *item_ref(QItem *q) { if (q && q->obj) rt_AddRef(q->obj); return q ? q->obj : NULL; }
static void *items_view(QItem **list, int n)   /* IVectorView<AppInstallItem> */
{
    const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IVectorView`1<" NS "AppInstallItem>"); IUnknown *it[MAXQ]; int i, k = 0;
    if (!c) return NULL;
    for (i = 0; i < n && k < MAXQ; i++) if (list[i] && list[i]->obj) it[k++] = (IUnknown *)list[i]->obj;
    return rt_new_coll_items(c, it, k);
}
static void *active_items(void) { QItem *l[MAXQ]; int i, n = 0; lock(); for (i = 0; i < g_nq; i++) if (!g_q[i].finished) l[n++] = &g_q[i]; unlock(); return items_view(l, n); }

/* ---- update checks ---- */
typedef struct { int update; char product[40]; } Chk;
static void chk_line(void *ctx, const char *l) { Chk *c = (Chk *)ctx; if (!strncmp(l, "@update ", 8)) c->update = !strcmp(l + 8, "yes"); else if (!strncmp(l, "@product ", 9)) lstrcpynA(c->product, l + 9, 40); }
static int has_update(const WCHAR *what) { Chk c; memset(&c, 0, sizeof c); return run_engine(what, 1, NULL, chk_line, &c) == 0 && c.update; }
typedef struct { void *op; WCHAR what[220]; int all; } Search;
/* the user's main packages: families of records that are neither frameworks nor resource packs */
static int main_families(WCHAR fam[][200], int cap)
{
    WCHAR root[MAX_PATH], ini[MAX_PATH], user[128], rec[MAX_PATH], v[16], *sec, *p; DWORD un = 128; int n = 0, i;
    if (!wxr_root(root) || !GetUserNameW(user, &un) || !(sec = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 65536 * sizeof(WCHAR)))) return 0;
    _snwprintf(ini, MAX_PATH, L"%s\\Users\\%s\\packages.ini", root, user); GetPrivateProfileSectionW(L"registered", sec, 65536, ini);
    for (p = sec; *p && n < cap; p += lstrlenW(p) + 1) { WCHAR full[220], f[200], *eq = wcschr(p, L'='); size_t len = eq ? (size_t)(eq - p) : (size_t)lstrlenW(p);
        if (!len || len >= 220) continue; memcpy(full, p, len * sizeof(WCHAR)); full[len] = 0;
        _snwprintf(rec, MAX_PATH, L"%s\\ProgramData\\Packages\\%s.ini", root, full);
        GetPrivateProfileStringW(L"package", L"IsFramework", L"", v, 16, rec); if (!lstrcmpiW(v, L"True")) continue;
        GetPrivateProfileStringW(L"package", L"IsResourcePackage", L"", v, 16, rec); if (!lstrcmpiW(v, L"True")) continue;
        if (!GetPrivateProfileStringW(L"package", L"PackageFamilyName", L"", f, 200, rec)) continue;
        for (i = 0; i < n; i++) if (!lstrcmpiW(fam[i], f)) break;
        if (i == n) lstrcpynW(fam[n++], f, 200); }
    HeapFree(GetProcessHeap(), 0, sec); return n;
}
static DWORD WINAPI search_thread(void *p)
{
    Search *s = (Search *)p; QItem *found[64]; int nf = 0, i;
    if (s->all) { static WCHAR fam[64][200]; int n = main_families(fam, 64);
        for (i = 0; i < n; i++) {
            if (!_wcsnicmp(fam[i], L"Microsoft.WindowsStore_", 23)) continue;      /* the running Store cannot replace itself */
            if (has_update(fam[i])) { QItem *q = enqueue(fam[i], 1); if (q && nf < 64) found[nf++] = q; rt_log("install: update available for %ls", fam[i]); }
            else rt_log("install: %ls is up to date", fam[i]); }
        { void *v = items_view(found, nf); rt_async_complete(s->op, (IUnknown *)v, NULL, 0); if (v) rt_Release(v); }
    } else {
        QItem *q = has_update(s->what) ? enqueue(s->what, 1) : NULL;
        rt_log("install: SearchForUpdates(%ls) -> %s", s->what, q ? "update queued" : "nothing");
        rt_async_complete(s->op, q ? (IUnknown *)q->obj : NULL, NULL, 0);
    }
    rt_Release(s->op); HeapFree(GetProcessHeap(), 0, s); return 0;
}
static HRESULT search(const WCHAR *what, int all, void **out)
{
    const RtAsync *a = rt_async_named(all ? "Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<" NS "AppInstallItem>>" : "Windows.Foundation.IAsyncOperation`1<" NS "AppInstallItem>");
    Search *s; HANDLE t;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    if (!(s = (Search *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s)) || !(s->op = rt_new_async_pending(a))) return E_OUTOFMEMORY;
    s->all = all; if (what) lstrcpynW(s->what, what, 220);
    rt_AddRef(s->op); *out = s->op;                                              /* one reference for the caller, one for the thread */
    rt_log("install: %s", all ? "SearchForAllUpdates" : "SearchForUpdates");
    if (!(t = CreateThread(NULL, 0, search_thread, s, 0, NULL))) { rt_async_complete(s->op, NULL, NULL, 0); rt_Release(s->op); HeapFree(GetProcessHeap(), 0, s); } else CloseHandle(t);
    return S_OK;
}
/* a started install as IAsyncOperation<AppInstallItem> (one = 1) or IAsyncOperation<IVectorView<AppInstallItem>> */
static HRESULT start(const WCHAR *what, int type, int one, void **out, const char *api)
{
    const RtAsync *a = rt_async_named(one ? "Windows.Foundation.IAsyncOperation`1<" NS "AppInstallItem>" : "Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<" NS "AppInstallItem>>");
    QItem *q; void *v; WCHAR id[256]; WCHAR *slash;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    lstrcpynW(id, what ? what : L"", 256);                              /* the Store passes "<ProductId>/<SkuId>"; wxrpkg store wants the ProductId alone */
    if ((slash = wcschr(id, L'/'))) *slash = 0;
    q = enqueue(id, type); rt_log("install: %s(%ls) -> %s", api, id, q ? "queued" : "REFUSED");
    if (one) { *out = rt_new_async_result(a, q ? (IUnknown *)q->obj : NULL); return *out ? S_OK : E_OUTOFMEMORY; }
    v = items_view(&q, q ? 1 : 0); *out = rt_new_async_result(a, (IUnknown *)v); if (v) rt_Release(v); return *out ? S_OK : E_OUTOFMEMORY;
}
static HRESULT yes(void **out) { const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<bool>"); BYTE t = 1; if (!out) return E_POINTER; *out = a ? rt_new_async_value(a, &t, 1) : NULL; return *out ? S_OK : E_NOTIMPL; }
static HRESULT cancel(const WCHAR *what)
{
    int i; lock();
    for (i = 0; i < g_nq; i++) if (!g_q[i].finished && (!lstrcmpiW(g_q[i].what, what) || !lstrcmpiW(g_q[i].product, what) || !lstrcmpiW(g_q[i].family, what))) { g_q[i].cancel = 1; if (g_q[i].proc) TerminateProcess(g_q[i].proc, 1223); rt_log("install: canceled %ls", what); }
    unlock(); return S_OK;
}

/* ---- AppInstallManager ---- */
#define M(n) HRESULT STDMETHODCALLTYPE S_IAppInstallManager##n
M(_get_AppInstallItems)(void *This, void **out) { (void)This; if (!out) return E_POINTER; *out = active_items(); return *out ? S_OK : E_OUTOFMEMORY; }
M(5_get_AppInstallItemsWithGroupSupport)(void *This, void **out) { (void)This; if (!out) return E_POINTER; *out = active_items(); return *out ? S_OK : E_OUTOFMEMORY; }
M(_Cancel)(void *This, void *id) { (void)This; return cancel(raw(id)); }
M(2_Cancel)(void *This, void *id, void *cv) { (void)This; (void)cv; return cancel(raw(id)); }
static HRESULT add_h(void *This, IUnknown *h, INT64 *tok, int done)
{
    int i; if (!h || !tok) return E_POINTER; lock();
    for (i = 0; i < 32; i++) if (!(done ? g_mDone[i].h : g_mStatus[i].h)) { h->lpVtbl->AddRef(h); rt_AddRef(This); if (done) { g_mDone[i].h = h; g_mDone[i].sender = This; } else { g_mStatus[i].h = h; g_mStatus[i].sender = This; } *tok = (done ? 2000 : 1000) + i; unlock(); return S_OK; }
    unlock(); *tok = 0; return S_OK;
}
static HRESULT rem_h(INT64 tok)
{
    IUnknown *h = NULL; void *s = NULL; int i; lock();
    if (tok >= 2000 && tok < 2032) { i = (int)tok - 2000; h = g_mDone[i].h; s = g_mDone[i].sender; g_mDone[i].h = NULL; }
    else if (tok >= 1000 && tok < 1032) { i = (int)tok - 1000; h = g_mStatus[i].h; s = g_mStatus[i].sender; g_mStatus[i].h = NULL; }
    unlock(); if (h) { h->lpVtbl->Release(h); rt_Release(s); } return S_OK;
}
M(_add_ItemCompleted)(void *This, IUnknown *h, INT64 *tok) { return add_h(This, h, tok, 1); }
M(_remove_ItemCompleted)(void *This, INT64 tok) { (void)This; return rem_h(tok); }
M(_add_ItemStatusChanged)(void *This, IUnknown *h, INT64 *tok) { return add_h(This, h, tok, 0); }
M(_remove_ItemStatusChanged)(void *This, INT64 tok) { (void)This; return rem_h(tok); }
M(_GetIsApplicableAsync)(void *This, void *pid, void *sku, void **out) { (void)This; (void)pid; (void)sku; return yes(out); }
M(3_GetIsApplicableForUserAsync)(void *This, void *u, void *pid, void *sku, void **out) { (void)This; (void)u; (void)pid; (void)sku; return yes(out); }
M(_GetIsAppAllowedToInstallAsync)(void *This, void *pid, void **out) { (void)This; (void)pid; return yes(out); }
M(2_GetIsAppAllowedToInstallAsync)(void *This, void *pid, void *sku, void *cat, void *cv, void **out) { (void)This; (void)pid; (void)sku; (void)cat; (void)cv; return yes(out); }
M(3_GetIsAppAllowedToInstallForUserAsync)(void *This, void *u, void *pid, void *sku, void *cat, void *cv, void **out) { (void)This; (void)u; (void)pid; (void)sku; (void)cat; (void)cv; return yes(out); }
M(_StartAppInstallAsync)(void *This, void *pid, void *sku, void *repair, void *force, void **out) { (void)This; (void)sku; (void)force; return start(raw(pid), (BYTE)(UINT_PTR)repair ? 2 : 0, 1, out, "StartAppInstall"); }
M(2_StartAppInstallAsync)(void *This, void *pid, void *sku, void *repair, void *force, void *cat, void *bundle, void *cv, void **out) { (void)This; (void)sku; (void)force; (void)cat; (void)bundle; (void)cv; return start(raw(pid), (BYTE)(UINT_PTR)repair ? 2 : 0, 1, out, "StartAppInstall"); }
M(3_StartProductInstallAsync)(void *This, void *pid, void *cat, void *flight, void *client, void *repair, void *force, void *cv, void *vol, void **out) { (void)This; (void)cat; (void)flight; (void)client; (void)force; (void)cv; (void)vol; return start(raw(pid), (BYTE)(UINT_PTR)repair ? 2 : 0, 0, out, "StartProductInstall"); }
M(3_StartProductInstallForUserAsync)(void *This, void *u, void *pid, void *cat, void *flight, void *client, void *repair, void *force, void *cv, void *vol, void **out) { (void)This; (void)u; (void)cat; (void)flight; (void)client; (void)force; (void)cv; (void)vol; return start(raw(pid), (BYTE)(UINT_PTR)repair ? 2 : 0, 0, out, "StartProductInstallForUser"); }
M(6_StartProductInstallAsync)(void *This, void *pid, void *flight, void *client, void *cv, void *opt, void **out) { (void)This; (void)flight; (void)client; (void)cv; (void)opt; return start(raw(pid), 0, 0, out, "StartProductInstall(options)"); }
M(6_StartProductInstallForUserAsync)(void *This, void *u, void *pid, void *flight, void *client, void *cv, void *opt, void **out) { (void)This; (void)u; (void)flight; (void)client; (void)cv; (void)opt; return start(raw(pid), 0, 0, out, "StartProductInstallForUser(options)"); }
M(_UpdateAppByPackageFamilyNameAsync)(void *This, void *pfn, void **out) { (void)This; return start(raw(pfn), 1, 1, out, "UpdateAppByPackageFamilyName"); }
M(2_UpdateAppByPackageFamilyNameAsync)(void *This, void *pfn, void *cv, void **out) { (void)This; (void)cv; return start(raw(pfn), 1, 1, out, "UpdateAppByPackageFamilyName"); }
M(3_UpdateAppByPackageFamilyNameForUserAsync)(void *This, void *u, void *pfn, void *cv, void **out) { (void)This; (void)u; (void)cv; return start(raw(pfn), 1, 1, out, "UpdateAppByPackageFamilyNameForUser"); }
M(_SearchForUpdatesAsync)(void *This, void *pid, void *sku, void **out) { (void)This; (void)sku; return search(raw(pid), 0, out); }
M(2_SearchForUpdatesAsync)(void *This, void *pid, void *sku, void *cat, void *cv, void **out) { (void)This; (void)sku; (void)cat; (void)cv; return search(raw(pid), 0, out); }
M(3_SearchForUpdatesForUserAsync)(void *This, void *u, void *pid, void *sku, void *cat, void *cv, void **out) { (void)This; (void)u; (void)sku; (void)cat; (void)cv; return search(raw(pid), 0, out); }
M(6_SearchForUpdatesAsync)(void *This, void *pid, void *sku, void *cv, void *client, void *opt, void **out) { (void)This; (void)sku; (void)cv; (void)client; (void)opt; return search(raw(pid), 0, out); }
M(6_SearchForUpdatesForUserAsync)(void *This, void *u, void *pid, void *sku, void *cv, void *client, void *opt, void **out) { (void)This; (void)u; (void)sku; (void)cv; (void)client; (void)opt; return search(raw(pid), 0, out); }
M(_SearchForAllUpdatesAsync)(void *This, void **out) { (void)This; return search(NULL, 1, out); }
M(2_SearchForAllUpdatesAsync)(void *This, void *cv, void **out) { (void)This; (void)cv; return search(NULL, 1, out); }
M(3_SearchForAllUpdatesForUserAsync)(void *This, void *u, void *cv, void **out) { (void)This; (void)u; (void)cv; return search(NULL, 1, out); }
M(6_SearchForAllUpdatesAsync)(void *This, void *cv, void *client, void *opt, void **out) { (void)This; (void)cv; (void)client; (void)opt; return search(NULL, 1, out); }
M(6_SearchForAllUpdatesForUserAsync)(void *This, void *u, void *cv, void *client, void *opt, void **out) { (void)This; (void)u; (void)cv; (void)client; (void)opt; return search(NULL, 1, out); }

/* ---- AppInstallItem ---- */
#define I(n) HRESULT STDMETHODCALLTYPE S_IAppInstallItem##n
static HRESULT put_str(void **out, const WCHAR *s) { if (!out) return E_POINTER; *out = s && s[0] ? rt_hstring(s) : NULL; return S_OK; }
I(_get_ProductId)(void *This, void **out) { QItem *q = item_of(This); return put_str(out, q ? q->product : L""); }
I(_get_PackageFamilyName)(void *This, void **out) { QItem *q = item_of(This); return put_str(out, q ? q->family : L""); }
I(_get_InstallType)(void *This, int *out) { QItem *q = item_of(This); if (out) *out = q ? q->type : 0; return S_OK; }
I(_get_IsUserInitiated)(void *This, BYTE *out) { (void)This; if (out) *out = 1; return S_OK; }
I(_GetCurrentStatus)(void *This, void **out)
{
    QItem *q = item_of(This); int cls = rt_class_index(L"" NS "AppInstallStatus"); QStatus *s;
    if (!out) return E_POINTER; *out = cls >= 0 ? rt_new_object(cls, NULL) : NULL; if (!*out) return E_OUTOFMEMORY;
    if (q && (s = (QStatus *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s))) { lock(); s->state = q->state; s->total = q->total; s->done = q->done; s->pct = q->pct; s->err = q->err; unlock(); rt_set_user(*out, s); }
    return S_OK;
}
I(_Cancel)(void *This) { QItem *q = item_of(This); return q ? cancel(q->what) : S_OK; }
I(2_Cancel)(void *This, void *cv) { QItem *q = item_of(This); (void)cv; return q ? cancel(q->what) : S_OK; }
static HRESULT item_add(void *This, IUnknown *h, INT64 *tok, int done)
{
    QItem *q = item_of(This); int i; if (!h || !tok) return E_POINTER; *tok = 0; if (!q) return S_OK; lock();
    for (i = 0; i < MAXH; i++) if (!(done ? q->hDone[i] : q->hStatus[i])) { h->lpVtbl->AddRef(h); if (done) q->hDone[i] = h; else q->hStatus[i] = h; *tok = (done ? 200 : 100) + i; break; }
    unlock(); return S_OK;
}
static HRESULT item_rem(void *This, INT64 tok)
{
    QItem *q = item_of(This); IUnknown *h = NULL; if (!q) return S_OK; lock();
    if (tok >= 200 && tok < 200 + MAXH) { h = q->hDone[tok - 200]; q->hDone[tok - 200] = NULL; } else if (tok >= 100 && tok < 100 + MAXH) { h = q->hStatus[tok - 100]; q->hStatus[tok - 100] = NULL; }
    unlock(); if (h) h->lpVtbl->Release(h); return S_OK;
}
I(_add_Completed)(void *This, IUnknown *h, INT64 *tok) { return item_add(This, h, tok, 1); }
I(_remove_Completed)(void *This, INT64 tok) { return item_rem(This, tok); }
I(_add_StatusChanged)(void *This, IUnknown *h, INT64 *tok) { return item_add(This, h, tok, 0); }
I(_remove_StatusChanged)(void *This, INT64 tok) { return item_rem(This, tok); }

/* ---- AppInstallStatus (snapshot), event args ---- */
static const QStatus g_nost;
#define ST(This) (rt_user(This) ? (const QStatus *)rt_user(This) : &g_nost)
HRESULT STDMETHODCALLTYPE S_IAppInstallStatus_get_InstallState(void *This, int *out) { if (out) *out = ST(This)->state; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppInstallStatus_get_DownloadSizeInBytes(void *This, UINT64 *out) { if (out) *out = ST(This)->total; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppInstallStatus_get_BytesDownloaded(void *This, UINT64 *out) { if (out) *out = ST(This)->done; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppInstallStatus_get_PercentComplete(void *This, double *out) { if (out) *out = ST(This)->pct; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppInstallStatus_get_ErrorCode(void *This, HRESULT *out) { if (out) *out = ST(This)->err; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppInstallStatus2_get_ReadyForLaunch(void *This, BYTE *out) { if (out) *out = ST(This)->state == ST_COMPLETED; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppInstallManagerItemEventArgs_get_Item(void *This, void **out) { if (!out) return E_POINTER; *out = item_ref(item_of(This)); return S_OK; }
