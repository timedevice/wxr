/* wxruri.inc - Windows.Foundation.Uri on top of urlmon's IUri (CreateUri, IE7+, present on Win7). Included from wxrsys.c.
   Before this the class came from VxKex's generic host fallback, whose strings reached XAML unterminated: CWinUriImpl::Create passes only
   the buffer to CreateUri, the garbage after the text made it fail with E_INVALIDARG, and every Control.DefaultStyleResourceUri style (the
   Community Toolkit's SettingsCard / SettingsExpander on Calculator's Settings page) silently failed to load. Every string handed out here
   is a real, terminated HSTRING.
   Layout (Windows.Foundation.winmd): IUriRuntimeClass {9E365E57} 6-22 AbsoluteUri, DisplayUri, Domain, Extension, Fragment, Host, Password,
   Path, Query, QueryParsed, RawUri, SchemeName, UserName, Port, Suspicious, Equals, CombineUri; IUriRuntimeClassWithAbsoluteCanonicalUri
   {758D9661} 6/7 AbsoluteCanonicalUri, DisplayIri; IStringable {96369F54} 6 ToString. Factory: IUriRuntimeClassFactory {44A9796F} 6 CreateUri,
   7 CreateWithRelativeUri; IUriEscapeStatics {C1D432BA} 6 UnescapeComponent, 7 EscapeComponent. */
/* no urlmon.h / shlwapi.h (they clash with wxrsys's own declarations): IUri through its vtable (urlmon.idl order: 3 GetPropertyBSTR,
   18 GetRawUri, 23 GetPort), CreateUri / CoInternetCombineUrlEx / UrlUnescapeW / UrlEscapeW by name */
/* IUri and Uri_PROPERTY come with windows.h */
#define Uri_CREATE_NO_DECODE_EXTRA_INFO_ 0x80
#define Uri_CREATE_CANONICALIZE_ 0x100
#define Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME_ 0x4   /* "E:\Music\FLAC" -> file:///E:/Music/FLAC, as Win10's Windows.Foundation.Uri accepts (Media Player recent folders) */
#define IURI_VT(u, k, T) ((T)(*(void ***)(u))[k])
#define IURI_ADDREF(u) IURI_VT(u, 1, ULONG (WINAPI *)(void *))(u)
#define IURI_RELEASE(u) IURI_VT(u, 2, ULONG (WINAPI *)(void *))(u)
#define IURI_GETPROPBSTR(u, p, b) IURI_VT(u, 3, HRESULT (WINAPI *)(void *, int, BSTR *, DWORD))(u, p, b, 0)
#define IURI_GETRAW(u, b) IURI_VT(u, 18, HRESULT (WINAPI *)(void *, BSTR *))(u, b)
#define IURI_GETPORT(u, p) IURI_VT(u, 23, HRESULT (WINAPI *)(void *, DWORD *))(u, p)
static void uri_free_hs(void *h) { typedef HRESULT (WINAPI *F)(void *); static F f; if (!f) f = (F)GetProcAddress(LoadLibraryA("api-ms-win-core-winrt-string-l1-1-0.dll"), "WindowsDeleteString"); if (f && h) f(h); }
static const GUID U_IID_Uri = {0x9E365E57, 0x48B2, 0x4160, {0x95, 0x6F, 0xC7, 0x38, 0x51, 0x20, 0xBB, 0xFC}};
static const GUID U_IID_UriCanon = {0x758D9661, 0x221C, 0x480F, {0xA3, 0x39, 0x50, 0x65, 0x66, 0x73, 0xF4, 0x6F}};
static const GUID U_IID_Stringable = {0x96369F54, 0x8EB6, 0x48F0, {0xAB, 0xCE, 0xC1, 0xB2, 0x11, 0xE6, 0x27, 0xC3}};
static const GUID U_IID_UriFactory = {0x44A9796F, 0x723E, 0x4FDF, {0xA2, 0x18, 0x03, 0x3E, 0x75, 0xB0, 0xC0, 0x84}};
static const GUID U_IID_UriEscape = {0xC1D432BA, 0xC824, 0x4452, {0xA7, 0xFD, 0x51, 0x2B, 0xC3, 0xBB, 0xE9, 0xA1}};
static const GUID U_IID_ActFactory = {0x00000035, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID U_IID_IUnknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID U_IID_IInspectable = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID U_IID_IAgile = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static const GUID U_IID_IUri = {0xA39EE748, 0x6A27, 0x4817, {0xA6, 0xF2, 0x13, 0x91, 0x4B, 0xEF, 0x58, 0x90}};

typedef struct { const void *vt_uri, *vt_canon, *vt_str; LONG ref; IUri *u; WCHAR *raw; } UriObj;
#define URI_OF(i, f) ((UriObj *)((BYTE *)(i) - offsetof(UriObj, f)))
static HRESULT uri_hs(const WCHAR *s, UINT32 n, void **out) { if (!out) return E_POINTER; *out = NULL; if (!hs_init()) return E_FAIL;
  return n ? p_create(s, n, out) : S_OK; }
static HRESULT uri_bstr_prop(UriObj *o, Uri_PROPERTY p, void **out)
{
  BSTR b = NULL; HRESULT hr; if (!out) return E_POINTER; *out = NULL;
  hr = IURI_GETPROPBSTR(o->u, p, &b); if (FAILED(hr)) return hr;
  hr = uri_hs(b ? b : L"", b ? SysStringLen(b) : 0, out); SysFreeString(b); return hr;
}
static HRESULT uri_new(IUri *u, const WCHAR *raw, void **out);
/* IUnknown / IInspectable shared by the three faces */
static HRESULT uri_qi_(UriObj *o, REFIID iid, void **out)
{
  if (!out) return E_POINTER; *out = NULL;
  if (!memcmp(iid, &U_IID_Uri, 16) || !memcmp(iid, &U_IID_IUnknown, 16) || !memcmp(iid, &U_IID_IInspectable, 16) || !memcmp(iid, &U_IID_IAgile, 16)) *out = &o->vt_uri;
  else if (!memcmp(iid, &U_IID_UriCanon, 16)) *out = &o->vt_canon;
  else if (!memcmp(iid, &U_IID_Stringable, 16)) *out = &o->vt_str;
  else return E_NOINTERFACE;
  InterlockedIncrement(&o->ref); return S_OK;
}
static ULONG uri_rl_(UriObj *o) { LONG r = InterlockedDecrement(&o->ref); if (!r) { IURI_RELEASE(o->u); free(o->raw); free(o); } return r; }
#define URI_IUNK(f, pre) \
  static HRESULT WINAPI pre##_qi(void *t, REFIID iid, void **o) { return uri_qi_(URI_OF(t, f), iid, o); } \
  static ULONG WINAPI pre##_ar(void *t) { return InterlockedIncrement(&URI_OF(t, f)->ref); } \
  static ULONG WINAPI pre##_rl(void *t) { return uri_rl_(URI_OF(t, f)); }
URI_IUNK(vt_uri, wfuur) URI_IUNK(vt_canon, wfuuc) URI_IUNK(vt_str, wfuus)
static HRESULT WINAPI uri_iids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT WINAPI uri_name(void *t, void **hs) { (void)t; return uri_hs(L"Windows.Foundation.Uri", 22, hs); }
static HRESULT WINAPI uri_trust(void *t, int *l) { (void)t; if (l) *l = 0; return S_OK; }
#define URI_PROP(name, prop) static HRESULT WINAPI wfuur_##name(void *t, void **o) { return uri_bstr_prop(URI_OF(t, vt_uri), prop, o); }
URI_PROP(absolute, Uri_PROPERTY_ABSOLUTE_URI) URI_PROP(display, Uri_PROPERTY_DISPLAY_URI) URI_PROP(domain, Uri_PROPERTY_DOMAIN)
URI_PROP(extension, Uri_PROPERTY_EXTENSION) URI_PROP(fragment, Uri_PROPERTY_FRAGMENT) URI_PROP(host, Uri_PROPERTY_HOST)
URI_PROP(password, Uri_PROPERTY_PASSWORD) URI_PROP(path, Uri_PROPERTY_PATH) URI_PROP(query, Uri_PROPERTY_QUERY)
URI_PROP(scheme, Uri_PROPERTY_SCHEME_NAME) URI_PROP(user, Uri_PROPERTY_USER_NAME)
static HRESULT WINAPI wfuur_queryparsed(void *t, void **o) { (void)t; if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT WINAPI wfuur_raw(void *t, void **o) { UriObj *u = URI_OF(t, vt_uri); return uri_hs(u->raw, (UINT32)lstrlenW(u->raw), o); }
static HRESULT WINAPI wfuur_port(void *t, INT32 *o) { DWORD p = 0; HRESULT hr; UriObj *u = URI_OF(t, vt_uri); if (!o) return E_POINTER; hr = IURI_GETPORT(u->u, &p); *o = SUCCEEDED(hr) ? (INT32)p : 0; return S_OK; }
static HRESULT WINAPI wfuur_suspicious(void *t, boolean *o) { (void)t; if (o) *o = 0; return S_OK; }
static HRESULT WINAPI wfuur_equals(void *t, void *other, boolean *o)
{
  void *a = NULL, *b = NULL; HRESULT hr; UINT32 na = 0, nb = 0; const WCHAR *sa, *sb; if (!o) return E_POINTER; *o = 0; if (!other) return S_OK;
  hr = wfuur_absolute(t, &a); if (SUCCEEDED(hr)) hr = ((HRESULT (WINAPI *)(void *, void **))(*(void ***)other)[6])(other, &b);
  if (SUCCEEDED(hr)) { sa = a ? p_raw(a, &na) : L""; sb = b ? p_raw(b, &nb) : L""; *o = (na == nb && !memcmp(sa, sb, (size_t)na * sizeof(WCHAR))); }
  uri_free_hs(a); uri_free_hs(b); return hr;
}
static HRESULT uri_combine(IUri *base, const WCHAR *rel, void **out)
{
  typedef HRESULT (WINAPI *F)(IUri *, LPCWSTR, DWORD, IUri **, DWORD_PTR); static F f; IUri *r = NULL; BSTR b = NULL; HRESULT hr;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("urlmon.dll"), "CoInternetCombineUrlEx"); if (!f) return E_NOTIMPL;
  hr = f(base, rel, 0, &r, 0); if (FAILED(hr)) return hr;
  if (SUCCEEDED(IURI_GETRAW(r, &b))) { hr = uri_new(r, b, out); SysFreeString(b); } else hr = uri_new(r, rel, out);
  IURI_RELEASE(r); return hr;
}
static HRESULT WINAPI wfuur_combine(void *t, void *rel, void **o) { const WCHAR *s = rel ? p_raw(rel, NULL) : L""; return uri_combine(URI_OF(t, vt_uri)->u, s, o); }
static HRESULT WINAPI wfuuc_canonical(void *t, void **o) { return uri_bstr_prop(URI_OF(t, vt_canon), Uri_PROPERTY_ABSOLUTE_URI, o); }
static HRESULT WINAPI wfuuc_displayiri(void *t, void **o) { return uri_bstr_prop(URI_OF(t, vt_canon), Uri_PROPERTY_DISPLAY_URI, o); }
static HRESULT WINAPI wfuus_tostring(void *t, void **o) { return uri_bstr_prop(URI_OF(t, vt_str), Uri_PROPERTY_ABSOLUTE_URI, o); }
static const void *const uri_vt_uri[] = { wfuur_qi, wfuur_ar, wfuur_rl, uri_iids, uri_name, uri_trust, wfuur_absolute, wfuur_display, wfuur_domain, wfuur_extension, wfuur_fragment,
  wfuur_host, wfuur_password, wfuur_path, wfuur_query, wfuur_queryparsed, wfuur_raw, wfuur_scheme, wfuur_user, wfuur_port, wfuur_suspicious, wfuur_equals, wfuur_combine };
static const void *const uri_vt_canon[] = { wfuuc_qi, wfuuc_ar, wfuuc_rl, uri_iids, uri_name, uri_trust, wfuuc_canonical, wfuuc_displayiri };
static const void *const uri_vt_str[] = { wfuus_qi, wfuus_ar, wfuus_rl, uri_iids, uri_name, uri_trust, wfuus_tostring };
/* takes a reference on u; out = the IUriRuntimeClass face */
static HRESULT uri_new(IUri *u, const WCHAR *raw, void **out)
{
  UriObj *o; size_t n = raw ? wcslen(raw) : 0; if (!out) return E_POINTER; *out = NULL;
  if (!(o = (UriObj *)calloc(1, sizeof *o)) || !(o->raw = (WCHAR *)malloc((n + 1) * sizeof(WCHAR)))) { free(o); return E_OUTOFMEMORY; }
  memcpy(o->raw, raw ? raw : L"", n * sizeof(WCHAR)); o->raw[n] = 0;
  o->vt_uri = uri_vt_uri; o->vt_canon = uri_vt_canon; o->vt_str = uri_vt_str; o->ref = 1; o->u = u; IURI_ADDREF(u);
  *out = &o->vt_uri; return S_OK;
}
static HRESULT uri_parse(const WCHAR *s, IUri **u)
{
  typedef HRESULT (WINAPI *F)(LPCWSTR, DWORD, DWORD_PTR, IUri **); static F f; HRESULT hr;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("urlmon.dll"), "CreateUri"); if (!f) return E_NOTIMPL;
  hr = f(s, Uri_CREATE_CANONICALIZE_ | Uri_CREATE_NO_DECODE_EXTRA_INFO_ | Uri_CREATE_ALLOW_IMPLICIT_FILE_SCHEME_, 0, u);
  return FAILED(hr) ? E_INVALIDARG : hr;                  /* Windows.Foundation.Uri reports a malformed URI as E_INVALIDARG */
}
/* ---- the factory (singleton): IActivationFactory, IUriRuntimeClassFactory, IUriEscapeStatics ---- */
static const void *uri_f_obj[3];
static HRESULT WINAPI wfuuf_qi(void *t, REFIID iid, void **o)
{
  (void)t; if (!o) return E_POINTER; *o = NULL;
  if (!memcmp(iid, &U_IID_IUnknown, 16) || !memcmp(iid, &U_IID_IInspectable, 16) || !memcmp(iid, &U_IID_IAgile, 16) || !memcmp(iid, &U_IID_ActFactory, 16)) *o = &uri_f_obj[0];
  else if (!memcmp(iid, &U_IID_UriFactory, 16)) *o = &uri_f_obj[1];
  else if (!memcmp(iid, &U_IID_UriEscape, 16)) *o = &uri_f_obj[2];
  else return E_NOINTERFACE;
  return S_OK;
}
static ULONG WINAPI wfuuf_ar(void *t) { (void)t; return 2; }
static ULONG WINAPI wfuuf_rl(void *t) { (void)t; return 1; }
static HRESULT WINAPI wfuuf_activate(void *t, void **o) { (void)t; if (o) *o = NULL; return E_NOTIMPL; }
static HRESULT WINAPI wfuuf_create(void *t, void *hs, void **o)
{ IUri *u = NULL; const WCHAR *s = hs ? p_raw(hs, NULL) : L""; HRESULT hr; (void)t; if (!o) return E_POINTER; *o = NULL;
  if (FAILED(hr = uri_parse(s, &u))) { slog("[uri] Uri('%ls') -> 0x%08lX", s, (unsigned long)hr); return hr; }
  hr = uri_new(u, s, o); IURI_RELEASE(u);
  return hr; }
static HRESULT WINAPI wfuuf_create_rel(void *t, void *base, void *rel, void **o)
{ IUri *u = NULL; HRESULT hr; (void)t; if (!o) return E_POINTER; *o = NULL;
  if (FAILED(hr = uri_parse(base ? p_raw(base, NULL) : L"", &u))) return hr; hr = uri_combine(u, rel ? p_raw(rel, NULL) : L"", o); IURI_RELEASE(u); return hr; }
static HRESULT WINAPI wfuuf_unescape(void *t, void *hs, void **o)
{ const WCHAR *s = hs ? p_raw(hs, NULL) : L""; WCHAR *b; DWORD n = (DWORD)wcslen(s) + 1; HRESULT hr; (void)t;
  typedef HRESULT (WINAPI *PUNESC)(LPWSTR, LPWSTR, DWORD *, DWORD); static PUNESC unesc;
  if (!unesc) unesc = (PUNESC)GetProcAddress(LoadLibraryA("shlwapi.dll"), "UrlUnescapeW");
  if (!(b = (WCHAR *)malloc(n * sizeof(WCHAR)))) return E_OUTOFMEMORY; lstrcpyW(b, s); if (unesc) unesc(b, NULL, NULL, 0x00100000 /* URL_UNESCAPE_INPLACE */);
  hr = uri_hs(b, (UINT32)wcslen(b), o); free(b); return hr; }
static HRESULT WINAPI wfuuf_escape(void *t, void *hs, void **o)
{ const WCHAR *s = hs ? p_raw(hs, NULL) : L""; WCHAR *b; DWORD n = (DWORD)wcslen(s) * 3 + 1; HRESULT hr; (void)t;
  if (!(b = (WCHAR *)malloc(n * sizeof(WCHAR)))) return E_OUTOFMEMORY;
  { typedef HRESULT (WINAPI *PESC)(LPCWSTR, LPWSTR, DWORD *, DWORD); static PESC esc; if (!esc) esc = (PESC)GetProcAddress(LoadLibraryA("shlwapi.dll"), "UrlEscapeW");
    hr = esc ? esc(s, b, &n, 0x00002000 | 0x00001000 /* URL_ESCAPE_SEGMENT_ONLY | URL_ESCAPE_PERCENT */) : E_NOTIMPL; }
  if (SUCCEEDED(hr)) hr = uri_hs(b, (UINT32)wcslen(b), o); free(b); return hr; }
static HRESULT WINAPI wfuuf_name(void *t, void **hs) { (void)t; return uri_hs(L"Windows.Foundation.Uri", 22, hs); }
static const void *const uri_vt_af[] = { wfuuf_qi, wfuuf_ar, wfuuf_rl, uri_iids, wfuuf_name, uri_trust, wfuuf_activate };
static const void *const uri_vt_fac[] = { wfuuf_qi, wfuuf_ar, wfuuf_rl, uri_iids, wfuuf_name, uri_trust, wfuuf_create, wfuuf_create_rel };
static const void *const uri_vt_esc[] = { wfuuf_qi, wfuuf_ar, wfuuf_rl, uri_iids, wfuuf_name, uri_trust, wfuuf_unescape, wfuuf_escape };
/* IActivationFactory* for Windows.Foundation.Uri */
static void *uri_factory(void)
{
  static LONG once;
  if (!InterlockedExchange(&once, 1)) slog("[act] Windows.Foundation.Uri -> built-in (urlmon IUri)");
  uri_f_obj[0] = uri_vt_af; uri_f_obj[1] = uri_vt_fac; uri_f_obj[2] = uri_vt_esc; return &uri_f_obj[0];
}
