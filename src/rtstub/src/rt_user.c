/* rt_user.c - Windows.Security.Authentication.Web.Core (WAM): the user sign-in the Store's "Get" needs.
 *
 * Win10: clicking Get on a product makes the Store ask the Web Account Manager (WebAuthenticationCoreManager) for a USER token
 * (a WLID1.0 RPS ticket) for the order service, scope "service::purchase.mp.microsoft.com::MBI_SSL". wlidsvc / TokenBroker answer
 * it from the signed-in Microsoft account. On WXR there is no such service, so the Store gets no user ticket, sends the order with
 * only the device ticket (rt_msa.c / rt_onlineid.c), and purchase.mp.microsoft.com answers 401. This file is that missing piece.
 *
 * STAGE A (now): the WAM classes exist and every token request is LOGGED (client id, scope, provider) exactly the way rt_onlineid.c
 * logged the device service/policy to discover them - so we learn precisely what the Store asks for before wiring the real login.
 * GetTokenSilentlyAsync / RequestTokenAsync return WebTokenRequestStatus=UserInteractionRequired for now (a machine with no account).
 * STAGE B (next): RequestTokenAsync drives a browser sign-in on Microsoft's real login page (the user's password never enters WXR),
 * turns the result into the compact WLID1.0 ticket for the request's scope (reusing the device identity), and returns it as
 * WebTokenResponse.Value so the Store puts it on the order.
 *
 * Objects the app creates come through our factories, so a request's fields are kept in its hand-written state (rt_user). */
#include "rt_core.h"
#include <stdio.h>

#define WNS  L"Windows.Security.Authentication.Web.Core."
#define CNS  L"Windows.Security.Credentials."

/* WebTokenRequestStatus */
enum { WTRS_Success = 0, WTRS_AccountSwitch = 1, WTRS_UserInteractionRequired = 2, WTRS_AccountProviderNotAvailable = 3,
       WTRS_UserCancel = 4, WTRS_ProviderError = 5 };

/* The user ticket comes from rt_msa.c (the one auth seam): service + policy, served from the 24 h cache or minted from the
 * stored refresh token. The WAM scope the Store sends is "service::<service>::<policy>" - split it to ask for that ticket. */
int msa_user_ticket(const WCHAR *service, const WCHAR *policy, WCHAR *ticket, int cap, WCHAR *id, int idcap);
static void scope_split(const WCHAR *scope, WCHAR *service, int scap, WCHAR *policy, int pcap)
{
    const WCHAR *p = scope ? scope : L"", *s; if (scap) service[0] = 0; if (pcap) policy[0] = 0;
    if (!wcsncmp(p, L"service::", 9)) p += 9;
    if ((s = wcsstr(p, L"::"))) { int n = (int)(s - p); if (n > scap - 1) n = scap - 1; lstrcpynW(service, p, n + 1); lstrcpynW(policy, s + 2, pcap); }
    else lstrcpynW(service, p, scap);
}

static const WCHAR *raw(void *hs)
{
    static const WCHAR *(WINAPI *p)(void *, UINT32 *);
    if (!hs) return L"";
    if (!p) p = (const WCHAR *(WINAPI *)(void *, UINT32 *))rt_winrt_fn("WindowsGetStringRawBuffer");
    return p ? p(hs, NULL) : L"";
}
static HRESULT put_str(void **out, const WCHAR *s) { if (!out) return E_POINTER; *out = s && s[0] ? rt_hstring(s) : NULL; return S_OK; }

/* ---------- WebAccountProvider ---------- */
typedef struct { WCHAR id[260], display[160], authority[160]; } Prov;
static void *new_provider(const WCHAR *id, const WCHAR *display, const WCHAR *authority)
{
    int cls = rt_class_index(CNS L"WebAccountProvider"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; Prov *p;
    if (o && (p = (Prov *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p))) {
        lstrcpynW(p->id, id ? id : L"", 260); lstrcpynW(p->display, display ? display : L"", 160); lstrcpynW(p->authority, authority ? authority : L"", 160);
        rt_set_user(o, p);
    }
    return o;
}
HRESULT STDMETHODCALLTYPE S_IWebAccountProviderFactory_CreateWebAccountProvider(void *This, void *id, void *display, void *iconUri, void **out)
{ (void)This; (void)iconUri; if (!out) return E_POINTER; *out = new_provider(raw(id), raw(display), L""); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IWebAccountProvider_get_Id(void *This, void **out) { Prov *p = (Prov *)rt_user(This); return put_str(out, p ? p->id : L""); }
HRESULT STDMETHODCALLTYPE S_IWebAccountProvider_get_DisplayName(void *This, void **out) { Prov *p = (Prov *)rt_user(This); return put_str(out, p ? p->display : L""); }
HRESULT STDMETHODCALLTYPE S_IWebAccountProvider2_get_Authority(void *This, void **out) { Prov *p = (Prov *)rt_user(This); return put_str(out, p ? p->authority : L""); }

/* ---------- WebTokenRequest ---------- */
typedef struct { void *provider; WCHAR scope[1024], clientId[260]; int prompt; } Req;
static void *new_request(void *provider, const WCHAR *scope, const WCHAR *clientId, int prompt)
{
    int cls = rt_class_index(WNS L"WebTokenRequest"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; Req *r;
    if (o && (r = (Req *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r))) {
        r->provider = provider; if (provider) rt_AddRef(provider);
        lstrcpynW(r->scope, scope ? scope : L"", 1024); lstrcpynW(r->clientId, clientId ? clientId : L"", 260); r->prompt = prompt;
        rt_set_user(o, r);
    }
    return o;
}
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestFactory_Create(void *This, void *provider, void *scope, void *clientId, void **out)
{ (void)This; if (!out) return E_POINTER; *out = new_request(provider, raw(scope), raw(clientId), 0); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestFactory_CreateWithPromptType(void *This, void *provider, void *scope, void *clientId, int prompt, void **out)
{ (void)This; if (!out) return E_POINTER; *out = new_request(provider, raw(scope), raw(clientId), prompt); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestFactory_CreateWithProvider(void *This, void *provider, void **out)
{ (void)This; if (!out) return E_POINTER; *out = new_request(provider, L"", L"", 0); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestFactory_CreateWithScope(void *This, void *provider, void *scope, void **out)
{ (void)This; if (!out) return E_POINTER; *out = new_request(provider, raw(scope), L"", 0); return *out ? S_OK : E_OUTOFMEMORY; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequest_get_ClientId(void *This, void **out) { Req *r = (Req *)rt_user(This); return put_str(out, r ? r->clientId : L""); }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequest_get_Scope(void *This, void **out) { Req *r = (Req *)rt_user(This); return put_str(out, r ? r->scope : L""); }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequest_get_PromptType(void *This, int *out) { Req *r = (Req *)rt_user(This); if (out) *out = r ? r->prompt : 0; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequest_get_WebAccountProvider(void *This, void **out)
{ Req *r = (Req *)rt_user(This); if (!out) return E_POINTER; *out = r ? r->provider : NULL; if (*out) rt_AddRef(*out); return S_OK; }

/* ---------- WebAccount (the signed-in identity the response carries) ---------- */
typedef struct { WCHAR id[160], user[160]; void *provider; int state; } Acct;
static void *new_webaccount(const WCHAR *id, const WCHAR *user, void *provider)
{
    int cls = rt_class_index(CNS L"WebAccount"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; Acct *a;
    if (o && (a = (Acct *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a))) {
        lstrcpynW(a->id, id ? id : L"", 160); lstrcpynW(a->user, user && user[0] ? user : (id ? id : L""), 160);
        a->provider = provider; if (provider) rt_AddRef(provider); a->state = 1;   /* WebAccountState.Connected */
        rt_set_user(o, a);
    }
    return o;
}
HRESULT STDMETHODCALLTYPE S_IWebAccount2_get_Id(void *This, void *out) { Acct *a = (Acct *)rt_user(This); return put_str((void **)out, a ? a->id : L""); }
HRESULT STDMETHODCALLTYPE S_IWebAccount_get_UserName(void *This, void *out) { Acct *a = (Acct *)rt_user(This); return put_str((void **)out, a ? a->user : L""); }
HRESULT STDMETHODCALLTYPE S_IWebAccount_get_State(void *This, void *out) { Acct *a = (Acct *)rt_user(This); if (out) *(int *)out = a ? a->state : 0; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebAccount_get_WebAccountProvider(void *This, void *out) { Acct *a = (Acct *)rt_user(This); if (!out) return E_POINTER; *(void **)out = a ? a->provider : NULL; if (*(void **)out) rt_AddRef(*(void **)out); return S_OK; }

/* ---------- WebTokenResponse (carries the ticket + the account) ---------- */
typedef struct { WCHAR *token; void *account; } Resp;
static void *empty_map(void) { const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IMapView`2<String,String>"); return c ? rt_new_coll(c) : NULL; }
static void *new_response(const WCHAR *token, void *account)
{
    int cls = rt_class_index(WNS L"WebTokenResponse"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; Resp *r;
    if (o && (r = (Resp *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r))) {
        if (token) { r->token = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(token) + 1) * sizeof(WCHAR)); if (r->token) lstrcpyW(r->token, token); }
        r->account = account; if (account) rt_AddRef(account);
        rt_set_user(o, r);
    }
    return o;
}
HRESULT STDMETHODCALLTYPE S_IWebTokenResponse_get_Token(void *This, void *out) { Resp *r = (Resp *)rt_user(This); return put_str((void **)out, r && r->token ? r->token : L""); }
HRESULT STDMETHODCALLTYPE S_IWebTokenResponse_get_WebAccount(void *This, void *out) { Resp *r = (Resp *)rt_user(This); if (!out) return E_POINTER; *(void **)out = r ? r->account : NULL; if (*(void **)out) rt_AddRef(*(void **)out); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebTokenResponse_get_ProviderError(void *This, void *out) { (void)This; if (out) *(void **)out = NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebTokenResponse_get_Properties(void *This, void *out) { (void)This; if (out) *(void **)out = empty_map(); return S_OK; }

/* ---------- WebTokenRequestResult ---------- */
typedef struct { int status; void *responses; } RResult;
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestResult_get_ResponseStatus(void *This, void *out) { RResult *s = (RResult *)rt_user(This); if (out) *(int *)out = s ? s->status : WTRS_UserInteractionRequired; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestResult_get_ResponseData(void *This, void *out) { RResult *s = (RResult *)rt_user(This); if (!out) return E_POINTER; *(void **)out = s ? s->responses : NULL; if (*(void **)out) rt_AddRef(*(void **)out); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebTokenRequestResult_get_ResponseError(void *This, void *out) { (void)This; if (out) *(void **)out = NULL; return S_OK; }
static void *new_result(int status, void *responses)
{
    int cls = rt_class_index(WNS L"WebTokenRequestResult"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; RResult *s;
    if (o && (s = (RResult *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s))) { s->status = status; s->responses = responses; if (responses) rt_AddRef(responses); rt_set_user(o, s); }
    return o;
}

/* Build the async result the Store awaits. If we can get a user ticket for the request's scope, return Success carrying one
 * WebTokenResponse (ticket + account); otherwise UserInteractionRequired, as a machine with no signed-in account would. */
static HRESULT token_result_async(void *request, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Security.Authentication.Web.Core.WebTokenRequestResult>");
    Req *rq = request ? (Req *)rt_user(request) : NULL;
    WCHAR service[400], policy[160], ticket[8192], id[96]; void *res, *resp = NULL, *acct = NULL, *vec = NULL; int status = WTRS_UserInteractionRequired;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    /* Only answer the WLID/RPS scopes we can actually mint ("service::<svc>::<policy>"). Other scopes - e.g. the AAD-style
     * Microsoft Graph profile scope "https://graph.microsoft.com/User.Read" - we cannot produce a valid token for; handing
     * back our RPS ticket for those gives the Store a token that the target service rejects, which it reads as a broken
     * sign-in. Decline them (UserInteractionRequired) so the Store degrades gracefully (e.g. no profile picture) instead. */
    if (rq && !wcsncmp(rq->scope, L"service::", 9)) {
        scope_split(rq->scope, service, 400, policy, 160);
        if (service[0] && msa_user_ticket(service, policy, ticket, 8192, id, 96)) {
            acct = new_webaccount(id, id, rq->provider);
            resp = new_response(ticket, acct);
            if (resp) { vec = rt_new_coll_items(rt_coll_named("Windows.Foundation.Collections.IVectorView`1<Windows.Security.Authentication.Web.Core.WebTokenResponse>"), (IUnknown **)&resp, 1); status = WTRS_Success; }
            rt_log("WAM: returning a USER token for service '%ls' policy '%ls' (account %ls) -> Success", service, policy, id[0] ? id : L"?");
            SecureZeroMemory(ticket, sizeof ticket);
        }
    }
    res = new_result(status, vec);
    if (acct) rt_Release(acct); if (resp) rt_Release(resp); if (vec) rt_Release(vec);
    if (!res) return E_OUTOFMEMORY;
    *out = rt_new_async_result(a, (IUnknown *)res); rt_Release(res);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ---------- WebAuthenticationCoreManager statics ---------- */
static void log_request(const char *how, void *request)
{
    Req *r = (Req *)rt_user(request); Prov *p = r ? (Prov *)rt_user(r->provider) : NULL;
    static LONG n; if (InterlockedIncrement(&n) > 40) return;
    rt_log("WAM %s: clientId '%ls' scope '%ls' provider '%ls' (authority '%ls') prompt %d", how,
           r ? r->clientId : L"?", r ? r->scope : L"?", p ? p->id : L"?", p ? p->authority : L"", r ? r->prompt : -1);
}
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_GetTokenSilentlyAsync(void *This, void *request, void **out)
{ (void)This; log_request("GetTokenSilently", request); return token_result_async(request, out); }
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_GetTokenSilentlyAsync_2(void *This, void *request, void *account, void **out)
{ (void)This; (void)account; log_request("GetTokenSilently(acct)", request); return token_result_async(request, out); }
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_RequestTokenAsync(void *This, void *request, void **out)
{ (void)This; log_request("RequestToken", request); return token_result_async(request, out); }
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_RequestTokenAsync_2(void *This, void *request, void *account, void **out)
{ (void)This; (void)account; log_request("RequestToken(acct)", request); return token_result_async(request, out); }

/* FindAccountProviderAsync: give back a provider with the requested id so the Store can build its token request. */
static HRESULT provider_async(const WCHAR *id, const WCHAR *authority, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Security.Credentials.WebAccountProvider>");
    void *prov; static LONG n;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    if (InterlockedIncrement(&n) <= 20) rt_log("WAM FindAccountProvider: id '%ls' authority '%ls'", id ? id : L"", authority ? authority : L"");
    if (!(prov = new_provider(id, L"Microsoft account", authority))) return E_OUTOFMEMORY;
    *out = rt_new_async_result(a, (IUnknown *)prov); rt_Release(prov);
    return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_FindAccountProviderAsync(void *This, void *id, void **out)
{ (void)This; return provider_async(raw(id), L"", out); }
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_FindAccountProviderAsync_2(void *This, void *id, void *authority, void **out)
{ (void)This; return provider_async(raw(id), raw(authority), out); }
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics2_FindAccountProviderAsync(void *This, void *id, void *authority, void *user, void **out)
{ (void)This; (void)user; return provider_async(raw(id), raw(authority), out); }

/* FindAccountAsync: resolve a stored web account id back into a WebAccount. The Store calls this after GetStoreWebAccountId
 * reports a signed-in id; the default stand-in completes with null, leaving the Store an id with no account - which it then
 * treats as a broken/offline state. Return a connected WebAccount with that id + provider so the signed-in state is coherent. */
HRESULT STDMETHODCALLTYPE S_IWebAuthenticationCoreManagerStatics_FindAccountAsync(void *This, void *provider, void *webAccountId, void *out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Security.Credentials.WebAccount>");
    const WCHAR *id = raw(webAccountId); void *acct; (void)This;
    if (!out) return E_POINTER; *(void **)out = NULL; if (!a) return E_NOTIMPL;
    rt_log("WAM FindAccount: id '%ls' -> WebAccount", id);
    if (!(acct = new_webaccount(id, id, provider))) return E_OUTOFMEMORY;
    *(void **)out = rt_new_async_result(a, (IUnknown *)acct); rt_Release(acct);
    return *(void **)out ? S_OK : E_OUTOFMEMORY;
}

/* ====================================================================================================================
 * AccountsSettingsPane - the account manager UI. The Store's "Sign in" calls Show / ShowManageAccountsAsync on it. We fire the
 * app's AccountCommandsRequested handler, let it append its WebAccountProviderCommand(s) to the vector we hand it (rt_core's
 * IVector now keeps appended items), then invoke each command's Invoked delegate - which is what makes the Store go on to call
 * WebAuthenticationCoreManager.RequestTokenAsync with its own client id and scope.
 * ==================================================================================================================== */
#define ANS L"Windows.UI.ApplicationSettings."
static IUnknown *g_cmd_handler;                 /* TypedEventHandler<AccountsSettingsPane, ...CommandsRequestedEventArgs> */
static void *g_pane;                            /* the single pane object (GetForCurrentView) */
static void *get_pane(void) { int c; if (!g_pane && (c = rt_class_index(ANS L"AccountsSettingsPane")) >= 0) g_pane = rt_new_object(c, NULL); return g_pane; }
static HRESULT invoke_delegate1(IUnknown *d, void *a) { return d ? ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))(*(void ***)d)[3])(d, a) : S_OK; }
static HRESULT invoke_delegate2(IUnknown *d, void *a, void *b) { return d ? ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *))(*(void ***)d)[3])(d, a, b) : S_OK; }

/* ---- WebAccountProviderCommand (the app builds these; it holds the provider + an Invoked delegate) ---- */
typedef struct { void *provider; IUnknown *invoked; } Cmd;
HRESULT STDMETHODCALLTYPE S_IWebAccountProviderCommandFactory_CreateWebAccountProviderCommand(void *This, void *provider, void *invoked, void **out)
{
    int cls = rt_class_index(ANS L"WebAccountProviderCommand"); void *o = cls >= 0 ? rt_new_object(cls, NULL) : NULL; Cmd *c;
    (void)This; if (!out) return E_POINTER; *out = NULL;
    if (o && (c = (Cmd *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *c))) {
        c->provider = provider; if (provider) rt_AddRef(provider);
        c->invoked = (IUnknown *)invoked; if (invoked) ((IUnknown *)invoked)->lpVtbl->AddRef((IUnknown *)invoked);
        rt_set_user(o, c);
    }
    *out = o; return o ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IWebAccountProviderCommand_get_WebAccountProvider(void *This, void **out)
{ Cmd *c = (Cmd *)rt_user(This); if (!out) return E_POINTER; *out = c ? c->provider : NULL; if (*out) rt_AddRef(*out); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IWebAccountProviderCommand_get_Invoked(void *This, void **out)
{ Cmd *c = (Cmd *)rt_user(This); if (!out) return E_POINTER; *out = c ? c->invoked : NULL; if (*out) ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out); return S_OK; }

/* ---- CommandsRequestedEventArgs (we create it and pass it to the handler) ---- */
typedef struct { void *provcmds; WCHAR header[256]; } EArgs;
static void *provcmd_vector(void *This)
{
    EArgs *e = (EArgs *)rt_user(This);
    if (e && !e->provcmds) { const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IVector`1<Windows.UI.ApplicationSettings.WebAccountProviderCommand>"); if (c) e->provcmds = rt_new_coll(c); }
    return e ? e->provcmds : NULL;
}
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_get_WebAccountProviderCommands(void *This, void **out)
{ void *v = provcmd_vector(This); if (!out) return E_POINTER; *out = v; if (v) rt_AddRef(v); return S_OK; }
static HRESULT empty_vec(const char *name, void **out)
{ const RtColl *c = rt_coll_named(name); if (!out) return E_POINTER; *out = c ? rt_new_coll(c) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_get_Commands(void *This, void **out)
{ (void)This; return empty_vec("Windows.Foundation.Collections.IVector`1<Windows.UI.ApplicationSettings.SettingsCommand>", out); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_get_WebAccountCommands(void *This, void **out)
{ (void)This; return empty_vec("Windows.Foundation.Collections.IVector`1<Windows.UI.ApplicationSettings.WebAccountCommand>", out); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_get_CredentialCommands(void *This, void **out)
{ (void)This; return empty_vec("Windows.Foundation.Collections.IVector`1<Windows.UI.ApplicationSettings.CredentialCommand>", out); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_get_HeaderText(void *This, void **out) { EArgs *e = (EArgs *)rt_user(This); return put_str(out, e ? e->header : L""); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_put_HeaderText(void *This, void *hs) { EArgs *e = (EArgs *)rt_user(This); if (e) lstrcpynW(e->header, raw(hs), 256); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneCommandsRequestedEventArgs_GetDeferral(void *This, void **out)
{ int c = rt_class_index(ANS L"AccountsSettingsPaneEventDeferral"); (void)This; if (!out) return E_POINTER; *out = c >= 0 ? rt_new_object(c, NULL) : NULL; return S_OK; }

/* ---- the fire-and-invoke core ---- */
static void drive_account_signin(const char *via)
{
    int ecls = rt_class_index(ANS L"AccountsSettingsPaneCommandsRequestedEventArgs"); void *args, *pane; EArgs *ea;
    IUnknown **items = NULL; int n, i;
    if (!g_cmd_handler) { rt_log("AccountsSettingsPane.%s: no AccountCommandsRequested handler registered yet", via); return; }
    if (ecls < 0 || !(args = rt_new_object(ecls, NULL))) return;
    if ((ea = (EArgs *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *ea))) rt_set_user(args, ea);
    pane = get_pane();
    rt_log("AccountsSettingsPane.%s: firing AccountCommandsRequested", via);
    invoke_delegate2(g_cmd_handler, pane ? pane : args, args);
    n = provcmd_vector(args) ? rt_coll_items(provcmd_vector(args), &items) : 0;
    rt_log("AccountsSettingsPane: the app added %d provider command(s)", n);
    for (i = 0; i < n; i++) {
        Cmd *c = (Cmd *)rt_user(items[i]); Prov *p = c ? (Prov *)rt_user(c->provider) : NULL;
        rt_log("  invoking provider command %d: provider '%ls'", i, p ? p->id : L"?");
        if (c && c->invoked) invoke_delegate1(c->invoked, items[i]);   /* -> app calls RequestTokenAsync */
    }
    rt_Release(args);
}

/* ---- AccountsSettingsPane instance + statics ---- */
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPane_add_AccountCommandsRequested(void *This, void *handler, void *token)
{
    (void)This;
    if (g_cmd_handler) g_cmd_handler->lpVtbl->Release(g_cmd_handler);
    g_cmd_handler = (IUnknown *)handler; if (handler) ((IUnknown *)handler)->lpVtbl->AddRef((IUnknown *)handler);
    if (token) *(INT64 *)token = 1;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPane_remove_AccountCommandsRequested(void *This, INT64 token)
{ (void)This; (void)token; if (g_cmd_handler) { g_cmd_handler->lpVtbl->Release(g_cmd_handler); g_cmd_handler = NULL; } return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneStatics_GetForCurrentView(void *This, void **out)
{ void *p = get_pane(); (void)This; if (!out) return E_POINTER; *out = p; if (p) rt_AddRef(p); return *out ? S_OK : E_FAIL; }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneStatics_Show(void *This) { (void)This; drive_account_signin("Show"); return S_OK; }
static HRESULT show_async(const char *via, void **out)
{ const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncAction"); drive_account_signin(via); if (!out) return E_POINTER; *out = a ? rt_new_async(a) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneStatics2_ShowAddAccountAsync(void *This, void **out) { (void)This; return show_async("ShowAddAccountAsync", out); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneStatics2_ShowManageAccountsAsync(void *This, void **out) { (void)This; return show_async("ShowManageAccountsAsync", out); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneStatics3_ShowAddAccountForUserAsync(void *This, void *user, void **out) { (void)This; (void)user; return show_async("ShowAddAccountForUserAsync", out); }
HRESULT STDMETHODCALLTYPE S_IAccountsSettingsPaneStatics3_ShowManageAccountsForUserAsync(void *This, void *user, void **out) { (void)This; (void)user; return show_async("ShowManageAccountsForUserAsync", out); }
