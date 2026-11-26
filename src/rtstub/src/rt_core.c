/* rt_core.c - the object model behind the generated stand-in classes: an object is an array of interface views {vtable, object};
 * statics objects (activation factories), instances, empty collections and completed async operations. */
#include "rt_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

typedef struct RObj RObj;
typedef struct { const void *vt; RObj *o; } RView;
struct RObj { LONG ref; int cls; int factory; const GUID *iid[24]; int n; RView v[24]; const RtColl *coll; const RtAsync *async; IUnknown *handler;
             IUnknown **items; int nitems, pos; IUnknown *result; int *ints; int nints; void *user; LONG pending; int nullres; HRESULT asyncerr; IUnknown *held; };   /* pending: an async operation that has not completed yet (rt_new_async_pending) */   /* user: hand-written state (rt_set_user), HeapFree'd with the object */   /* items: object collections (rt_new_coll_items); result: rt_new_async_result */
#define OBJ(t) (((RView *)(t))->o)
/* collection items: objects (AddRef/Release) or HSTRINGs (duplicate/delete), by the collection's element kind */
static int item_str(const RObj *o) { return o->coll && o->coll->elem == RT_EL_STR; }
static IUnknown *item_dup(const RObj *o, IUnknown *v)
{
    if (!v) return NULL;
    if (item_str(o)) { static HRESULT (WINAPI *dup)(void *, void **); void *d = NULL;
        if (!dup) dup = (HRESULT (WINAPI *)(void *, void **))rt_winrt_fn("WindowsDuplicateString");
        return dup && SUCCEEDED(dup(v, &d)) ? (IUnknown *)d : NULL; }
    v->lpVtbl->AddRef(v); return v;
}
static void item_free(const RObj *o, IUnknown *v)
{
    if (!v) return;
    if (item_str(o)) { static HRESULT (WINAPI *del)(void *); if (!del) del = (HRESULT (WINAPI *)(void *))rt_winrt_fn("WindowsDeleteString"); if (del) del(v); }
    else v->lpVtbl->Release(v);
}
static int item_eq(const RObj *o, IUnknown *a, IUnknown *b)
{
    if (item_str(o)) { static HRESULT (WINAPI *cmp)(void *, void *, INT32 *); INT32 r = 1;
        if (!cmp) cmp = (HRESULT (WINAPI *)(void *, void *, INT32 *))rt_winrt_fn("WindowsCompareStringOrdinal");
        return cmp && SUCCEEDED(cmp(a, b, &r)) && r == 0; }
    return a == b;
}

static const GUID IID_IUnknown_ = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IInspectable_ = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID IID_IAgileObject_ = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static const GUID IID_IActivationFactory_ = {0x00000035, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID IID_IAsyncInfo_ = {0x00000036, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
#define E_BOUNDS_ ((HRESULT)0x8000000BL)

void rt_log(const char *fmt, ...)
{
    char b[600]; va_list ap; int n;
#ifdef WXR_RETAIL
    (void)fmt; return;   /* retail build: no logging */
#endif
    n = sprintf(b, "[rtstub] ");
    va_start(ap, fmt); _vsnprintf(b + n, sizeof b - n - 2, fmt, ap); va_end(ap); b[sizeof b - 2] = 0; strcat(b, "\n");
    { DWORD le = GetLastError(); typedef void (WINAPI *PLOG)(const char *); static PLOG lg; static int tried;
      if (!tried) { HMODULE m = GetModuleHandleA("wxrsys.dll"); lg = m ? (PLOG)GetProcAddress(m, "WxrLog") : NULL; tried = 1; }
      if (lg) { b[strlen(b) - 1] = 0; lg(b); } else OutputDebugStringA(b); SetLastError(le); }   /* into wxrsys.log when present */
}
void rt_called(const char *what, void *caller)
{
    static const char *seen[4096]; static int nseen; static SRWLOCK lk = SRWLOCK_INIT; int i, found = 0;
    AcquireSRWLockExclusive(&lk);
    for (i = 0; i < nseen; i++) if (seen[i] == what) { found = 1; break; }
    if (!found && nseen < 4096) seen[nseen++] = what;
    ReleaseSRWLockExclusive(&lk);
    if (!found) { HMODULE m = NULL; char mn[MAX_PATH] = "?", *b; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)caller, &m);
        if (m) GetModuleFileNameA(m, mn, MAX_PATH); b = strrchr(mn, '\\'); rt_log("%s (stand-in) from %s+0x%llX", what, b ? b + 1 : mn, (unsigned long long)((char *)caller - (char *)m)); }
}

static RObj *obj_new(int cls)
{
    RObj *o = (RObj *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *o);
    if (o) { o->ref = 1; o->cls = cls; }
    return o;
}
static void obj_add(RObj *o, const GUID *iid, const void *vt) { if (o->n < 24) { o->iid[o->n] = iid; o->v[o->n].vt = vt; o->v[o->n].o = o; o->n++; } }
static void *obj_view(RObj *o, const GUID *iid)
{
    int i; if (!iid) return &o->v[0];
    for (i = 0; i < o->n; i++) if (IsEqualGUID(o->iid[i], iid)) return &o->v[i];
    return &o->v[0];
}

HRESULT STDMETHODCALLTYPE rt_QI(void *This, REFIID iid, void **out)
{
    RObj *o = OBJ(This); int i;
    if (!out) return E_POINTER; *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IAgileObject_)) { *out = &o->v[0]; InterlockedIncrement(&o->ref); return S_OK; }
    for (i = 0; i < o->n; i++) if (IsEqualGUID(o->iid[i], iid)) { *out = &o->v[i]; InterlockedIncrement(&o->ref);
        return S_OK; }
    if (o->factory && o->cls >= 0 && o->cls < g_nrtclass && rt_extra_factory_qi(g_rtclass[o->cls].name, iid, out) == S_OK) return S_OK;   /* hand-written (non-winmd) interfaces */
    {   /* refused: logged once per class + IID (IMarshal & co. are expected; WinRT interfaces mean a stand-in lacks something) */
        static struct { int cls; GUID iid; } seen[256]; static int nseen; static SRWLOCK lk = SRWLOCK_INIT; int k, found = 0;
        AcquireSRWLockExclusive(&lk);
        for (k = 0; k < nseen; k++) if (seen[k].cls == o->cls && IsEqualGUID(&seen[k].iid, iid)) { found = 1; break; }
        if (!found && nseen < 256) { seen[nseen].cls = o->cls; seen[nseen].iid = *iid; nseen++; }
        ReleaseSRWLockExclusive(&lk);
        if (!found) rt_log("%ls: no interface {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", o->cls >= 0 && o->cls < g_nrtclass ? g_rtclass[o->cls].name : L"(collection/async)",
            iid->Data1, iid->Data2, iid->Data3, iid->Data4[0], iid->Data4[1], iid->Data4[2], iid->Data4[3], iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7]); }
    return E_NOINTERFACE;
}
ULONG STDMETHODCALLTYPE rt_AddRef(void *This) { return (ULONG)InterlockedIncrement(&OBJ(This)->ref); }
ULONG STDMETHODCALLTYPE rt_Release(void *This)
{
    RObj *o = OBJ(This); LONG r = InterlockedDecrement(&o->ref);
    if (!r) { int i; if (o->handler) o->handler->lpVtbl->Release(o->handler); if (o->result) o->result->lpVtbl->Release(o->result); if (o->held) o->held->lpVtbl->Release(o->held);
        for (i = 0; i < o->nitems; i++) item_free(o, o->items[i]); if (o->items) HeapFree(GetProcessHeap(), 0, o->items); if (o->ints) HeapFree(GetProcessHeap(), 0, o->ints); if (o->user) HeapFree(GetProcessHeap(), 0, o->user); HeapFree(GetProcessHeap(), 0, o); }
    return (ULONG)r;
}
void *rt_user(void *This) { return This ? OBJ(This)->user : NULL; }
void rt_set_user(void *This, void *user) { if (This) OBJ(This)->user = user; }
IUnknown *rt_held(void *This) { return This ? OBJ(This)->held : NULL; }
void rt_hold(void *This, IUnknown *v) { RObj *o; IUnknown *old; if (!This) return; o = OBJ(This); if (v) v->lpVtbl->AddRef(v); old = o->held; o->held = v; if (old) old->lpVtbl->Release(old); }
HRESULT STDMETHODCALLTYPE rt_GetIids(void *This, ULONG *n, GUID **ids) { (void)This; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
typedef HRESULT (WINAPI *PWCS)(const WCHAR *, UINT32, void **);
typedef const WCHAR *(WINAPI *PGRB)(void *, UINT32 *);
static PWCS p_create; static PGRB p_raw;
static int hs_init(void)
{
    if (!p_create) {
        HMODULE m = GetModuleHandleA("combase.dll"); if (!m) m = LoadLibraryA("combase.dll");
        if (!m) m = GetModuleHandleA("api-ms-win-core-winrt-string-l1-1-0.dll");
        if (!m) m = LoadLibraryA("api-ms-win-core-winrt-string-l1-1-0.dll");
        if (m) { p_create = (PWCS)GetProcAddress(m, "WindowsCreateString"); p_raw = (PGRB)GetProcAddress(m, "WindowsGetStringRawBuffer"); }
    }
    return p_create && p_raw;
}
void *rt_hstring(const WCHAR *s) { void *h = NULL; if (s && hs_init()) p_create(s, (UINT32)lstrlenW(s), &h); return h; }
HRESULT STDMETHODCALLTYPE rt_GetRuntimeClassName(void *This, void **name)
{
    RObj *o = OBJ(This); if (!name) return E_POINTER; *name = NULL;
    /* factories (statics objects) report no class name, as WRL activation factories do: .NET would otherwise take one for an instance */
    if (!o->factory && o->cls >= 0 && o->cls < g_nrtclass && hs_init()) return p_create(g_rtclass[o->cls].name, (UINT32)lstrlenW(g_rtclass[o->cls].name), name);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE rt_GetTrustLevel(void *This, int *level) { (void)This; if (level) *level = 0; return S_OK; }

/* ---- instances and activation factories ---- */
void *rt_new_object(int cls, const GUID *iid)
{
    RObj *o; const RtIface *f;
    if (cls < 0 || cls >= g_nrtclass) return NULL;
    if (!(o = obj_new(cls))) return NULL;
    for (f = g_rtclass[cls].ifs; f->iid; f++) obj_add(o, f->iid, f->vt);
    if (!o->n) { HeapFree(GetProcessHeap(), 0, o); return NULL; }
    rt_log("new %ls", g_rtclass[cls].name);
    return obj_view(o, iid);
}
static HRESULT STDMETHODCALLTYPE fac_ActivateInstance(void *This, void **out)
{
    RObj *o = OBJ(This); if (!out) return E_POINTER; *out = NULL;
    if (!g_rtclass[o->cls].activatable) return E_NOTIMPL;
    *out = rt_new_object(o->cls, NULL); return *out ? S_OK : E_OUTOFMEMORY;
}
static const void *const vt_factory[] = { RT_BASE, (void *)fac_ActivateInstance };

__declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(void *hs, void **out)
{
    const WCHAR *n; UINT32 len = 0; int i; RObj *o; const RtIface *f;
    if (!out) return E_POINTER; *out = NULL;
    if (!hs_init()) return E_FAIL;
    n = p_raw(hs, &len);
    for (i = 0; i < g_nrtclass; i++) if ((UINT32)lstrlenW(g_rtclass[i].name) == len && !memcmp(g_rtclass[i].name, n, len * sizeof(WCHAR))) break;
    if (i == g_nrtclass || !g_rtclass[i].listed) return 0x80040154L;   /* REGDB_E_CLASSNOTREG (also for classes only pulled in as return types) */
    if (!(o = obj_new(i))) return E_OUTOFMEMORY;
    o->factory = 1; obj_add(o, &IID_IActivationFactory_, vt_factory);
    for (f = g_rtclass[i].statics; f->iid; f++) obj_add(o, f->iid, f->vt);
    rt_log("factory %ls", g_rtclass[i].name);
    *out = &o->v[0]; return S_OK;
}
__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

/* ---- empty collections ---- */
static HRESULT STDMETHODCALLTYPE c_bounds(void *t) { (void)t; return E_BOUNDS_; }
static HRESULT STDMETHODCALLTYPE c_ok(void *t) { (void)t; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_zero1(void *t, void *a) { (void)t; if (a) *(UINT32 *)a = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_zero2(void *t, void *x, void *a) { (void)t; (void)x; if (a) *(UINT32 *)a = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_getat(void *t, void *i, void *a) { (void)t; (void)i; (void)a; return E_BOUNDS_; }
static HRESULT STDMETHODCALLTYPE c_indexof(void *t, void *v, void *idx, void *found) { (void)t; (void)v; if (idx) *(UINT32 *)idx = 0; if (found) *(BYTE *)found = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_getmany(void *t, void *s, void *cap, void *items, void *actual) { (void)t; (void)s; (void)cap; (void)items; if (actual) *(UINT32 *)actual = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_it_getmany(void *t, void *cap, void *items, void *actual) { (void)t; (void)cap; (void)items; if (actual) *(UINT32 *)actual = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_null(void *t, void *a) { (void)t; if (a) *(void **)a = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_split(void *t, void *a, void *b) { (void)t; if (a) *(void **)a = NULL; if (b) *(void **)b = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_insert(void *t, void *k, void *v, void *rep) { (void)t; (void)k; (void)v; if (rep) *(BYTE *)rep = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_noop1(void *t, void *a) { (void)t; (void)a; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_noop2(void *t, void *a, void *b) { (void)t; (void)a; (void)b; return S_OK; }
static HRESULT STDMETHODCALLTYPE c_first(void *t, void **out);
static const void *const vt_iterable[] = { RT_BASE, (void *)c_first };
/* object collections: an empty one is the same thing with nitems = 0 */
static HRESULT STDMETHODCALLTYPE i_getat(void *t, UINT32 i, IUnknown **a) { RObj *o = OBJ(t); if (!a) return E_POINTER; *a = NULL; if ((int)i >= o->nitems) return E_BOUNDS_; *a = item_dup(o, o->items[i]); return S_OK; }
static HRESULT STDMETHODCALLTYPE i_size(void *t, UINT32 *a) { if (a) *a = (UINT32)OBJ(t)->nitems; return S_OK; }
static HRESULT STDMETHODCALLTYPE i_indexof(void *t, IUnknown *v, UINT32 *idx, BYTE *found) { RObj *o = OBJ(t); int i; if (idx) *idx = 0; if (found) *found = 0;
    for (i = 0; i < o->nitems; i++) if (item_eq(o, o->items[i], v)) { if (idx) *idx = (UINT32)i; if (found) *found = 1; break; } return S_OK; }
static HRESULT STDMETHODCALLTYPE i_getmany(void *t, UINT32 s, UINT32 cap, IUnknown **items, UINT32 *actual) { RObj *o = OBJ(t); UINT32 k = 0;
    for (; s + k < (UINT32)o->nitems && k < cap && items; k++) items[k] = item_dup(o, o->items[s + k]); if (actual) *actual = k; return S_OK; }
static HRESULT STDMETHODCALLTYPE it_current(void *t, IUnknown **a) { RObj *o = OBJ(t); if (!a) return E_POINTER; *a = NULL; if (o->pos >= o->nitems) return E_BOUNDS_; *a = item_dup(o, o->items[o->pos]); return S_OK; }
static HRESULT STDMETHODCALLTYPE it_has(void *t, BYTE *a) { RObj *o = OBJ(t); if (a) *a = o->pos < o->nitems; return S_OK; }
static HRESULT STDMETHODCALLTYPE it_next(void *t, BYTE *a) { RObj *o = OBJ(t); if (o->pos < o->nitems) o->pos++; if (a) *a = o->pos < o->nitems; return S_OK; }
static HRESULT STDMETHODCALLTYPE it_getmany(void *t, UINT32 cap, IUnknown **items, UINT32 *actual) { RObj *o = OBJ(t); UINT32 k = 0;
    for (; o->pos < o->nitems && k < cap && items; k++, o->pos++) items[k] = item_dup(o, o->items[o->pos]); if (actual) *actual = k; return S_OK; }
static const void *const vt_iterator[] = { RT_BASE, (void *)it_current, (void *)it_has, (void *)it_next, (void *)it_getmany };
static const void *const vt_vectorview[] = { RT_BASE, (void *)i_getat, (void *)i_size, (void *)i_indexof, (void *)i_getmany };
/* IVector<object>: appended items are kept (in the same o->items an object collection uses) so a caller that fills the vector we
   hand it - the Store's AccountsSettingsPane.AccountCommandsRequested handler adding provider commands - can be read back with
   rt_coll_items. Still empty until something appends, so readers that only test for emptiness are unaffected.
   IVector<String> keeps copies of the strings (FileOpenPicker.FileTypeFilter: Media Player's Append(".mp3") was AddRef'd as an
   object and killed the app); vectors of plain values ignore Append as before. */
static HRESULT STDMETHODCALLTYPE i_append(void *t, IUnknown *v)
{
    RObj *o = OBJ(t); IUnknown **n;
    if (o->coll && o->coll->elem == RT_EL_VAL) return S_OK;
    n = (IUnknown **)(o->items ? HeapReAlloc(GetProcessHeap(), 0, o->items, (o->nitems + 1) * sizeof *n)
                                                              : HeapAlloc(GetProcessHeap(), 0, sizeof *n));
    if (!n) return E_OUTOFMEMORY;
    o->items = n; o->items[o->nitems] = item_dup(o, v); o->nitems++; return S_OK;
}
static HRESULT STDMETHODCALLTYPE i_clear(void *t) { RObj *o = OBJ(t); int i; for (i = 0; i < o->nitems; i++) item_free(o, o->items[i]); o->nitems = 0; return S_OK; }
/* IVector: GetAt, Size, GetView, IndexOf, SetAt, InsertAt, RemoveAt, Append, RemoveAtEnd, Clear, GetMany, ReplaceAll */
static const void *const vt_vector[] = { RT_BASE, (void *)i_getat, (void *)i_size, (void *)c_null, (void *)i_indexof, (void *)c_noop2, (void *)c_noop2,
    (void *)c_noop1, (void *)i_append, (void *)c_ok, (void *)i_clear, (void *)i_getmany, (void *)c_noop2 };
int rt_coll_items(void *coll, IUnknown ***items) { RObj *o = coll ? OBJ(coll) : NULL; if (items) *items = o ? o->items : NULL; return o ? o->nitems : 0; }
static const void *const vt_mapview[] = { RT_BASE, (void *)c_getat, (void *)c_zero1, (void *)c_zero2, (void *)c_split };
/* IMap: Lookup, Size, HasKey, GetView, Insert, Remove, Clear */
static const void *const vt_map[] = { RT_BASE, (void *)c_getat, (void *)c_zero1, (void *)c_zero2, (void *)c_null, (void *)c_insert, (void *)c_noop1, (void *)c_ok };
void *rt_new_coll(const RtColl *c)
{
    static const void *const *vts[] = { vt_iterable, vt_iterator, vt_vectorview, vt_vector, vt_mapview, vt_map };
    RObj *o = obj_new(-1); if (!o) return NULL;
    o->coll = c; obj_add(o, c->iid, vts[c->kind]);
    if (c->iterator && c->iterator->kind == RT_ITERABLE) obj_add(o, c->iterator->iid, vt_iterable);   /* vector/map: their IIterable */
    return &o->v[0];
}
static HRESULT STDMETHODCALLTYPE c_first(void *t, void **out)
{
    RObj *o = OBJ(t); const RtColl *it = NULL;
    if (!out) return E_POINTER; *out = NULL;
    if (o->coll->kind == RT_ITERABLE) it = o->coll->iterator; else if (o->coll->iterator) it = o->coll->iterator->iterator;
    if (!it) return E_NOTIMPL;
    *out = rt_new_coll_items(it, o->items, o->nitems); return *out ? S_OK : E_OUTOFMEMORY;
}

/* ---- completed async operations: Completed handlers run at once, GetResults gives a zero value or an inert object ----
   (pending ones, rt_new_async_pending: status Started until rt_async_complete, which then runs the handler on its caller's thread) */
static SRWLOCK g_async_lk = SRWLOCK_INIT;
static HRESULT STDMETHODCALLTYPE a_put_completed(void *t, IUnknown *h)
{
    RObj *o = OBJ(t); IUnknown *old; LONG pending;
    if (!h) return S_OK;
    h->lpVtbl->AddRef(h);
    AcquireSRWLockExclusive(&g_async_lk); old = o->handler; o->handler = h; pending = o->pending; ReleaseSRWLockExclusive(&g_async_lk);
    if (old) old->lpVtbl->Release(old);
    if (pending) return S_OK;   /* rt_async_complete invokes it */
    /* AsyncOperationCompletedHandler::Invoke(asyncInfo, AsyncStatus_Completed = 1 / Error = 3), slot 3 of the delegate */
    return ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, int))(*(void ***)h)[3])(h, &o->v[0], FAILED(o->asyncerr) ? 3 : 1);
}
static HRESULT STDMETHODCALLTYPE a_get_completed(void *t, IUnknown **h) { RObj *o = OBJ(t); if (!h) return E_POINTER; *h = o->handler; if (*h) (*h)->lpVtbl->AddRef(*h); return S_OK; }
/* An operation whose result type is a collection gives an EMPTY collection, never NULL: the real APIs do not return null
   lists, and .NET callers dispatch on the result at once (Store: StoreConfiguration.FilterUnsupportedSystemFeaturesAsync before
   every product page - a null list killed the navigation). Found by the generated type name:
   "Windows.Foundation.IAsyncOperation`1<X>" -> collection descriptor X. */
static const RtColl *a_result_coll(const RtAsync *a)
{
    const RtNamed *n, *c; const char *lt; char inner[400]; size_t len;
    for (n = g_rtasync_names; n->name; n++) if (n->desc == (const void *)a) break;
    if (!n->name || !(lt = strchr(n->name, '<'))) return NULL;
    len = strlen(lt + 1); if (len < 2 || len >= sizeof inner) return NULL;
    memcpy(inner, lt + 1, len - 1); inner[len - 1] = 0;          /* without the closing '>' */
    for (c = g_rtcoll_names; c->name; c++) if (!strcmp(c->name, inner)) return (const RtColl *)c->desc;
    return NULL;
}
static HRESULT STDMETHODCALLTYPE a_results(void *t, void *out)
{
    RObj *o = OBJ(t); const RtAsync *a = o->async;
    if (a->kind == RT_A_ACT || a->kind == RT_A_ACTP || !out) return S_OK;
    if (o->result) { o->result->lpVtbl->AddRef(o->result); *(void **)out = o->result; return S_OK; }
    if (o->nullres) { memset(out, 0, a->result_size); return S_OK; }   /* completed with "nothing" (rt_async_complete(op, NULL, NULL, 0)) */
    if (o->ints) { memset(out, 0, a->result_size); memcpy(out, o->ints, (size_t)a->result_size < o->nints * sizeof(int) ? (size_t)a->result_size : o->nints * sizeof(int)); return S_OK; }   /* rt_new_async_value */
    if (a->result_class >= 0) { *(void **)out = rt_new_object(a->result_class, a->result_iid); return S_OK; }
    if (a->result_size == sizeof(void *)) { const RtColl *c = a_result_coll(a); if (c) { *(void **)out = rt_new_coll(c); return S_OK; } }
    memset(out, 0, a->result_size); return S_OK;
}
static HRESULT STDMETHODCALLTYPE a_id(void *t, UINT32 *id) { (void)t; if (id) *id = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE a_status(void *t, int *s) { RObj *o = OBJ(t); if (s) *s = o->pending ? 0 : FAILED(o->asyncerr) ? 3 : 1; return S_OK; }   /* AsyncStatus: Started 0, Completed 1, Error 3 */
static HRESULT STDMETHODCALLTYPE a_error(void *t, HRESULT *e) { if (e) *e = OBJ(t)->pending ? S_OK : OBJ(t)->asyncerr; return S_OK; }
static const void *const vt_async_noprog[] = { RT_BASE, (void *)a_put_completed, (void *)a_get_completed, (void *)a_results };
static const void *const vt_async_prog[] = { RT_BASE, (void *)c_noop1 /*put_Progress*/, (void *)c_null /*get_Progress*/, (void *)a_put_completed, (void *)a_get_completed, (void *)a_results };
static const void *const vt_asyncinfo[] = { RT_BASE, (void *)a_id, (void *)a_status, (void *)a_error, (void *)c_ok /*Cancel*/, (void *)c_ok /*Close*/ };
void *rt_new_async(const RtAsync *a)
{
    RObj *o = obj_new(-1); if (!o) return NULL;
    o->async = a; obj_add(o, a->iid, (a->kind == RT_A_ACTP || a->kind == RT_A_OPP) ? vt_async_prog : vt_async_noprog);
    obj_add(o, &IID_IAsyncInfo_, vt_asyncinfo);
    return &o->v[0];
}
void *rt_new_coll_items(const RtColl *c, IUnknown **items, int n)
{
    void *v = rt_new_coll(c); RObj *o; int i;
    if (!v || n <= 0) return v;
    o = OBJ(v); o->items = (IUnknown **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof *o->items);
    if (!o->items) { rt_Release(v); return NULL; }
    for (i = 0; i < n; i++) o->items[i] = item_dup(o, items[i]); o->nitems = n;
    return v;
}
/* ---- value collections: items are Int32 / enum values (IVectorView<T>, IIterable<T>, IIterator<T> with a 4-byte T) ---- */
static HRESULT STDMETHODCALLTYPE v_getat(void *t, UINT32 i, int *a) { RObj *o = OBJ(t); if (!a) return E_POINTER; if ((int)i >= o->nints) return E_BOUNDS_; *a = o->ints[i]; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_size(void *t, UINT32 *a) { if (a) *a = (UINT32)OBJ(t)->nints; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_indexof(void *t, int v, UINT32 *idx, BYTE *found) { RObj *o = OBJ(t); int i; if (idx) *idx = 0; if (found) *found = 0;
    for (i = 0; i < o->nints; i++) if (o->ints[i] == v) { if (idx) *idx = (UINT32)i; if (found) *found = 1; break; } return S_OK; }
static HRESULT STDMETHODCALLTYPE v_getmany(void *t, UINT32 s, UINT32 cap, int *items, UINT32 *actual) { RObj *o = OBJ(t); UINT32 k = 0;
    for (; s + k < (UINT32)o->nints && k < cap && items; k++) items[k] = o->ints[s + k]; if (actual) *actual = k; return S_OK; }
static HRESULT STDMETHODCALLTYPE vi_current(void *t, int *a) { RObj *o = OBJ(t); if (!a) return E_POINTER; if (o->pos >= o->nints) return E_BOUNDS_; *a = o->ints[o->pos]; return S_OK; }
static HRESULT STDMETHODCALLTYPE vi_has(void *t, BYTE *a) { RObj *o = OBJ(t); if (a) *a = o->pos < o->nints; return S_OK; }
static HRESULT STDMETHODCALLTYPE vi_next(void *t, BYTE *a) { RObj *o = OBJ(t); if (o->pos < o->nints) o->pos++; if (a) *a = o->pos < o->nints; return S_OK; }
static HRESULT STDMETHODCALLTYPE vi_getmany(void *t, UINT32 cap, int *items, UINT32 *actual) { RObj *o = OBJ(t); UINT32 k = 0;
    for (; o->pos < o->nints && k < cap && items; k++, o->pos++) items[k] = o->ints[o->pos]; if (actual) *actual = k; return S_OK; }
static HRESULT STDMETHODCALLTYPE v_first(void *t, void **out);
static const void *const vt_viterable[] = { RT_BASE, (void *)v_first };
static const void *const vt_viterator[] = { RT_BASE, (void *)vi_current, (void *)vi_has, (void *)vi_next, (void *)vi_getmany };
static const void *const vt_vvectorview[] = { RT_BASE, (void *)v_getat, (void *)v_size, (void *)v_indexof, (void *)v_getmany };
void *rt_new_coll_ints(const RtColl *c, const int *vals, int n)
{
    RObj *o; const RtColl *itb;
    if (!c || (c->kind != RT_VECTORVIEW && c->kind != RT_ITERABLE && c->kind != RT_ITERATOR) || !(o = obj_new(-1))) return NULL;
    o->coll = c; itb = c->kind == RT_VECTORVIEW ? c->iterator : NULL;
    obj_add(o, c->iid, c->kind == RT_VECTORVIEW ? vt_vvectorview : c->kind == RT_ITERABLE ? vt_viterable : vt_viterator);
    if (itb && itb->kind == RT_ITERABLE) obj_add(o, itb->iid, vt_viterable);
    if (n > 0) { o->ints = (int *)HeapAlloc(GetProcessHeap(), 0, n * sizeof(int)); if (!o->ints) { rt_Release(&o->v[0]); return NULL; } memcpy(o->ints, vals, n * sizeof(int)); o->nints = n; }
    return &o->v[0];
}
static HRESULT STDMETHODCALLTYPE v_first(void *t, void **out)
{
    RObj *o = OBJ(t); const RtColl *it = NULL;
    if (!out) return E_POINTER; *out = NULL;
    if (o->coll->kind == RT_ITERABLE) it = o->coll->iterator; else if (o->coll->iterator) it = o->coll->iterator->iterator;
    if (!it) return E_NOTIMPL;
    *out = rt_new_coll_ints(it, o->ints, o->nints); return *out ? S_OK : E_OUTOFMEMORY;
}
void *rt_new_async_result(const RtAsync *a, IUnknown *result)
{
    void *v = rt_new_async(a); if (v && result) { result->lpVtbl->AddRef(result); OBJ(v)->result = result; } return v;
}
void *rt_new_async_pending(const RtAsync *a) { void *v = rt_new_async(a); if (v) OBJ(v)->pending = 1; return v; }
/* completes a pending operation: result = an object (AddRef'd) or NULL; value/size = a value-type result instead */
void rt_async_complete(void *op, IUnknown *result, const void *value, int size)
{
    RObj *o; IUnknown *h;
    if (!op) return; o = OBJ(op);
    if (value && size > 0) { int n = (size + 3) / 4; int *b = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof(int)); if (b) { memcpy(b, value, size); o->ints = b; o->nints = n; } }
    if (result) result->lpVtbl->AddRef(result);
    if (!result && !(value && size > 0)) o->nullres = 1;
    AcquireSRWLockExclusive(&g_async_lk); o->result = result; o->pending = 0; h = o->handler; if (h) h->lpVtbl->AddRef(h); ReleaseSRWLockExclusive(&g_async_lk);
    if (h) { ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, int))(*(void ***)h)[3])(h, &o->v[0], FAILED(o->asyncerr) ? 3 : 1); h->lpVtbl->Release(h); }
}
/* completes a pending operation as FAILED (AsyncStatus Error, ErrorCode hr). GetResults still gives the result: a failed deployment's
   DeploymentResult (ErrorText, ExtendedErrorCode) is read from the failed operation, as on Windows. */
void rt_async_fail(void *op, HRESULT hr, IUnknown *result)
{
    if (!op) return;
    OBJ(op)->asyncerr = FAILED(hr) ? hr : E_FAIL;
    rt_async_complete(op, result, NULL, 0);
}
void *rt_new_async_value(const RtAsync *a, const void *value, int size)
{
    void *v = rt_new_async(a); RObj *o; int n = (size + 3) / 4;
    if (!v || size <= 0) return v;
    o = OBJ(v); o->ints = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof(int)); if (!o->ints) { rt_Release(v); return NULL; }
    memcpy(o->ints, value, size); o->nints = n; return v;
}
static const void *named(const RtNamed *t, const char *name) { for (; t->name; t++) if (!strcmp(t->name, name)) return t->desc; rt_log("no descriptor for %s", name); return NULL; }
const RtColl *rt_coll_named(const char *name) { return (const RtColl *)named(g_rtcoll_names, name); }
const RtAsync *rt_async_named(const char *name) { return (const RtAsync *)named(g_rtasync_names, name); }FARPROC rt_winrt_fn(const char *name)
{
    static const char *const mods[] = { "wxrsys.dll", "combase.dll", "api-ms-win-core-winrt-string-l1-1-0.dll", "api-ms-win-core-winrt-l1-1-0.dll" }; int i;
    for (i = 0; i < 4; i++) { HMODULE m = GetModuleHandleA(mods[i]); FARPROC f = m ? GetProcAddress(m, name) : NULL; if (f) return f; }
    return NULL;
}
