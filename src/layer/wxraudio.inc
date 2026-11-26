/* wxraudio.inc - audio: endpoint helpers, IAudioClient2/3, spatial audio, MMCSS task indexes, pipeline diagnostics. Included by wxrsys.c. */
/* ---- Win10 audio endpoint helpers over Win7's MMDevAPI ("We can't find an audio playback device" in Media Player) ----
   ext-ms-win-audiocore-pal (GetDefaultEndpointForCategory & co.) does not exist on Win7, and Win10 mmdevapi's private
   #23 mmdDevGetMMDeviceFromInterfaceId / #25 mmdDevGetMMDeviceIdFromInterfaceId neither. Win10 hands device-interface paths around
   (\\?\SWD#MMDEVAPI#{0.0.0.00000000}.{guid}#{e6327cad-...}); the MMDevice endpoint id is the part after "MMDEVAPI#". */
static const GUID CLSID_MMDeviceEnumerator_ = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID IID_IMMDeviceEnumerator_ = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static void *mm_enum(void) {
  typedef HRESULT (WINAPI *F)(REFCLSID, IUnknown *, DWORD, REFIID, void **); static F f; void *e = NULL;
  if (!f) f = (F)GetProcAddress(LoadLibraryA("ole32.dll"), "CoCreateInstance");
  if (f) f(&CLSID_MMDeviceEnumerator_, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator_, &e);
  return e; }
static void mm_id_from_iface(const WCHAR *in, WCHAR *out, size_t cap) {
  const WCHAR *p = in, *q; size_t n;
  for (q = in; q && *q; q++) if (!_wcsnicmp(q, L"MMDEVAPI#", 9)) { p = q + 9; break; }
  q = p == in ? p + wcslen(p) : wcschr(p, L'#'); if (!q) q = p + wcslen(p);
  n = (size_t)(q - p); if (n >= cap) n = cap - 1; memcpy(out, p, n * 2); out[n] = 0; }
__declspec(dllexport) HRESULT WINAPI mmdDevGetMMDeviceIdFromInterfaceId(LPCWSTR iface, LPWSTR *out) {
  WCHAR id[512]; size_t n; if (!out) return E_POINTER; *out = NULL; if (!iface) return E_INVALIDARG;
  mm_id_from_iface(iface, id, 512); n = (wcslen(id) + 1) * 2; if (!(*out = (LPWSTR)CoTaskMemAlloc(n))) return E_OUTOFMEMORY; memcpy(*out, id, n);
  slog("[audio] mmdDevGetMMDeviceIdFromInterfaceId(%ls) -> %ls", iface, id); return S_OK; }
__declspec(dllexport) HRESULT WINAPI mmdDevGetMMDeviceFromInterfaceId(LPCWSTR iface, void **dev) {
  WCHAR id[512]; void *e; HRESULT hr; if (!dev) return E_POINTER; *dev = NULL; if (!iface) return E_INVALIDARG;
  mm_id_from_iface(iface, id, 512); if (!(e = mm_enum())) return E_FAIL;
  hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, LPCWSTR, void **))(*(void ***)e)[5])(e, id, dev);   /* IMMDeviceEnumerator::GetDevice */
  ((IUnknown *)e)->lpVtbl->Release((IUnknown *)e); slog("[audio] mmdDevGetMMDeviceFromInterfaceId(%ls) -> 0x%08lX", iface, (unsigned long)hr); return hr; }
/* GetDefaultEndpointForCategory(AUDIO_STREAM_CATEGORY, EDataFlow, WCHAR *buf, SIZE_T *cch): the default endpoint as an interface path.
   Category Communications (3) -> eCommunications, everything else eMultimedia. */
__declspec(dllexport) HRESULT WINAPI GetDefaultEndpointForCategory(int cat, int flow, WCHAR *buf, SIZE_T *cch) {
  void *e = mm_enum(), *d = NULL; LPWSTR id = NULL; WCHAR path[600]; size_t n; HRESULT hr;
  if (!cch) return E_POINTER; if (!e) return E_FAIL;
  hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, int, int, void **))(*(void ***)e)[4])(e, flow, cat == 3 ? 2 : 1, &d);   /* GetDefaultAudioEndpoint */
  ((IUnknown *)e)->lpVtbl->Release((IUnknown *)e);
  if (SUCCEEDED(hr)) { hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, LPWSTR *))(*(void ***)d)[5])(d, &id); ((IUnknown *)d)->lpVtbl->Release((IUnknown *)d); }   /* IMMDevice::GetId */
  if (FAILED(hr)) { slog("[audio] GetDefaultEndpointForCategory(%d, %d) -> 0x%08lX", cat, flow, (unsigned long)hr); return hr; }
  _snwprintf(path, 600, L"\\\\?\\SWD#MMDEVAPI#%ls#{e6327cad-dcec-4949-ae8a-991e976a79d2}", id); CoTaskMemFree(id); path[599] = 0;
  n = wcslen(path) + 1;
  if (!buf || *cch < n) { *cch = n; return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); }
  memcpy(buf, path, n * 2); *cch = n; slog("[audio] GetDefaultEndpointForCategory(%d, %d) -> %ls", cat, flow, path); return S_OK; }
__declspec(dllexport) BOOL WINAPI IsEndpointDeterminedByCategory(int cat) { (void)cat; return FALSE; }
__declspec(dllexport) HRESULT WINAPI RegisterForCategoryEndpointChangeNotification(int cat, void **cookie) { (void)cat; if (cookie) *cookie = (void *)(ULONG_PTR)1; return S_OK; }
__declspec(dllexport) HRESULT WINAPI UnregisterForCategoryEndpointChangeNotification(void *cookie) { (void)cookie; return S_OK; }
__declspec(dllexport) BOOL WINAPI IsAudioPlayerSupported(void) { return TRUE; }
/* patches one vtable slot (process-wide: every object of the class); *orig gets the previous entry */
static void vt_set(void **vt, int slot, void *fn, void **orig) { DWORD old; if (VirtualProtect(&vt[slot], sizeof(void *), PAGE_READWRITE, &old)) { *orig = vt[slot]; vt[slot] = fn; VirtualProtect(&vt[slot], sizeof(void *), old, &old); } }
/* IAudioClient2/3 over Win7's IAudioClient. Win10 mfcore's audio renderer activates IAudioClient3 on the endpoint; Win7's MMDevAPI
   answers E_NOINTERFACE and the renderer reports MF_E_NO_AUDIO_PLAYBACK_DEVICE ("We can't find an audio playback device").
   Win7's endpoint device vtable gets an Activate that wraps an IAudioClient for those two IIDs (this process only).
   IAudioClient: Initialize, GetBufferSize, GetStreamLatency, GetCurrentPadding, IsFormatSupported, GetMixFormat, GetDevicePeriod, Start,
   Stop, Reset, SetEventHandle, GetService; 2: IsOffloadCapable, SetClientProperties, GetBufferSizeLimits;
   3: GetSharedModeEnginePeriod, GetCurrentSharedModeEnginePeriod, InitializeSharedAudioStream. */
static const GUID IID_IAudioClient_ = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID IID_IAudioClient2_ = {0x726778CD, 0xF60A, 0x4EDA, {0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA}};
static const GUID IID_IAudioClient3_ = {0x7ED4EE07, 0x8E67, 0x4CD4, {0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42}};
typedef struct AC3 { const void *const *vt; LONG ref; IUnknown *in; } AC3;
#define AC_IN(a, slot, T) ((T)(*(void ***)((AC3 *)(a))->in)[slot])
static HRESULT STDMETHODCALLTYPE ac_qi(AC3 *a, REFIID iid, void **o) {
  static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (IsEqualGUID(iid, &iu) || IsEqualGUID(iid, &IID_IAudioClient_) || IsEqualGUID(iid, &IID_IAudioClient2_) || IsEqualGUID(iid, &IID_IAudioClient3_)) { *o = a; InterlockedIncrement(&a->ref); return S_OK; }
  { HRESULT hr = a->in->lpVtbl->QueryInterface(a->in, iid, o); if (FAILED(hr)) { char g[48]; guid_str(iid, g); slog("[audio] IAudioClient QI %s -> 0x%08lX", g, (unsigned long)hr); } return hr; } }
static ULONG STDMETHODCALLTYPE ac_ar(AC3 *a) { return (ULONG)InterlockedIncrement(&a->ref); }
static ULONG STDMETHODCALLTYPE ac_rl(AC3 *a) { LONG n = InterlockedDecrement(&a->ref); if (!n) { a->in->lpVtbl->Release(a->in); HeapFree(GetProcessHeap(), 0, a); } return (ULONG)n; }
static HRESULT STDMETHODCALLTYPE ac_init(AC3 *a, int mode, DWORD flags, LONGLONG buf, LONGLONG per, const void *fmt, const GUID *sess) {
  HRESULT hr; DWORD f7 = flags & ~(0x80000000u | 0x08000000u);   /* AUTOCONVERTPCM, SRC_DEFAULT_QUALITY: Win8+ */
  if (mode == 0) f7 |= 0x00100000u;   /* RATEADJUST: Win7 hands out IAudioClockAdjustment only then (the renderer asks for it: E_INVALIDARG otherwise) */
  hr = AC_IN(a, 3, HRESULT (STDMETHODCALLTYPE *)(void *, int, DWORD, LONGLONG, LONGLONG, const void *, const GUID *))(a->in, mode, f7, buf, per, fmt, sess);
  slog("[audio] IAudioClient::Initialize(mode %d, flags 0x%lx -> 0x%lx, buffer %lld, tag 0x%x ch %u rate %lu bits %u) -> 0x%08lX", mode, flags, f7, buf,
    fmt ? *(const WORD *)fmt : 0, fmt ? *(const WORD *)((const BYTE *)fmt + 2) : 0, fmt ? *(const DWORD *)((const BYTE *)fmt + 4) : 0, fmt ? *(const WORD *)((const BYTE *)fmt + 14) : 0, (unsigned long)hr); return hr; }
#define AC_FWD(n, slot) static HRESULT STDMETHODCALLTYPE ac_##n(AC3 *a, void *p1, void *p2, void *p3, void *p4) { HRESULT hr = AC_IN(a, slot, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *, void *, void *))(a->in, p1, p2, p3, p4); \
  if (FAILED(hr) || (slot == 7 && hr == S_FALSE)) { static LONG k; if (InterlockedIncrement(&k) <= 10) { slog("[audio] IAudioClient slot %d -> 0x%08lX", slot, (unsigned long)hr); \
    if (slot == 7 && p2) { const BYTE *w = (const BYTE *)p2; slog("[audio]   IsFormatSupported(mode %d, tag 0x%x ch %u rate %lu bits %u)", (int)(INT_PTR)p1, *(const WORD *)w, *(const WORD *)(w + 2), *(const DWORD *)(w + 4), *(const WORD *)(w + 14)); } } } return hr; }
AC_FWD(4, 4) AC_FWD(5, 5) AC_FWD(7, 7)
/* IsFormatSupported: in shared mode Win10 refuses anything that is not PCM / IEEE float (AUDCLNT_E_UNSUPPORTED_FORMAT); Win7 answers a
   compressed tag such as FLAC (0xF1AC) with S_FALSE + a closest match. mfcore's audio renderer then kept FLAC as its stream format, and its
   CAudStreamSink::GetFastestRate caps the rate at 1x for non-PCM formats (mfcore+0x27e84 / +0x1424): 2x played at 1x. */
static int ac_pcm_like(const BYTE *w) {
  static const BYTE ks[12] = {0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};   /* KSDATAFORMAT_SUBTYPE_* tail */
  WORD tag = *(const WORD *)w;
  if (tag == 1 || tag == 3) return 1;
  if (tag == 0xFFFE && *(const WORD *)(w + 16) >= 22) { const BYTE *sub = w + 24; DWORD d1 = *(const DWORD *)sub; return (d1 == 1 || d1 == 3) && !memcmp(sub + 4, ks, 12); }
  return 0; }
static HRESULT STDMETHODCALLTYPE ac_7w(AC3 *a, void *p1, void *p2, void *p3, void *p4) {
  if ((int)(INT_PTR)p1 == 0 && p2 && !ac_pcm_like((const BYTE *)p2)) { static LONG k;
    if (p3) *(void **)p3 = NULL;
    if (InterlockedIncrement(&k) <= 10) slog("[audio] IsFormatSupported(shared, tag 0x%x) -> AUDCLNT_E_UNSUPPORTED_FORMAT (Win10 behaviour)", *(const WORD *)p2);
    return (HRESULT)0x88890008L; }
  return ac_7(a, p1, p2, p3, p4); }
AC_FWD(6, 6) AC_FWD(8, 8) AC_FWD(9, 9) AC_FWD(13, 13) AC_FWD(14, 14)
#define AC_LOG(n, slot, nm) static HRESULT STDMETHODCALLTYPE ac_##n(AC3 *a, void *p1, void *p2, void *p3, void *p4) { HRESULT hr = AC_IN(a, slot, HRESULT (STDMETHODCALLTYPE *)(void *, void *, void *, void *, void *))(a->in, p1, p2, p3, p4); \
  { static LONG k; if (InterlockedIncrement(&k) <= 8) slog("[audio] IAudioClient::%s -> 0x%08lX", nm, (unsigned long)hr); } return hr; }
AC_LOG(10, 10, "Start") AC_LOG(11b, 11, "Stop") AC_LOG(12b, 12, "Reset")
static HRESULT STDMETHODCALLTYPE ac_offload(AC3 *a, int cat, BOOL *b) { (void)a; slog("[audio] IAudioClient2::IsOffloadCapable(cat %d)", cat); if (b) *b = FALSE; return S_OK; }
/* AudioClientProperties: cbSize, bIsOffload, eCategory, Options (Win8.1+) */
static HRESULT STDMETHODCALLTYPE ac_props(AC3 *a, const void *p) { (void)a; if (p) { const DWORD *d = (const DWORD *)p; slog("[audio] IAudioClient2::SetClientProperties(cb %lu, offload %lu, cat %lu, opts 0x%lx)", d[0], d[1], d[2], d[0] >= 16 ? d[3] : 0); } return S_OK; }
static HRESULT STDMETHODCALLTYPE ac_limits(AC3 *a, const void *fmt, BOOL ev, LONGLONG *mn, LONGLONG *mx) {
  LONGLONG def = 0, min = 0; HRESULT hr = AC_IN(a, 9, HRESULT (STDMETHODCALLTYPE *)(void *, LONGLONG *, LONGLONG *))(a->in, &def, &min); (void)fmt; (void)ev;
  if (SUCCEEDED(hr)) { if (mn) *mn = min; if (mx) *mx = 20000000; } return hr; }   /* GetDevicePeriod; max 2 s */
static UINT32 ac_frames(AC3 *a, const void *fmt, LONGLONG hns) {
  DWORD rate = 48000; BYTE *mix = NULL;   /* WAVEFORMATEX.nSamplesPerSec is the DWORD at offset 4 */
  if (fmt) rate = *(const DWORD *)((const BYTE *)fmt + 4); else if (SUCCEEDED(AC_IN(a, 8, HRESULT (STDMETHODCALLTYPE *)(void *, BYTE **))(a->in, &mix)) && mix) { rate = *(DWORD *)(mix + 4); CoTaskMemFree(mix); }
  return (UINT32)(hns * rate / 10000000); }
static HRESULT STDMETHODCALLTYPE ac_engper(AC3 *a, const void *fmt, UINT32 *def, UINT32 *fund, UINT32 *min, UINT32 *max) {
  LONGLONG d = 0, m = 0; UINT32 f; HRESULT hr = AC_IN(a, 9, HRESULT (STDMETHODCALLTYPE *)(void *, LONGLONG *, LONGLONG *))(a->in, &d, &m);
  slog("[audio] IAudioClient3::GetSharedModeEnginePeriod -> 0x%08lX", (unsigned long)hr); if (FAILED(hr)) return hr; f = ac_frames(a, fmt, d);
  if (def) *def = f; if (fund) *fund = f; if (min) *min = f; if (max) *max = f; return S_OK; }   /* Win7: one fixed engine period */
static HRESULT STDMETHODCALLTYPE ac_curper(AC3 *a, void **fmt, UINT32 *per) {
  LONGLONG d = 0, m = 0; HRESULT hr = AC_IN(a, 8, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(a->in, fmt);   /* GetMixFormat */
  slog("[audio] IAudioClient3::GetCurrentSharedModeEnginePeriod -> 0x%08lX", (unsigned long)hr); if (FAILED(hr)) return hr; AC_IN(a, 9, HRESULT (STDMETHODCALLTYPE *)(void *, LONGLONG *, LONGLONG *))(a->in, &d, &m);
  if (per) *per = ac_frames(a, fmt ? *fmt : NULL, d); return S_OK; }
static HRESULT STDMETHODCALLTYPE ac_initshared(AC3 *a, DWORD flags, UINT32 per, const void *fmt, const GUID *sess) {
  slog("[audio] IAudioClient3::InitializeSharedAudioStream(flags 0x%lx, period %u)", flags, per); return ac_init(a, 0 /* AUDCLNT_SHAREMODE_SHARED */, flags, 0, 0, fmt, sess); }
static const void *const g_ac_vt[21] = { (void *)ac_qi, (void *)ac_ar, (void *)ac_rl, (void *)ac_init, (void *)ac_4, (void *)ac_5, (void *)ac_6, (void *)ac_7w, (void *)ac_8,
  (void *)ac_9, (void *)ac_10, (void *)ac_11b, (void *)ac_12b, (void *)ac_13, (void *)ac_14, (void *)ac_offload, (void *)ac_props, (void *)ac_limits,
  (void *)ac_engper, (void *)ac_curper, (void *)ac_initshared };
/* ISpatialAudioClient stand-in. Win10 mfcore's audio renderer rebuilds its device on a playback-rate change (and in other re-create paths)
   through a helper that activates ISpatialAudioClient next to IAudioClient and treats a failure as fatal (mfcore+0x250f4, callers +0x24ba1,
   +0xd0002) -> MF_E_NO_AUDIO_PLAYBACK_DEVICE. On Win10 that activation always succeeds; without Windows Sonic etc. the client just has
   no spatial stream. This one says exactly that: no formats, no objects, stream not available. */
static const GUID IID_ISpatialAudioClient_ = {0xBBF8E066, 0xAAAA, 0x49BE, {0x9A, 0x4D, 0xFD, 0x2A, 0x85, 0x8E, 0xA2, 0x7F}};
static const GUID IID_IAudioFormatEnumerator_ = {0xDCDAA858, 0x895A, 0x4A22, {0xA5, 0xEB, 0x67, 0xBD, 0xA5, 0x06, 0x09, 0x6D}};
#define SPTL_E_STREAM_NOT_AVAILABLE ((HRESULT)0x88890107L)
#define SPTL_E_UNSUPPORTED_FORMAT ((HRESULT)0x88890008L)   /* AUDCLNT_E_UNSUPPORTED_FORMAT */
typedef struct SpObj { const void *const *vt; LONG ref; } SpObj;
static HRESULT STDMETHODCALLTYPE spa_qi_any(SpObj *s, REFIID iid, const GUID *own, void **o) {
  static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
  if (!o) return E_POINTER; if (IsEqualGUID(iid, &iu) || IsEqualGUID(iid, own)) { *o = s; InterlockedIncrement(&s->ref); return S_OK; }
  { char g[48]; guid_str(iid, g); slog("[audio] spatial stand-in QI %s refused", g); } *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE spa_ar(SpObj *s) { return (ULONG)InterlockedIncrement(&s->ref); }
static ULONG STDMETHODCALLTYPE spa_rl(SpObj *s) { LONG n = InterlockedDecrement(&s->ref); if (!n) HeapFree(GetProcessHeap(), 0, s); return (ULONG)n; }
static HRESULT STDMETHODCALLTYPE spe_qi(SpObj *s, REFIID iid, void **o) { return spa_qi_any(s, iid, &IID_IAudioFormatEnumerator_, o); }
static HRESULT STDMETHODCALLTYPE spe_count(SpObj *s, UINT32 *n) { (void)s; slog("[audio] spatial stand-in: formats GetCount -> 0"); if (!n) return E_POINTER; *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE spe_get(SpObj *s, UINT32 i, void **f) { (void)s; slog("[audio] spatial stand-in: GetFormat(%u) -> E_INVALIDARG", i); if (f) *f = NULL; return E_INVALIDARG; }
static const void *const g_spe_vt[5] = { (void *)spe_qi, (void *)spa_ar, (void *)spa_rl, (void *)spe_count, (void *)spe_get };
static SpObj *spa_new(const void *const *vt) { SpObj *s = (SpObj *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *s); if (s) { s->vt = vt; s->ref = 1; } return s; }
static HRESULT STDMETHODCALLTYPE spc_qi(SpObj *s, REFIID iid, void **o) { return spa_qi_any(s, iid, &IID_ISpatialAudioClient_, o); }
static HRESULT STDMETHODCALLTYPE spc_pos(SpObj *s, int type, float *x, float *y, float *z) { (void)s; slog("[audio] spatial stand-in: GetStaticObjectPosition(%d) -> not available", type);
  if (x) *x = 0; if (y) *y = 0; if (z) *z = 0; return SPTL_E_STREAM_NOT_AVAILABLE; }
static HRESULT STDMETHODCALLTYPE spc_mask(SpObj *s, DWORD *m) { (void)s; slog("[audio] spatial stand-in: GetNativeStaticObjectTypeMask -> 0"); if (!m) return E_POINTER; *m = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE spc_maxdyn(SpObj *s, UINT32 *n) { (void)s; slog("[audio] spatial stand-in: GetMaxDynamicObjectCount -> 0"); if (!n) return E_POINTER; *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE spc_enum(SpObj *s, void **e) { (void)s; slog("[audio] spatial stand-in: GetSupportedAudioObjectFormatEnumerator -> empty"); if (!e) return E_POINTER;
  *e = spa_new(g_spe_vt); return *e ? S_OK : E_OUTOFMEMORY; }
static HRESULT STDMETHODCALLTYPE spc_maxfr(SpObj *s, const void *f, UINT32 *n) { (void)s; (void)f; slog("[audio] spatial stand-in: GetMaxFrameCount -> unsupported"); if (n) *n = 0; return SPTL_E_UNSUPPORTED_FORMAT; }
static HRESULT STDMETHODCALLTYPE spc_fmtok(SpObj *s, const void *f) { (void)s; (void)f; slog("[audio] spatial stand-in: IsAudioObjectFormatSupported -> unsupported"); return SPTL_E_UNSUPPORTED_FORMAT; }
static HRESULT STDMETHODCALLTYPE spc_avail(SpObj *s, REFIID id, const void *aux) { char g[48]; (void)s; (void)aux; guid_str(id, g); slog("[audio] spatial stand-in: IsSpatialAudioStreamAvailable(%s) -> not available", g); return SPTL_E_STREAM_NOT_AVAILABLE; }
static HRESULT STDMETHODCALLTYPE spc_act(SpObj *s, const void *p, REFIID id, void **o) { char g[48]; (void)s; (void)p; guid_str(id, g); slog("[audio] spatial stand-in: ActivateSpatialAudioStream(%s) -> not available", g); if (o) *o = NULL; return SPTL_E_STREAM_NOT_AVAILABLE; }
static const void *const g_spc_vt[11] = { (void *)spc_qi, (void *)spa_ar, (void *)spa_rl, (void *)spc_pos, (void *)spc_mask, (void *)spc_maxdyn, (void *)spc_enum, (void *)spc_maxfr,
  (void *)spc_fmtok, (void *)spc_avail, (void *)spc_act };
typedef HRESULT (STDMETHODCALLTYPE *PMMACT)(void *, REFIID, DWORD, void *, void **);
static PMMACT g_mm_activate;
static HRESULT STDMETHODCALLTYPE mmdev_activate(void *dev, REFIID iid, DWORD ctx, void *params, void **out) {
  HRESULT hr; IUnknown *ac = NULL; AC3 *w;
  if (!IsEqualGUID(iid, &IID_IAudioClient2_) && !IsEqualGUID(iid, &IID_IAudioClient3_)) { char g[48]; hr = g_mm_activate(dev, iid, ctx, params, out); guid_str(iid, g);
    slog("[audio] IMMDevice::Activate(%s, ctx 0x%lx, params %p) -> 0x%08lX", g, ctx, params, (unsigned long)hr);
    if (hr == E_NOINTERFACE && out && IsEqualGUID(iid, &IID_ISpatialAudioClient_) && (*out = spa_new(g_spc_vt)) != NULL) {
      slog("[audio]   -> ISpatialAudioClient stand-in (no spatial stream)"); return S_OK; }
    return hr; }
  if (!out) return E_POINTER; *out = NULL;
  if (FAILED(hr = g_mm_activate(dev, &IID_IAudioClient_, ctx, params, (void **)&ac))) return hr;
  if (!(w = (AC3 *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *w))) { ac->lpVtbl->Release(ac); return E_OUTOFMEMORY; }
  w->vt = g_ac_vt; w->ref = 1; w->in = ac; *out = w; slog("[audio] IMMDevice::Activate(IAudioClient%d) -> Win7 IAudioClient, wrapped", IsEqualGUID(iid, &IID_IAudioClient3_) ? 3 : 2); return S_OK; }
static void mmdev_patch(void) {
  static LONG done; void *e, *d = NULL; void **vt; DWORD old;
  if (InterlockedExchange(&done, 1) || !(e = mm_enum())) return;
  if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, int, int, void **))(*(void ***)e)[4])(e, 0, 1, &d)) && d) {   /* default render endpoint */
    vt = *(void ***)d;
    if (VirtualProtect(&vt[3], sizeof(void *), PAGE_READWRITE, &old)) { g_mm_activate = (PMMACT)vt[3]; vt[3] = (void *)mmdev_activate; VirtualProtect(&vt[3], sizeof(void *), old, &old); slog("[audio] Win7 endpoint device vtable patched: IAudioClient2/3 wrapped"); }
    ((IUnknown *)d)->lpVtbl->Release((IUnknown *)d); }
  ((IUnknown *)e)->lpVtbl->Release((IUnknown *)e); }
/* AvCreateTaskIndex (avrt, Win8+; rtworkq's MMCSS work queues: (task name, 0, &index) -> handle checked for NULL only). The stub left
   Win10 rtworkq's "Audio" queue unusable: the audio renderer started and never rendered a buffer. A token handle and index 0;
   the threads still register with Win7's AvSetMmThreadCharacteristicsW themselves. */
/* The task handle is an auto-reset event: rtworkq's MMCSS workers idle in AvTaskIndexYield(handle, index, timeout ms) and are woken by
   AvTaskIndexYieldCancel(handle) when work arrives (stubs returned at once: the "Audio" queue never ran the renderer's refill work,
   only the 200 ms preroll was heard). */
__declspec(dllexport) HANDLE WINAPI AvCreateTaskIndex(LPCWSTR task, DWORD reserved, LPDWORD index) { HANDLE e = CreateEventW(NULL, FALSE, FALSE, NULL); (void)reserved; if (index) *index = 0; slog("[audio] AvCreateTaskIndex(%ls) -> %p", task ? task : L"(null)", e); return e; }
/* On Win10 the audio engine wakes the task index every engine period (the SAR's render pump runs on those wakes; without them it rendered
   the preroll and never called GetCurrentPadding/GetBuffer again). Win7's engine knows no task indexes: a 10 ms period is emulated. */
__declspec(dllexport) BOOL WINAPI AvTaskIndexYield(HANDLE h, DWORD index, DWORD timeout) {
  DWORD w = 10 < timeout ? 10 : timeout; (void)index;
  if (h) WaitForSingleObject(h, w); return TRUE; }
__declspec(dllexport) BOOL WINAPI AvTaskIndexYieldCancel(HANDLE h) { if (h) SetEvent(h); return TRUE; }
__declspec(dllexport) BOOL WINAPI AvSetMultimediaMode(HANDLE h, BOOL on) { (void)h; (void)on; return TRUE; }
__declspec(dllexport) LONG WINAPI TpSetPoolThreadBasePriority(void *pool, ULONG prio) { (void)pool; (void)prio; return 0; }   /* STATUS_SUCCESS: a priority hint */
