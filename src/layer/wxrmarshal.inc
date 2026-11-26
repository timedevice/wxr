/* wxrmarshal.inc - marshaling: Win10 proxy/stubs registered in-process, storage items passed by value, tracing. Included by wxrsys.c. */
/* ---- marshaling trace (WinRT interfaces cross apartments inside Application.Start) ---- */
#define OLE_FWD(name) (f ? f : (f = (void *)GetProcAddress(ole32_mod(), name)))
/* Proxy/stubs for Win10-private interfaces. Win10 registers them under HKCR\Interface\{iid}\ProxyStubClsid32; Win7 has no entry ->
   standard marshaling fails with REGDB_E_IIDNOTREG (windows.storage CThreadAffineStorageQueryItemArray: folder queries, Media Player
   "Open folder"). Registered in-process instead (no registry step): CoRegisterPSClsid + the PSFactoryBuffer class object from the
   Win10 proxy DLL (g_comcls), once per apartment (class registrations are per apartment), before any marshal/unmarshal. */
static const struct { GUID iid; GUID ps; } g_w10ps[] = {
  /* IStorageQueryItemArray -> OneCoreUAPCommonProxyStub.dll */
  { {0xAC736759, 0x22C6, 0x4DBA, {0xB0, 0x9E, 0x04, 0xFD, 0x18, 0x89, 0x34, 0x8F}}, {0x95E15D0A, 0x66E6, 0x93D9, {0xC5, 0x3C, 0x76, 0xE6, 0x21, 0x9D, 0x33, 0x41}} },
};
static void ps_ensure(void) {
  static DWORD tls = TLS_OUT_OF_INDEXES; static LONG mta_done; typedef HRESULT (WINAPI *GAT)(APTTYPE *, APTTYPEQUALIFIER *);
  typedef HRESULT (WINAPI *RCO)(REFCLSID, IUnknown *, DWORD, DWORD, DWORD *); typedef HRESULT (WINAPI *RPS)(REFIID, REFCLSID);
  static GAT gat; static RCO rco; static RPS rps; APTTYPE t; APTTYPEQUALIFIER q; unsigned i;

  if (tls == TLS_OUT_OF_INDEXES) { DWORD n = TlsAlloc(); if (InterlockedCompareExchange((LONG *)&tls, (LONG)n, (LONG)TLS_OUT_OF_INDEXES) != (LONG)TLS_OUT_OF_INDEXES) TlsFree(n); }
  if (TlsGetValue(tls)) return;
  TlsSetValue(tls, (void *)1);
  if (!gat) { gat = (GAT)GetProcAddress(ole32_mod(), "CoGetApartmentType"); rco = (RCO)GetProcAddress(ole32_mod(), "CoRegisterClassObject"); rps = (RPS)GetProcAddress(ole32_mod(), "CoRegisterPSClsid"); }
  if (!gat || !rco || !rps || FAILED(gat(&t, &q))) { TlsSetValue(tls, NULL); return; }
  if (t != APTTYPE_STA && t != APTTYPE_MAINSTA && InterlockedExchange(&mta_done, 1)) return;   /* MTA / NA: one registration serves them */
  for (i = 0; i < sizeof g_w10ps / sizeof g_w10ps[0]; i++) {
    static const GUID iunk = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *cf = NULL; DWORD cookie = 0; HRESULT h1, h2 = E_FAIL, h3;
    h1 = com_inproc(&g_w10ps[i].ps, NULL, &iunk, (void **)&cf, 1);
    if (SUCCEEDED(h1) && cf) { h2 = rco(&g_w10ps[i].ps, cf, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &cookie); cf->lpVtbl->Release(cf); }
    h3 = rps(&g_w10ps[i].iid, &g_w10ps[i].ps);
    { char a[48]; guid_str(&g_w10ps[i].iid, a); slog("[marshal] Win10 proxy/stub for %s (apt %d): class 0x%08lX, register 0x%08lX, PSClsid 0x%08lX", a, (int)t, (unsigned long)h1, (unsigned long)h2, (unsigned long)h3); } }
}
/* Storage items by value. Win10's CStorageFile/CStorageFolder are apartment-bound and marshal through a lightweight handler that rebuilds
   the item in the receiving apartment. Here they marshal through the FTM (CoGetStdMarshalEx), and the unmarshal shortcut below handed the
   original object to the other thread, which then refused every call with RPC_E_WRONG_THREAD (Media Player: Properties 0x8001010E, the
   access list could not read the path, no tags). So: at marshal time (owning thread) the item's path is recorded by object identity;
   where the shortcut unmarshals, a fresh item for that path is made on the receiving thread (windows.storage
   CreateStorageItemFromPath_FullTrustCaller(path, flags 0, riid, out) - synchronous). */
static struct { void *ident; WCHAR *path; } g_sip[128]; static int g_sip_next; static SRWLOCK g_sip_lk = SRWLOCK_INIT;
static const GUID SIP_IID_IStorageItem = {0x4207A996, 0xCA2F, 0x42F7, {0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30}};
static FARPROC sip_fn(const char *n) { HMODULE m = GetModuleHandleA("wxrsys.dll"); FARPROC f = m ? GetProcAddress(m, n) : NULL; if (!f && (m = GetModuleHandleA("combase.dll"))) f = GetProcAddress(m, n); return f; }
static void *sip_ident(IUnknown *u) { static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *i = NULL; if (FAILED(u->lpVtbl->QueryInterface(u, &iu, (void **)&i)) || !i) return NULL; i->lpVtbl->Release(i); return i; }
static void sip_record(IUnknown *unk) {   /* owning thread: the item's path, if it is a windows.storage item */
  void **vt = *(void ***)unk; HMODULE mod = NULL; WCHAR mn[MAX_PATH] = L""; void *it = NULL, *hs = NULL, *id; int k;

  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[0], &mod) || !GetModuleFileNameW(mod, mn, MAX_PATH) || !wcsstr(mn, L"indows.storage.dll")) return;
  if (FAILED(unk->lpVtbl->QueryInterface(unk, &SIP_IID_IStorageItem, &it)) || !it) return;
  if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)it)[12])(it, &hs)) && hs) {   /* IStorageItem.get_Path */
    typedef const WCHAR *(WINAPI *PRAW)(void *, UINT32 *); typedef HRESULT (WINAPI *PDEL)(void *); PRAW raw = (PRAW)sip_fn("WindowsGetStringRawBuffer"); PDEL del = (PDEL)sip_fn("WindowsDeleteString");
    const WCHAR *p = raw ? raw(hs, NULL) : NULL;
    if (p && *p && (id = sip_ident(unk)) != NULL) { WCHAR *d = (WCHAR *)malloc((lstrlenW(p) + 1) * sizeof(WCHAR));
      if (d) { lstrcpyW(d, p); AcquireSRWLockExclusive(&g_sip_lk);
        for (k = 0; k < 128 && g_sip[k].ident != id; k++);
        if (k == 128) { k = g_sip_next; g_sip_next = (g_sip_next + 1) % 128; }
        free(g_sip[k].path); g_sip[k].ident = id; g_sip[k].path = d; ReleaseSRWLockExclusive(&g_sip_lk); } }
    if (del) del(hs); }
  ((IUnknown *)it)->lpVtbl->Release((IUnknown *)it); }
static void sip_rebuild(REFIID iid, void **ppv) {   /* receiving thread: replace the original object by a fresh item for its path */
  typedef HRESULT (WINAPI *PCSI)(const WCHAR *, int, REFIID, void **); static PCSI csi; static int tried; void *id, *n = NULL; WCHAR p[MAX_PATH * 2] = L""; int k; HRESULT hr;
  if (!ppv || !*ppv || !(id = sip_ident((IUnknown *)*ppv))) return;
  AcquireSRWLockShared(&g_sip_lk); for (k = 0; k < 128; k++) if (g_sip[k].ident == id && g_sip[k].path) { lstrcpynW(p, g_sip[k].path, MAX_PATH * 2); break; } ReleaseSRWLockShared(&g_sip_lk);
  if (!p[0]) return;
  if (!tried) { HMODULE ws = GetModuleHandleW(L"windows.storage.dll"); csi = ws ? (PCSI)GetProcAddress(ws, "CreateStorageItemFromPath_FullTrustCaller") : NULL; tried = 1; }
  if (!csi) return;
  hr = csi(p, 0, iid, &n);
  { static LONG c; if (InterlockedIncrement(&c) <= 40 || FAILED(hr)) slog("[marshal]   storage item %ls rebuilt on thread %lu -> 0x%08lX", p, GetCurrentThreadId(), (unsigned long)hr); }
  if (SUCCEEDED(hr) && n) { ((IUnknown *)*ppv)->lpVtbl->Release((IUnknown *)*ppv); *ppv = n; } }
__declspec(dllexport) HRESULT WINAPI CoMarshalInterface(LPSTREAM strm, REFIID iid, IUnknown *unk, DWORD dctx, void *dptr, DWORD flags) {
  typedef HRESULT (WINAPI *F)(LPSTREAM, REFIID, IUnknown *, DWORD, void *, DWORD); static F f; HRESULT hr; char a[48]; OLE_FWD("CoMarshalInterface");
  ps_ensure(); hr = f(strm, iid, unk, dctx, dptr, flags); guid_str(iid, a); slog("[marshal] CoMarshalInterface(iid=%s, dctx=%lu, flags=%lx) -> 0x%08lX", a, dctx, flags, (unsigned long)hr);
  if (SUCCEEDED(hr) && unk) sip_record(unk);
  { static const GUID iim = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *m = NULL; GUID cls; char c[48], g[48];   /* who marshals custom: its unmarshal class */
    if (unk && SUCCEEDED(unk->lpVtbl->QueryInterface(unk, &iim, (void **)&m)) && m) {
      if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, REFIID, void *, DWORD, void *, DWORD, GUID *))(*(void ***)m)[3])(m, iid, unk, dctx, dptr, flags, &cls))) {
        void **vt = *(void ***)unk; HMODULE mod = NULL; WCHAR mn[MAX_PATH] = L"?"; guid_str(&cls, c); guid_str(iid, g);
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[0], &mod)) GetModuleFileNameW(mod, mn, MAX_PATH);
        slog("[marshal]   object vtable %ls+0x%Ix: IMarshal unmarshal class %s", wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, (ULONG_PTR)vt - (ULONG_PTR)mod, c); }
      m->lpVtbl->Release(m); } }
  return hr; }
__declspec(dllexport) HRESULT WINAPI CoUnmarshalInterface(LPSTREAM strm, REFIID iid, void **ppv) {
  typedef HRESULT (WINAPI *F)(LPSTREAM, REFIID, void **); static F f; HRESULT hr; char a[48]; OLE_FWD("CoUnmarshalInterface");
  LARGE_INTEGER z; ULARGE_INTEGER pos; BYTE hdr[160] = {0}; ULONG got = 0; z.QuadPart = 0;
  if (strm && SUCCEEDED(strm->lpVtbl->Seek(strm, z, STREAM_SEEK_CUR, &pos))) { LARGE_INTEGER back; strm->lpVtbl->Read(strm, hdr, sizeof hdr, &got); back.QuadPart = (LONGLONG)pos.QuadPart; strm->lpVtbl->Seek(strm, back, STREAM_SEEK_SET, NULL); }
  /* a bare FTM payload (mshlflags <= 4, pointer, process GUID) where an OBJREF is expected: Win10 lightweight handlers ({BE88F957}) unmarshal
     their inner object with CoUnmarshalInterface, but the marshal side wrote it with our CoGetStdMarshalEx (= FTM) */
  if (got >= 24 && *(DWORD *)hdr != 0x574F454D && *(DWORD *)hdr <= 4) { static const GUID iim0 = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *ftm = NULL, *m0 = NULL; HRESULT h0 = E_FAIL;
    if (SUCCEEDED(CoCreateFreeThreadedMarshaler(NULL, &ftm)) && ftm) { if (SUCCEEDED(ftm->lpVtbl->QueryInterface(ftm, &iim0, (void **)&m0)) && m0) {
        h0 = ((HRESULT (STDMETHODCALLTYPE *)(void *, LPSTREAM, REFIID, void **))(*(void ***)m0)[6])(m0, strm, iid, ppv); m0->lpVtbl->Release(m0); } ftm->lpVtbl->Release(ftm); }
    guid_str(iid, a); slog("[marshal] CoUnmarshalInterface(iid=%s): bare FTM payload -> 0x%08lX", a, (unsigned long)h0); return h0; }
  /* custom OBJREF naming a shell class (CLSID_ShellItem ...): Win10 shell items marshal by value and name their CLSID as the unmarshaler.
     Win7 COM would create that class from Win7's registry (shell32) and hand Win10 windows.storage a Win7 shell item (apartment-bound:
     RPC_E_WRONG_THREAD in GetThumbnailAsync, no BHID_RandomAccessStream). The unmarshaler comes from Win10 windows.storage instead. */
  if (got >= 48 && *(DWORD *)hdr == 0x574F454D && *(DWORD *)(hdr + 4) == 4) {
    static const GUID iim1 = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *m1 = NULL;
    if (SUCCEEDED(com_storage_first((GUID *)(hdr + 24), NULL, &iim1, (void **)&m1)) && m1) { LARGE_INTEGER at; HRESULT h1; char c1[48];
      at.QuadPart = (LONGLONG)pos.QuadPart + 48; strm->lpVtbl->Seek(strm, at, STREAM_SEEK_SET, NULL);
      h1 = ((HRESULT (STDMETHODCALLTYPE *)(void *, LPSTREAM, REFIID, void **))(*(void ***)m1)[6])(m1, strm, iid, ppv); m1->lpVtbl->Release(m1);
      guid_str((GUID *)(hdr + 24), c1); { static LONG k; if (InterlockedIncrement(&k) <= 10 || FAILED(h1)) slog("[marshal] CoUnmarshalInterface: shell class %s unmarshaled by Win10 windows.storage -> 0x%08lX", c1, (unsigned long)h1); }
      if (SUCCEEDED(h1)) return h1;
      { LARGE_INTEGER back; back.QuadPart = (LONGLONG)pos.QuadPart; strm->lpVtbl->Seek(strm, back, STREAM_SEEK_SET, NULL); } } }
  ps_ensure(); hr = f(strm, iid, ppv); guid_str(iid, a); slog("[marshal] CoUnmarshalInterface(iid=%s) -> 0x%08lX", a, (unsigned long)hr);
  if (FAILED(hr) && got >= 40) { char c[48], o[48]; guid_str((GUID *)(hdr + 8), o); guid_str((GUID *)(hdr + 24), c);   /* OBJREF: 'MEOW', flags, iid, then (custom) clsid */
    slog("[marshal]   OBJREF sig 0x%08lX flags %lu iid %s next %s", *(DWORD *)hdr, *(DWORD *)(hdr + 4), o, c);
    { char hx[400]; ULONG k, n = got < 128 ? got : 128; for (k = 0; k < n; k++) sprintf(hx + k * 3, "%02X ", hdr[k]); hx[n * 3] = 0; slog("[marshal]   bytes @%llu: %s", (unsigned long long)pos.QuadPart, hx); }
    /* OBJREF_CUSTOM whose unmarshaler only Win10 registers (windows.storage's StorageItem marshaler {BE88F957}: a picked file crossing to a
       background thread failed with REGDB_E_CLASSNOTREG -> E_NOINTERFACE fail-fast in the app). Create it from our class table and hand it
       the custom data: header = sig, flags, iid, clsid, cbExtension, size (48 bytes). */
    if (hr == REGDB_E_CLASSNOTREG && *(DWORD *)hdr == 0x574F454D && *(DWORD *)(hdr + 4) == 4) {
      static const GUID iim = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *m = NULL; HRESULT h2 = E_FAIL;
      /* Win10 lightweight handlers (windows.storage CStorageFile) marshal through CoGetStdMarshalEx, which we answer with the FTM: the custom
         payload then starts with FTM data (mshlflags, pointer, process GUID) - in this process the FTM unmarshals it to the original object. */
      if (got >= 72 && *(DWORD *)(hdr + 48) <= 4) { IUnknown *ftm = NULL;
        if (SUCCEEDED(CoCreateFreeThreadedMarshaler(NULL, &ftm)) && ftm) {
          if (SUCCEEDED(ftm->lpVtbl->QueryInterface(ftm, &iim, (void **)&m)) && m) { LARGE_INTEGER at; at.QuadPart = (LONGLONG)pos.QuadPart + 48; strm->lpVtbl->Seek(strm, at, STREAM_SEEK_SET, NULL);
            h2 = ((HRESULT (STDMETHODCALLTYPE *)(void *, LPSTREAM, REFIID, void **))(*(void ***)m)[6])(m, strm, iid, ppv); m->lpVtbl->Release(m); m = NULL; }
          ftm->lpVtbl->Release(ftm); }
        slog("[marshal]   custom OBJREF with FTM payload -> FTM unmarshal 0x%08lX", (unsigned long)h2); if (SUCCEEDED(h2)) { sip_rebuild(iid, ppv); return h2; } }
      h2 = com_inproc((GUID *)(hdr + 24), NULL, &iim, (void **)&m, 0);
      if (SUCCEEDED(h2) && m) { LARGE_INTEGER at; at.QuadPart = (LONGLONG)pos.QuadPart + 48; strm->lpVtbl->Seek(strm, at, STREAM_SEEK_SET, NULL);
        h2 = ((HRESULT (STDMETHODCALLTYPE *)(void *, LPSTREAM, REFIID, void **))(*(void ***)m)[6])(m, strm, iid, ppv); m->lpVtbl->Release(m); }
      slog("[marshal]   custom unmarshal via Win10 class %s -> 0x%08lX", c, (unsigned long)h2); if (SUCCEEDED(h2)) hr = h2; } }
  return hr; }
__declspec(dllexport) HRESULT WINAPI CoGetMarshalSizeMax(ULONG *sz, REFIID iid, IUnknown *unk, DWORD dctx, void *dptr, DWORD flags) {
  typedef HRESULT (WINAPI *F)(ULONG *, REFIID, IUnknown *, DWORD, void *, DWORD); static F f; HRESULT hr; char a[48]; OLE_FWD("CoGetMarshalSizeMax");
  hr = f(sz, iid, unk, dctx, dptr, flags); guid_str(iid, a); slog("[marshal] CoGetMarshalSizeMax(iid=%s) -> 0x%08lX", a, (unsigned long)hr); return hr; }
__declspec(dllexport) HRESULT WINAPI CoGetInterfaceAndReleaseStream(LPSTREAM strm, REFIID iid, void **ppv) {
  typedef HRESULT (WINAPI *F)(LPSTREAM, REFIID, void **); static F f; HRESULT hr; char a[48]; OLE_FWD("CoGetInterfaceAndReleaseStream");
  if (strm) {   /* through our CoUnmarshalInterface (shell-class / FTM fallbacks), then the stream is released */
    LARGE_INTEGER z; z.QuadPart = 0; strm->lpVtbl->Seek(strm, z, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(strm, iid, ppv); strm->lpVtbl->Release(strm);
    guid_str(iid, a); slog("[marshal] CoGetInterfaceAndReleaseStream(iid=%s) via CoUnmarshalInterface -> 0x%08lX", a, (unsigned long)hr); return hr; }
  ps_ensure(); hr = f(strm, iid, ppv); guid_str(iid, a); slog("[marshal] CoGetInterfaceAndReleaseStream(iid=%s) -> 0x%08lX", a, (unsigned long)hr); return hr; }
__declspec(dllexport) HRESULT WINAPI CoMarshalInterThreadInterfaceInStream(REFIID iid, IUnknown *unk, LPSTREAM *strm) {
  typedef HRESULT (WINAPI *F)(REFIID, IUnknown *, LPSTREAM *); static F f; HRESULT hr; char a[48]; OLE_FWD("CoMarshalInterThreadInterfaceInStream");
  ps_ensure(); hr = f(iid, unk, strm); guid_str(iid, a); slog("[marshal] CoMarshalInterThreadInterfaceInStream(iid=%s) -> 0x%08lX", a, (unsigned long)hr);
  /* Win7 ole32 refuses RPC_E_WRONG_THREAD for objects that marshal themselves (Win10 CShellItem: CMarshalByValue) once they were seen in
     another apartment; Win10 asks the object. Retry: an OBJREF_CUSTOM written from the object's own IMarshal (in-process context). */
  if (hr == RPC_E_WRONG_THREAD && unk && strm) {
    static const GUID iim = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; IUnknown *m = NULL; IStream *s = NULL; GUID cls; HRESULT h2 = E_FAIL; ULONG sz = 0;
    if (SUCCEEDED(unk->lpVtbl->QueryInterface(unk, &iim, (void **)&m)) && m) {
      typedef HRESULT (STDMETHODCALLTYPE *GUC)(void *, REFIID, void *, DWORD, void *, DWORD, GUID *); typedef HRESULT (STDMETHODCALLTYPE *GMS)(void *, REFIID, void *, DWORD, void *, DWORD, ULONG *);
      typedef HRESULT (STDMETHODCALLTYPE *MI)(void *, IStream *, REFIID, void *, DWORD, void *, DWORD);
      if (SUCCEEDED(h2 = ((GUC)(*(void ***)m)[3])(m, iid, unk, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL, &cls)) && SUCCEEDED(h2 = ((GMS)(*(void ***)m)[4])(m, iid, unk, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL, &sz)) &&
          SUCCEEDED(h2 = CreateStreamOnHGlobal(NULL, TRUE, &s))) {
        DWORD hdr2[2] = { 0x574F454D, 4 }, ext[2] = { 0, 0 }; LARGE_INTEGER z, at; ULARGE_INTEGER end; z.QuadPart = 0;
        s->lpVtbl->Write(s, hdr2, 8, NULL); s->lpVtbl->Write(s, iid, 16, NULL); s->lpVtbl->Write(s, &cls, 16, NULL); s->lpVtbl->Write(s, ext, 8, NULL);   /* size patched below */
        if (SUCCEEDED(h2 = ((MI)(*(void ***)m)[5])(m, s, iid, unk, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL))) {
          s->lpVtbl->Seek(s, z, STREAM_SEEK_CUR, &end); ext[1] = (DWORD)(end.QuadPart - 48); at.QuadPart = 40; s->lpVtbl->Seek(s, at, STREAM_SEEK_SET, NULL); s->lpVtbl->Write(s, ext, 8, NULL);
          s->lpVtbl->Seek(s, z, STREAM_SEEK_SET, NULL); if (*strm) (*strm)->lpVtbl->Release(*strm); *strm = s; s = NULL; hr = S_OK; } }
      if (s) s->lpVtbl->Release(s); m->lpVtbl->Release(m); }
    slog("[marshal]   self-marshal retry (object IMarshal, OBJREF_CUSTOM) -> 0x%08lX (size %lu, stage %s)", (unsigned long)h2, sz, sz ? "MarshalInterface" : "GetUnmarshalClass/GetMarshalSizeMax"); if (SUCCEEDED(hr)) return hr; }
  if (FAILED(hr) && unk) { void **vt = *(void ***)unk; HMODULE mod = NULL; WCHAR mn[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[0], &mod)) GetModuleFileNameW(mod, mn, MAX_PATH);
    slog("[marshal]   failed object %p: vtable %ls+0x%Ix", (void *)unk, wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, (ULONG_PTR)vt - (ULONG_PTR)mod); log_callers("[marshal]   failed marshal caller"); }
  return hr; }
__declspec(dllexport) HRESULT WINAPI CoGetStandardMarshal(REFIID iid, IUnknown *unk, DWORD dctx, void *dptr, DWORD flags, LPMARSHAL *pm) {
  typedef HRESULT (WINAPI *F)(REFIID, IUnknown *, DWORD, void *, DWORD, LPMARSHAL *); static F f; HRESULT hr; char a[48]; OLE_FWD("CoGetStandardMarshal");
  hr = f(iid, unk, dctx, dptr, flags, pm); guid_str(iid, a); slog("[marshal] CoGetStandardMarshal(iid=%s) -> 0x%08lX", a, (unsigned long)hr); return hr; }
/* CoGetStdMarshalEx(outer, smexflags, &inner): WinRT objects marshal through the standard marshaler. Without Win10's proxy/stub registry
   entries a free-threaded marshaler (in-process pointers) is the practical replacement (same trick as wine-uwp). */
__declspec(dllexport) HRESULT WINAPI CoGetStdMarshalEx(IUnknown *outer, DWORD flags, IUnknown **inner) {
  HRESULT hr; (void)flags; if (!inner) return E_POINTER;
  hr = CoCreateFreeThreadedMarshaler(outer, inner); slog("[marshal] CoGetStdMarshalEx -> FTM 0x%08lX", (unsigned long)hr); return hr; }
