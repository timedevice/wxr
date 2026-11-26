/* wxrstorage.inc - a small Windows.Storage.StorageFolder for Package.InstalledLocation, included by wxrsys.c.
   Full-trust Windows.ApplicationModel builds InstalledLocation with
     CreateStorageItemFromShellItem_FullTrustCaller_ForPackage(IShellItem *item, ?, PCWSTR package, REFIID riid (IStorageFolder), void **out)
   (ext-ms-win-shell-shell32-l1-2-0). Win10 hosts it in windows.storage.dll, whose own shell namespace needs Win10 shell infrastructure
   (it crashes or hangs on Win7). The consumers here only read Path/Name (.NET Native resource lookup); GraphControl's GetFileAsync for its
   shaders (graphing mode) is logged so it shows up when needed. */
typedef struct SFolder { const void *const *vt_folder; const void *const *vt_item; const void *const *vt_folder2; LONG ref; WCHAR *path; } SFolder;
static const GUID IID_IStorageFolder_ = {0x72d1cb78, 0xb3ef, 0x4f75, {0xa8, 0x0b, 0x6f, 0xd9, 0xda, 0xe2, 0x94, 0x4b}};
static const GUID IID_IStorageItem_ = {0x4207a996, 0xca2f, 0x42f7, {0xbd, 0xe8, 0x8b, 0x10, 0x45, 0x7a, 0x7f, 0x30}};
static const GUID IID_IStorageFolder2_ = {0xe827e8b9, 0x08d9, 0x4a8e, {0xa0, 0xac, 0xfe, 0x5e, 0xd3, 0xcb, 0xbb, 0xd3}};
static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID IID_IInspectable_sf = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID IID_IUnknown_sf = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
#define SF_OF_ITEM(p) ((SFolder *)((char *)(p) - offsetof(SFolder, vt_item)))
#define SF_OF_F2(p) ((SFolder *)((char *)(p) - offsetof(SFolder, vt_folder2)))
static HRESULT sf_qi(SFolder *f, REFIID r, void **o) {
  if (!o) return E_POINTER;
  if (IsEqualGUID(r, &IID_IUnknown_sf) || IsEqualGUID(r, &IID_IInspectable_sf) || IsEqualGUID(r, &IID_IStorageFolder_) || IsEqualGUID(r, &IID_IAgileObject_)) *o = &f->vt_folder;
  else if (IsEqualGUID(r, &IID_IStorageItem_)) *o = &f->vt_item;
  else if (IsEqualGUID(r, &IID_IStorageFolder2_)) *o = &f->vt_folder2;
  else { char g[48]; *o = NULL; guid_str(r, g); slog("[storage] StorageFolder QI %s refused", g); return E_NOINTERFACE; }
  InterlockedIncrement(&f->ref); return S_OK; }
static ULONG sf_release(SFolder *f) { LONG n = InterlockedDecrement(&f->ref); if (!n) { HeapFree(GetProcessHeap(), 0, f->path); HeapFree(GetProcessHeap(), 0, f); } return (ULONG)n; }
/* IStorageFolder view */
static HRESULT WINAPI sff_qi(SFolder *f, REFIID r, void **o) { return sf_qi(f, r, o); }
static ULONG WINAPI sff_ar(SFolder *f) { return (ULONG)InterlockedIncrement(&f->ref); }
static ULONG WINAPI sff_rl(SFolder *f) { return sf_release(f); }
static HRESULT WINAPI sf_iids(void *t, ULONG *n, GUID **ids) { (void)t; if (n) *n = 0; if (ids) *ids = NULL; return S_OK; }
static HRESULT WINAPI sf_name_cls(void *t, void **hs) { static const WCHAR c[] = L"Windows.Storage.StorageFolder"; (void)t; if (!hs_init()) return E_FAIL; return p_create(c, (UINT32)(sizeof c / 2 - 1), hs); }
static HRESULT WINAPI sf_trust(void *t, int *lvl) { (void)t; if (lvl) *lvl = 0; return S_OK; }
#define SFF_NI(k, nm) static HRESULT WINAPI sff_ni##k(SFolder *f) { (void)f; slog("[storage] InstalledLocation IStorageFolder slot %d %s not implemented", k, nm); return E_NOTIMPL; }
/* Files in these folders are real Win10 StorageFiles: the file is created / found on disk here, the object comes from Win10 windows.storage's
   StorageFile.GetFileFromPathAsync (its operation is handed back as is: the same IAsyncOperation<StorageFile>). */
__declspec(dllexport) HRESULT WINAPI RoGetActivationFactory(void *hs, const GUID *iid, void **out);
static HRESULT done_op(HRESULT hr, IUnknown *result, void **out);
static HRESULT sf_create(const WCHAR *path, REFIID riid, void **out);
static HRESULT sf_real_file_op(const WCHAR *path, void **op) {
  static const GUID iid_sfs = {0x5984C710, 0xDAF2, 0x43C8, {0x8B, 0xB4, 0xA4, 0xD3, 0xEA, 0xCF, 0xD0, 0x3F}};   /* IStorageFileStatics */
  static const WCHAR cls[] = L"Windows.Storage.StorageFile"; void *hc = NULL, *hp = NULL, *st = NULL; HRESULT hr;
  if (!hs_init()) return E_FAIL;
  p_create(cls, (UINT32)(sizeof cls / 2 - 1), &hc); p_create(path, (UINT32)wcslen(path), &hp);
  hr = RoGetActivationFactory(hc, &iid_sfs, &st);
  if (SUCCEEDED(hr) && st) { hr = ((HRESULT (WINAPI *)(void *, void *, void **))(*(void ***)st)[6])(st, hp, op); ((IUnknown *)st)->lpVtbl->Release((IUnknown *)st); }
  { typedef HRESULT (WINAPI *PDEL)(void *); static PDEL del; if (!del) { HMODULE m = GetModuleHandleA("api-ms-win-core-winrt-string-l1-1-0.dll"); if (!m) m = GetModuleHandleA("combase.dll"); if (m) del = (PDEL)GetProcAddress(m, "WindowsDeleteString"); }
    if (del) { if (hc) del(hc); if (hp) del(hp); } }
  return hr; }
/* CreateFileAsync(name[, CreationCollisionOption: 0 GenerateUniqueName, 1 ReplaceExisting, 2 FailIfExists (default), 3 OpenIfExists]) */
static HRESULT sf_createfile(SFolder *f, void *name, int opt, void **op) {
  WCHAR p[MAX_PATH]; const WCHAR *n = name && hs_init() ? p_raw(name, NULL) : L""; HANDLE h; DWORD disp; int k; HRESULT hr;
  if (!op) return E_POINTER; *op = NULL;
  _snwprintf(p, MAX_PATH, L"%ls\\%ls", f->path, n);
  if (opt == 0 && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {   /* "name (2).ext" */
    const WCHAR *dot = wcsrchr(n, L'.'); int stem = dot ? (int)(dot - n) : (int)wcslen(n);
    for (k = 2; k < 1000; k++) { _snwprintf(p, MAX_PATH, L"%ls\\%.*ls (%d)%ls", f->path, stem, n, k, dot ? dot : L""); if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) break; } }
  disp = opt == 1 ? CREATE_ALWAYS : opt == 3 ? OPEN_ALWAYS : CREATE_NEW;
  h = CreateFileW(p, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) { hr = HRESULT_FROM_WIN32(GetLastError()); slog("[storage] CreateFileAsync(%ls, %d) -> 0x%08lX", p, opt, (unsigned long)hr); return done_op(hr, NULL, op); }
  CloseHandle(h);
  hr = sf_real_file_op(p, op); slog("[storage] CreateFileAsync(%ls, %d) -> real StorageFile 0x%08lX", p, opt, (unsigned long)hr); return hr; }
static HRESULT WINAPI sff_ni6(SFolder *f, void *name, void **op) { return sf_createfile(f, name, 2, op); }
static HRESULT WINAPI sff_ni7(SFolder *f, void *name, int opt, void **op) { return sf_createfile(f, name, opt, op); }
/* CreateFolderAsync(name[, option]) / GetFolderAsync(name): subfolders are stand-ins too */
static HRESULT sf_createfolder(SFolder *f, void *name, int opt, void **op) {
  WCHAR p[MAX_PATH]; const WCHAR *n = name && hs_init() ? p_raw(name, NULL) : L""; IUnknown *sub = NULL; HRESULT hr; BOOL made;
  if (!op) return E_POINTER; _snwprintf(p, MAX_PATH, L"%ls\\%ls", f->path, n);
  made = CreateDirectoryW(p, NULL);
  if (!made && (GetLastError() != ERROR_ALREADY_EXISTS || opt == 2)) return done_op(HRESULT_FROM_WIN32(GetLastError()), NULL, op);
  if (FAILED(hr = sf_create(p, &IID_IStorageFolder_, (void **)&sub))) return done_op(hr, NULL, op);
  hr = done_op(S_OK, sub, op); sub->lpVtbl->Release(sub); return hr; }
static HRESULT WINAPI sff_ni8(SFolder *f, void *name, void **op) { return sf_createfolder(f, name, 2, op); }
static HRESULT WINAPI sff_ni9(SFolder *f, void *name, int opt, void **op) { return sf_createfolder(f, name, opt, op); }
static HRESULT WINAPI sff_ni11(SFolder *f, void *name, void **op) {
  WCHAR p[MAX_PATH]; const WCHAR *n = name && hs_init() ? p_raw(name, NULL) : L""; DWORD a; IUnknown *sub = NULL; HRESULT hr;
  if (!op) return E_POINTER; _snwprintf(p, MAX_PATH, L"%ls\\%ls", f->path, n); a = GetFileAttributesW(p);
  if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return done_op(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), NULL, op);
  if (FAILED(hr = sf_create(p, &IID_IStorageFolder_, (void **)&sub))) return done_op(hr, NULL, op);
  hr = done_op(S_OK, sub, op); sub->lpVtbl->Release(sub); return hr; }
/* ---- IAsyncOperation<T> born completed (T an interface, or an error): answers any parameterized IID for the operation view ---- */
typedef struct DoneOp { const void *const *vt_op; const void *const *vt_info; LONG ref; HRESULT hr; IUnknown *result; IUnknown *handler; } DoneOp;
static const GUID IID_IAsyncInfo_ = {0x00000036, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
#define DO_OF_INFO(p) ((DoneOp *)((char *)(p) - offsetof(DoneOp, vt_info)))
/* the operation view answers IUnknown/IInspectable/IAgileObject and parameterized interfaces only: pinterface IIDs (IAsyncOperation<T>) are
   generated version-5 UUIDs; everything else (IMarshal, IWeakReferenceSource, ...) is refused */
static HRESULT WINAPI do_qi(DoneOp *d, REFIID r, void **o) {
  if (IsEqualGUID(r, &IID_IAsyncInfo_)) *o = &d->vt_info;
  else if (IsEqualGUID(r, &IID_IUnknown_sf) || IsEqualGUID(r, &IID_IInspectable_sf) || IsEqualGUID(r, &IID_IAgileObject_) || (r->Data3 >> 12) == 5) *o = &d->vt_op;
  else { *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&d->ref); return S_OK; }
static ULONG WINAPI do_ar(DoneOp *d) { return (ULONG)InterlockedIncrement(&d->ref); }
static ULONG WINAPI do_rl(DoneOp *d) { LONG n = InterlockedDecrement(&d->ref); if (!n) { if (d->handler) d->handler->lpVtbl->Release(d->handler); if (d->result) d->result->lpVtbl->Release(d->result); HeapFree(GetProcessHeap(), 0, d); } return (ULONG)n; }
static HRESULT WINAPI do_put_completed(DoneOp *d, IUnknown *h) {              /* AsyncOperationCompletedHandler<T>::Invoke(op, status) at once */
  if (d->handler) d->handler->lpVtbl->Release(d->handler); d->handler = h; if (!h) return S_OK; h->lpVtbl->AddRef(h);
  return ((HRESULT (WINAPI *)(void *, void *, int))(*(void ***)h)[3])(h, &d->vt_op, SUCCEEDED(d->hr) ? 1 : 3); }   /* Completed / Error */
static HRESULT WINAPI do_get_completed(DoneOp *d, IUnknown **h) { if ((*h = d->handler)) d->handler->lpVtbl->AddRef(d->handler); return S_OK; }
static HRESULT WINAPI do_results(DoneOp *d, IUnknown **r) { if (!r) return E_POINTER; *r = NULL; if (FAILED(d->hr)) return d->hr; if ((*r = d->result)) d->result->lpVtbl->AddRef(d->result); return S_OK; }
static HRESULT WINAPI doi_qi(void *i, REFIID r, void **o) { return do_qi(DO_OF_INFO(i), r, o); }
static ULONG WINAPI doi_ar(void *i) { return do_ar(DO_OF_INFO(i)); }
static ULONG WINAPI doi_rl(void *i) { return do_rl(DO_OF_INFO(i)); }
static HRESULT WINAPI doi_id(void *i, UINT32 *id) { (void)i; *id = 1; return S_OK; }
static HRESULT WINAPI doi_status(void *i, int *s) { *s = SUCCEEDED(DO_OF_INFO(i)->hr) ? 1 : 3; return S_OK; }
static HRESULT WINAPI doi_error(void *i, HRESULT *e) { *e = DO_OF_INFO(i)->hr; return S_OK; }
static HRESULT WINAPI doi_ok(void *i) { (void)i; return S_OK; }
static const void *const g_do_op_vt[9] = { (void *)do_qi, (void *)do_ar, (void *)do_rl, (void *)sf_iids, (void *)sf_name_cls, (void *)sf_trust, (void *)do_put_completed, (void *)do_get_completed, (void *)do_results };
static const void *const g_do_info_vt[11] = { (void *)doi_qi, (void *)doi_ar, (void *)doi_rl, (void *)sf_iids, (void *)sf_name_cls, (void *)sf_trust, (void *)doi_id, (void *)doi_status, (void *)doi_error, (void *)doi_ok, (void *)doi_ok };
static HRESULT done_op(HRESULT hr, IUnknown *result, void **out) {
  DoneOp *d = (DoneOp *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *d); if (!d) return E_OUTOFMEMORY;
  d->vt_op = g_do_op_vt; d->vt_info = g_do_info_vt; d->ref = 1; d->hr = hr; d->result = result; if (result) result->lpVtbl->AddRef(result); *out = &d->vt_op; return S_OK; }
static HRESULT sf_create(const WCHAR *path, REFIID riid, void **out);
/* GetItemAsync(name): a subfolder becomes another stand-in; anything else completes with "not found" (files are not modelled yet) */
static HRESULT WINAPI sff_ni12(SFolder *f, void *name, void **op) {
  WCHAR p[MAX_PATH]; const WCHAR *n = name && hs_init() ? p_raw(name, NULL) : L""; DWORD a; IUnknown *sub = NULL; HRESULT hr;
  if (!op) return E_POINTER; _snwprintf(p, MAX_PATH, L"%ls\\%ls", f->path, n); a = GetFileAttributesW(p);
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && SUCCEEDED(sf_create(p, &IID_IStorageItem_, (void **)&sub))) { hr = done_op(S_OK, sub, op); sub->lpVtbl->Release(sub); return hr; }
  slog("[storage] InstalledLocation.GetItemAsync(%ls) -> %s", n, a == INVALID_FILE_ATTRIBUTES ? "not found" : "file (not modelled): not found");
  return done_op(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), NULL, op); }
SFF_NI(13, "GetFilesAsync") SFF_NI(14, "GetFoldersAsync") SFF_NI(15, "GetItemsAsync")
/* GetFileAsync(name): files are not modelled yet - completes with "not found" (Clock's LocalState session file: first-run path) */
static HRESULT WINAPI sff_getfile(SFolder *f, void *name, void **op) {   /* an existing file: a real StorageFile; otherwise "not found" */
  WCHAR p[MAX_PATH]; const WCHAR *n = name && hs_init() ? p_raw(name, NULL) : L"?"; DWORD a;
  if (!op) return E_POINTER; _snwprintf(p, MAX_PATH, L"%ls\\%ls", f->path, n); a = GetFileAttributesW(p);
  if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY) && SUCCEEDED(sf_real_file_op(p, op))) { slog("[storage] GetFileAsync(%ls) -> real StorageFile", p); return S_OK; }
  slog("[storage] GetFileAsync(%ls): not found", p); return done_op(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), NULL, op); }
/* IStorageFolder2::TryGetItemAsync(name) (slot 6): a subfolder, else a NULL result (files not modelled yet) */
static HRESULT WINAPI sf2_qi(void *i, REFIID r, void **o) { return sf_qi(SF_OF_F2(i), r, o); }
static ULONG WINAPI sf2_ar(void *i) { return (ULONG)InterlockedIncrement(&SF_OF_F2(i)->ref); }
static ULONG WINAPI sf2_rl(void *i) { return sf_release(SF_OF_F2(i)); }
static HRESULT WINAPI sf2_tryget(void *i, void *name, void **op) {
  SFolder *f = SF_OF_F2(i); WCHAR p[MAX_PATH]; const WCHAR *n = name && hs_init() ? p_raw(name, NULL) : L""; DWORD a; IUnknown *sub = NULL; HRESULT hr;
  if (!op) return E_POINTER; _snwprintf(p, MAX_PATH, L"%ls\\%ls", f->path, n); a = GetFileAttributesW(p);
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && SUCCEEDED(sf_create(p, &IID_IStorageItem_, (void **)&sub))) { hr = done_op(S_OK, sub, op); sub->lpVtbl->Release(sub); return hr; }
  slog("[storage] TryGetItemAsync(%ls) -> %s", p, a == INVALID_FILE_ATTRIBUTES ? "none" : "file (not modelled): none"); return done_op(S_OK, NULL, op); }
static const void *const g_sf2_vt[7] = { (void *)sf2_qi, (void *)sf2_ar, (void *)sf2_rl, (void *)sf_iids, (void *)sf_name_cls, (void *)sf_trust, (void *)sf2_tryget };
/* IStorageItem view */
static HRESULT WINAPI sfi_qi(void *i, REFIID r, void **o) { return sf_qi(SF_OF_ITEM(i), r, o); }
static ULONG WINAPI sfi_ar(void *i) { return (ULONG)InterlockedIncrement(&SF_OF_ITEM(i)->ref); }
static ULONG WINAPI sfi_rl(void *i) { return sf_release(SF_OF_ITEM(i)); }
static HRESULT WINAPI sfi_notimpl(void *i) { (void)i; slog("[storage] StorageFolder(InstalledLocation) IStorageItem async method not implemented"); return E_NOTIMPL; }
static HRESULT WINAPI sfi_name(void *i, void **hs) { const WCHAR *p = SF_OF_ITEM(i)->path, *s = wcsrchr(p, L'\\'); s = s ? s + 1 : p; if (!hs_init()) return E_FAIL; return p_create(s, (UINT32)wcslen(s), hs); }
static HRESULT WINAPI sfi_path(void *i, void **hs) { const WCHAR *p = SF_OF_ITEM(i)->path; if (!hs_init()) return E_FAIL; return p_create(p, (UINT32)wcslen(p), hs); }
static HRESULT WINAPI sfi_attrs(void *i, UINT32 *a) { (void)i; if (a) *a = 16; return S_OK; }                 /* FileAttributes.Directory */
static HRESULT WINAPI sfi_created(void *i, INT64 *t) { WIN32_FILE_ATTRIBUTE_DATA d; if (!t) return E_POINTER; *t = 0;
  if (GetFileAttributesExW(SF_OF_ITEM(i)->path, GetFileExInfoStandard, &d)) *t = ((INT64)d.ftCreationTime.dwHighDateTime << 32) | d.ftCreationTime.dwLowDateTime; return S_OK; }
static HRESULT WINAPI sfi_isoftype(void *i, int type, unsigned char *r) { (void)i; if (r) *r = type == 2; return S_OK; }  /* StorageItemTypes.Folder */
static const void *const g_sff_vt[16] = { (void *)sff_qi, (void *)sff_ar, (void *)sff_rl, (void *)sf_iids, (void *)sf_name_cls, (void *)sf_trust,
  (void *)sff_ni6, (void *)sff_ni7, (void *)sff_ni8, (void *)sff_ni9, (void *)sff_getfile, (void *)sff_ni11, (void *)sff_ni12, (void *)sff_ni13, (void *)sff_ni14, (void *)sff_ni15 };
static const void *const g_sfi_vt[16] = { (void *)sfi_qi, (void *)sfi_ar, (void *)sfi_rl, (void *)sf_iids, (void *)sf_name_cls, (void *)sf_trust,
  (void *)sfi_notimpl, (void *)sfi_notimpl, (void *)sfi_notimpl, (void *)sfi_notimpl, (void *)sfi_notimpl, (void *)sfi_name, (void *)sfi_path, (void *)sfi_attrs, (void *)sfi_created, (void *)sfi_isoftype };
static HRESULT sf_create(const WCHAR *path, REFIID riid, void **out) {
  SFolder *f; size_t n = wcslen(path); HRESULT hr;
  f = (SFolder *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *f); if (!f) return E_OUTOFMEMORY;
  f->path = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * 2); if (!f->path) { HeapFree(GetProcessHeap(), 0, f); return E_OUTOFMEMORY; }
  memcpy(f->path, path, (n + 1) * 2); while (n > 3 && f->path[n - 1] == L'\\') f->path[--n] = 0;
  f->vt_folder = g_sff_vt; f->vt_item = g_sfi_vt; f->vt_folder2 = g_sf2_vt; f->ref = 1;
  hr = sf_qi(f, riid, out); sf_release(f); return hr; }
/* Win7 shell items do not know BHID_RandomAccessStream (Win8+). Win10 windows.storage still ends up holding Win7 items here and there
   (StorageFile.OpenAsync -> BindToHandler -> MK_E_NOOBJECT, "0x800401E5" in Media Player for every file), so Win7 shell32's shell-item
   vtable gets that one handler, in this process: the file opened as an IStream (read, or read/write for a ReadWrite bind context) and
   wrapped by CreateRandomAccessStreamOverStream. Everything else goes to Win7's BindToHandler. The vtable is taken from a real Win7 item
   (the first one Windows.ApplicationModel hands to us below), slot 3. */
static const GUID BHID_RandomAccessStream_ = {0xf16fc93b, 0x77ae, 0x4cfe, {0xbd, 0xa7, 0xa8, 0x66, 0xee, 0xa6, 0x87, 0x8d}};
typedef HRESULT (STDMETHODCALLTYPE *PBTH)(void *, void *, REFGUID, REFIID, void **);
static PBTH g_w7_bth;
__declspec(dllexport) HRESULT WINAPI CreateRandomAccessStreamOverStream(IStream *s, int opts, REFIID iid, void **o);
static PBTH g_w10_bth; static void **g_w10_itemvt;
static HRESULT STDMETHODCALLTYPE w7item_bth(void *si, void *bc, REFGUID bhid, REFIID riid, void **ppv)
{
  WCHAR *path = NULL; IStream *st = NULL; HRESULT hr; DWORD mode = STGM_READ;
  if (!IsEqualGUID(bhid, &BHID_RandomAccessStream_)) {
    PBTH real = g_w10_itemvt && *(void ***)si == g_w10_itemvt ? g_w10_bth : g_w7_bth; HRESULT h = real(si, bc, bhid, riid, ppv);
    static const GUID bhsf = {0x3981E224, 0xF559, 0x11D3, {0x8E, 0x3A, 0x00, 0xC0, 0x4F, 0x68, 0x37, 0xD5}};
    if (FAILED(h) && IsEqualGUID(bhid, &bhsf)) {   /* diagnostic: which folder object lacks the interface (BHID_SFObject + IShellFolder3 for folder queries) */
      static const GUID isf = {0x000214E6, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; static LONG n; IUnknown *o = NULL; char g1[48], g2[48];
      if (InterlockedIncrement(&n) <= 12) { WCHAR mn[MAX_PATH] = L"-"; HMODULE m = NULL; ULONG_PTR off = 0;
        if (SUCCEEDED(real(si, bc, bhid, &isf, (void **)&o)) && o) { void **vt = *(void ***)o; if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[0], &m)) { GetModuleFileNameW(m, mn, MAX_PATH); off = (ULONG_PTR)vt - (ULONG_PTR)m; } o->lpVtbl->Release(o); }
        guid_str(bhid, g1); guid_str(riid, g2);
        slog("[storage] BindToHandler(%s, %s) -> 0x%08lX; as IShellFolder: %ls+0x%Ix", g1, g2, (unsigned long)h, wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, off); } }
    return h; }
  if (!ppv) return E_POINTER; *ppv = NULL;
  if (bc) { BIND_OPTS bo; bo.cbStruct = sizeof bo; if (SUCCEEDED(((IBindCtx *)bc)->lpVtbl->GetBindOptions((IBindCtx *)bc, &bo)) && (bo.grfMode & (STGM_WRITE | STGM_READWRITE))) mode = STGM_READWRITE; }
  hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, DWORD, WCHAR **))(*(void ***)si)[5])(si, 0x80058000u /* SIGDN_FILESYSPATH */, &path);   /* IShellItem::GetDisplayName */
  if (FAILED(hr)) return hr;
  { typedef HRESULT (WINAPI *PSF)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **); static PSF sf; if (!sf) sf = (PSF)GetProcAddress(LoadLibraryA("shlwapi.dll"), "SHCreateStreamOnFileEx");
    hr = sf ? sf(path, mode | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &st) : E_NOTIMPL; }
  if (SUCCEEDED(hr)) { hr = CreateRandomAccessStreamOverStream(st, 0, riid, ppv); st->lpVtbl->Release(st); }
  slog("[storage] %s shell item %ls: BHID_RandomAccessStream (mode 0x%lx) -> 0x%08lX", g_w10_itemvt && *(void ***)si == g_w10_itemvt ? "Win10" : "Win7", path, mode, (unsigned long)hr);
  CoTaskMemFree(path); return hr;
}
static void w7item_patch(void *item)
{
  static LONG done; void **vt; DWORD old; HMODULE s32 = GetModuleHandleA("shell32.dll"), owner = NULL;
  if (!item || done) return;
  vt = *(void ***)item;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[3], &owner) || owner != s32) return;   /* only Win7 shell32's items */
  if (InterlockedExchange(&done, 1)) return;
  if (!VirtualProtect(&vt[3], sizeof(void *), PAGE_READWRITE, &old)) { slog("[storage] Win7 shell item vtable: VirtualProtect failed"); return; }
  g_w7_bth = (PBTH)vt[3]; vt[3] = (void *)w7item_bth; VirtualProtect(&vt[3], sizeof(void *), old, &old);
  slog("[storage] Win7 shell item vtable patched: BindToHandler gets BHID_RandomAccessStream");
}
static void w10item_patch(HMODULE ws, void **vt)
{
  DWORD old; HMODULE own = NULL;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)vt[3], &own) && own == ws && VirtualProtect(&vt[3], sizeof(void *), PAGE_READWRITE, &old)) {
    g_w10_bth = (PBTH)vt[3]; g_w10_itemvt = vt; vt[3] = (void *)w7item_bth; VirtualProtect(&vt[3], sizeof(void *), old, &old);
    slog("[storage] Win10 shell item vtable patched: BindToHandler gets BHID_RandomAccessStream"); }
  else slog("[storage] Win10 shell item vtable not recognised: not patched");
}
__declspec(dllexport) HRESULT WINAPI CreateStorageItemFromShellItem_FullTrustCaller_ForPackage(void *item, void *unused, const WCHAR *pkg, REFIID riid, void **out) {
  WCHAR *path = NULL; HRESULT hr; (void)unused;
  if (!out) return E_POINTER; *out = NULL; if (!item) return E_INVALIDARG;
  w7item_patch(item);
  hr = ((HRESULT (WINAPI *)(void *, DWORD, WCHAR **))(*(void ***)item)[5])(item, 0x80058000u /* SIGDN_FILESYSPATH */, &path); if (FAILED(hr)) return hr;   /* IShellItem::GetDisplayName */
  hr = sf_create(path, riid, out);
  slog("[storage] CreateStorageItemFromShellItem_FullTrustCaller_ForPackage(%ls, %ls) -> StorageFolder stand-in 0x%08lX", path, pkg ? pkg : L"(null)", (unsigned long)hr);
  CoTaskMemFree(path); return hr; }
