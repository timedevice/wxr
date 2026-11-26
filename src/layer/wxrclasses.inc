/* wxrclasses.inc - Win10 COM classes served from the image, Media Foundation local registration, shell classes. Included by wxrsys.c. */
/* ---- Process State Manager stand-ins (the PSM service does not exist): in-proc IPsmBroker + PsmQueryBackgroundActivationType ---- */
static HRESULT WINAPI psm_noop(void *t) { (void)t; return S_OK; }
static HRESULT WINAPI psm_qi(void *t, REFIID iid, void **o) {
  static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, ip = {0x33C255C3, 0x1A86, 0x45FC, {0x94, 0x98, 0xF6, 0x0D, 0x8F, 0x56, 0xB9, 0xD3}};
  if (!memcmp(iid, &iu, sizeof iu) || !memcmp(iid, &ip, sizeof ip)) { *o = t; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI psm_ref(void *t) { (void)t; return 2; }
static const void *const psm_vtbl[] = { psm_qi, psm_ref, psm_ref, psm_noop, psm_noop, psm_noop, psm_noop, psm_noop, psm_noop, psm_noop, psm_noop };
static const void *g_psm_obj = psm_vtbl;
__declspec(dllexport) HRESULT WINAPI PsmQueryBackgroundActivationType(HANDLE h, DWORD *type) { (void)h; if (type) *type = 0; return S_OK; }
/* CLSIDs that live in the image's DLLs (the real registry entries are HKCR\CLSID\...\InprocServer32 on Win10) */
/* prefer = 1: served from the Win10 DLL even when Win7 registers the class (Win7's implementation behaves differently); only Win10 callers
   come through here, Win7's own components keep Win7's class */
static const struct { GUID clsid; const WCHAR *dll; int prefer; } g_comcls[] = {
  { {0x96C7A5EF, 0x0E2C, 0x46D7, {0x9B, 0xC1, 0x64, 0x45, 0xC2, 0x44, 0x4D, 0x7A}}, L"threadpoolwinrt.dll" },
  { {0xDBCE7E40, 0x7345, 0x439D, {0xB1, 0x2C, 0x11, 0x4A, 0x11, 0x81, 0x9A, 0x09}}, L"MrmCoreR.dll" },   /* CLSID_MrtResourceManager */
  { {0x5842A140, 0xFF9F, 0x4166, {0x8F, 0x5C, 0x62, 0xF5, 0xB7, 0xB0, 0xC7, 0x81}}, L"AppxPackaging.dll" },   /* CLSID_AppxFactory: App Installer reads .appx/.msix with it */
  { {0x378E0446, 0x5384, 0x43B7, {0x88, 0x77, 0xE7, 0xDB, 0xDD, 0x88, 0x34, 0x46}}, L"AppxPackaging.dll" },   /* CLSID_AppxBundleFactory: .appxbundle/.msixbundle */
  { {0x54E211B6, 0x3650, 0x4F75, {0x83, 0x34, 0xFA, 0x35, 0x95, 0x98, 0xE1, 0xC5}}, L"directmanipulation.dll" },   /* CLSID_DirectManipulationManager */
  { {0x99793286, 0x77CC, 0x4B57, {0x96, 0xDB, 0x3B, 0x35, 0x4F, 0x6F, 0x9F, 0xB5}}, L"directmanipulation.dll" },   /* CLSID_DirectManipulationSharedManager */
  { {0x79DEA627, 0xA08A, 0x43AC, {0x8E, 0xF5, 0x69, 0x00, 0xB9, 0x29, 0x91, 0x26}}, L"directmanipulation.dll" },   /* CLSID_DCompManipulationCompositor */
  { {0xB44392DA, 0x499B, 0x446B, {0xA4, 0xCB, 0x00, 0x5F, 0xEA, 0xD0, 0xE6, 0xD5}}, L"mfmediaengine.dll" },   /* CLSID_MFMediaEngineClassFactory (Win8+) */
  { {0xC2B94E6D, 0x6EBD, 0x44EE, {0xAA, 0xC1, 0xEB, 0x80, 0x9C, 0x98, 0x46, 0x99}}, L"mfcore.dll" },   /* CLSID_MFSeamlessAudioEffect (Media Engine audio effect; Win10-only: class not registered -> MF_E_INVALIDREQUEST, 0xC00D36B2) */
  { {0x0E41CFB8, 0x0506, 0x40F4, {0xA5, 0x16, 0x77, 0xCC, 0x23, 0x64, 0x2D, 0x91}}, L"msflacdecoder.dll" },   /* FLAC byte stream handler (mf_register_local) */
  { {0x6B0B3E6B, 0xA2C5, 0x4514, {0x80, 0x55, 0xAF, 0xE8, 0xA9, 0x52, 0x42, 0xD9}}, L"msflacdecoder.dll" },   /* FLAC decoder MFT (mf_register_local) */
  { {0xA82E50BA, 0x8E92, 0x41EB, {0x9D, 0xF2, 0x43, 0x3F, 0x50, 0xEC, 0x29, 0x93}}, L"mfsrcsnk.dll", 1 },   /* MP3 byte stream handler (mf_register_local; Win7's is in its mf.dll) */
  { {0xBBEEA841, 0x0A63, 0x4F52, {0xA7, 0xAB, 0xA9, 0xB3, 0xA8, 0x4E, 0xD3, 0x8A}}, L"mp3dmod.dll", 1 },    /* MP3 decoder MFT (mf_register_local; Win7 has its own mp3dmod) */
  { {0xC120DE80, 0xFDE4, 0x49F5, {0xA7, 0x13, 0xE9, 0x02, 0xEF, 0x06, 0x2B, 0x8A}}, L"mfsrcsnk.dll", 1 },   /* "MF MP3 Property Handler" (tags; Win7 registers its own in mf.dll) */
  { {0xC10E23A2, 0xBFF0, 0x4113, {0xBC, 0xE8, 0xC9, 0x1B, 0xE7, 0x54, 0xE2, 0x81}}, L"msflacdecoder.dll" },   /* "MF FLAC Media Property Handler": Win10's FLAC tag reader (wxrprop.inc) */
  { {0x934D4698, 0x6A59, 0x48F8, {0x9F, 0x29, 0x9F, 0xB3, 0x06, 0x70, 0x32, 0x0E}}, L"StructuredQuery.dll", 1 },
  { {0xBE88F957, 0x42CC, 0x4DA7, {0x92, 0xCF, 0x9B, 0xC3, 0x5C, 0x5D, 0x5E, 0xE2}}, L"windows.storage.dll" },
  /* ms-appx: URI -> path resolver (windows.storage SHGetPathFromMsUri, iid {428BB582}): StorageFile.GetFileFromApplicationUriAsync. Not in
     Win7's registry -> CLASSNOTREG; the WinUI 3 Gallery's startup (FileLoader.LoadText of its control list) threw and never activated its window */
  { {0xA8CA4495, 0x65AB, 0x4333, {0x9F, 0xF9, 0x4F, 0xEB, 0x6F, 0xBF, 0x51, 0x69}}, L"windows.storage.dll" },
  /* Memory Mapped Cache Mgr (property schema cache): Win10 propsys asks it for a Win10 interface; Win7's (Win7 propsys) has none ->
     Win10 propsys has no schema (PSGetNameFromPropertyKey E_NOINTERFACE even for System.ItemNameDisplay). */
  { {0x1F486A52, 0x3CB1, 0x48FD, {0x8F, 0x50, 0xB8, 0xDC, 0x30, 0x0D, 0x9F, 0x9D}}, L"propsys.dll", 1 },
  /* CLSID_PropertyKeyStore (Win10-only; PSCreatePropertyKeyStore / windows.storage CItemCollection::_CreateDefProjectionKeyStore):
     not in Win7's registry -> REGDB_E_CLASSNOTREG -> folder queries failed */
  { {0x7EC8C678, 0xDB97, 0x4C70, {0x8C, 0x09, 0x06, 0x5B, 0xA4, 0xAB, 0x60, 0xE7}}, L"propsys.dll" },
  /* "Property System Both Class Factory" (ICreateObject): Win10 propsys CreateObjectWithCachedFactory creates its classes through it
     (MTA path). Win7 registers it to Win7 propsys, which does not know the Win10 classes (PropertyKeyStore) -> CLASSNOTREG */
  { {0x76765B11, 0x3F95, 0x4AF2, {0xAC, 0x9D, 0xEA, 0x55, 0xD8, 0x99, 0x4F, 0x1A}}, L"propsys.dll", 1 },
  /* PSFactoryBuffer of OneCoreUAPCommonProxyStub.dll (Win10 proxy/stubs; registered in-process by ps_ensure) */
  { {0x95E15D0A, 0x66E6, 0x93D9, {0xC5, 0x3C, 0x76, 0xE6, 0x21, 0x9D, 0x33, 0x41}}, L"OneCoreUAPCommonProxyStub.dll" },   /* StorageItem custom marshaler (CoUnmarshalInterface fallback) */   /* ConditionFactory (IConditionFactory2): Win7's gave E_INVALIDARG to Windows.Storage's StorageFile checks */
};
/* outer != NULL: CoCreateInstance; outer == NULL and called from CoGetClassObject: returns the class factory (iid), else creates an instance */
/* Win10 Media Foundation codecs Win7's registry knows nothing about: registered for this process only, in Win10 mfplat (the one Windows.Media
   and the Media Engine use), the first time the Media Engine is created. FLAC: byte stream handler + decoder MFT, both in msflacdecoder.dll
   (CLSIDs mapped in g_comcls). Activation objects come from MFCreateTransformActivate with MFT_TRANSFORM_CLSID_Attribute set. */
/* IMFActivate for a plain COM class (the FLAC byte stream handler): IMFAttributes forwarded to a real attribute store (Win10 mfplat
   MFCreateAttributes), ActivateObject = CoCreateInstance through our hook (g_comcls maps the CLSID to msflacdecoder.dll).
   MFCreateTransformActivate's object only hands out IMFTransform (E_NOINTERFACE for IMFByteStreamHandler). */
typedef struct ClsAct { const void *const *vt; LONG ref; IUnknown *attrs; GUID clsid; } ClsAct;
#define CA_ATTR(a) (*(void ***)((ClsAct *)(a))->attrs)
#define CA_FWD(n, slot) static HRESULT STDMETHODCALLTYPE ca_##n(ClsAct *a, void *p1, void *p2, void *p3, void *p4) { \
    return ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *, void *, void *))CA_ATTR(a)[slot])(a->attrs, p1, p2, p3, p4); }
CA_FWD(3, 3) CA_FWD(4, 4) CA_FWD(5, 5) CA_FWD(6, 6) CA_FWD(7, 7) CA_FWD(8, 8) CA_FWD(9, 9) CA_FWD(10, 10) CA_FWD(11, 11) CA_FWD(12, 12)
CA_FWD(13, 13) CA_FWD(14, 14) CA_FWD(15, 15) CA_FWD(16, 16) CA_FWD(17, 17) CA_FWD(18, 18) CA_FWD(19, 19) CA_FWD(20, 20) CA_FWD(21, 21)
CA_FWD(22, 22) CA_FWD(24, 24) CA_FWD(25, 25) CA_FWD(26, 26) CA_FWD(27, 27) CA_FWD(28, 28) CA_FWD(29, 29) CA_FWD(30, 30) CA_FWD(31, 31) CA_FWD(32, 32)
static HRESULT STDMETHODCALLTYPE ca_23(ClsAct *a, REFGUID k, double v) { return ((HRESULT (STDMETHODCALLTYPE *)(void *, REFGUID, double))CA_ATTR(a)[23])(a->attrs, k, v); }   /* SetDouble */
static HRESULT STDMETHODCALLTYPE ca_qi(ClsAct *a, REFIID iid, void **o) {
  static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}, iattr = {0x2CD2D921, 0xC447, 0x44A7, {0xA1, 0x3C, 0x4A, 0xDA, 0xBF, 0xC2, 0x47, 0xE3}}, iact = {0x7FEE9E9A, 0x4A89, 0x47A6, {0x89, 0x9C, 0xB6, 0xA5, 0x3A, 0x70, 0xFB, 0x67}};
  if (IsEqualGUID(iid, &iu) || IsEqualGUID(iid, &iattr) || IsEqualGUID(iid, &iact)) { *o = a; InterlockedIncrement(&a->ref); return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE ca_ar(ClsAct *a) { return (ULONG)InterlockedIncrement(&a->ref); }
static ULONG STDMETHODCALLTYPE ca_rl(ClsAct *a) { LONG n = InterlockedDecrement(&a->ref); if (!n) { a->attrs->lpVtbl->Release(a->attrs); HeapFree(GetProcessHeap(), 0, a); } return (ULONG)n; }
/* msflacdecoder's FLAC media source hard-codes 1.0 as its fastest rate (GetFastestRate stores 1.0f, IsRateSupported refuses anything above
   with MF_E_UNSUPPORTED_RATE; its SetRate asks IsRateSupported through the vtable): Media Player's 1.5x/2x played at 1x. It is a pull
   source that decodes on request, and the audio renderer time-stretches up to 8x, so forward non-thinned rates up to 8x are allowed here.
   Patched in the shared IMFRateSupport vtable of the first FLAC source (reached through the FLAC byte stream handler's EndCreateObject). */
typedef HRESULT (STDMETHODCALLTYPE *PBSHEND)(void *, void *, int *, IUnknown **);
static PBSHEND g_bsh_end;
#define SRC_MAX_RATE 8.0f
typedef HRESULT (STDMETHODCALLTYPE *PRSFAST)(void *, int, BOOL, float *);
typedef HRESULT (STDMETHODCALLTYPE *PRSOK)(void *, BOOL, float, float *);
static PRSFAST g_rs_fast; static PRSOK g_rs_ok;
static HRESULT STDMETHODCALLTYPE rs_fast(void *t, int dir, BOOL thin, float *r) { HRESULT hr = g_rs_fast(t, dir, thin, r);
  if (SUCCEEDED(hr) && dir == 0 && r && *r > 0 && *r < SRC_MAX_RATE) *r = SRC_MAX_RATE; return hr; }
static HRESULT STDMETHODCALLTYPE rs_ok(void *t, BOOL thin, float rate, float *nearest) {
  if (!thin && rate > 1.0f && rate <= SRC_MAX_RATE) { static LONG k; if (nearest) *nearest = rate; if (InterlockedIncrement(&k) <= 10) slog("[mf] source IsRateSupported(%d/1000) -> allowed (FLAC source caps at 1x)", (int)(rate * 1000)); return S_OK; }
  return g_rs_ok(t, thin, rate, nearest); }
static void flac_lift_rate(IUnknown *src) {
  static const GUID igs = {0xFA993888, 0x4383, 0x415A, {0xA9, 0x30, 0xDD, 0x47, 0x2A, 0x8C, 0xF6, 0xF7}};          /* IMFGetService */
  static const GUID svc = {0x866FA297, 0xB802, 0x4BF8, {0x9D, 0xC9, 0x5E, 0x3B, 0x6A, 0x9F, 0x53, 0xC9}};          /* MF_RATE_CONTROL_SERVICE */
  static const GUID irs = {0x0A9CCDBC, 0xD797, 0x4563, {0x96, 0x67, 0x94, 0xEC, 0x5D, 0x79, 0x29, 0x2D}};          /* IMFRateSupport */
  IUnknown *gs = NULL, *rs = NULL;
  if (g_rs_fast || FAILED(src->lpVtbl->QueryInterface(src, &igs, (void **)&gs)) || !gs) return;
  if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, REFGUID, REFIID, void **))(*(void ***)gs)[3])(gs, &svc, &irs, (void **)&rs)) && rs) {
    void **vt = *(void ***)rs;
    vt_set(vt, 4, (void *)rs_fast, (void **)&g_rs_fast); vt_set(vt, 5, (void *)rs_ok, (void **)&g_rs_ok); slog("[mf] FLAC source rate cap lifted (forward up to 8x)");
    rs->lpVtbl->Release(rs); }
  gs->lpVtbl->Release(gs); }
static HRESULT STDMETHODCALLTYPE bsh_end(void *h, void *res, int *type, IUnknown **obj) {
  HRESULT hr = g_bsh_end(h, res, type, obj); if (SUCCEEDED(hr) && obj && *obj) flac_lift_rate(*obj); return hr; }
static HRESULT STDMETHODCALLTYPE ca_activate(ClsAct *a, REFIID iid, void **o) { HRESULT hr = CoCreateInstance(&a->clsid, NULL, CLSCTX_INPROC_SERVER, iid, o); char g[48]; guid_str(&a->clsid, g); slog("[mf] activate %s -> 0x%08lX", g, (unsigned long)hr);
  { static const GUID ibsh = {0xBB420AA4, 0x765B, 0x4A1F, {0x91, 0xFE, 0xD6, 0xA8, 0xA1, 0x43, 0x92, 0x4C}};       /* IMFByteStreamHandler */
    static const GUID flac = {0x0E41CFB8, 0x0506, 0x40F4, {0xA5, 0x16, 0x77, 0xCC, 0x23, 0x64, 0x2D, 0x91}};       /* the FLAC byte stream handler */
    if (SUCCEEDED(hr) && o && *o && IsEqualGUID(iid, &ibsh) && IsEqualGUID(&a->clsid, &flac) && !g_bsh_end) vt_set(*(void ***)*o, 4, (void *)bsh_end, (void **)&g_bsh_end); }
  return hr; }
static HRESULT STDMETHODCALLTYPE ca_ok(ClsAct *a) { (void)a; return S_OK; }
static const void *const g_ca_vt[36] = { (void *)ca_qi, (void *)ca_ar, (void *)ca_rl, (void *)ca_3, (void *)ca_4, (void *)ca_5, (void *)ca_6, (void *)ca_7, (void *)ca_8, (void *)ca_9,
  (void *)ca_10, (void *)ca_11, (void *)ca_12, (void *)ca_13, (void *)ca_14, (void *)ca_15, (void *)ca_16, (void *)ca_17, (void *)ca_18, (void *)ca_19, (void *)ca_20,
  (void *)ca_21, (void *)ca_22, (void *)ca_23, (void *)ca_24, (void *)ca_25, (void *)ca_26, (void *)ca_27, (void *)ca_28, (void *)ca_29, (void *)ca_30, (void *)ca_31,
  (void *)ca_32, (void *)ca_activate, (void *)ca_ok, (void *)ca_ok };   /* IMFAttributes 3..32, then ActivateObject, ShutdownObject, DetachObject */
static void mf_register_local(void)
{
  static LONG once; int d; HMODULE mf = NULL; void *act = NULL; HRESULT hr;
  typedef HRESULT (WINAPI *PCTA)(void **); typedef HRESULT (WINAPI *PRBSH)(const WCHAR *, const WCHAR *, void *);
  typedef HRESULT (WINAPI *PRMFT)(REFCLSID, REFGUID, const WCHAR *, UINT32, UINT32, const GUID *, UINT32, const GUID *);
  static const GUID clsid_flac_bsh = {0x0E41CFB8, 0x0506, 0x40F4, {0xA5, 0x16, 0x77, 0xCC, 0x23, 0x64, 0x2D, 0x91}};
  static const GUID clsid_flac_dec = {0x6B0B3E6B, 0xA2C5, 0x4514, {0x80, 0x55, 0xAF, 0xE8, 0xA9, 0x52, 0x42, 0xD9}};
  static const GUID mft_clsid_attr = {0x6821C42B, 0x65A4, 0x4E82, {0x99, 0xBC, 0x9A, 0x88, 0x20, 0x5E, 0xCD, 0x0C}};
  static const GUID cat_audio_dec = {0x9EA73FB4, 0xEF7A, 0x4559, {0x8D, 0x5D, 0x71, 0x9D, 0x8F, 0x04, 0x26, 0xC7}};
  static const GUID in_types[2] = { {0x73647561, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}}, {0x0000F1AC, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}} };   /* Audio / FLAC */
  static const GUID out_types[4] = { {0x73647561, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}}, {0x00000001, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}},       /* Audio / PCM */
                                     {0x73647561, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}}, {0x00000003, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}} };     /* Audio / Float */
  PCTA cta; PRBSH rbsh; PRMFT rmft;
  if (InterlockedExchange(&once, 1)) return;
  mmdev_patch();   /* IAudioClient3 for Win10 mfcore's audio renderer */
  { int d2; alock(1); act_init(); alock(0); for (d2 = 0; d2 < g_ndll; d2++) if (!lstrcmpiW(g_dllname[d2], L"windows.storage.dll")) { storage_noaffinity(act_load(d2)); break; } }
  { typedef HRESULT (WINAPI *PCI)(const WCHAR *, void *, REFIID, void **); static const GUID iish = {0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};
    HMODULE s = LoadLibraryA("shell32.dll"); PCI ci = s ? (PCI)GetProcAddress(s, "SHCreateItemFromParsingName") : NULL; IUnknown *it = NULL;   /* Win7 shell item class: BindToHandler patch */
    if (ci && SUCCEEDED(ci(L"C:\\", NULL, &iish, (void **)&it)) && it) { w7item_patch(it); it->lpVtbl->Release(it); } }
  alock(1); act_init(); alock(0);
  for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"mfplat.dll")) { mf = act_load(d); break; }
  if (!mf) { slog("[mf] Win10 mfplat not loaded: no local codec registration"); return; }
  cta = (PCTA)GetProcAddress(mf, "MFCreateTransformActivate"); rbsh = (PRBSH)GetProcAddress(mf, "MFRegisterLocalByteStreamHandler"); rmft = (PRMFT)GetProcAddress(mf, "MFTRegisterLocalByCLSID");
  if (!cta || !rbsh || !rmft) { slog("[mf] local registration exports missing"); return; }
  hr = rmft(&clsid_flac_dec, &cat_audio_dec, L"Microsoft FLAC Audio Decoder MFT", 0, 1, in_types, 2, out_types);
  slog("[mf] FLAC decoder MFT registered locally -> 0x%08lX", (unsigned long)hr);
  { typedef HRESULT (WINAPI *PCA)(IUnknown **, UINT32); PCA ca = (PCA)GetProcAddress(mf, "MFCreateAttributes"); IUnknown *at = NULL; ClsAct *a;
    (void)cta; (void)mft_clsid_attr;
    if (ca && SUCCEEDED(hr = ca(&at, 4)) && at && (a = (ClsAct *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a)) != NULL) { a->vt = g_ca_vt; a->ref = 1; a->attrs = at; a->clsid = clsid_flac_bsh; act = a; }
    else { if (at) at->lpVtbl->Release(at); hr = E_OUTOFMEMORY; } }
  if (act) {
    { HRESULT a = rbsh(L".flac", NULL, act), b = rbsh(NULL, L"audio/flac", act), c = rbsh(NULL, L"audio/x-flac", act);
      slog("[mf] FLAC byte stream handler registered locally -> 0x%08lX 0x%08lX 0x%08lX", (unsigned long)a, (unsigned long)b, (unsigned long)c); }
    ((IUnknown *)act)->lpVtbl->Release((IUnknown *)act);
  } else slog("[mf] activation object for the FLAC byte stream handler -> 0x%08lX", (unsigned long)hr);
  /* MP3 the same way: Win10's MP3 byte stream handler (mfsrcsnk.dll) and MP3 decoder MFT (mp3dmod.dll), both from the
     image via g_comcls. Win7 registers its own copies of both CLSIDs (mf.dll / mp3dmod.dll), which Win10 mfplat cannot use: Media
     Player sat on an MP3 forever without a source. */
  { static const GUID clsid_mp3_bsh = {0xA82E50BA, 0x8E92, 0x41EB, {0x9D, 0xF2, 0x43, 0x3F, 0x50, 0xEC, 0x29, 0x93}};
    static const GUID clsid_mp3_dec = {0xBBEEA841, 0x0A63, 0x4F52, {0xA7, 0xAB, 0xA9, 0xB3, 0xA8, 0x4E, 0xD3, 0x8A}};
    static const GUID in_mp3[4] = { {0x73647561, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}}, {0x00000055, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}},    /* Audio / MP3 */
                                    {0x73647561, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}}, {0x00000050, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71}} };  /* Audio / MPEG */
    typedef HRESULT (WINAPI *PCA)(IUnknown **, UINT32); PCA ca = (PCA)GetProcAddress(mf, "MFCreateAttributes"); IUnknown *at = NULL; ClsAct *a = NULL;
    hr = rmft(&clsid_mp3_dec, &cat_audio_dec, L"MP3 Decoder MFT", 0, 2, in_mp3, 2, out_types);
    slog("[mf] MP3 decoder MFT registered locally -> 0x%08lX", (unsigned long)hr);
    if (ca && SUCCEEDED(ca(&at, 4)) && at && (a = (ClsAct *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a)) != NULL) { a->vt = g_ca_vt; a->ref = 1; a->attrs = at; a->clsid = clsid_mp3_bsh; }
    else if (at) at->lpVtbl->Release(at);
    if (a) { HRESULT x = rbsh(L".mp3", NULL, a), y = rbsh(NULL, L"audio/mpeg", a), z = rbsh(NULL, L"audio/mp3", a);
      slog("[mf] MP3 byte stream handler registered locally -> 0x%08lX 0x%08lX 0x%08lX", (unsigned long)x, (unsigned long)y, (unsigned long)z);
      ((IUnknown *)a)->lpVtbl->Release((IUnknown *)a); } }
}
/* Win10 windows.storage CShellItem::Save (its marshal-by-value) refuses RPC_E_WRONG_THREAD when the item is used outside the apartment that
   created it - except in explorer.exe, where a global (RVA 0x726C94 in 10.0.19041 windows.storage, 1 = enforce, 2 = off) turns the check
   off. We hand out raw pointers across apartments (CoGetStdMarshalEx = FTM: no Win7 proxies for Win10 interfaces), so the check only breaks
   things here (StorageFile.GetThumbnailAsync after a pick -> 0x8001010E, playback never started). Set to 2 after verifying the code that
   reads it. */
static BOOL WINAPI storage_tryenter(CRITICAL_SECTION *cs) { EnterCriticalSection(cs); return TRUE; }
static void storage_noaffinity(HMODULE ws)
{
  static LONG done; BYTE *b = (BYTE *)ws; IMAGE_NT_HEADERS *nt;
  if (!ws || InterlockedExchange(&done, 1)) return;
  nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
  /* per build (from the public PDBs): code = CShellItem::Save+0x5b `mov ecx, [rip+rel]` reading s_tbEnforceForProcess (flag), vt = the
     CShellItem IShellItem2 vtable. 19041.6456 and 19041.3803 (the image) are known. The code bytes identify the build. */
  static const struct { DWORD code, flag, vt; BYTE bytes[6]; const char *build; } site[] = {
    { 0x16F3FB, 0x726C94, 0x59E8D8, { 0x8B, 0x0D, 0x93, 0x78, 0x5B, 0x00 }, "19041.6456" },
    { 0x1704BB, 0x71DC58, 0x5B3EE0, { 0x8B, 0x0D, 0x97, 0xD7, 0x5A, 0x00 }, "19041.3803" } };
  int si, hit = -1;
  for (si = 0; si < (int)(sizeof site / sizeof site[0]); si++)
    if (nt->OptionalHeader.SizeOfImage > site[si].flag + 4 && !memcmp(b + site[si].code, site[si].bytes, 6)) { hit = si; break; }
  if (hit >= 0) {
    volatile LONG *flag = (volatile LONG *)(b + site[hit].flag); DWORD old;
    if (VirtualProtect((void *)flag, 4, PAGE_READWRITE, &old)) { *flag = 2; VirtualProtect((void *)flag, 4, old, &old); slog("[storage] windows.storage %s: shell-item apartment check off (explorer mode)", site[hit].build); } }
  else slog("[storage] windows.storage build not known: shell-item apartment check left on, CShellItem not patched");
  /* CObjectWithThreadUseDetection::TryEnterOnCurrentThread: an object whose own mode ([this+0x2c]) is 2 ("always enforce") skips the
     process flag above and compares apartments anyway - App Installer reads StorageFile.Path (CStorageItemProxyBase -> CShellItem
     property store) on a worker thread and got RPC_E_WRONG_THREAD ("Cannot open app package: error in parsing"). `cmp ebx, r14d; je`
     -> `cmp; nop nop`, so mode 2 obeys the process flag like the rest. 19041.3803 only (code bytes checked). */
  { static const struct { DWORD rva; BYTE bytes[6]; const char *build; } tue[] = { { 0x114962, { 0x41, 0x3B, 0xDE, 0x74, 0x13, 0x8B }, "19041.3803" } };
    int ti, done2 = 0;
    for (ti = 0; ti < (int)(sizeof tue / sizeof tue[0]); ti++)
      if (nt->OptionalHeader.SizeOfImage > tue[ti].rva + 8 && !memcmp(b + tue[ti].rva, tue[ti].bytes, 6)) { DWORD old; BYTE *j = b + tue[ti].rva + 3;
        if (VirtualProtect(j, 2, PAGE_EXECUTE_READWRITE, &old)) { j[0] = 0x90; j[1] = 0x90; VirtualProtect(j, 2, old, &old); FlushInstructionCache(GetCurrentProcess(), j, 2); done2 = 1;
          slog("[storage] windows.storage %s: per-object thread-use enforcement follows the process flag", tue[ti].build); } break; }
    if (!done2) slog("[storage] windows.storage: per-object thread-use enforcement not patched (build not known)"); }
  /* Win10 CShellItem's IShellItem2 vtable: BindToHandler(BHID_RandomAccessStream) through our file stream as for Win7 items -
     Win10's own path (_CreateRandomAccessStream, file placeholder helpers) hits an unimplemented NT call and crashes on Win7 */
  if (hit >= 0 && nt->OptionalHeader.SizeOfImage > site[hit].vt + 32) w10item_patch(ws, (void **)(b + site[hit].vt));
  /* CShellItem's "thread use detection": each method TryEnterCriticalSection()s the item's lock and fails with RPC_E_WRONG_THREAD when
     another thread is inside the item. On Win10 calls reach an item one at a time through its apartment; with our shared raw pointers two
     threads do overlap (folder queries: GetParent / GetScopeForSearch -> E_NOINTERFACE -> "Open folder" crash). windows.storage's
     TryEnterCriticalSection waits instead (the lock is re-entrant). */
  {
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; IMAGE_IMPORT_DESCRIPTOR *id; int n = 0;
    for (id = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); dd->VirtualAddress && id->Name; id++) {
      IMAGE_THUNK_DATA *on = (IMAGE_THUNK_DATA *)(b + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk)), *ft = (IMAGE_THUNK_DATA *)(b + id->FirstThunk);
      for (; on->u1.AddressOfData; on++, ft++) { DWORD old;
        if (IMAGE_SNAP_BY_ORDINAL(on->u1.Ordinal) || strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(b + on->u1.AddressOfData))->Name, "TryEnterCriticalSection")) continue;
        if (VirtualProtect(&ft->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) { ft->u1.Function = (ULONG_PTR)storage_tryenter; VirtualProtect(&ft->u1.Function, sizeof(void *), old, &old); n++; } } }
    slog("[storage] windows.storage TryEnterCriticalSection -> blocking enter (%d import(s))", n); }
}
/* Shell classes: Win10 implements what Win7 registers to shell32.dll in windows.storage.dll (folders, shell items, ...). For Win10 callers
   those come from Win10 windows.storage's class factory first, as on Win10 - a Win7 shell32 folder in a Win10 namespace hands out Win7
   shell items (no BHID_RandomAccessStream: StorageFile.OpenAsync MK_E_NOOBJECT). Classes windows.storage does not have stay Win7's. */
static HRESULT com_storage_first(const GUID *clsid, IUnknown *outer, const GUID *iid, void **ppv)
{
  static HMODULE ws; static LONG init; typedef HRESULT (WINAPI *PGCO)(const GUID *, const GUID *, void **);
  static const GUID iidcf = {0x00000001, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  /* only the namespace classes: every shell32 class from Win10 recursed (Shell Library API {D9B3211D}: stack overflow at startup) */
  static const GUID only[] = {
    {0x00021400, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}},                                   /* CLSID_ShellDesktop */
    {0x20D04FE0, 0x3AEA, 0x1069, {0xA2, 0xD8, 0x08, 0x00, 0x2B, 0x30, 0x30, 0x9D}},      /* CLSID_MyComputer */
    {0xF3364BA0, 0x65B9, 0x11CE, {0xA9, 0xBA, 0x00, 0xAA, 0x00, 0x4A, 0xE8, 0x37}},      /* CLSID_ShellFSFolder */
    {0x9AC9FBE1, 0xE0A2, 0x4AD6, {0xB4, 0xEE, 0xE2, 0x12, 0x01, 0x3E, 0xA9, 0x17}},      /* CLSID_ShellItem */
    {0x4DF0C730, 0xDF9D, 0x4AE3, {0x91, 0x53, 0xAA, 0x6B, 0x82, 0xE9, 0x79, 0x5A}} };    /* CLSID_KnownFolderManager (Win7's gave library items KnownFolders.VideosLibrary could not use: E_NOINTERFACE) */
  WCHAR key[80], val[MAX_PATH]; DWORD cb = sizeof val, ty; char g[48]; const WCHAR *fn; PGCO gco; IClassFactory *cf = NULL; HRESULT hr; int d; unsigned k;
  /* every class Win7 registers to SearchFolder.dll: Win10's (the image's) - Win10 windows.storage builds its folder queries on the search
     folder classes (ConditionAttribute, ScopeFactory, SearchIDListFactory ...); Win7's hand back Win7 folder objects without IShellFolder3
     -> StorageFolder queries failed (Media Player "Open folder"). */
  { static HMODULE sf; static LONG sfinit; guid_str(clsid, g); _snwprintf(key, 80, L"CLSID\\%hs\\InProcServer32", g); cb = sizeof val;
    if (RegGetValueW(HKEY_CLASSES_ROOT, key, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &ty, val, &cb) == ERROR_SUCCESS) {
      fn = wcsrchr(val, L'\\'); fn = fn ? fn + 1 : val;
      if (!lstrcmpiW(fn, L"SearchFolder.dll")) {
        if (!InterlockedExchange(&sfinit, 1)) { alock(1); act_init(); alock(0); for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"searchfolder.dll")) { sf = act_load(d); break; } slog("[com] Win10 SearchFolder.dll %s", sf ? "loaded" : "not harvested"); }
        hr = E_FAIL; if (sf && (gco = (PGCO)GetProcAddress(sf, "DllGetClassObject")) != NULL) hr = gco(clsid, &iidcf, (void **)&cf);
        if (FAILED(hr) || !cf) {   /* classes Win10 moved out of SearchFolder.dll (ConditionAttribute ...): Win10 windows.storage */
          static HMODULE wst; static LONG wsi; if (!InterlockedExchange(&wsi, 1)) { alock(1); act_init(); alock(0); for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"windows.storage.dll")) { wst = act_load(d); break; } }
          cf = NULL; if (wst && (gco = (PGCO)GetProcAddress(wst, "DllGetClassObject")) != NULL) hr = gco(clsid, &iidcf, (void **)&cf);
          if (FAILED(hr) || !cf) { static HMODULE sq; static LONG sqi; if (!InterlockedExchange(&sqi, 1)) { alock(1); act_init(); alock(0); for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"StructuredQuery.dll")) { sq = act_load(d); break; } }
            cf = NULL; if (sq && (gco = (PGCO)GetProcAddress(sq, "DllGetClassObject")) != NULL) hr = gco(clsid, &iidcf, (void **)&cf); }
          if (FAILED(hr) || !cf) { static HMODULE wss; static LONG wssi; if (!InterlockedExchange(&wssi, 1)) { alock(1); act_init(); alock(0); for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"Windows.Storage.Search.dll")) { wss = act_load(d); break; } }
            cf = NULL; if (wss && (gco = (PGCO)GetProcAddress(wss, "DllGetClassObject")) != NULL) hr = gco(clsid, &iidcf, (void **)&cf); } }
        if (FAILED(hr) || !cf) { static LONG nf; if (InterlockedIncrement(&nf) <= 60) slog("[com] %s: Win10 search DLLs DllGetClassObject -> 0x%08lX", g, (unsigned long)hr); }
        else {
          hr = cf->lpVtbl->CreateInstance(cf, outer, iid, ppv); cf->lpVtbl->Release(cf);
          { static GUID seen[64]; static LONG ns; LONG i2, n2 = ns; HMODULE own = NULL; WCHAR on[MAX_PATH] = L"?";
            for (i2 = 0; i2 < n2 && i2 < 64; i2++) if (IsEqualGUID(&seen[i2], clsid)) break;
            if (i2 == n2 || FAILED(hr)) { if (n2 < 64) seen[InterlockedIncrement(&ns) - 1] = *clsid;
              if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)gco, &own)) GetModuleFileNameW(own, on, MAX_PATH);
              slog("[com] %s: search class from Win10 %ls -> 0x%08lX", g, wcsrchr(on, L'\\') ? wcsrchr(on, L'\\') + 1 : on, (unsigned long)hr); } }
          if (SUCCEEDED(hr)) return hr; } } }
    cb = sizeof val; }
  for (k = 0; k < sizeof only / sizeof only[0]; k++) if (IsEqualGUID(clsid, &only[k])) break;
  if (k == sizeof only / sizeof only[0]) return REGDB_E_CLASSNOTREG;
  guid_str(clsid, g); _snwprintf(key, 80, L"CLSID\\%hs\\InProcServer32", g);
  if (RegGetValueW(HKEY_CLASSES_ROOT, key, NULL, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &ty, val, &cb) != ERROR_SUCCESS) return REGDB_E_CLASSNOTREG;
  fn = wcsrchr(val, L'\\'); fn = fn ? fn + 1 : val; if (lstrcmpiW(fn, L"shell32.dll")) return REGDB_E_CLASSNOTREG;
  if (!InterlockedExchange(&init, 1)) { alock(1); act_init(); alock(0); for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"windows.storage.dll")) { ws = act_load(d); break; } storage_noaffinity(ws); }
  if (!ws || !(gco = (PGCO)GetProcAddress(ws, "DllGetClassObject"))) return REGDB_E_CLASSNOTREG;
  if (FAILED(hr = gco(clsid, &iidcf, (void **)&cf)) || !cf) return REGDB_E_CLASSNOTREG;
  hr = cf->lpVtbl->CreateInstance(cf, outer, iid, ppv); cf->lpVtbl->Release(cf);
  if (SUCCEEDED(hr)) slog("[com] %s: shell class from Win10 windows.storage", g);
  return hr;
}
static int com_preferred(const GUID *clsid) { unsigned i; for (i = 0; i < sizeof g_comcls / sizeof g_comcls[0]; i++) if (g_comcls[i].prefer && !memcmp(&g_comcls[i].clsid, clsid, sizeof(GUID))) return 1; return 0; }
static HRESULT com_inproc(const GUID *clsid, IUnknown *outer, const GUID *iid, void **ppv, int getclass) {
  unsigned i; typedef HRESULT (WINAPI *PGCO)(const GUID *, const GUID *, void **);
  if (!getclass) { HRESULT sh = shell_create(clsid, iid, ppv); if (sh != REGDB_E_CLASSNOTREG) return sh; }
  { static const GUID clsid_psm = {0x02844640, 0xE37C, 0x4322, {0xA3, 0xB8, 0x4C, 0x61, 0xA2, 0xE5, 0x88, 0x79}};
    if (!memcmp(clsid, &clsid_psm, sizeof(GUID)) && !getclass) { slog("[com] PsmBroker (built-in)"); return psm_qi((void *)&g_psm_obj, iid, ppv); } }
  for (i = 0; i < sizeof g_comcls / sizeof g_comcls[0]; i++) if (!memcmp(&g_comcls[i].clsid, clsid, sizeof(GUID))) {
    int d; HMODULE m; PGCO gco; void *cf = NULL; HRESULT hr;
    /* DirectManipulation (scrolling, content alignment) runs on the dcomp bridge (dcomp_manip.c) */
    alock(1); act_init(); alock(0);
    for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], g_comcls[i].dll)) break;
    if (d == g_ndll || !(m = act_load(d))) return REGDB_E_CLASSNOTREG;
    gco = (PGCO)GetProcAddress(m, "DllGetClassObject"); if (!gco) return REGDB_E_CLASSNOTREG;
    if (getclass) return gco(clsid, iid, ppv);
    { static const GUID iidcf = {0x00000001, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; hr = gco(clsid, &iidcf, &cf); } if (FAILED(hr)) return hr;
    hr = ((IClassFactory *)cf)->lpVtbl->CreateInstance((IClassFactory *)cf, outer, iid, ppv); ((IClassFactory *)cf)->lpVtbl->Release((IClassFactory *)cf);
    slog("[com] in-proc %ls -> 0x%08lX", g_comcls[i].dll, (unsigned long)hr); return hr;
  }
  return REGDB_E_CLASSNOTREG;
}
typedef HRESULT (WINAPI *PRoGet)(void *, const GUID *, void **); typedef HRESULT (WINAPI *PRoAct)(void *, void **);
static PRoGet host_get(void) { static PRoGet f; if (!f) { HMODULE m = LoadLibraryA("api-ms-win-core-winrt-l1-1-0.dll"); if (m) f = (PRoGet)GetProcAddress(m, "RoGetActivationFactory"); } return f; }
