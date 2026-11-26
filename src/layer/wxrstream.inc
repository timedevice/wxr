/* wxrstream.inc - shcore's CreateRandomAccessStreamOverStream (Win8+; Win7 has no shcore): a WinRT IRandomAccessStream over a COM IStream.
   Windows.Storage's InMemoryRandomAccessStream is SHCreateMemStream + this; with a stub it kept a NULL inner stream and crashed in WriteAsync
   once WinUI (seeing the real API set through ApiInformation) started streaming assets. The async operations complete synchronously: the work
   is done when ReadAsync/WriteAsync/FlushAsync return, and a Completed handler is invoked as soon as it is set. Included from wxrsys.c. */

static const GUID RS_IID_IRandomAccessStream = {0x905A0FE1, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID RS_IID_IInputStream = {0x905A0FE2, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID RS_IID_IOutputStream = {0x905A0FE6, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID RS_IID_IClosable = {0x30D5A829, 0x7FA4, 0x4026, {0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E}};
static const GUID RS_IID_IBufferByteAccess = {0x905A0FEF, 0xBC53, 0x11DF, {0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA}};
static const GUID RS_IID_IAsyncInfo = {0x00000036, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID RS_IID_IUnknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID RS_IID_IInspectable = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID RS_IID_IAgileObject = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static const GUID RS_IID_IRandomAccessStreamWithContentType = {0xCC254827, 0x4B3D, 0x438F, {0x92, 0x32, 0x10, 0xC7, 0x6B, 0xC7, 0xE0, 0x38}};
static const GUID RS_IID_IContentTypeProvider = {0x97D098A5, 0x3B99, 0x4DE9, {0x88, 0xA5, 0xE1, 0x1D, 0x2F, 0x50, 0xC7, 0x95}};
#define RS_VT(o, k, T) ((T)(*(void ***)(o))[k])
#ifndef RO_E_CLOSED
#define RO_E_CLOSED ((HRESULT)0x80000013L)
#endif
/* IIDs of the form {000000xx-0000-0000-C000-000000000046} other than the ones an object names are classic COM (IMarshal, ...): refused */
static int rs_classic_com(REFIID iid) { static const BYTE t[8] = {0xC0, 0, 0, 0, 0, 0, 0, 0x46}; return !memcmp(iid->Data4, t, 8) && !iid->Data2 && !iid->Data3; }
static HRESULT WINAPI rs_iids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT WINAPI rs_trust(void *t, int *l) { (void)t; if (l) *l = 0; return S_OK; }

/* ---- completed async operation: IAsyncOperation<T> / IAsyncOperationWithProgress<T,P> share the object; IAsyncInfo is a second vtable ----
   generic interface IIDs are owned by the callers, so any non-classic IID is answered with the operation vtable. */
typedef struct { const void *vt_op; const void *vt_info; LONG ref; HRESULT hr; int kind; /* 0 IBuffer*, 1 UINT32, 2 boolean, 3 UINT64 */ void *res_ptr; UINT32 res_u32; UINT64 res_u64; } RsOp;
#define RSOP_FROM_INFO(i) ((RsOp *)((BYTE *)(i) - offsetof(RsOp, vt_info)))
static HRESULT WINAPI rsop_qi(void *t, REFIID iid, void **o) {
  RsOp *p = (RsOp *)t; if (!o) return E_POINTER; *o = NULL;
  if (!memcmp(iid, &RS_IID_IAsyncInfo, 16)) *o = &p->vt_info;
  /* the operation's own interfaces are parameterized (IAsyncOperation<T>...): their IIDs are SHA-1 (version 5) GUIDs. Answering every
     non-classic IID gave the Store our vtable for another interface - it called slot 3 as Read(buf, 0x400, ...) and crashed in GetIids. */
  else if (!memcmp(iid, &RS_IID_IUnknown, 16) || !memcmp(iid, &RS_IID_IInspectable, 16) || !memcmp(iid, &RS_IID_IAgileObject, 16) || (!rs_classic_com(iid) && (iid->Data3 >> 12) == 5)) *o = p;
  else { char g[48]; static LONG n; guid_str(iid, g); if (InterlockedIncrement(&n) <= 20) slog("[stream] async operation QI %s refused", g); return E_NOINTERFACE; }
  InterlockedIncrement(&p->ref); return S_OK; }
static ULONG WINAPI rsop_ar(void *t) { return InterlockedIncrement(&((RsOp *)t)->ref); }
static ULONG WINAPI rsop_rl(void *t) { RsOp *p = (RsOp *)t; LONG r = InterlockedDecrement(&p->ref); if (!r) { if (p->res_ptr) ((IUnknown *)p->res_ptr)->lpVtbl->Release((IUnknown *)p->res_ptr); free(p); } return r; }
static HRESULT WINAPI rsop_name(void *t, void **hs) { (void)t; if (hs) *hs = NULL; return S_OK; }
static HRESULT WINAPI rsop_put_completed(void *t, void *h) {      /* handler->Invoke(op, status): Completed 1 / Error 3 */
  RsOp *p = (RsOp *)t; if (h) RS_VT(h, 3, HRESULT (WINAPI *)(void *, void *, int))(h, p, SUCCEEDED(p->hr) ? 1 : 3); return S_OK; }
static HRESULT WINAPI rsop_get_null(void *t, void **h) { (void)t; if (h) *h = NULL; return S_OK; }
static HRESULT WINAPI rsop_put_progress(void *t, void *h) { (void)t; (void)h; return S_OK; }
static HRESULT WINAPI rsop_results(void *t, void *out) {
  RsOp *p = (RsOp *)t; if (!out) return E_POINTER; if (FAILED(p->hr)) return p->hr;
  if (p->kind == 0) { *(void **)out = p->res_ptr; if (p->res_ptr) ((IUnknown *)p->res_ptr)->lpVtbl->AddRef((IUnknown *)p->res_ptr); }
  else if (p->kind == 1) *(UINT32 *)out = p->res_u32; else if (p->kind == 3) *(UINT64 *)out = p->res_u64; else *(boolean *)out = (boolean)(p->res_u32 != 0);
  return S_OK; }
/* IAsyncOperation<T>: put_Completed, get_Completed, GetResults; ...WithProgress<T,P>: put_Progress, get_Progress first */
static const void *const rsop_vt_plain[] = { rsop_qi, rsop_ar, rsop_rl, rs_iids, rsop_name, rs_trust, rsop_put_completed, rsop_get_null, rsop_results };
static const void *const rsop_vt_prog[] = { rsop_qi, rsop_ar, rsop_rl, rs_iids, rsop_name, rs_trust, rsop_put_progress, rsop_get_null, rsop_put_completed, rsop_get_null, rsop_results };
static HRESULT WINAPI rsi_qi(void *t, REFIID iid, void **o) { return rsop_qi(RSOP_FROM_INFO(t), iid, o); }
static ULONG WINAPI rsi_ar(void *t) { return rsop_ar(RSOP_FROM_INFO(t)); }
static ULONG WINAPI rsi_rl(void *t) { return rsop_rl(RSOP_FROM_INFO(t)); }
static HRESULT WINAPI rsi_id(void *t, UINT32 *v) { (void)t; if (v) *v = 1; return S_OK; }
static HRESULT WINAPI rsi_status(void *t, int *v) { if (v) *v = SUCCEEDED(RSOP_FROM_INFO(t)->hr) ? 1 : 3; return S_OK; }
static HRESULT WINAPI rsi_error(void *t, HRESULT *v) { if (v) *v = RSOP_FROM_INFO(t)->hr; return S_OK; }
static HRESULT WINAPI rsi_nop(void *t) { (void)t; return S_OK; }
static const void *const rsop_vt_info[] = { rsi_qi, rsi_ar, rsi_rl, rs_iids, rsop_name, rs_trust, rsi_id, rsi_status, rsi_error, rsi_nop, rsi_nop };
static HRESULT rsop_make(int progress, int kind, HRESULT hr, void *res_ptr, UINT32 res_u32, void **o) {
  RsOp *p = (RsOp *)calloc(1, sizeof *p); if (!p) { if (res_ptr) ((IUnknown *)res_ptr)->lpVtbl->Release((IUnknown *)res_ptr); return E_OUTOFMEMORY; }
  p->vt_op = progress ? (const void *)rsop_vt_prog : (const void *)rsop_vt_plain; p->vt_info = rsop_vt_info; p->ref = 1; p->hr = hr; p->kind = kind; p->res_ptr = res_ptr; p->res_u32 = res_u32;
  *o = p; return S_OK; }

/* ---- the stream: one object, four vtables (IRandomAccessStream, IInputStream, IOutputStream, IClosable) ---- */
/* every wrapper keeps its own position over a shared IStream (seek + read under one lock): GetInputStreamAt used IStream::Clone, which
   Win7's file stream does not do as a real independent clone - Media Foundation's reads then ignored the requested positions and the
   FLAC source fed the decoder from the wrong offsets (no frame was ever decoded; Media Player stuck "loading") */
typedef struct { const void *vt_ras, *vt_in, *vt_out, *vt_cl, *vt_ct; LONG ref; IStream *s; UINT64 pos; } RsStream;
static SRWLOCK g_rs_lock = SRWLOCK_INIT;
#define RS_FROM(i, f) ((RsStream *)((BYTE *)(i) - offsetof(RsStream, f)))
static HRESULT rs_new(IStream *s, void **o);
static HRESULT rs_qi_(RsStream *r, REFIID iid, void **o) {
  if (!o) return E_POINTER; *o = NULL;
  if (!memcmp(iid, &RS_IID_IRandomAccessStream, 16) || !memcmp(iid, &RS_IID_IUnknown, 16) || !memcmp(iid, &RS_IID_IInspectable, 16) || !memcmp(iid, &RS_IID_IAgileObject, 16)) *o = &r->vt_ras;
  else if (!memcmp(iid, &RS_IID_IInputStream, 16)) *o = &r->vt_in;
  else if (!memcmp(iid, &RS_IID_IOutputStream, 16)) *o = &r->vt_out;
  else if (!memcmp(iid, &RS_IID_IClosable, 16)) *o = &r->vt_cl;
  else if (!memcmp(iid, &RS_IID_IRandomAccessStreamWithContentType, 16)) *o = &r->vt_ras;   /* no methods of its own (requires IRandomAccessStream, IContentTypeProvider ...) */
  else if (!memcmp(iid, &RS_IID_IContentTypeProvider, 16)) *o = &r->vt_ct;
  else if (!memcmp(iid, "\x0c\0\0\0\0\0\0\0\xC0\0\0\0\0\0\0\x46", 16) && r->s) { r->s->lpVtbl->AddRef(r->s); *o = r->s; return S_OK; }   /* the COM stream underneath (Media Foundation's byte stream reads it directly) */
  else { char g[48]; guid_str(iid, g); if ((iid->Data3 >> 12) != 5 && memcmp(&iid->Data2, "\0\0\0\0\xC0\0\0\0\0\0\0\x46", 12)) slog("[stream] RandomAccessStream QI %s refused", g); return E_NOINTERFACE; }
  InterlockedIncrement(&r->ref); return S_OK; }
static ULONG rs_rl_(RsStream *r) { LONG n = InterlockedDecrement(&r->ref); if (!n) { if (r->s) r->s->lpVtbl->Release(r->s); free(r); } return n; }
#define RS_IUNK(f, pre) \
  static HRESULT WINAPI pre##_qi(void *t, REFIID iid, void **o) { return rs_qi_(RS_FROM(t, f), iid, o); } \
  static ULONG WINAPI pre##_ar(void *t) { return InterlockedIncrement(&RS_FROM(t, f)->ref); } \
  static ULONG WINAPI pre##_rl(void *t) { return rs_rl_(RS_FROM(t, f)); }
RS_IUNK(vt_ras, rsa) RS_IUNK(vt_in, rsn) RS_IUNK(vt_out, rso) RS_IUNK(vt_cl, rsc) RS_IUNK(vt_ct, rst)
static HRESULT WINAPI rs_name(void *t, void **hs) { (void)t; if (!hs || !hs_init()) return E_FAIL; return p_create(L"Windows.Storage.Streams.RandomAccessStream", 41, hs); }
static HRESULT rs_seek(IStream *s, UINT64 pos) { LARGE_INTEGER li; li.QuadPart = (LONGLONG)pos; return s ? s->lpVtbl->Seek(s, li, STREAM_SEEK_SET, NULL) : RO_E_CLOSED; }
static HRESULT WINAPI rsa_get_size(void *t, UINT64 *v) { RsStream *r = RS_FROM(t, vt_ras); STATSTG st; HRESULT hr;
  if (!v) return E_POINTER; if (!r->s) return RO_E_CLOSED; memset(&st, 0, sizeof st); hr = r->s->lpVtbl->Stat(r->s, &st, STATFLAG_NONAME); *v = st.cbSize.QuadPart; return hr; }
static HRESULT WINAPI rsa_put_size(void *t, UINT64 v) { RsStream *r = RS_FROM(t, vt_ras); ULARGE_INTEGER u; u.QuadPart = v; return r->s ? r->s->lpVtbl->SetSize(r->s, u) : RO_E_CLOSED; }
static HRESULT rs_clone_at(RsStream *r, UINT64 pos, const GUID *iid, void **o) {
  void *n = NULL; HRESULT hr; RsStream *c; if (!o) return E_POINTER; *o = NULL; if (!r->s) return RO_E_CLOSED;
  if (FAILED(hr = rs_new(r->s, &n))) return hr;   /* same IStream, own position */
  c = (RsStream *)((BYTE *)n - offsetof(RsStream, vt_ras)); c->pos = pos;
  hr = rs_qi_(c, iid, o); rs_rl_(c); return hr; }
static HRESULT WINAPI rsa_input_at(void *t, UINT64 pos, void **o) { return rs_clone_at(RS_FROM(t, vt_ras), pos, &RS_IID_IInputStream, o); }
static HRESULT WINAPI rsa_output_at(void *t, UINT64 pos, void **o) { return rs_clone_at(RS_FROM(t, vt_ras), pos, &RS_IID_IOutputStream, o); }
static HRESULT WINAPI rsa_get_pos(void *t, UINT64 *v) { RsStream *r = RS_FROM(t, vt_ras); if (!v) return E_POINTER; if (!r->s) return RO_E_CLOSED; *v = r->pos; return S_OK; }
static HRESULT WINAPI rsa_seek(void *t, UINT64 pos) { RsStream *r = RS_FROM(t, vt_ras); if (!r->s) return RO_E_CLOSED; r->pos = pos; return S_OK; }
static HRESULT WINAPI rsa_clone(void *t, void **o) { return rs_clone_at(RS_FROM(t, vt_ras), 0, &RS_IID_IRandomAccessStream, o); }   /* clones start at 0 */
static HRESULT WINAPI rsa_true(void *t, boolean *v) { (void)t; if (v) *v = 1; return S_OK; }
/* IInputStream::ReadAsync(buffer, count, options, op): reads straight into the caller's buffer, which is the operation's result */
static HRESULT WINAPI rsn_read(void *t, void *buf, UINT32 count, int opts, void **op) {
  RsStream *r = RS_FROM(t, vt_in); void *acc = NULL; BYTE *data = NULL; UINT32 cap = 0; ULONG got = 0; HRESULT hr; (void)opts;
  if (!buf || !op) return E_POINTER; *op = NULL; if (!r->s) return RO_E_CLOSED;
  RS_VT(buf, 6, HRESULT (WINAPI *)(void *, UINT32 *))(buf, &cap);                                  /* IBuffer::get_Capacity */
  if (count > cap) count = cap;
  if (FAILED(hr = ((IUnknown *)buf)->lpVtbl->QueryInterface((IUnknown *)buf, &RS_IID_IBufferByteAccess, &acc))) { slog("[stream] ReadAsync: buffer has no IBufferByteAccess 0x%08lX", (unsigned long)hr); return hr; }
  hr = RS_VT(acc, 3, HRESULT (WINAPI *)(void *, BYTE **))(acc, &data); ((IUnknown *)acc)->lpVtbl->Release((IUnknown *)acc);
  if (SUCCEEDED(hr) && count) { AcquireSRWLockExclusive(&g_rs_lock); hr = rs_seek(r->s, r->pos); if (SUCCEEDED(hr)) hr = r->s->lpVtbl->Read(r->s, data, count, &got); if (hr == S_FALSE) hr = S_OK; if (SUCCEEDED(hr)) r->pos += got; ReleaseSRWLockExclusive(&g_rs_lock); }
  else if (FAILED(hr)) slog("[stream] ReadAsync: IBufferByteAccess::Buffer 0x%08lX", (unsigned long)hr);
  if (SUCCEEDED(hr)) RS_VT(buf, 8, HRESULT (WINAPI *)(void *, UINT32))(buf, (UINT32)got);         /* IBuffer::put_Length */
  { static LONG n; if (InterlockedIncrement(&n) <= 400) { LARGE_INTEGER z; ULARGE_INTEGER p; z.QuadPart = 0; p.QuadPart = 0; r->s->lpVtbl->Seek(r->s, z, STREAM_SEEK_CUR, &p);
      slog("[stream] ReadAsync(count %u, cap %u) -> got %lu hr 0x%08lX, now at %llu, first %02X %02X %02X %02X", count, cap, got, (unsigned long)hr, (unsigned long long)p.QuadPart, got > 0 ? data[0] : 0, got > 1 ? data[1] : 0, got > 2 ? data[2] : 0, got > 3 ? data[3] : 0); } }
  ((IUnknown *)buf)->lpVtbl->AddRef((IUnknown *)buf);
  return rsop_make(1, 0, hr, buf, 0, op); }
/* IOutputStream::WriteAsync(buffer, op) -> bytes written; FlushAsync(op) -> true */
static HRESULT WINAPI rso_write(void *t, void *buf, void **op) {
  RsStream *r = RS_FROM(t, vt_out); void *acc = NULL; BYTE *data = NULL; UINT32 len = 0; ULONG put = 0; HRESULT hr;
  if (!buf || !op) return E_POINTER; *op = NULL; if (!r->s) return RO_E_CLOSED;
  RS_VT(buf, 7, HRESULT (WINAPI *)(void *, UINT32 *))(buf, &len);                                  /* IBuffer::get_Length */
  if (FAILED(hr = ((IUnknown *)buf)->lpVtbl->QueryInterface((IUnknown *)buf, &RS_IID_IBufferByteAccess, &acc))) return hr;
  hr = RS_VT(acc, 3, HRESULT (WINAPI *)(void *, BYTE **))(acc, &data); ((IUnknown *)acc)->lpVtbl->Release((IUnknown *)acc);
  if (SUCCEEDED(hr) && len) { AcquireSRWLockExclusive(&g_rs_lock); hr = rs_seek(r->s, r->pos); if (SUCCEEDED(hr)) hr = r->s->lpVtbl->Write(r->s, data, len, &put); if (SUCCEEDED(hr)) r->pos += put; ReleaseSRWLockExclusive(&g_rs_lock); }
  return rsop_make(1, 1, hr, NULL, (UINT32)put, op); }
static HRESULT WINAPI rso_flush(void *t, void **op) { RsStream *r = RS_FROM(t, vt_out); HRESULT hr;
  if (!op) return E_POINTER; *op = NULL; if (!r->s) return RO_E_CLOSED; hr = r->s->lpVtbl->Commit(r->s, STGC_DEFAULT); if (hr == E_NOTIMPL) hr = S_OK;
  return rsop_make(0, 2, hr, NULL, SUCCEEDED(hr), op); }
static HRESULT WINAPI rsc_close(void *t) { RsStream *r = RS_FROM(t, vt_cl); IStream *s = (IStream *)InterlockedExchangePointer((void **)&r->s, NULL); if (s) s->lpVtbl->Release(s); return S_OK; }
static const void *const rs_vt_ras[] = { rsa_qi, rsa_ar, rsa_rl, rs_iids, rs_name, rs_trust,
  rsa_get_size, rsa_put_size, rsa_input_at, rsa_output_at, rsa_get_pos, rsa_seek, rsa_clone, rsa_true, rsa_true };
static const void *const rs_vt_in[] = { rsn_qi, rsn_ar, rsn_rl, rs_iids, rs_name, rs_trust, rsn_read };
static const void *const rs_vt_out[] = { rso_qi, rso_ar, rso_rl, rs_iids, rs_name, rs_trust, rso_write, rso_flush };
static const void *const rs_vt_cl[] = { rsc_qi, rsc_ar, rsc_rl, rs_iids, rs_name, rs_trust, rsc_close };
/* IContentTypeProvider::get_ContentType: from the file name's extension (registry "Content Type", then a short list Win7 lacks), else
   application/octet-stream. StorageFile.OpenReadAsync promises an IRandomAccessStreamWithContentType (the app cast failed without it). */
static HRESULT WINAPI rst_type(void *t, void **hs) {
  RsStream *r = RS_FROM(t, vt_ct); STATSTG st; WCHAR ct[96] = L"application/octet-stream"; const WCHAR *ext = NULL;
  static const WCHAR *const known[][2] = { {L".flac", L"audio/flac"}, {L".ogg", L"audio/ogg"}, {L".opus", L"audio/ogg"}, {L".m4a", L"audio/mp4"}, {L".mka", L"audio/x-matroska"}, {L".mkv", L"video/x-matroska"} };
  if (!hs || !hs_init()) return E_POINTER; *hs = NULL;
  memset(&st, 0, sizeof st);
  if (r->s && SUCCEEDED(r->s->lpVtbl->Stat(r->s, &st, STATFLAG_DEFAULT)) && st.pwcsName) ext = wcsrchr(st.pwcsName, L'.');
  if (ext) { DWORD cb = sizeof ct; unsigned k; int got = 0; WCHAR tmp[96];
    for (k = 0; k < sizeof known / sizeof known[0]; k++) if (!lstrcmpiW(ext, known[k][0])) { lstrcpynW(ct, known[k][1], 96); got = 1; break; }
    if (!got && RegGetValueW(HKEY_CLASSES_ROOT, ext, L"Content Type", RRF_RT_REG_SZ, NULL, tmp, &cb) == ERROR_SUCCESS) lstrcpynW(ct, tmp, 96); }
  if (st.pwcsName) CoTaskMemFree(st.pwcsName);
  return p_create(ct, (UINT32)lstrlenW(ct), hs); }
static const void *const rs_vt_ct[] = { rst_qi, rst_ar, rst_rl, rs_iids, rs_name, rs_trust, rst_type };
/* returns the IRandomAccessStream face with one reference */
static HRESULT rs_new(IStream *s, void **o) {
  RsStream *r = (RsStream *)calloc(1, sizeof *r); if (!r) return E_OUTOFMEMORY;
  r->vt_ras = rs_vt_ras; r->vt_in = rs_vt_in; r->vt_out = rs_vt_out; r->vt_cl = rs_vt_cl; r->vt_ct = rs_vt_ct; r->ref = 1; r->s = s; s->lpVtbl->AddRef(s);
  { LARGE_INTEGER z; ULARGE_INTEGER p; z.QuadPart = 0; p.QuadPart = 0; if (SUCCEEDED(s->lpVtbl->Seek(s, z, STREAM_SEEK_CUR, &p))) r->pos = p.QuadPart; }   /* starts where the stream is */
  *o = &r->vt_ras; return S_OK; }
/* HRESULT CreateRandomAccessStreamOverStream(IStream *, BSOS_OPTIONS, REFIID, void **): options (BSOS_PREFERDESTINATIONSTREAM) do not matter here */
__declspec(dllexport) HRESULT WINAPI CreateRandomAccessStreamOverStream(IStream *s, int opts, REFIID iid, void **o) {
  void *n = NULL; HRESULT hr; (void)opts; if (!o) return E_POINTER; *o = NULL; if (!s) return E_INVALIDARG;
  if (FAILED(hr = rs_new(s, &n))) return hr;
  hr = rs_qi_(RS_FROM(n, vt_ras), iid, o); rs_rl_(RS_FROM(n, vt_ras)); return hr; }
/* HRESULT CreateRandomAccessStreamOnFile(PCWSTR path, FileAccessMode mode, REFIID, void **): mode 0 Read, 1 ReadWrite (opens or creates,
   no truncation - Windows.Storage sets Size itself). A Win7 shlwapi file stream wrapped like any other IStream. */
__declspec(dllexport) HRESULT WINAPI CreateRandomAccessStreamOnFile(LPCWSTR path, DWORD mode, REFIID iid, void **o) {
  typedef HRESULT (WINAPI *PSF)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **); static PSF sf; IStream *st = NULL; HRESULT hr;
  if (!o) return E_POINTER; *o = NULL; if (!path) return E_INVALIDARG;
  if (!sf) sf = (PSF)GetProcAddress(LoadLibraryA("shlwapi.dll"), "SHCreateStreamOnFileEx"); if (!sf) return E_NOTIMPL;
  /* ReadWrite: open the existing file, else create it (fCreate=TRUE without STGM_CREATE fails on an existing file: 0x80070050) */
  if (mode) { hr = sf(path, STGM_READWRITE | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &st);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) hr = sf(path, STGM_READWRITE | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, TRUE, NULL, &st); }
  else hr = sf(path, STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, NULL, &st);
  if (SUCCEEDED(hr)) { hr = CreateRandomAccessStreamOverStream(st, 0, iid, o); st->lpVtbl->Release(st); }
  slog("[stream] CreateRandomAccessStreamOnFile(%ls, mode %lu) -> 0x%08lX", path, (unsigned long)mode, (unsigned long)hr); return hr; }
/* shcore #100 SHManagedCreateStreamOnFile(path, grfMode, attributes, fCreate, IStream *template, DWORD flags, IStream **out):
   SHCreateStreamOnFileEx plus a flags DWORD (19041 windows.storage InitializeFileHandlerWithStream passes 7 arguments: NULL template,
   flags 0/1/0x11). Win10 windows.storage opens files for property handlers with it. Win10 shcore's own needs an oplock provider Win7
   lacks, so the loader binds this one (forced list); with 6 parameters `flags` was taken for the out pointer (an access violation). */
__declspec(dllexport) HRESULT WINAPI SHManagedCreateStreamOnFile(LPCWSTR path, DWORD mode, DWORD attrs, BOOL create, IStream *tmpl, DWORD flags, IStream **out) {
  typedef HRESULT (WINAPI *PSF)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **); static PSF sf; HRESULT hr; (void)flags; (void)tmpl;
  if (!out) return E_POINTER; *out = NULL;
  if (!sf) sf = (PSF)GetProcAddress(LoadLibraryA("shlwapi.dll"), "SHCreateStreamOnFileEx"); if (!sf) return E_NOTIMPL;
  /* read-only opens share everything: Win10 asks "deny write" and relies on oplocks (which Win7 has no provider for here) to step aside;
     with a plain deny-write open, reading tags of the track being played failed with a sharing violation (0x80070020) */
  if ((mode & 3) == STGM_READ && !create) mode = (mode & ~0x70u) | STGM_SHARE_DENY_NONE;
  hr = sf(path, mode, attrs, create, NULL, out);
  { static LONG n; if (InterlockedIncrement(&n) <= 20 || FAILED(hr)) slog("[stream] SHManagedCreateStreamOnFile(%ls, mode 0x%lx) -> 0x%08lX", path ? path : L"?", mode, (unsigned long)hr); }
  return hr; }
/* shcore #115 (named via ordmap): same as above, the access mode is the first DWORD of an options struct (19041 shcore reads only that
   for the mode; the rest goes to its internal open and is ignored here) */
__declspec(dllexport) HRESULT WINAPI CreateRandomAccessStreamOnFileWithOptions(LPCWSTR path, const DWORD *opts, REFIID iid, void **o) {
  return CreateRandomAccessStreamOnFile(path, opts ? opts[0] : 0, iid, o); }
/* HRESULT CreateStreamOverRandomAccessStream(IUnknown *ras, REFIID, void **): the reverse - a COM IStream over a WinRT IRandomAccessStream.
   App Installer opens the package file as a StorageFile stream and hands this IStream to AppxPackaging's bundle reader; as a logging thunk
   it returned S_OK with no stream ("Cannot open app package: error in parsing", CreateBundleReader -> E_POINTER). Our streams carry the
   IStream they were made over: that one is returned, at the random-access stream's current position. Foreign streams are refused. */
__declspec(dllexport) HRESULT WINAPI CreateStreamOverRandomAccessStream(IUnknown *ras, REFIID iid, void **o) {
  IStream *s = NULL; HRESULT hr;
  if (!o) return E_POINTER; *o = NULL; if (!ras) return E_INVALIDARG;
  if (*(const void **)ras == (const void *)rs_vt_ras) {   /* one of ours: the shared IStream, moved to this wrapper's position */
    RsStream *r = RS_FROM(ras, vt_ras); LARGE_INTEGER li;
    if (!r->s) return RO_E_CLOSED;
    AcquireSRWLockExclusive(&g_rs_lock); li.QuadPart = (LONGLONG)r->pos; r->s->lpVtbl->Seek(r->s, li, STREAM_SEEK_SET, NULL); ReleaseSRWLockExclusive(&g_rs_lock);
    hr = r->s->lpVtbl->QueryInterface(r->s, iid, o); }
  else if (SUCCEEDED(hr = ras->lpVtbl->QueryInterface(ras, (REFIID)"\x0c\0\0\0\0\0\0\0\xC0\0\0\0\0\0\0\x46", (void **)&s)) && s) {   /* IID_IStream */ hr = s->lpVtbl->QueryInterface(s, iid, o); s->lpVtbl->Release(s); }
  else hr = E_NOTIMPL;
  slog("[stream] CreateStreamOverRandomAccessStream(%s) -> 0x%08lX", *(const void **)ras == (const void *)rs_vt_ras ? "WXR stream" : "foreign stream", (unsigned long)hr);
  return hr; }
/* the WXR stream behind any of its interface pointers (IRandomAccessStream, IInputStream, IOutputStream, IClosable, IContentTypeProvider) */
static RsStream *rs_of(void *p) {
  const void *vt = p ? *(const void **)p : NULL;
  if (vt == (const void *)rs_vt_ras) return RS_FROM(p, vt_ras); if (vt == (const void *)rs_vt_in) return RS_FROM(p, vt_in);
  if (vt == (const void *)rs_vt_out) return RS_FROM(p, vt_out); if (vt == (const void *)rs_vt_cl) return RS_FROM(p, vt_cl);
  if (vt == (const void *)rs_vt_ct) return RS_FROM(p, vt_ct); return NULL; }
/* shcore #116 RandomAccessStreamCopyAsync(IInputStream *src, IOutputStream *dst, UINT64 bytes, DWORD options, REFIID, void **op):
   Windows.Storage's RandomAccessStream.CopyAsync / CopySizeAsync (options 0) / CopyAndCloseAsync (3 = close source 1 | destination 2).
   As a logging thunk it returned S_OK with no operation (App Installer vanished right after). Done synchronously between WXR streams
   (each wrapper keeps its own position over its IStream); the operation is born completed with the byte count (UINT64). */
__declspec(dllexport) HRESULT WINAPI RandomAccessStreamCopyAsync(IUnknown *src, IUnknown *dst, UINT64 bytes, DWORD options, REFIID iid, void **op) {
  RsStream *a = rs_of(src), *b = rs_of(dst); UINT64 total = 0; HRESULT hr = S_OK; BYTE buf[65536]; RsOp *p;
  (void)iid; if (!op) return E_POINTER; *op = NULL; if (!src || !dst) return E_INVALIDARG;
  if (!a || !b) { slog("[stream] RandomAccessStreamCopyAsync: %s stream - not supported", !a ? "source is a foreign" : "destination is a foreign"); return E_NOTIMPL; }
  AcquireSRWLockExclusive(&g_rs_lock);
  if (!a->s || !b->s) hr = RO_E_CLOSED;
  while (SUCCEEDED(hr) && total < bytes) {
    ULONG want = (ULONG)((bytes - total) < sizeof buf ? (bytes - total) : sizeof buf), got = 0, put = 0;
    if (FAILED(hr = rs_seek(a->s, a->pos)) || FAILED(hr = a->s->lpVtbl->Read(a->s, buf, want, &got))) break;
    if (!got) { hr = S_OK; break; }   /* end of the source (bytes = all: UINT64 max) */
    if (FAILED(hr = rs_seek(b->s, b->pos)) || FAILED(hr = b->s->lpVtbl->Write(b->s, buf, got, &put))) break;
    a->pos += got; b->pos += put; total += put; if (put < got) { hr = STG_E_MEDIUMFULL; break; } }
  ReleaseSRWLockExclusive(&g_rs_lock);
  if (SUCCEEDED(hr)) { if (options & 1) rsc_close(&a->vt_cl); if (options & 2) rsc_close(&b->vt_cl); }
  slog("[stream] RandomAccessStreamCopyAsync(options %lu) -> %llu bytes, 0x%08lX", (unsigned long)options, (unsigned long long)total, (unsigned long)hr);
  if (FAILED(rsop_make(1, 3, hr, NULL, 0, op))) return E_OUTOFMEMORY;
  p = (RsOp *)*op; p->res_u64 = total;
  return S_OK; }
