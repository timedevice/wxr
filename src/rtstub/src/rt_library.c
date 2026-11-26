/* rt_library.c - Windows.Storage.StorageLibrary stand-in backed by Win7's shell libraries (%APPDATA%\Microsoft\Windows\Libraries).
 * Win10's StorageLibrary.GetLibraryAsync completes with E_NOTIMPL on Win7 (its library code and the picker broker are Win10-only), and
 * Media Player's "add folder to library" crashed on it. Here:
 *   GetLibraryAsync(id)     -> a library object for KnownLibraryId id (Music 0, Pictures 1, Videos 2, Documents 3)
 *   Folders                 -> IObservableVector<StorageFolder> of the Win7 library's existing file-system folders (one per library)
 *   SaveFolder              -> the Win7 library's default save folder
 *   RequestAddFolderAsync   -> Explorer folder dialog, folder added to the Win7 library, Folders + DefinitionChanged updated
 *   RequestRemoveFolderAsync-> folder removed from the Win7 library, same updates
 *   ChangeTracker, AreFolderSuggestionsAvailableAsync: generated inert stubs. */
#define COBJMACROS
#include "rt_core.h"
#include <stdio.h>
#include <shobjidl.h>
#include <knownfolders.h>

int run_dialog(int folders, int multi, WCHAR **paths, int max);               /* rt_pickers.c */
HRESULT from_path(const WCHAR *cls, const GUID *iid, const WCHAR *path, IUnknown **op);
IUnknown *await_obj(IUnknown *op);

static const GUID IID_IStorageFolderStatics_l = { 0x08F327FF, 0x85D5, 0x48B9, { 0xAE, 0xE9, 0x28, 0x51, 0x1E, 0x33, 0x9F, 0x9F } };
static const GUID IID_IStorageLibrary_l = { 0x1EDD7103, 0x0E5E, 0x4D6C, { 0xB5, 0xE8, 0x93, 0x18, 0x98, 0x3D, 0x6A, 0x03 } };
static const GUID IID_IStorageItem_l = { 0x4207A996, 0xCA2F, 0x42F7, { 0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30 } };
/* parameterized IIDs (pinterface SHA-1 algorithm; IIterable<StorageFolder> checked against the generator's) */
static const GUID IID_IObservableVector_SF = { 0x358F44DF, 0x2A45, 0x5CB8, { 0x93, 0x85, 0x1F, 0xF6, 0x68, 0x08, 0xCD, 0xE0 } };
static const GUID IID_IVector_SF = { 0x6C26B7BE, 0x5F01, 0x5A60, { 0x9D, 0xD7, 0xFD, 0x17, 0xBE, 0x3A, 0x9D, 0xD6 } };
static const GUID IID_IIterable_SF = { 0x4669BEFC, 0xAE5C, 0x52B1, { 0x8A, 0x97, 0x54, 0x66, 0xCE, 0x61, 0xE9, 0x4E } };
static const GUID IID_IVectorChangedEventArgs_l = { 0x575933DF, 0x34FE, 0x4480, { 0xAF, 0x15, 0x07, 0x69, 0x1F, 0x3D, 0x5D, 0x9B } };
static const GUID IID_IUnknown_l = { 0, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_l = { 0xAF86E2E0, 0xB12D, 0x4C6A, { 0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90 } };
static const GUID IID_IAgileObject_l = { 0x94EA2B94, 0xE9CC, 0x49E0, { 0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90 } };
static const GUID IID_IAsyncInfo_l = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID *const g_lib_folderid[4] = { &FOLDERID_MusicLibrary, &FOLDERID_PicturesLibrary, &FOLDERID_VideosLibrary, &FOLDERID_DocumentsLibrary };
#define VT(o, k, T) ((T)(*(void ***)(o))[k])
static int is_base_iid(REFIID iid) { return IsEqualGUID(iid, &IID_IUnknown_l) || IsEqualGUID(iid, &IID_IInspectable_l) || IsEqualGUID(iid, &IID_IAgileObject_l); }
static HRESULT STDMETHODCALLTYPE l_iids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE l_name(void *t, void **s) { (void)t; if (s) *s = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE l_trust(void *t, int *l) { (void)t; if (l) *l = 0; return S_OK; }

/* ---- completed operation: IAsyncOperation<T> (any IID that is not classic COM) + IAsyncInfo; result = object (may be NULL) or boolean ---- */
typedef struct { const void *vt_op, *vt_info; LONG ref; IUnknown *obj; int is_bool, b; const GUID *iid; } LOp;
#define LOP_INFO(i) ((LOp *)((BYTE *)(i) - offsetof(LOp, vt_info)))
static int classic(REFIID iid) { static const BYTE t[8] = { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 }; return !memcmp(iid->Data4, t, 8) && !iid->Data2 && !iid->Data3; }
static HRESULT STDMETHODCALLTYPE lop_qi(void *t, REFIID iid, void **o) { LOp *p = (LOp *)t; if (!o) return E_POINTER; *o = NULL;
  { char g[48]; sprintf(g, "{%08lX-%04X-%04X-%02X%02X-...}", iid->Data1, iid->Data2, iid->Data3, iid->Data4[0], iid->Data4[1]); rt_log("op %p QI %s", t, g); }
  if (IsEqualGUID(iid, &IID_IAsyncInfo_l)) *o = &p->vt_info; else if (is_base_iid(iid) || (p->iid && IsEqualGUID(iid, p->iid))) *o = p; else return E_NOINTERFACE;   /* only its own IAsyncOperation<T>: .NET probes others (IReferenceTrackerTarget...) and calls them */
  InterlockedIncrement(&p->ref); return S_OK; }
static ULONG STDMETHODCALLTYPE lop_ar(void *t) { return InterlockedIncrement(&((LOp *)t)->ref); }
static ULONG STDMETHODCALLTYPE lop_rl(void *t) { LOp *p = (LOp *)t; LONG r = InterlockedDecrement(&p->ref); if (!r) { if (p->obj) p->obj->lpVtbl->Release(p->obj); HeapFree(GetProcessHeap(), 0, p); } return r; }
static HRESULT STDMETHODCALLTYPE lop_put_completed(void *t, void *h) { rt_log("op %p put_Completed(%p) -> invoking", t, h); if (h) VT(h, 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *, int))(h, t, 1); return S_OK; }
static HRESULT STDMETHODCALLTYPE lop_null(void *t, void **h) { (void)t; if (h) *h = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE lop_results(void *t, void *out) { LOp *p = (LOp *)t; rt_log("op %p GetResults", t); if (!out) return E_POINTER;
  if (p->is_bool == 2) *(UINT32 *)out = (UINT32)p->b; else if (p->is_bool) *(boolean *)out = (boolean)(p->b != 0); else { *(IUnknown **)out = p->obj; if (p->obj) p->obj->lpVtbl->AddRef(p->obj); } return S_OK; }
static const void *const vt_lop[] = { lop_qi, lop_ar, lop_rl, l_iids, l_name, l_trust, lop_put_completed, lop_null, lop_results };
static HRESULT STDMETHODCALLTYPE lopi_qi(void *t, REFIID iid, void **o) { return lop_qi(LOP_INFO(t), iid, o); }
static ULONG STDMETHODCALLTYPE lopi_ar(void *t) { return lop_ar(LOP_INFO(t)); }
static ULONG STDMETHODCALLTYPE lopi_rl(void *t) { return lop_rl(LOP_INFO(t)); }
static HRESULT STDMETHODCALLTYPE lopi_id(void *t, UINT32 *v) { (void)t; if (v) *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE lopi_status(void *t, int *v) { rt_log("op %p get_Status", (void *)LOP_INFO(t)); if (v) *v = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE lopi_error(void *t, HRESULT *v) { (void)t; if (v) *v = S_OK; return S_OK; }
static HRESULT STDMETHODCALLTYPE lopi_ok(void *t) { (void)t; return S_OK; }
static const void *const vt_lopi[] = { lopi_qi, lopi_ar, lopi_rl, l_iids, l_name, l_trust, lopi_id, lopi_status, lopi_error, lopi_ok, lopi_ok };
void *lop_new(const char *type, IUnknown *obj, int is_bool, int b) {   /* also used by rt_launcher.c */   /* AddRefs obj; type = the operation's type name (rtstub descriptor) */
  const RtAsync *d = rt_async_named(type);
  LOp *p = (LOp *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p); if (!p) return NULL;
  p->vt_op = vt_lop; p->vt_info = vt_lopi; p->ref = 1; p->is_bool = is_bool; p->b = b; p->obj = obj; p->iid = d ? d->iid : NULL; if (obj) obj->lpVtbl->AddRef(obj); return p; }

/* ---- IVectorChangedEventArgs ---- */
typedef struct { const void *vt; LONG ref; int change; UINT32 index; } LArgs;
static HRESULT STDMETHODCALLTYPE la_qi(void *t, REFIID iid, void **o) { if (!o) return E_POINTER; *o = NULL; if (is_base_iid(iid) || IsEqualGUID(iid, &IID_IVectorChangedEventArgs_l)) { *o = t; InterlockedIncrement(&((LArgs *)t)->ref); return S_OK; } return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE la_ar(void *t) { return InterlockedIncrement(&((LArgs *)t)->ref); }
static ULONG STDMETHODCALLTYPE la_rl(void *t) { LONG r = InterlockedDecrement(&((LArgs *)t)->ref); if (!r) HeapFree(GetProcessHeap(), 0, t); return r; }
static HRESULT STDMETHODCALLTYPE la_change(void *t, int *v) { if (!v) return E_POINTER; *v = ((LArgs *)t)->change; return S_OK; }
static HRESULT STDMETHODCALLTYPE la_index(void *t, UINT32 *v) { if (!v) return E_POINTER; *v = ((LArgs *)t)->index; return S_OK; }
static const void *const vt_largs[] = { la_qi, la_ar, la_rl, l_iids, l_name, l_trust, la_change, la_index };

/* ---- the folder vector: IObservableVector<StorageFolder> / IVector<StorageFolder> / IIterable<StorageFolder> ---- */
#define MAXH 16
typedef struct { const void *vt_obs, *vt_vec, *vt_it; LONG ref; IUnknown **items; UINT32 n, cap; IUnknown *h[MAXH]; CRITICAL_SECTION cs; } FVec;
#define FV(i, f) ((FVec *)((BYTE *)(i) - offsetof(FVec, f)))
static HRESULT fv_qi_(FVec *v, REFIID iid, void **o) { if (!o) return E_POINTER; *o = NULL;
  if (is_base_iid(iid) || IsEqualGUID(iid, &IID_IObservableVector_SF)) *o = &v->vt_obs; else if (IsEqualGUID(iid, &IID_IVector_SF)) *o = &v->vt_vec; else if (IsEqualGUID(iid, &IID_IIterable_SF)) *o = &v->vt_it;
  else return E_NOINTERFACE; InterlockedIncrement(&v->ref); return S_OK; }
static ULONG fv_rl_(FVec *v) { LONG r = InterlockedDecrement(&v->ref); return r; }   /* one vector per library, kept for the process lifetime */
#define FV_IUNK(f, pre) static HRESULT STDMETHODCALLTYPE pre##_qi(void *t, REFIID i, void **o) { return fv_qi_(FV(t, f), i, o); } \
  static ULONG STDMETHODCALLTYPE pre##_ar(void *t) { return InterlockedIncrement(&FV(t, f)->ref); } static ULONG STDMETHODCALLTYPE pre##_rl(void *t) { return fv_rl_(FV(t, f)); }
FV_IUNK(vt_obs, fo) FV_IUNK(vt_vec, fvv) FV_IUNK(vt_it, fi)
static void fv_fire(FVec *v, int change, UINT32 index) {   /* VectorChangedEventHandler<T>::Invoke(sender IObservableVector, args) */
  IUnknown *h[MAXH]; int k, n = 0; LArgs *a = (LArgs *)HeapAlloc(GetProcessHeap(), 0, sizeof *a); if (!a) return;
  a->vt = vt_largs; a->ref = 1; a->change = change; a->index = index;
  EnterCriticalSection(&v->cs); for (k = 0; k < MAXH; k++) if (v->h[k]) { h[n] = v->h[k]; h[n++]->lpVtbl->AddRef(v->h[k]); } LeaveCriticalSection(&v->cs);
  for (k = 0; k < n; k++) { VT(h[k], 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))(h[k], &v->vt_obs, a); h[k]->lpVtbl->Release(h[k]); }
  la_rl(a); }
static HRESULT STDMETHODCALLTYPE fo_add(void *t, IUnknown *h, INT64 *tok) { FVec *v = FV(t, vt_obs); int k; if (!h || !tok) return E_POINTER;
  EnterCriticalSection(&v->cs); for (k = 0; k < MAXH && v->h[k]; k++); if (k < MAXH) { h->lpVtbl->AddRef(h); v->h[k] = h; } LeaveCriticalSection(&v->cs);
  *tok = k + 1; return k < MAXH ? S_OK : E_OUTOFMEMORY; }
static HRESULT STDMETHODCALLTYPE fo_remove(void *t, INT64 tok) { FVec *v = FV(t, vt_obs); IUnknown *h = NULL;
  EnterCriticalSection(&v->cs); if (tok >= 1 && tok <= MAXH) { h = v->h[tok - 1]; v->h[tok - 1] = NULL; } LeaveCriticalSection(&v->cs); if (h) h->lpVtbl->Release(h); return S_OK; }
static const void *const vt_fo[] = { fo_qi, fo_ar, fo_rl, l_iids, l_name, l_trust, fo_add, fo_remove };
static HRESULT fv_insert(FVec *v, UINT32 at, IUnknown *x) {
  if (at > v->n) return E_BOUNDS;
  if (v->n == v->cap) { UINT32 c = v->cap ? v->cap * 2 : 8; IUnknown **ni = (IUnknown **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, c * sizeof *ni); if (!ni) return E_OUTOFMEMORY;
    if (v->items) { memcpy(ni, v->items, v->n * sizeof *ni); HeapFree(GetProcessHeap(), 0, v->items); } v->items = ni; v->cap = c; }
  memmove(v->items + at + 1, v->items + at, (v->n - at) * sizeof *v->items); v->items[at] = x; if (x) x->lpVtbl->AddRef(x); v->n++; return S_OK; }
static HRESULT STDMETHODCALLTYPE fvv_getat(void *t, UINT32 i, IUnknown **o) { FVec *v = FV(t, vt_vec); if (!o) return E_POINTER; *o = NULL;
  EnterCriticalSection(&v->cs); if (i < v->n) { *o = v->items[i]; if (*o) (*o)->lpVtbl->AddRef(*o); } LeaveCriticalSection(&v->cs); return i < v->n ? S_OK : E_BOUNDS; }
static HRESULT STDMETHODCALLTYPE fvv_size(void *t, UINT32 *n) { if (!n) return E_POINTER; *n = FV(t, vt_vec)->n; return S_OK; }
static void *fv_view(FVec *v, const char *name) { void *r; EnterCriticalSection(&v->cs); r = rt_new_coll_items(rt_coll_named(name), v->items, (int)v->n); LeaveCriticalSection(&v->cs); return r; }
static HRESULT STDMETHODCALLTYPE fvv_view(void *t, void **o) { if (!o) return E_POINTER; *o = fv_view(FV(t, vt_vec), "Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFolder>"); return *o ? S_OK : E_OUTOFMEMORY; }
static HRESULT STDMETHODCALLTYPE fvv_indexof(void *t, IUnknown *x, UINT32 *idx, boolean *found) { FVec *v = FV(t, vt_vec); UINT32 i; if (!idx || !found) return E_POINTER; *found = 0; *idx = 0;
  for (i = 0; i < v->n; i++) if (v->items[i] == x) { *idx = i; *found = 1; break; } return S_OK; }
static HRESULT STDMETHODCALLTYPE fvv_setat(void *t, UINT32 i, IUnknown *x) { FVec *v = FV(t, vt_vec); IUnknown *old; if (i >= v->n) return E_BOUNDS;
  old = v->items[i]; v->items[i] = x; if (x) x->lpVtbl->AddRef(x); if (old) old->lpVtbl->Release(old); fv_fire(v, 3, i); return S_OK; }
static HRESULT STDMETHODCALLTYPE fvv_insertat(void *t, UINT32 i, IUnknown *x) { FVec *v = FV(t, vt_vec); HRESULT hr; EnterCriticalSection(&v->cs); hr = fv_insert(v, i, x); LeaveCriticalSection(&v->cs); if (SUCCEEDED(hr)) fv_fire(v, 1, i); return hr; }
static HRESULT STDMETHODCALLTYPE fvv_removeat(void *t, UINT32 i) { FVec *v = FV(t, vt_vec); IUnknown *old; if (i >= v->n) return E_BOUNDS;
  EnterCriticalSection(&v->cs); old = v->items[i]; memmove(v->items + i, v->items + i + 1, (v->n - i - 1) * sizeof *v->items); v->n--; LeaveCriticalSection(&v->cs);
  if (old) old->lpVtbl->Release(old); fv_fire(v, 2, i); return S_OK; }
static HRESULT STDMETHODCALLTYPE fvv_append(void *t, IUnknown *x) { return fvv_insertat(t, FV(t, vt_vec)->n, x); }
static HRESULT STDMETHODCALLTYPE fvv_removeend(void *t) { FVec *v = FV(t, vt_vec); return v->n ? fvv_removeat(t, v->n - 1) : E_BOUNDS; }
static void fv_clear_(FVec *v) { UINT32 i; EnterCriticalSection(&v->cs); for (i = 0; i < v->n; i++) if (v->items[i]) v->items[i]->lpVtbl->Release(v->items[i]); v->n = 0; LeaveCriticalSection(&v->cs); }
static HRESULT STDMETHODCALLTYPE fvv_clear(void *t) { FVec *v = FV(t, vt_vec); fv_clear_(v); fv_fire(v, 0, 0); return S_OK; }
static HRESULT STDMETHODCALLTYPE fvv_getmany(void *t, UINT32 start, UINT32 cap, IUnknown **o, UINT32 *got) { FVec *v = FV(t, vt_vec); UINT32 i, k = 0; if (!got) return E_POINTER;
  EnterCriticalSection(&v->cs); for (i = start; i < v->n && k < cap; i++, k++) { o[k] = v->items[i]; if (o[k]) o[k]->lpVtbl->AddRef(o[k]); } LeaveCriticalSection(&v->cs); *got = k; return S_OK; }
static HRESULT STDMETHODCALLTYPE fvv_replaceall(void *t, UINT32 n, IUnknown **x) { FVec *v = FV(t, vt_vec); UINT32 i; fv_clear_(v);
  EnterCriticalSection(&v->cs); for (i = 0; i < n; i++) fv_insert(v, v->n, x[i]); LeaveCriticalSection(&v->cs); fv_fire(v, 0, 0); return S_OK; }
static const void *const vt_fvv[] = { fvv_qi, fvv_ar, fvv_rl, l_iids, l_name, l_trust, fvv_getat, fvv_size, fvv_view, fvv_indexof, fvv_setat, fvv_insertat, fvv_removeat,
  fvv_append, fvv_removeend, fvv_clear, fvv_getmany, fvv_replaceall };
static HRESULT STDMETHODCALLTYPE fi_first(void *t, void **o) {   /* IIterable<T>::First: through a snapshot iterable of rtstub's */
  void *it = fv_view(FV(t, vt_it), "Windows.Foundation.Collections.IIterable`1<Windows.Storage.StorageFolder>"); HRESULT hr;
  if (!o) return E_POINTER; *o = NULL; if (!it) return E_OUTOFMEMORY;
  hr = VT(it, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(it, o); ((IUnknown *)it)->lpVtbl->Release((IUnknown *)it); return hr; }
static const void *const vt_fi[] = { fi_qi, fi_ar, fi_rl, l_iids, l_name, l_trust, fi_first };

/* ---- per library state ---- */
typedef struct { FVec *vec; IUnknown *defh[MAXH]; void *objs[16]; int nobj; int loaded; } LibState;
static LibState g_lib[4]; static CRITICAL_SECTION g_lib_cs; static LONG g_lib_init;
static void lib_init(void) { if (!InterlockedCompareExchange(&g_lib_init, 1, 0)) { InitializeCriticalSection(&g_lib_cs); g_lib_init = 2; } while (g_lib_init != 2) Sleep(0); }
static int lib_of(void *iface) { int i, k, id = -1; lib_init(); EnterCriticalSection(&g_lib_cs);
  for (i = 0; i < 4 && id < 0; i++) for (k = 0; k < g_lib[i].nobj; k++) if (g_lib[i].objs[k] == iface) { id = i; break; } LeaveCriticalSection(&g_lib_cs);
  if (id < 0) rt_log("library: unknown library object %p, using Music", iface); return id < 0 ? 0 : id; }
static IShellLibrary *shell_lib(int id, DWORD mode) {
  IShellLibrary *l = NULL; HRESULT hr = CoCreateInstance(&CLSID_ShellLibrary, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLibrary, (void **)&l);
  if (SUCCEEDED(hr)) hr = IShellLibrary_LoadLibraryFromKnownFolder(l, g_lib_folderid[id], mode);
  if (FAILED(hr)) { rt_log("library %d: Win7 shell library not loaded (0x%08lX)", id, (unsigned long)hr); if (l) IShellLibrary_Release(l); return NULL; }
  return l; }
static IUnknown *folder_obj(const WCHAR *path) { IUnknown *op = NULL, *f = NULL;
  if (SUCCEEDED(from_path(L"Windows.Storage.StorageFolder", &IID_IStorageFolderStatics_l, path, &op)) && op) { f = await_obj(op); op->lpVtbl->Release(op); }
  return f; }
/* (re)fills the vector from the Win7 library: folders that exist on disk, as real StorageFolder objects */
static void lib_load(int id) {
  IShellLibrary *l = shell_lib(id, STGM_READ); IShellItemArray *arr = NULL; DWORD n = 0, i; FVec *v = g_lib[id].vec;
  fv_clear_(v);
  if (l && SUCCEEDED(IShellLibrary_GetFolders(l, LFF_FORCEFILESYSTEM, &IID_IShellItemArray, (void **)&arr)) && arr) {
    IShellItemArray_GetCount(arr, &n);
    for (i = 0; i < n; i++) { IShellItem *it = NULL; WCHAR *p = NULL; IUnknown *f;
      if (FAILED(IShellItemArray_GetItemAt(arr, i, &it))) continue;
      if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p)) && p) {
        DWORD a = GetFileAttributesW(p);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && (f = folder_obj(p)) != NULL) { EnterCriticalSection(&v->cs); fv_insert(v, v->n, f); LeaveCriticalSection(&v->cs); f->lpVtbl->Release(f); }
        else rt_log("library %d: folder %ls skipped (missing)", id, p);
        CoTaskMemFree(p); }
      IShellItem_Release(it); }
    IShellItemArray_Release(arr); }
  if (l) IShellLibrary_Release(l);
  g_lib[id].loaded = 1; rt_log("library %d: %u folder(s)", id, v->n); }
static FVec *lib_vec(int id) {
  lib_init(); EnterCriticalSection(&g_lib_cs);
  if (!g_lib[id].vec) { FVec *v = (FVec *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *v);
    if (v) { v->vt_obs = vt_fo; v->vt_vec = vt_fvv; v->vt_it = vt_fi; v->ref = 1; InitializeCriticalSection(&v->cs); g_lib[id].vec = v; } }
  LeaveCriticalSection(&g_lib_cs);
  if (g_lib[id].vec && !g_lib[id].loaded) lib_load(id);
  return g_lib[id].vec; }
static void lib_fire_definition(int id, void *sender) {   /* TypedEventHandler<StorageLibrary, Object>::Invoke(sender, NULL) */
  IUnknown *h[MAXH]; int k, n = 0; EnterCriticalSection(&g_lib_cs);
  for (k = 0; k < MAXH; k++) if (g_lib[id].defh[k]) { h[n] = g_lib[id].defh[k]; h[n]->lpVtbl->AddRef(h[n]); n++; } LeaveCriticalSection(&g_lib_cs);
  for (k = 0; k < n; k++) { VT(h[k], 3, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))(h[k], sender, NULL); h[k]->lpVtbl->Release(h[k]); } }

HRESULT STDMETHODCALLTYPE S_IStorageLibraryStatics_GetLibraryAsync(void *This, void *a0, void *a1)
{
    int id = (int)(INT_PTR)a0; void *obj, *lib = NULL; (void)This;
    if (!a1) return E_POINTER; *(void **)a1 = NULL;
    if (id < 0 || id > 3) id = 0;
    obj = rt_new_object(rt_class_index(L"Windows.Storage.StorageLibrary"), NULL); if (!obj) return E_OUTOFMEMORY;
    ((IUnknown *)obj)->lpVtbl->QueryInterface((IUnknown *)obj, &IID_IStorageLibrary_l, &lib); ((IUnknown *)obj)->lpVtbl->Release((IUnknown *)obj);
    if (!lib) return E_NOINTERFACE;
    lib_init(); EnterCriticalSection(&g_lib_cs);
    if (g_lib[id].nobj == 16) { memmove(g_lib[id].objs, g_lib[id].objs + 1, 15 * sizeof(void *)); g_lib[id].nobj = 15; } g_lib[id].objs[g_lib[id].nobj++] = lib;
    LeaveCriticalSection(&g_lib_cs);
    rt_log("StorageLibrary.GetLibraryAsync(%d) -> %p (Win7 shell library)", id, lib);
    *(void **)a1 = lop_new("Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageLibrary>", (IUnknown *)lib, 0, 0); ((IUnknown *)lib)->lpVtbl->Release((IUnknown *)lib);
    return *(void **)a1 ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IStorageLibraryStatics2_GetLibraryForUserAsync(void *This, void *a0, void *a1, void *a2) { (void)a0; return S_IStorageLibraryStatics_GetLibraryAsync(This, a1, a2); }
HRESULT STDMETHODCALLTYPE S_IStorageLibrary_get_Folders(void *This, void *a0)
{
    FVec *v = lib_vec(lib_of(This)); if (!a0) return E_POINTER; *(void **)a0 = NULL; if (!v) return E_OUTOFMEMORY;
    InterlockedIncrement(&v->ref); *(void **)a0 = &v->vt_obs; return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageLibrary_get_SaveFolder(void *This, void *a0)
{
    int id = lib_of(This); IShellLibrary *l = shell_lib(id, STGM_READ); IShellItem *it = NULL; WCHAR *p = NULL; IUnknown *f = NULL;
    if (!a0) return E_POINTER; *(void **)a0 = NULL;
    if (l && SUCCEEDED(IShellLibrary_GetDefaultSaveFolder(l, DSFT_DETECT, &IID_IShellItem, (void **)&it)) && it) {
        if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p)) && p) { f = folder_obj(p); CoTaskMemFree(p); } IShellItem_Release(it); }
    if (l) IShellLibrary_Release(l);
    if (!f) { FVec *v = lib_vec(id); if (v && v->n && v->items[0]) { f = v->items[0]; f->lpVtbl->AddRef(f); } }
    *(void **)a0 = f; return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageLibrary_add_DefinitionChanged(void *This, void *a0, void *a1)
{
    int id = lib_of(This), k; IUnknown *h = (IUnknown *)a0; if (!h || !a1) return E_POINTER;
    EnterCriticalSection(&g_lib_cs); for (k = 0; k < MAXH && g_lib[id].defh[k]; k++); if (k < MAXH) { h->lpVtbl->AddRef(h); g_lib[id].defh[k] = h; } LeaveCriticalSection(&g_lib_cs);
    *(INT64 *)a1 = (INT64)(id * 100 + k + 1); return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageLibrary_remove_DefinitionChanged(void *This, void *a0)
{
    INT64 tok = *(INT64 *)&a0; int id = (int)(tok / 100), k = (int)(tok % 100) - 1; IUnknown *h = NULL; (void)This;
    if (id < 0 || id > 3 || k < 0 || k >= MAXH) return S_OK;
    lib_init(); EnterCriticalSection(&g_lib_cs); h = g_lib[id].defh[k]; g_lib[id].defh[k] = NULL; LeaveCriticalSection(&g_lib_cs); if (h) h->lpVtbl->Release(h); return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IStorageLibrary_RequestAddFolderAsync(void *This, void *a0)
{
    int id = lib_of(This); WCHAR *p[1]; IUnknown *f = NULL;
    if (!a0) return E_POINTER; *(void **)a0 = NULL;
    rt_log("StorageLibrary(%d).RequestAddFolderAsync", id);
    if (run_dialog(1, 0, p, 1) == 1) {
        IShellLibrary *l = shell_lib(id, STGM_READWRITE); IShellItem *it = NULL; HRESULT hr = E_FAIL;
        if (l && SUCCEEDED(hr = SHCreateItemFromParsingName(p[0], NULL, &IID_IShellItem, (void **)&it))) { hr = IShellLibrary_AddFolder(l, it); if (SUCCEEDED(hr)) hr = IShellLibrary_Commit(l); IShellItem_Release(it); }
        if (l) IShellLibrary_Release(l);
        rt_log("library %d: add %ls -> 0x%08lX", id, p[0], (unsigned long)hr);
        f = folder_obj(p[0]); CoTaskMemFree(p[0]);
        if (f && SUCCEEDED(hr)) { FVec *v = lib_vec(id); if (v) { UINT32 at; EnterCriticalSection(&v->cs); at = v->n; fv_insert(v, at, f); LeaveCriticalSection(&v->cs); fv_fire(v, 1, at); } lib_fire_definition(id, This); }
    }
    *(void **)a0 = lop_new("Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>", f, 0, 0); if (f) f->lpVtbl->Release(f);   /* NULL folder = cancelled, as on Win10 */
    return *(void **)a0 ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IStorageLibrary_RequestRemoveFolderAsync(void *This, void *a0, void *a1)
{
    typedef const WCHAR *(WINAPI *PGRB)(void *, UINT32 *); static PGRB grb;
    int id = lib_of(This), ok = 0; IUnknown *item = NULL; void *hs = NULL;
    if (!a1) return E_POINTER; *(void **)a1 = NULL;
    if (!grb) { HMODULE m = GetModuleHandleA("combase.dll"); if (!m) m = LoadLibraryA("api-ms-win-core-winrt-string-l1-1-0.dll"); grb = m ? (PGRB)GetProcAddress(m, "WindowsGetStringRawBuffer") : NULL; }
    if (a0 && SUCCEEDED(((IUnknown *)a0)->lpVtbl->QueryInterface((IUnknown *)a0, &IID_IStorageItem_l, (void **)&item)) && item) {
        if (SUCCEEDED(VT(item, 12, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(item, &hs)) && hs && grb) {   /* IStorageItem.get_Path */
            UINT32 n = 0; const WCHAR *p = grb(hs, &n); IShellLibrary *l = shell_lib(id, STGM_READWRITE); IShellItem *it = NULL;
            if (l && p && SUCCEEDED(SHCreateItemFromParsingName(p, NULL, &IID_IShellItem, (void **)&it))) { ok = SUCCEEDED(IShellLibrary_RemoveFolder(l, it)) && SUCCEEDED(IShellLibrary_Commit(l)); IShellItem_Release(it); }
            if (l) IShellLibrary_Release(l);
            rt_log("library %d: remove %ls -> %d", id, p ? p : L"?", ok); }
        item->lpVtbl->Release(item); }
    if (ok) { FVec *v = lib_vec(id); g_lib[id].loaded = 0; if (v) { lib_load(id); fv_fire(v, 0, 0); } lib_fire_definition(id, This); }
    *(void **)a1 = lop_new("Windows.Foundation.IAsyncOperation`1<bool>", NULL, 1, ok); return *(void **)a1 ? S_OK : E_OUTOFMEMORY;
}
