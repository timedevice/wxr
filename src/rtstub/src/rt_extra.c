/* rt_extra.c - interfaces of stand-in factories that are not in the winmd (COM interop interfaces), hand-written.
 *   SystemMediaTransportControls: ISystemMediaTransportControlsInterop::GetForWindow (Media Player asks for it at startup). */
#include "rt_core.h"
#include <stdlib.h>

int rt_class_index(const WCHAR *name)
{
    int i; for (i = 0; i < g_nrtclass; i++) if (!lstrcmpW(g_rtclass[i].name, name)) return i; return -1;
}

static const GUID IID_SmtcInterop = { 0xddb0472d, 0xc911, 0x4a1f, { 0x86, 0xd9, 0xdc, 0x3d, 0x71, 0xa9, 0x5f, 0x5a } };
static const GUID IID_IUnknown_x = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IInspectable_x = { 0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 } };

/* one SMTC per process: the app may ask more than once (GetForWindow on the same window gives the same object in Win10) */
static void *g_smtc;

static HRESULT STDMETHODCALLTYPE si_QI(void *This, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown_x) || IsEqualGUID(iid, &IID_IInspectable_x) || IsEqualGUID(iid, &IID_SmtcInterop)) { *out = This; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE si_AddRef(void *This) { (void)This; return 2; }     /* static object */
static ULONG STDMETHODCALLTYPE si_Release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE si_GetIids(void *This, ULONG *n, GUID **ids) { (void)This; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE si_GetName(void *This, void **name) { (void)This; if (name) *name = NULL; return S_OK; }
static HRESULT STDMETHODCALLTYPE si_GetTrust(void *This, int *lv) { (void)This; if (lv) *lv = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE si_GetForWindow(void *This, HWND wnd, REFIID iid, void **out)
{
    typedef HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **);
    int cls = rt_class_index(L"Windows.Media.SystemMediaTransportControls"); (void)This;
    if (!out) return E_POINTER; *out = NULL;
    rt_log("SystemMediaTransportControls.GetForWindow(%p) (stand-in)", (void *)wnd);
    if (cls < 0) return E_NOTIMPL;
    if (!g_smtc) { void *o = rt_new_object(cls, NULL); if (!o) return E_OUTOFMEMORY; if (InterlockedCompareExchangePointer(&g_smtc, o, NULL)) rt_Release(o); }
    return (*(QI *)*(void ***)g_smtc)(g_smtc, iid, out);
}
static const void *const si_vt[] = { (void *)si_QI, (void *)si_AddRef, (void *)si_Release, (void *)si_GetIids, (void *)si_GetName, (void *)si_GetTrust, (void *)si_GetForWindow };
static const void *const *si_obj = si_vt;

HRESULT rt_extra_factory_qi(const WCHAR *cls, REFIID iid, void **out)
{
    if (!lstrcmpW(cls, L"Windows.Media.SystemMediaTransportControls") && IsEqualGUID(iid, &IID_SmtcInterop)) { *out = (void *)&si_obj; return S_OK; }
    return E_NOINTERFACE;
}

/* AppCapability.CheckAccess: AppCapabilityAccessStatus.Allowed (4); Win7 has no capability consent to honour. */
HRESULT STDMETHODCALLTYPE S_IAppCapability_CheckAccess(void *This, int *out) { (void)This; if (!out) return E_POINTER; *out = 4; return S_OK; }

/* StorageItemAccessList.MaximumItemsAllowed: 0 made Media Player's recent-files code index out of range after a pick (Win10: 25 recent, 1000 future-access). */
HRESULT STDMETHODCALLTYPE S_IStorageItemAccessList_get_MaximumItemsAllowed(void *This, UINT32 *out) { (void)This; if (!out) return E_POINTER; *out = 1000; return S_OK; }

/* DisplayRequest.RequestActive / RequestRelease: a process-wide count; the display stays on while it is above zero
   (SetThreadExecutionState on the calling thread, as Win10 holds the request for the app, not a thread) */
static LONG g_display_req;
HRESULT STDMETHODCALLTYPE S_IDisplayRequest_RequestActive(void *This) { (void)This; if (InterlockedIncrement(&g_display_req) == 1) SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IDisplayRequest_RequestRelease(void *This) { (void)This; if (InterlockedDecrement(&g_display_req) <= 0) { g_display_req = 0; SetThreadExecutionState(ES_CONTINUOUS); } return S_OK; }

/* User.GetFromId: no Win10 user manager -> null (Calculator: no Education policy, graphing stays on). An inert User object would make it query
   NamedPolicy for that user. */
HRESULT STDMETHODCALLTYPE S_IUserStatics_GetFromId(void *This, void *id, void **out) { (void)This; (void)id; if (!out) return E_POINTER; *out = NULL; return S_OK; }

/* SoundLevelBroker.SoundLevel: SoundLevel.Full (2). Win7 has no per-app audio policy to say otherwise (Muted 0 / Low 1). */
HRESULT STDMETHODCALLTYPE S_ISoundLevelBrokerStatics_get_SoundLevel(void *This, int *out) { (void)This; if (!out) return E_POINTER; *out = 2; return S_OK; }
HRESULT STDMETHODCALLTYPE S_ISystemMediaTransportControls_get_SoundLevel(void *This, int *out) { (void)This; if (!out) return E_POINTER; *out = 2; return S_OK; }   /* Full, not Muted */

/* AppServiceConnection.OpenAsync: no app services on Win7 (the Store connects to its desktop extension). Answered Success (0): the
   Store then carries on without its extension. */
void *lop_new(const char *type, IUnknown *obj, int is_bool, int b);
HRESULT STDMETHODCALLTYPE S_IAppServiceConnection_OpenAsync(void *This, void *a0) { (void)This; if (!a0) return E_POINTER;
  rt_log("AppServiceConnection.OpenAsync -> status 0");
  *(void **)a0 = lop_new("Windows.Foundation.IAsyncOperation`1<Windows.ApplicationModel.AppService.AppServiceConnectionStatus>", NULL, 2, 0); return *(void **)a0 ? S_OK : E_OUTOFMEMORY; }
static const WCHAR *as_raw(void *hs) { typedef const WCHAR *(WINAPI *P)(void *, UINT32 *); static P p; if (!hs) return L"";
  if (!p) p = (P)rt_winrt_fn("WindowsGetStringRawBuffer"); return p ? p(hs, NULL) : L""; }
HRESULT STDMETHODCALLTYPE S_IAppServiceConnection_put_AppServiceName(void *This, void *a0) { (void)This; rt_log("AppServiceConnection.AppServiceName = %ls", as_raw(a0)); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAppServiceConnection_put_PackageFamilyName(void *This, void *a0) { (void)This; rt_log("AppServiceConnection.PackageFamilyName = %ls", as_raw(a0)); return S_OK; }
