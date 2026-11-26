/* comp_light.c - small hand-written objects the generator has no class for (first needed by the Microsoft Store):
   - VisualUnorderedCollection for CompositionLight.Targets / ExclusionsFromTargets. Lights are inert here, so the collection only
     counts; the Store's hero-card SpotLight did light.Targets.Add(visual) on the NULL the stub returned -> NullReferenceException
     (E_POINTER) in a XAML event handler -> home page never left its loading skeleton.
   - an already-completed IAsyncAction for Compositor.RequestCommitAsync (was E_NOTIMPL).
   Plain C COM objects (own vtables), not CObj: nothing else needs to find them. */
#include <windows.h>
#include <inspectable.h>
#include <stddef.h>
#ifndef E_ILLEGAL_DELEGATE_ASSIGNMENT
#define E_ILLEGAL_DELEGATE_ASSIGNMENT ((HRESULT)0x80000018L)
#endif

void comp_log(const char *fmt, ...);

static const GUID IID_IUnknown_c     = {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID IID_IInspectable_c = {0xAF86E2E0,0xB12D,0x4C6A,{0x9C,0x5A,0xD7,0xAA,0x65,0x10,0x1E,0x90}};
static const GUID IID_IAgileObject_c = {0x94EA2B94,0xE9CC,0x49E0,{0xC0,0xFF,0xEE,0x64,0xCA,0x8F,0x5B,0x90}};
static const GUID IID_IVisualUnorderedCollection_c = {0x338FAA70,0x54C8,0x40A7,{0x80,0x29,0xC9,0xCE,0xEB,0x0A,0xA2,0x50}};
static const GUID IID_IAsyncAction_c = {0x5A648006,0x843A,0x4DA9,{0x86,0x5B,0x9D,0x26,0xE5,0xDF,0xAD,0x7B}};
static const GUID IID_IAsyncInfo_c   = {0x00000036,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

typedef HRESULT (WINAPI *PCreateStr_)(const WCHAR *, UINT32, HSTRING *);
static HRESULT make_hstring(const WCHAR *s, HSTRING *out) {
  static PCreateStr_ f; if (!out) return E_POINTER; *out = NULL;
  if (!f) { HMODULE m = LoadLibraryW(L"combase.dll"); f = m ? (PCreateStr_)GetProcAddress(m, "WindowsCreateString") : NULL; } if (!f) return E_NOTIMPL;
  return f(s, (UINT32)lstrlenW(s), out); }

/* ---------------- VisualUnorderedCollection: IInspectable + get_Count, Add, Remove, RemoveAll ---------------- */
typedef struct { const void *vt; LONG ref; INT32 n; } VuColl;
static HRESULT STDMETHODCALLTYPE vu_qi(VuColl *t, REFIID r, void **o) {
  if (!o) return E_POINTER;
  if (IsEqualGUID(r, &IID_IUnknown_c) || IsEqualGUID(r, &IID_IInspectable_c) || IsEqualGUID(r, &IID_IAgileObject_c) || IsEqualGUID(r, &IID_IVisualUnorderedCollection_c)) {
    *o = t; InterlockedIncrement(&t->ref); return S_OK; }
  *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE vu_addref(VuColl *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG STDMETHODCALLTYPE vu_release(VuColl *t) { LONG r = InterlockedDecrement(&t->ref); if (!r) HeapFree(GetProcessHeap(), 0, t); return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE vu_iids(VuColl *t, ULONG *n, IID **iids) {
  (void)t; if (!n || !iids) return E_POINTER; *iids = (IID *)CoTaskMemAlloc(sizeof(IID)); if (!*iids) return E_OUTOFMEMORY; **iids = IID_IVisualUnorderedCollection_c; *n = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE vu_name(VuColl *t, HSTRING *s) { (void)t; return make_hstring(L"Windows.UI.Composition.VisualUnorderedCollection", s); }
static HRESULT STDMETHODCALLTYPE vu_trust(VuColl *t, TrustLevel *l) { (void)t; if (!l) return E_POINTER; *l = BaseTrust; return S_OK; }
static HRESULT STDMETHODCALLTYPE vu_count(VuColl *t, INT32 *r) { if (!r) return E_POINTER; *r = t->n; return S_OK; }
static HRESULT STDMETHODCALLTYPE vu_add(VuColl *t, IUnknown *v) { if (!v) return E_INVALIDARG; t->n++; return S_OK; }
static HRESULT STDMETHODCALLTYPE vu_remove(VuColl *t, IUnknown *v) { if (!v) return E_INVALIDARG; if (t->n > 0) t->n--; return S_OK; }
static HRESULT STDMETHODCALLTYPE vu_removeall(VuColl *t) { t->n = 0; return S_OK; }
static const void *const VT_VuColl[] = { vu_qi, vu_addref, vu_release, vu_iids, vu_name, vu_trust, vu_count, vu_add, vu_remove, vu_removeall };

HRESULT comp_new_visual_unordered_collection(void **out) {
  VuColl *c; static LONG once;
  if (!out) return E_POINTER; *out = NULL;
  c = (VuColl *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *c); if (!c) return E_OUTOFMEMORY;
  c->vt = VT_VuColl; c->ref = 1; *out = c;
  if (!InterlockedExchange(&once, 1)) comp_log("VisualUnorderedCollection (inert, counts only) handed out for CompositionLight targets\n");
  return S_OK; }

/* ---------------- completed IAsyncAction (+ IAsyncInfo) ---------------- */
typedef struct DoneAct_ { const void *vt; const void *vt_info; LONG ref; IUnknown *handler; } DoneAct;
#define ACT_OF_INFO(p) ((DoneAct *)((BYTE *)(p) - offsetof(DoneAct, vt_info)))
typedef struct { const struct { HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **); ULONG (STDMETHODCALLTYPE *AddRef)(void *); ULONG (STDMETHODCALLTYPE *Release)(void *);
  HRESULT (STDMETHODCALLTYPE *Invoke)(void *, void *, int); } *vt; } Handler_;
static HRESULT STDMETHODCALLTYPE da_qi(DoneAct *t, REFIID r, void **o) {
  if (!o) return E_POINTER;
  if (IsEqualGUID(r, &IID_IUnknown_c) || IsEqualGUID(r, &IID_IInspectable_c) || IsEqualGUID(r, &IID_IAgileObject_c) || IsEqualGUID(r, &IID_IAsyncAction_c)) { *o = t; InterlockedIncrement(&t->ref); return S_OK; }
  if (IsEqualGUID(r, &IID_IAsyncInfo_c)) { *o = &t->vt_info; InterlockedIncrement(&t->ref); return S_OK; }
  *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE da_addref(DoneAct *t) { return (ULONG)InterlockedIncrement(&t->ref); }
static ULONG STDMETHODCALLTYPE da_release(DoneAct *t) { LONG r = InterlockedDecrement(&t->ref); if (!r) { if (t->handler) t->handler->lpVtbl->Release(t->handler); HeapFree(GetProcessHeap(), 0, t); } return (ULONG)r; }
static HRESULT STDMETHODCALLTYPE da_iids(DoneAct *t, ULONG *n, IID **iids) {
  (void)t; if (!n || !iids) return E_POINTER; *iids = (IID *)CoTaskMemAlloc(2 * sizeof(IID)); if (!*iids) return E_OUTOFMEMORY; (*iids)[0] = IID_IAsyncAction_c; (*iids)[1] = IID_IAsyncInfo_c; *n = 2; return S_OK; }
static HRESULT STDMETHODCALLTYPE da_name(DoneAct *t, HSTRING *s) { (void)t; return make_hstring(L"Windows.Foundation.IAsyncAction", s); }
static HRESULT STDMETHODCALLTYPE da_trust(DoneAct *t, TrustLevel *l) { (void)t; if (!l) return E_POINTER; *l = BaseTrust; return S_OK; }
static HRESULT STDMETHODCALLTYPE da_put_completed(DoneAct *t, IUnknown *h) {   /* already complete: call the handler now (AsyncStatus.Completed = 1) */
  if (!h) return S_OK; if (t->handler) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
  h->lpVtbl->AddRef(h); t->handler = h; ((Handler_ *)h)->vt->Invoke(h, t, 1); return S_OK; }
static HRESULT STDMETHODCALLTYPE da_get_completed(DoneAct *t, IUnknown **h) { if (!h) return E_POINTER; *h = t->handler; if (*h) (*h)->lpVtbl->AddRef(*h); return S_OK; }
static HRESULT STDMETHODCALLTYPE da_results(DoneAct *t) { (void)t; return S_OK; }
static const void *const VT_DoneAct[] = { da_qi, da_addref, da_release, da_iids, da_name, da_trust, da_put_completed, da_get_completed, da_results };
/* IAsyncInfo: get_Id, get_Status, get_ErrorCode, Cancel, Close */
static HRESULT STDMETHODCALLTYPE di_qi(void *p, REFIID r, void **o) { return da_qi(ACT_OF_INFO(p), r, o); }
static ULONG STDMETHODCALLTYPE di_addref(void *p) { return da_addref(ACT_OF_INFO(p)); }
static ULONG STDMETHODCALLTYPE di_release(void *p) { return da_release(ACT_OF_INFO(p)); }
static HRESULT STDMETHODCALLTYPE di_iids(void *p, ULONG *n, IID **i) { return da_iids(ACT_OF_INFO(p), n, i); }
static HRESULT STDMETHODCALLTYPE di_name(void *p, HSTRING *s) { return da_name(ACT_OF_INFO(p), s); }
static HRESULT STDMETHODCALLTYPE di_trust(void *p, TrustLevel *l) { return da_trust(ACT_OF_INFO(p), l); }
static HRESULT STDMETHODCALLTYPE di_id(void *p, UINT32 *r) { (void)p; if (!r) return E_POINTER; *r = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_status(void *p, int *r) { (void)p; if (!r) return E_POINTER; *r = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_error(void *p, HRESULT *r) { (void)p; if (!r) return E_POINTER; *r = S_OK; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_cancel(void *p) { (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE di_close(void *p) { (void)p; return S_OK; }
static const void *const VT_DoneInfo[] = { di_qi, di_addref, di_release, di_iids, di_name, di_trust, di_id, di_status, di_error, di_cancel, di_close };

HRESULT comp_new_completed_action(void **out) {
  DoneAct *a;
  if (!out) return E_POINTER; *out = NULL;
  a = (DoneAct *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a); if (!a) return E_OUTOFMEMORY;
  a->vt = VT_DoneAct; a->vt_info = VT_DoneInfo; a->ref = 1; *out = a; return S_OK; }
