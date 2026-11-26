/* rt_onlineid.c - Windows.Security.Authentication.OnlineId: OnlineIdSystemAuthenticator (the DEVICE's Microsoft identity), hand-written.
 *
 * Win10: the Microsoft Account service (wlidsvc) holds a device identity and hands out service tickets for it; the Store asks for one
 * before it places an order (also for free products) - without it purchase.mp.microsoft.com answers 401 and Get stops.
 * Here: OnlineIdServiceTicketRequest (service + policy, made by the app), GetTicketAsync -> OnlineIdSystemTicketResult { Status,
 * Identity { Id, Ticket { Value } } }. The ticket comes from msa_device_ticket() (rt_msa.c); while that cannot deliver, the result is
 * Status = Error, which the Store handles like a machine without a device identity.
 * Every request is logged ("[rtstub] OnlineId: ...") - that is how the wanted services / policies were found. */
#include "rt_core.h"
#include <stdio.h>

#define NS L"Windows.Security.Authentication.OnlineId."
int msa_device_ticket(const WCHAR *service, const WCHAR *policy, WCHAR *ticket, int cap, WCHAR *deviceId, int idcap);   /* rt_msa.c: 1 = ticket delivered */
int msa_user_ticket(const WCHAR *service, const WCHAR *policy, WCHAR *ticket, int cap, WCHAR *id, int idcap);          /* rt_msa.c: an imported Win10 USER ticket, preferred for the order */
void msa_set_app(const GUID *id);

typedef struct { WCHAR service[400], policy[120]; } Req;
typedef struct { int status; HRESULT err; WCHAR id[80]; WCHAR *ticket; Req req; } Res;   /* ticket: text inside this block, after the struct */

static const WCHAR *raw(void *hs)
{
    static const WCHAR *(WINAPI *p)(void *, UINT32 *);
    if (!hs) return L"";
    if (!p) p = (const WCHAR *(WINAPI *)(void *, UINT32 *))rt_winrt_fn("WindowsGetStringRawBuffer");
    return p ? p(hs, NULL) : L"";
}
static void hs_free(void *hs) { static HRESULT (WINAPI *p)(void *); if (!p) p = (HRESULT (WINAPI *)(void *))rt_winrt_fn("WindowsDeleteString"); if (p && hs) p(hs); }
static HRESULT put_str(void **out, const WCHAR *s) { if (!out) return E_POINTER; *out = s && s[0] ? rt_hstring(s) : NULL; return S_OK; }

/* ---- OnlineIdServiceTicketRequest ---- */
static void *new_req(const WCHAR *service, const WCHAR *policy)
{
    int cls = rt_class_index(NS L"OnlineIdServiceTicketRequest"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; Req *r;
    if (o && (r = (Req *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r))) { lstrcpynW(r->service, service, 400); lstrcpynW(r->policy, policy, 120); rt_set_user(o, r); }
    return o;
}
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicketRequestFactory_CreateOnlineIdServiceTicketRequest(void *This, void *service, void *policy, void **out)
{ (void)This; if (!out) return E_POINTER; *out = new_req(raw(service), raw(policy)); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicketRequestFactory_CreateOnlineIdServiceTicketRequestAdvanced(void *This, void *service, void **out)
{ (void)This; if (!out) return E_POINTER; *out = new_req(raw(service), L""); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicketRequest_get_Service(void *This, void **out) { Req *r = (Req *)rt_user(This); return put_str(out, r ? r->service : L""); }
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicketRequest_get_Policy(void *This, void **out) { Req *r = (Req *)rt_user(This); return put_str(out, r ? r->policy : L""); }

/* ---- OnlineIdSystemAuthenticator[ForUser] ---- */
static GUID g_appid;
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemAuthenticatorForUser_put_ApplicationId(void *This, const GUID *id)
{ (void)This; if (id) { static LONG n; g_appid = *id; msa_set_app(id); if (InterlockedIncrement(&n) <= 3) rt_log("OnlineId: ApplicationId = {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", id->Data1, id->Data2, id->Data3, id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3], id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7]); } return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemAuthenticatorForUser_get_ApplicationId(void *This, GUID *out) { (void)This; if (out) *out = g_appid; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemAuthenticatorForUser_GetTicketAsync(void *This, void *request, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Security.Authentication.OnlineId.OnlineIdSystemTicketResult>");
    int cls = rt_class_index(NS L"OnlineIdSystemTicketResult"); void *hs = NULL, *hp = NULL, *res; Res *r; WCHAR *tk; int ok; enum { TKCAP = 8192 };
    (void)This; if (!out) return E_POINTER; *out = NULL; if (!a || cls < 0) return E_NOTIMPL;
    if (!(r = (Res *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r + TKCAP * sizeof(WCHAR)))) return E_OUTOFMEMORY;
    tk = (WCHAR *)(r + 1); r->ticket = tk;
    if (request) {   /* IOnlineIdServiceTicketRequest: 6 get_Service, 7 get_Policy (the app's object, ours or not) */
        ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)request)[6])(request, &hs); ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)request)[7])(request, &hp);
        lstrcpynW(r->req.service, raw(hs), 400); lstrcpynW(r->req.policy, raw(hp), 120); hs_free(hs); hs_free(hp); }
    ok = msa_user_ticket(r->req.service, r->req.policy, tk, TKCAP, r->id, 80);   /* an imported Win10 USER ticket is what the order wants */
    { int user = ok; if (!ok) ok = msa_device_ticket(r->req.service, r->req.policy, tk, TKCAP, r->id, 80);
      r->status = ok ? 0 : 1;                    /* OnlineIdSystemTicketStatus: Success 0, Error 1, ServiceConnectionError 2 */
      r->err = ok ? S_OK : (HRESULT)0x80004005L;
      { static LONG n; if (InterlockedIncrement(&n) <= 60) rt_log("OnlineId: %s ticket for service '%ls' policy '%ls' -> %s (%d chars)", user ? "USER" : "device", r->req.service, r->req.policy, ok ? "delivered" : "NONE", ok ? lstrlenW(tk) : 0); } }
    if (!(res = rt_new_object(cls, NULL))) { HeapFree(GetProcessHeap(), 0, r); return E_OUTOFMEMORY; }
    rt_set_user(res, r);
    *out = rt_new_async_result(a, (IUnknown *)res); rt_Release(res);
    return *out ? S_OK : E_OUTOFMEMORY;
}
/* result -> identity -> ticket: each object carries a copy of the result block */
static void *sub(void *This, const WCHAR *cls_name)
{
    Res *r = (Res *)rt_user(This), *c; int cls = rt_class_index(cls_name); void *o; size_t sz;
    if (!r || cls < 0 || !(o = rt_new_object(cls, NULL))) return NULL;
    sz = sizeof *r + (lstrlenW(r->ticket) + 1) * sizeof(WCHAR);
    if ((c = (Res *)HeapAlloc(GetProcessHeap(), 0, sz))) { memcpy(c, r, sizeof *r); c->ticket = (WCHAR *)(c + 1); lstrcpyW(c->ticket, r->ticket); rt_set_user(o, c); }
    return o;
}
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemTicketResult_get_Status(void *This, int *out) { Res *r = (Res *)rt_user(This); if (out) *out = r ? r->status : 1; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemTicketResult_get_ExtendedError(void *This, HRESULT *out) { Res *r = (Res *)rt_user(This); if (out) *out = r ? r->err : E_FAIL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemTicketResult_get_Identity(void *This, void **out)
{ Res *r = (Res *)rt_user(This); if (!out) return E_POINTER; *out = (r && r->status == 0) ? sub(This, NS L"OnlineIdSystemIdentity") : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemIdentity_get_Id(void *This, void **out) { Res *r = (Res *)rt_user(This); return put_str(out, r ? r->id : L""); }
HRESULT STDMETHODCALLTYPE S_IOnlineIdSystemIdentity_get_Ticket(void *This, void **out) { if (!out) return E_POINTER; *out = sub(This, NS L"OnlineIdServiceTicket"); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicket_get_Value(void *This, void **out) { Res *r = (Res *)rt_user(This); return put_str(out, r ? r->ticket : L""); }
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicket_get_Request(void *This, void **out) { Res *r = (Res *)rt_user(This); if (!out) return E_POINTER; *out = r ? new_req(r->req.service, r->req.policy) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IOnlineIdServiceTicket_get_ErrorCode(void *This, int *out) { Res *r = (Res *)rt_user(This); if (out) *out = r ? (int)r->err : 0; return S_OK; }
