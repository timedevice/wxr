/* wxrshell.inc - stand-ins for the Windows shell brokers a packaged app talks to (ported in shape from wine-uwp's wineuwp):
     * CLSID_ActivatableApplicationRegistrar {dea794e0-...}: CoreApplication::RegisterActivatableApplication hands the app's activation object here,
       and we call back IActivatableApplication::Activate(...) with a LaunchActivatedEventArgs, from a separate MTA thread (that is what the shell does).
   Included at the end of wxrsys.c. */

typedef HRESULT (WINAPI *PHsCreate)(const WCHAR *, UINT32, void **);
typedef HRESULT (WINAPI *PHsDup)(void *, void **);
typedef HRESULT (WINAPI *PHsDel)(void *);
static PHsDup p_hsdup; static PHsDel p_hsdel;
static int hs_full(void) {
  static int tried;
  if (!hs_init()) return 0;
  if (!tried) { HMODULE m = LoadLibraryA("api-ms-win-core-winrt-string-l1-1-0.dll"); tried = 1;
    if (m) { p_hsdup = (PHsDup)GetProcAddress(m, "WindowsDuplicateString"); p_hsdel = (PHsDel)GetProcAddress(m, "WindowsDeleteString"); } }
  return p_hsdup && p_hsdel;
}
static void *hs_new(const WCHAR *s) { void *h = NULL; if (hs_full()) p_create(s, (UINT32)wcslen(s), &h); return h; }

static const GUID SH_IID_IUnknown = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID SH_IID_IInspectable = {0xAF86E2E0, 0xB12D, 0x4C6A, {0x9C, 0x5A, 0xD7, 0xAA, 0x65, 0x10, 0x1E, 0x90}};
static const GUID SH_IID_IAgileObject = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
static const GUID SH_IID_IMarshal = {0x00000003, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID SH_IID_IActivatedEventArgs = {0xCF651713, 0xCD08, 0x4FD8, {0xB6, 0x97, 0xA2, 0x81, 0xB6, 0x54, 0x4E, 0x2E}};
static const GUID SH_IID_IActivatedEventArgsWithUser = {0x1CF09B9E, 0x9962, 0x4936, {0x80, 0xFF, 0xAF, 0xC8, 0xE8, 0xAE, 0x5C, 0x8C}};
static const GUID SH_IID_IApplicationViewActivatedEventArgs = {0x930CEF4B, 0xB829, 0x40FC, {0x88, 0xF4, 0x85, 0x13, 0xE8, 0xA6, 0x47, 0x38}};
static const GUID SH_IID_ILaunchActivatedEventArgs = {0xFBC93E26, 0xA14A, 0x4B4F, {0x82, 0xB0, 0x33, 0xBE, 0xD9, 0x20, 0xAF, 0x52}};
static const GUID SH_IID_ILaunchActivatedEventArgs2 = {0x0FD37EBC, 0x9DC9, 0x46B5, {0x9A, 0xCE, 0xBD, 0x95, 0xD4, 0x56, 0x53, 0x45}};
static const GUID SH_IID_IPrelaunchActivatedEventArgs = {0x0C44717B, 0x19F7, 0x48D6, {0xB0, 0x46, 0xCF, 0x22, 0x82, 0x6E, 0xAA, 0x74}};
static const GUID SH_IID_IViewSwitcherProvider = {0x33F288A6, 0x5C2C, 0x4D27, {0xBA, 0xC7, 0x75, 0x36, 0x08, 0x8F, 0x12, 0x19}};
static const GUID SH_IID_ISplashScreen = {0xCA4D975C, 0xD4D6, 0x43F0, {0x97, 0xC0, 0x08, 0x33, 0xC6, 0x39, 0x1C, 0x24}};
static const GUID SH_IID_IInitializeActivatedEventArgs = {0x99FC44E3, 0xCE9A, 0x4284, {0xBF, 0x04, 0x76, 0x33, 0x1D, 0x1B, 0x17, 0x88}};
static const GUID SH_IID_IUserTokenArgs = {0xEB97821F, 0xF2B3, 0x4129, {0xA7, 0x36, 0x1C, 0xBA, 0xDD, 0xA8, 0x65, 0xE8}};   /* cloaked; slot 6 = get token (-> UserContextExtSetToken) */
static const GUID SH_IID_IActivationFactory_ = {0x00000035, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID SH_IID_IActivatableApplication = {0x92696C00, 0x7578, 0x48E1, {0xAC, 0x1A, 0x2C, 0xA9, 0x09, 0xE2, 0xC8, 0xCF}};
static const GUID SH_IID_IActivatableApplicationRegistrar = {0x036C57FC, 0xE73D, 0x4F3A, {0xB0, 0x95, 0x8B, 0x81, 0x3F, 0xA2, 0xF7, 0x64}};
static const GUID SH_CLSID_ActivatableApplicationRegistrar = {0xDEA794E0, 0x1C1D, 0x4363, {0xB1, 0x71, 0x98, 0xD0, 0xB1, 0x70, 0x35, 0x86}};

/* ---------------- LaunchActivatedEventArgs ---------------- */
typedef struct {
  const void *vt_activated, *vt_launch, *vt_launch2, *vt_appview, *vt_prelaunch, *vt_viewswitch, *vt_withuser, *vt_init, *vt_utok, *vt_file;
  IUnknown *splash, *ftm; LONG ref; void *arguments, *tile_id; IUnknown *files; int is_file; WCHAR *file_path; SRWLOCK files_lk;
} LArgs;
/* FileActivatedEventArgs (ActivationKind::File = 3): .Files is an IVectorView<IStorageItem> holding the activated file(s). */
static const GUID SH_IID_IFileActivatedEventArgs = {0xBB2AFC33, 0x93B1, 0x42ED, {0x8B, 0x26, 0x23, 0x6D, 0xD9, 0xC7, 0x84, 0x96}};
static const GUID SH_IID_IVectorViewStorage = {0x85575A41, 0x06CB, 0x58D0, {0xB9, 0x8A, 0x7C, 0x8F, 0x06, 0xE6, 0xE9, 0xD7}};
static const GUID SH_IID_IIterableStorage = {0xBB8B8418, 0x65D1, 0x544B, {0xB0, 0x83, 0x6D, 0x17, 0x2F, 0x56, 0x8C, 0x73}};
static const GUID SH_IID_IIteratorStorage = {0x05B487C2, 0x3830, 0x5D3C, {0x98, 0xDA, 0x25, 0xFA, 0x11, 0x54, 0x2D, 0xBD}};
static const GUID SH_IID_IStorageItemX = {0x4207A996, 0xCA2F, 0x42F7, {0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30}};
#define LA_FROM(i, f) ((LArgs *)((char *)(i) - offsetof(LArgs, f)))

static HRESULT la_qi(LArgs *a, REFIID iid, void **o) {
  const void **p = NULL;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IActivatedEventArgs, 16)) p = &a->vt_activated;
  else if (!memcmp(iid, &SH_IID_ILaunchActivatedEventArgs, 16)) p = &a->vt_launch;
  else if (!memcmp(iid, &SH_IID_ILaunchActivatedEventArgs2, 16)) p = &a->vt_launch2;
  else if (!memcmp(iid, &SH_IID_IApplicationViewActivatedEventArgs, 16)) p = &a->vt_appview;
  else if (!memcmp(iid, &SH_IID_IPrelaunchActivatedEventArgs, 16)) p = &a->vt_prelaunch;
  else if (!memcmp(iid, &SH_IID_IViewSwitcherProvider, 16)) p = &a->vt_viewswitch;
  else if (!memcmp(iid, &SH_IID_IActivatedEventArgsWithUser, 16)) p = &a->vt_withuser;
  else if (!memcmp(iid, &SH_IID_IInitializeActivatedEventArgs, 16)) p = &a->vt_init;
  else if (!memcmp(iid, &SH_IID_IUserTokenArgs, 16)) p = &a->vt_utok;
  else if (a->is_file && !memcmp(iid, &SH_IID_IFileActivatedEventArgs, 16)) p = &a->vt_file;
  else if (!memcmp(iid, &SH_IID_ISplashScreen, 16)) { a->splash->lpVtbl->AddRef(a->splash); *o = a->splash; return S_OK; }
  else if (a->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return a->ftm->lpVtbl->QueryInterface(a->ftm, iid, o);
  else { char g[48]; guid_str(iid, g); slog("[shell] LaunchActivatedEventArgs: no interface %s", g); log_callers("args QI"); *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&a->ref); *o = (void *)p; return S_OK;
}
static ULONG la_addref(LArgs *a) { return InterlockedIncrement(&a->ref); }
static ULONG la_release(LArgs *a) {
  LONG r = InterlockedDecrement(&a->ref);
  if (!r) { if (p_hsdel) { p_hsdel(a->arguments); p_hsdel(a->tile_id); } if (a->files) a->files->lpVtbl->Release(a->files); free(a->file_path); if (a->splash) a->splash->lpVtbl->Release(a->splash); if (a->ftm) a->ftm->lpVtbl->Release(a->ftm); free(a); }
  return r;
}
#define LA_UNK(f) \
  static HRESULT WINAPI f##_QI(void *i, REFIID iid, void **o) { return la_qi(LA_FROM(i, f), iid, o); } \
  static ULONG WINAPI f##_AR(void *i) { return la_addref(LA_FROM(i, f)); } \
  static ULONG WINAPI f##_RL(void *i) { return la_release(LA_FROM(i, f)); }
LA_UNK(vt_activated) LA_UNK(vt_launch) LA_UNK(vt_launch2) LA_UNK(vt_appview) LA_UNK(vt_prelaunch) LA_UNK(vt_viewswitch) LA_UNK(vt_withuser) LA_UNK(vt_init) LA_UNK(vt_utok) LA_UNK(vt_file)
static HRESULT WINAPI la_iids(void *i, ULONG *n, GUID **ids) { (void)i; *n = 0; *ids = NULL; return S_OK; }
static HRESULT WINAPI la_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI la_trust(void *i, int *l) { (void)i; *l = 0; return S_OK; }
static HRESULT WINAPI la_kind(void *i, int *k) { *k = LA_FROM(i, vt_activated)->is_file ? 3 : 0; slog("[shell] args.Kind -> %d", *k); return S_OK; }   /* ActivationKind::File=3, Launch=0 */
static HRESULT WINAPI la_prevstate(void *i, int *s) { (void)i; *s = 0; return S_OK; }                    /* NotRunning */
static HRESULT WINAPI la_splash_get(void *i, void **o) { slog("[shell] args.SplashScreen"); LArgs *a = LA_FROM(i, vt_activated); a->splash->lpVtbl->AddRef(a->splash); *o = a->splash; return S_OK; }
static HRESULT WINAPI la_get_arguments(void *i, void **o) { slog("[shell] args.Arguments"); return p_hsdup(LA_FROM(i, vt_launch)->arguments, o); }
static HRESULT WINAPI la_get_tileid(void *i, void **o) { return p_hsdup(LA_FROM(i, vt_launch)->tile_id, o); }
static HRESULT WINAPI la_null_out(void *i, void **o) { (void)i; *o = NULL; return S_OK; }
#define LA_NULL(n) static HRESULT WINAPI la_null_##n(void *i, void **o) { (void)i; *o = NULL; slog("[shell] args getter -> NULL (" #n ")"); return S_OK; }
LA_NULL(launch2) LA_NULL(viewswitch) LA_NULL(withuser) LA_NULL(utok)
/* Windows.System.User stand-in for IActivatedEventArgsWithUser::get_User (Calculator dereferences it) */
static const GUID SH_IID_IUser = {0xDF9A26C6, 0xE746, 0x4BCD, {0xB5, 0xD4, 0x12, 0x01, 0x03, 0xC4, 0x20, 0x9B}};
typedef struct { const void *vt; IUnknown *ftm; LONG ref; } SysUser;
static SysUser g_sysuser;
static HRESULT WINAPI su_qi(void *i, REFIID iid, void **o) {
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IUser, 16)) { *o = i; InterlockedIncrement(&g_sysuser.ref); return S_OK; }
  if (g_sysuser.ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return g_sysuser.ftm->lpVtbl->QueryInterface(g_sysuser.ftm, iid, o);
  *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI su_ar(void *i) { (void)i; return InterlockedIncrement(&g_sysuser.ref); }
static ULONG WINAPI su_rl(void *i) { (void)i; return InterlockedDecrement(&g_sysuser.ref) + 1; }
static HRESULT WINAPI su_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.System.User"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI su_id(void *i, void **hs) { (void)i; *hs = hs_new(L"S-1-5-21-1-1-1-1000"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI su_status(void *i, int *v) { (void)i; *v = 1; return S_OK; }   /* LocallyAuthenticated */
static HRESULT WINAPI su_type(void *i, int *v) { (void)i; *v = 0; return S_OK; }     /* LocalUser */
static HRESULT WINAPI su_async(void *i, void *a, void **o) { (void)i; (void)a; *o = NULL; return E_NOTIMPL; }
static HRESULT WINAPI su_pic(void *i, int a, int b, void **o) { (void)i; (void)a; (void)b; *o = NULL; return E_NOTIMPL; }
static const void *su_vt[] = { su_qi, su_ar, su_rl, la_iids, su_class, la_trust, su_id, su_status, su_type, su_async, su_async, su_pic };
static HRESULT WINAPI la_user(void *i, void **o) {
  (void)i; if (!g_sysuser.vt) { g_sysuser.vt = su_vt; g_sysuser.ref = 1; CoCreateFreeThreadedMarshaler((IUnknown *)&g_sysuser, &g_sysuser.ftm); }
  InterlockedIncrement(&g_sysuser.ref); *o = &g_sysuser; slog("[shell] args.User -> stand-in Windows.System.User"); return S_OK; }static HRESULT WINAPI la_zero_i32(void *i, int *o) { (void)i; *o = 0; slog("[shell] args getter -> 0 (appview)"); return S_OK; }
static HRESULT WINAPI la_zero_bool(void *i, unsigned char *o) { (void)i; *o = 0; slog("[shell] args.PrelaunchActivated"); return S_OK; }
/* IInitializeActivatedEventArgs::Initialize( UINT64, HSTRING args, state, byte, byte, HSTRING tile, GUID* ) / InitializeViewDependent( state, viewId, ISplashScreen*, byte ) */
static HRESULT WINAPI la_init1(void *i, unsigned long long u, void *args, int st, unsigned char f1, unsigned char f2, void *tile, const GUID *id) {
  LArgs *a = LA_FROM(i, vt_init); (void)u; (void)st; (void)f1; (void)f2; (void)id;
  if (args) { p_hsdel(a->arguments); p_hsdup(args, &a->arguments); } if (tile) { p_hsdel(a->tile_id); p_hsdup(tile, &a->tile_id); } return S_OK; }
static HRESULT WINAPI la_init2(void *i, int st, int view, IUnknown *splash, unsigned char f) {
  LArgs *a = LA_FROM(i, vt_init); (void)st; (void)view; (void)f;
  if (splash) { splash->lpVtbl->AddRef(splash); if (a->splash) a->splash->lpVtbl->Release(a->splash); a->splash = splash; } return S_OK; }
#define LA_HEAD(f) f##_QI, f##_AR, f##_RL, la_iids, la_class, la_trust
static const void *const lvt_activated[] = { LA_HEAD(vt_activated), la_kind, la_prevstate, la_splash_get };
static const void *const lvt_launch[] = { LA_HEAD(vt_launch), la_get_arguments, la_get_tileid };
static const void *const lvt_launch2[] = { LA_HEAD(vt_launch2), la_null_launch2 };
static const void *const lvt_appview[] = { LA_HEAD(vt_appview), la_zero_i32 };
static const void *const lvt_prelaunch[] = { LA_HEAD(vt_prelaunch), la_zero_bool };
static const void *const lvt_viewswitch[] = { LA_HEAD(vt_viewswitch), la_null_viewswitch };
static const void *const lvt_withuser[] = { LA_HEAD(vt_withuser), la_user };
static const void *const lvt_utok[] = { LA_HEAD(vt_utok), la_null_utok };
static const void *const lvt_init[] = { LA_HEAD(vt_init), la_init1, la_init2 };

/* SplashScreen: separate object (the projection types a wrapper from the canonical IUnknown's class name) */
typedef struct { const void *vt; IUnknown *ftm; LONG ref; } Splash;
static HRESULT WINAPI sp_qi(void *i, REFIID iid, void **o) {
  Splash *s = (Splash *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_ISplashScreen, 16)) { InterlockedIncrement(&s->ref); *o = i; return S_OK; }
  if (s->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return s->ftm->lpVtbl->QueryInterface(s->ftm, iid, o);
  *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI sp_ar(void *i) { return InterlockedIncrement(&((Splash *)i)->ref); }
static ULONG WINAPI sp_rl(void *i) { Splash *s = (Splash *)i; LONG r = InterlockedDecrement(&s->ref); if (!r) { if (s->ftm) s->ftm->lpVtbl->Release(s->ftm); free(s); } return r; }
static HRESULT WINAPI sp_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.ApplicationModel.Activation.SplashScreen"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI sp_rect(void *i, float *r) { (void)i; r[0] = r[1] = 0; r[2] = 1280; r[3] = 800; return S_OK; }
static HRESULT WINAPI sp_add(void *i, void *h, unsigned long long *tok) { (void)i; (void)h; if (tok) *tok = 1; return S_OK; }
static HRESULT WINAPI sp_remove(void *i, unsigned long long tok) { (void)i; (void)tok; return S_OK; }
static const void *const svt_splash[] = { sp_qi, sp_ar, sp_rl, la_iids, sp_class, la_trust, sp_rect, sp_add, sp_remove };

static IUnknown *make_splash(void) {
  Splash *s = (Splash *)calloc(1, sizeof *s); if (!s) return NULL; s->vt = svt_splash; s->ref = 1;
  CoCreateFreeThreadedMarshaler((IUnknown *)&s->vt, &s->ftm); return (IUnknown *)&s->vt; }
/* ---------------- Files: IVectorView<IStorageItem> (+ IIterable) over one item ---------------- */
typedef struct { const void *vt_view, *vt_iter; IUnknown *item, *ftm; LONG ref; } FileVec;
typedef struct { const void *vt; IUnknown *item, *ftm; LONG ref, pos; } FileIter;
#define FV_FROM(i, f) ((FileVec *)((char *)(i) - offsetof(FileVec, f)))
static HRESULT fv_qi(FileVec *v, REFIID iid, void **o) {
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IVectorViewStorage, 16)) *o = &v->vt_view;
  else if (!memcmp(iid, &SH_IID_IIterableStorage, 16)) *o = &v->vt_iter;
  else if (v->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return v->ftm->lpVtbl->QueryInterface(v->ftm, iid, o);
  else { *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&v->ref); return S_OK; }
static ULONG fv_rel(FileVec *v) { LONG r = InterlockedDecrement(&v->ref); if (!r) { if (v->item) v->item->lpVtbl->Release(v->item); if (v->ftm) v->ftm->lpVtbl->Release(v->ftm); free(v); } return r; }
static HRESULT WINAPI fvv_qi(void *i, REFIID iid, void **o) { return fv_qi(FV_FROM(i, vt_view), iid, o); }
static ULONG WINAPI fvv_ar(void *i) { return InterlockedIncrement(&FV_FROM(i, vt_view)->ref); }
static ULONG WINAPI fvv_rl(void *i) { return fv_rel(FV_FROM(i, vt_view)); }
static HRESULT WINAPI fvi_qi(void *i, REFIID iid, void **o) { return fv_qi(FV_FROM(i, vt_iter), iid, o); }
static ULONG WINAPI fvi_ar(void *i) { return InterlockedIncrement(&FV_FROM(i, vt_iter)->ref); }
static ULONG WINAPI fvi_rl(void *i) { return fv_rel(FV_FROM(i, vt_iter)); }
static HRESULT WINAPI fv_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI fv_getat(void *i, unsigned idx, void **o) { FileVec *v = FV_FROM(i, vt_view); if (idx != 0 || !v->item) { *o = NULL; return 0x8000000BL /* E_BOUNDS */; } v->item->lpVtbl->AddRef(v->item); *o = v->item; return S_OK; }
static HRESULT WINAPI fv_size(void *i, unsigned *n) { *n = FV_FROM(i, vt_view)->item ? 1 : 0; return S_OK; }
static HRESULT WINAPI fv_indexof(void *i, void *x, unsigned *idx, unsigned char *found) { FileVec *v = FV_FROM(i, vt_view); *idx = 0; *found = (v->item && x == v->item) ? 1 : 0; return S_OK; }
static HRESULT WINAPI fv_getmany(void *i, unsigned start, unsigned cap, void **dst, unsigned *actual) { FileVec *v = FV_FROM(i, vt_view);
  if (start == 0 && cap >= 1 && v->item) { v->item->lpVtbl->AddRef(v->item); dst[0] = v->item; *actual = 1; } else *actual = 0; return S_OK; }
/* iterator */
static HRESULT WINAPI fit_qi(void *i, REFIID iid, void **o) { FileIter *t = (FileIter *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IIteratorStorage, 16)) { InterlockedIncrement(&t->ref); *o = i; return S_OK; }
  if (t->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return t->ftm->lpVtbl->QueryInterface(t->ftm, iid, o);
  *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI fit_ar(void *i) { return InterlockedIncrement(&((FileIter *)i)->ref); }
static ULONG WINAPI fit_rl(void *i) { FileIter *t = (FileIter *)i; LONG r = InterlockedDecrement(&t->ref); if (!r) { if (t->item) t->item->lpVtbl->Release(t->item); if (t->ftm) t->ftm->lpVtbl->Release(t->ftm); free(t); } return r; }
static HRESULT WINAPI fit_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.Foundation.Collections.IIterator`1<Windows.Storage.IStorageItem>"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI fit_current(void *i, void **o) { FileIter *t = (FileIter *)i; if (t->pos == 0 && t->item) { t->item->lpVtbl->AddRef(t->item); *o = t->item; return S_OK; } *o = NULL; return 0x8000000BL; }
static HRESULT WINAPI fit_has(void *i, unsigned char *b) { FileIter *t = (FileIter *)i; *b = (t->pos == 0 && t->item) ? 1 : 0; return S_OK; }
static HRESULT WINAPI fit_next(void *i, unsigned char *b) { FileIter *t = (FileIter *)i; t->pos++; *b = 0; return S_OK; }
static HRESULT WINAPI fit_getmany(void *i, unsigned cap, void **dst, unsigned *actual) { FileIter *t = (FileIter *)i;
  if (t->pos == 0 && cap >= 1 && t->item) { t->item->lpVtbl->AddRef(t->item); dst[0] = t->item; t->pos = 1; *actual = 1; } else *actual = 0; return S_OK; }
static const void *const fit_vt[] = { fit_qi, fit_ar, fit_rl, la_iids, fit_class, la_trust, fit_current, fit_has, fit_next, fit_getmany };
static HRESULT WINAPI fv_first(void *i, void **o) { FileVec *v = FV_FROM(i, vt_iter); FileIter *t = (FileIter *)calloc(1, sizeof *t); if (!t) return E_OUTOFMEMORY;
  t->vt = fit_vt; t->ref = 1; t->pos = 0; if ((t->item = v->item)) t->item->lpVtbl->AddRef(t->item); CoCreateFreeThreadedMarshaler((IUnknown *)&t->vt, &t->ftm); *o = &t->vt; return S_OK; }
static const void *const fvv_view[] = { fvv_qi, fvv_ar, fvv_rl, la_iids, fv_class, la_trust, fv_getat, fv_size, fv_indexof, fv_getmany };
static const void *const fvv_iter[] = { fvi_qi, fvi_ar, fvi_rl, la_iids, fv_class, la_trust, fv_first };
/* a real Windows.Storage.StorageFile for a path (synchronous windows.storage helper, same one sip_rebuild uses). The item is
   apartment-bound, so it is built lazily in get_Files - on the app's own (view) thread, where it will be used. */
static IUnknown *make_storage_item(const WCHAR *path, HRESULT *phr) {
  typedef HRESULT (WINAPI *PCSI)(const WCHAR *, int, REFIID, void **); static PCSI csi; static int tried; void *it = NULL; HRESULT hr;
  if (!tried) { HMODULE ws = NULL; int d;   /* the image's windows.storage, patched first (thread checks off) - as the COM paths that load it do */
    alock(1); act_init(); alock(0);
    for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"windows.storage.dll")) { ws = act_load(d); storage_noaffinity(ws); break; }
    csi = ws ? (PCSI)GetProcAddress(ws, "CreateStorageItemFromPath_FullTrustCaller") : NULL; tried = 1; }
  if (!csi) { slog("[shell] file activation: windows.storage has no CreateStorageItemFromPath_FullTrustCaller"); *phr = E_NOTIMPL; return NULL; }
  hr = csi(path, 0, &SH_IID_IStorageItemX, &it);
  if (hr == CO_E_NOTINITIALIZED) { CoInitializeEx(NULL, COINIT_MULTITHREADED); it = NULL; hr = csi(path, 0, &SH_IID_IStorageItemX, &it); }   /* a worker thread that never joined an apartment: take the MTA */
  if (FAILED(hr) || !it) { slog("[shell] file activation: could not open %ls -> 0x%08lX (thread %lu)", path, (unsigned long)hr, GetCurrentThreadId()); *phr = FAILED(hr) ? hr : E_FAIL; return NULL; }
  *phr = S_OK; return (IUnknown *)it; }
/* IFileActivatedEventArgs: get_Files -> IVectorView<IStorageItem> (built on first call, on the caller's thread), get_Verb -> "" */
static HRESULT WINAPI fa_get_files(void *i, void **o) {
  LArgs *a = LA_FROM(i, vt_file); HRESULT hr = S_OK; *o = NULL; slog("[shell] args.Files (thread %lu)", GetCurrentThreadId());
  AcquireSRWLockExclusive(&a->files_lk);
  if (!a->files && a->file_path) { IUnknown *item = make_storage_item(a->file_path, &hr); FileVec *v;
    if (item) { if ((v = (FileVec *)calloc(1, sizeof *v))) { v->vt_view = fvv_view; v->vt_iter = fvv_iter; v->item = item; v->ref = 1;
        CoCreateFreeThreadedMarshaler((IUnknown *)&v->vt_view, &v->ftm); a->files = (IUnknown *)&v->vt_view; } else { item->lpVtbl->Release(item); hr = E_OUTOFMEMORY; } } }
  if (a->files) { a->files->lpVtbl->AddRef(a->files); *o = a->files; hr = S_OK; }
  else if (SUCCEEDED(hr)) hr = E_FAIL;   /* never S_OK with no vector: the app dereferences it (App Installer: AV at +0x2060F) */
  ReleaseSRWLockExclusive(&a->files_lk);
  return hr; }
static HRESULT WINAPI fa_get_verb(void *i, void **o) { (void)i; *o = hs_new(L""); return S_OK; }
static const void *const lvt_file[] = { LA_HEAD(vt_file), fa_get_files, fa_get_verb };
static IUnknown *make_file_args(const WCHAR *path) {
  LArgs *a; size_t n;
  if (!hs_full() || !(a = (LArgs *)calloc(1, sizeof *a))) return NULL;
  a->vt_activated = lvt_activated; a->vt_launch = lvt_launch; a->vt_launch2 = lvt_launch2; a->vt_appview = lvt_appview; a->vt_prelaunch = lvt_prelaunch;
  a->vt_viewswitch = lvt_viewswitch; a->vt_withuser = lvt_withuser; a->vt_init = lvt_init; a->vt_utok = lvt_utok; a->vt_file = lvt_file; a->ref = 1;
  a->splash = make_splash(); a->arguments = hs_new(L""); a->tile_id = hs_new(L""); a->is_file = 1; InitializeSRWLock(&a->files_lk);
  n = (wcslen(path) + 1) * sizeof(WCHAR); if ((a->file_path = (WCHAR *)malloc(n))) memcpy(a->file_path, path, n);
  CoCreateFreeThreadedMarshaler((IUnknown *)&a->vt_activated, &a->ftm);
  slog("[shell] file activation: %ls", path);
  return (IUnknown *)&a->vt_activated; }
/* the file path the app was activated with: the first command-line argument that is not a switch (skips the exe and -ServerName) and names an existing file */
static int activation_file_arg(WCHAR *out, int cap) {
  typedef LPWSTR *(WINAPI *PCLA)(LPCWSTR, int *); static PCLA cla; static int tried; LPWSTR *av = NULL; int n = 0, i, got = 0;
  if (!tried) { HMODULE h = LoadLibraryA("shell32.dll"); cla = h ? (PCLA)GetProcAddress(h, "CommandLineToArgvW") : NULL; tried = 1; }
  if (!cla || !(av = cla(GetCommandLineW(), &n))) return 0;
  for (i = 1; i < n; i++) { if (av[i][0] == L'-' || av[i][0] == L'/' || !wcschr(av[i], L'\\')) continue; lstrcpynW(out, av[i], cap); got = 1; break; }
  LocalFree(av);
  return got && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES; }
/* Windows.ApplicationModel.AppInstance.GetActivatedEventArgs (the Windows App SDK / packaged-Win32 activation path, which does NOT
   use CoreApplication::RegisterActivatableApplication): rtstub forwards here. Returns a FileActivatedEventArgs when the app was
   launched with a file on its command line, else S_FALSE (caller reports no activation args = plain launch). Runs on the app's thread. */
__declspec(dllexport) HRESULT WINAPI WxrGetActivatedEventArgs(void **out) {
  WCHAR fp[MAX_PATH * 2]; IUnknown *a;
  if (!out) return E_POINTER; *out = NULL;
  if (!activation_file_arg(fp, MAX_PATH * 2)) return S_FALSE;
  if (!(a = make_file_args(fp))) return S_FALSE;
  *out = a; return S_OK; }

static IUnknown *make_launch_args(const WCHAR *arguments) {
  LArgs *a = (LArgs *)calloc(1, sizeof *a); if (!a || !hs_full()) return NULL;
  a->vt_activated = lvt_activated; a->vt_launch = lvt_launch; a->vt_launch2 = lvt_launch2; a->vt_appview = lvt_appview; a->vt_prelaunch = lvt_prelaunch;
  a->vt_viewswitch = lvt_viewswitch; a->vt_withuser = lvt_withuser; a->vt_init = lvt_init; a->vt_utok = lvt_utok; a->ref = 1;
  a->splash = make_splash(); a->arguments = hs_new(arguments); a->tile_id = hs_new(L"");
  CoCreateFreeThreadedMarshaler((IUnknown *)&a->vt_activated, &a->ftm);
  return (IUnknown *)&a->vt_activated; }

/* ---------------- ActivatableApplicationRegistrar ---------------- */
typedef struct { const void *vt; LONG ref; IUnknown *app; } Registrar;
typedef HRESULT (WINAPI *PActivate)(void *self, void *aumid, void *contract, void *actid, void *extra, IUnknown *args, unsigned long long cookie, UINT32 options);
typedef struct { Registrar *r; IUnknown *args; void *app, *aumid, *contract, *actid, *extra; HRESULT hr; HANDLE done; } ActCtx;
/* runs ON the view thread: CoreApplicationView / DXamlCore are per-thread, so Activate must be made from there. Delivered from the
   view thread's dispatcher wait (view_call_poll in CoMsgWaitInProcessEvents); an APC only if the thread never gets there. */
static VOID CALLBACK shell_activate_apc(ULONG_PTR p) {
  ActCtx *c = (ActCtx *)p;
  slog("[shell] Activate on view thread %lu", GetCurrentThreadId());
  c->hr = ((PActivate)((void **)*(void ***)c->app)[6])(c->app, c->aumid, c->contract, c->actid, c->extra, c->args, 0, 0);
  SetEvent(c->done);
}
static DWORD WINAPI shell_activate_thread(void *arg) {
  Registrar *r = (Registrar *)arg; HRESULT hr, hrc = CoInitializeEx(NULL, COINIT_MULTITHREADED); ActCtx c; int tries;
  WCHAR au[300] = L"App"; UINT32 n = 300;
  memset(&c, 0, sizeof c); c.r = r;
  if (GetCurrentApplicationUserModelId(&n, au)) lstrcpyW(au, L"App");
  hr = r->app->lpVtbl->QueryInterface(r->app, &SH_IID_IActivatableApplication, &c.app);
  if (FAILED(hr)) { slog("[shell] app does not expose IActivatableApplication: 0x%08lX", (unsigned long)hr); goto done; }
  { WCHAR fp[MAX_PATH * 2]; IUnknown *fa = NULL;
    if (activation_file_arg(fp, MAX_PATH * 2)) fa = make_file_args(fp);
    if (fa) { c.args = fa; c.contract = hs_new(L"Windows.File"); }
    else { c.args = make_launch_args(L""); c.contract = hs_new(L"Windows.Launch"); } }
  c.aumid = hs_new(au); c.actid = hs_new(L""); c.extra = hs_new(L"");
  c.done = CreateEventW(NULL, FALSE, FALSE, NULL);
  for (tries = 0; tries < 200 && !g_view_tid; tries++) Sleep(50);
  if (g_view_tid) {
    for (tries = 0; tries < 40; tries++) {
      c.hr = E_FAIL; ResetEvent(c.done);
      g_view_call_arg = (ULONG_PTR)&c; InterlockedExchangePointer((void *volatile *)&g_view_call, (void *)shell_activate_apc);
      if (WaitForSingleObject(c.done, 15000) != WAIT_OBJECT_0) {   /* done is auto-reset: wait again only if this one timed out */
        if (InterlockedExchangePointer((void *volatile *)&g_view_call, NULL)) {
          HANDLE th = OpenThread(THREAD_SET_CONTEXT, FALSE, g_view_tid);   /* not picked up: the view thread is not in its dispatcher wait */
          if (!th) { slog("[shell] cannot open view thread %lu: %lu", g_view_tid, GetLastError()); break; }
          slog("[shell] Activate: view thread not in its dispatcher wait, queued as an APC");
          if (!QueueUserAPC(shell_activate_apc, th, (ULONG_PTR)&c)) { CloseHandle(th); break; }
          CloseHandle(th); }
        if (WaitForSingleObject(c.done, 120000) != WAIT_OBJECT_0) { slog("[shell] Activate did not complete in 120 s"); break; } }
      if (c.hr != E_POINTER) break; Sleep(50);
    }
    hr = c.hr;
  } else { slog("[shell] no view thread appeared; activating on the MTA thread"); hr = ((PActivate)((void **)*(void ***)c.app)[6])(c.app, c.aumid, c.contract, c.actid, c.extra, c.args, 0, 0); }
  slog("[shell] Activate returned 0x%08lX", (unsigned long)hr); wxr_splash_activated();
done:
  if (SUCCEEDED(hrc)) CoUninitialize();
  return 0;
}static HRESULT WINAPI reg_qi(void *i, REFIID iid, void **o) {
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IActivatableApplicationRegistrar, 16)) { InterlockedIncrement(&((Registrar *)i)->ref); *o = i; return S_OK; }
  { char g[48]; guid_str(iid, g); slog("[shell] registrar: no interface %s", g); } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI reg_ar(void *i) { return InterlockedIncrement(&((Registrar *)i)->ref); }
static ULONG WINAPI reg_rl(void *i) { return InterlockedDecrement(&((Registrar *)i)->ref); }
static HRESULT WINAPI reg_register(void *i, IUnknown *app) {
  Registrar *r = (Registrar *)i; HANDLE t; slog("[shell] RegisterActivatableApplication(%p)", (void *)app);
  if (!app) return E_INVALIDARG; app->lpVtbl->AddRef(app); r->app = app;
  t = CreateThread(NULL, 0, shell_activate_thread, r, 0, NULL); if (t) CloseHandle(t); return S_OK; }
static HRESULT WINAPI reg_other(void *i) { (void)i; slog("[shell] registrar: unexpected method"); return E_NOTIMPL; }
static const void *const rvt_registrar[] = { reg_qi, reg_ar, reg_rl, reg_register, reg_other, reg_other, reg_other };
static Registrar g_registrar = { rvt_registrar, 1, NULL };


/* ---------------- INavigationClient stand-in (slot-for-slot with CoreUIComponents' NavigationClient; 138 slots) ---------------- */
typedef struct { const void *vt_client, *vt_prepare; IUnknown *ftm; LONG ref; } NavClient;
static const GUID SH_IID_INavPrepare = {0x075BE329, 0x825A, 0x4F2A, {0xA8, 0xAF, 0xCD, 0x2F, 0x9A, 0x81, 0x02, 0xEC}};
static const GUID SH_IID_IAsyncActionX = {0x5A648006, 0x843A, 0x4DA9, {0x86, 0x5B, 0x9D, 0x26, 0xE5, 0xDF, 0xAD, 0x7B}};
static const GUID SH_IID_IAsyncInfoX = {0x00000036, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID SH_IID_ICoreWindowNavigation = {0xBB4CBBE9, 0x65AF, 0x4730, {0x9E, 0xD2, 0xAB, 0xBD, 0xB5, 0xCC, 0x15, 0xB9}};
static const GUID SH_IID_ICoreWindow = {0x79B9D5F2, 0x879E, 0x4B89, {0xB7, 0x98, 0x79, 0xE4, 0x75, 0x98, 0x03, 0x0C}};
static const GUID SH_IID_ICoreWindowBroker = {0x0F4ACCB1, 0xD8F9, 0x4011, {0xBA, 0x37, 0x25, 0x57, 0x92, 0x5A, 0x78, 0xCF}};
static const GUID SH_IID_ICoreWindowFactory = {0xCD292360, 0x2763, 0x4085, {0x8A, 0x9F, 0x74, 0xB2, 0x24, 0xA2, 0x91, 0x75}};
static const GUID SH_IID_IConfigureWindowFactory = {0x601D51E3, 0x801E, 0x49C9, {0xBB, 0xFA, 0xFE, 0x29, 0xA6, 0x62, 0xAE, 0xAD}};
static const GUID SH_CLSID_CoreWindowBroker = {0x3480A401, 0xBDE9, 0x4407, {0xBC, 0x02, 0x79, 0x8A, 0x86, 0x6A, 0xC0, 0x51}};
static NavClient *nav_of_prepare(void *i) { return (NavClient *)((char *)i - offsetof(NavClient, vt_prepare)); }

/* completed IAsyncAction (PrepareToActivate has nothing to wait for) */
typedef struct { const void *vt_action, *vt_info; IUnknown *ftm; LONG ref; } DoneAction;
static DoneAction *da_of_info(void *i) { return (DoneAction *)((char *)i - offsetof(DoneAction, vt_info)); }
static HRESULT WINAPI da_qi(void *i, REFIID iid, void **o) {
  DoneAction *a = (DoneAction *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IAsyncActionX, 16)) *o = &a->vt_action;
  else if (!memcmp(iid, &SH_IID_IAsyncInfoX, 16)) *o = &a->vt_info;
  else if (a->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return a->ftm->lpVtbl->QueryInterface(a->ftm, iid, o);
  else { *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&a->ref); return S_OK; }
static ULONG WINAPI da_ar(void *i) { return InterlockedIncrement(&((DoneAction *)i)->ref); }
static ULONG WINAPI da_rl(void *i) { DoneAction *a = (DoneAction *)i; LONG r = InterlockedDecrement(&a->ref); if (!r) { if (a->ftm) a->ftm->lpVtbl->Release(a->ftm); free(a); } return r; }
static HRESULT WINAPI da_info_qi(void *i, REFIID iid, void **o) { return da_qi(da_of_info(i), iid, o); }
static ULONG WINAPI da_info_ar(void *i) { return da_ar(da_of_info(i)); }
static ULONG WINAPI da_info_rl(void *i) { return da_rl(da_of_info(i)); }
static HRESULT WINAPI da_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.Foundation.IAsyncAction"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI da_put_completed(void *i, IUnknown *h) {
  if (!h) return S_OK; { void **vt = *(void ***)h; return ((HRESULT (WINAPI *)(void *, void *, int))vt[3])(h, i, 1 /* Completed */); } }
static HRESULT WINAPI da_get_completed(void *i, IUnknown **h) { (void)i; *h = NULL; return S_OK; }
static HRESULT WINAPI da_results(void *i) { (void)i; return S_OK; }
static HRESULT WINAPI da_id(void *i, UINT32 *id) { (void)i; *id = 1; return S_OK; }
static HRESULT WINAPI da_status(void *i, int *s) { (void)i; *s = 1; return S_OK; }
static HRESULT WINAPI da_errcode(void *i, HRESULT *h) { (void)i; *h = S_OK; return S_OK; }
static HRESULT WINAPI da_ok(void *i) { (void)i; return S_OK; }
static const void *const dvt_action[] = { da_qi, da_ar, da_rl, la_iids, da_class, la_trust, da_put_completed, da_get_completed, da_results };
static const void *const dvt_info[] = { da_info_qi, da_info_ar, da_info_rl, da_id, da_status, da_errcode, da_ok, da_ok };
static HRESULT make_done_action(void **out) {
  DoneAction *a = (DoneAction *)calloc(1, sizeof *a); if (!a) return E_OUTOFMEMORY; a->vt_action = dvt_action; a->vt_info = dvt_info; a->ref = 1;
  CoCreateFreeThreadedMarshaler((IUnknown *)&a->vt_action, &a->ftm); *out = &a->vt_action; return S_OK; }

static HRESULT WINAPI nv_ok(void) { return S_OK; }
static HRESULT WINAPI nv_notimpl(void) { log_callers("E_NOTIMPL from a stand-in slot"); return E_NOTIMPL; }
static HRESULT WINAPI nv_b0(void *t, unsigned char *v) { (void)t; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT WINAPI nv_b1(void *t, unsigned char *v) { (void)t; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT WINAPI nv_p0(void *t, void **v) { (void)t; if (!v) return E_POINTER; *v = NULL; return S_OK; }
static HRESULT WINAPI nv_p0_ni(void *t, void **v) { (void)t; if (v) *v = NULL; return E_NOTIMPL; }
static HRESULT WINAPI nv_i0(void *t, int *v) { (void)t; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT WINAPI nv_i1(void *t, int *v) { (void)t; if (!v) return E_POINTER; *v = 1; return S_OK; }
static HRESULT WINAPI nv_u100(void *t, int *v) { (void)t; if (!v) return E_POINTER; *v = 100; return S_OK; }
static HRESULT WINAPI nv_u64z(void *t, unsigned long long *v) { (void)t; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT WINAPI nv_d0(void *t, double *v) { (void)t; if (!v) return E_POINTER; *v = 0; return S_OK; }
static HRESULT WINAPI nv_tok(void *t, void *h, unsigned long long *tok) { (void)t; (void)h; if (!tok) return E_POINTER; *tok = 0; return S_OK; }
static HRESULT WINAPI nv_size_max(void *t, float *v) { (void)t; if (!v) return E_POINTER; v[0] = 1280; v[1] = 800; return S_OK; }
static HRESULT WINAPI nv_size_zero(void *t, float *v) { (void)t; if (!v) return E_POINTER; v[0] = 0; v[1] = 0; return S_OK; }
static HRESULT WINAPI nv_guid0(void *t, GUID *v) { (void)t; if (!v) return E_POINTER; memset(v, 0, 16); return S_OK; }
static HWND g_hosted_wnd;
static void view_size(float *w, float *h) { RECT rc; if (g_hosted_wnd && GetClientRect(g_hosted_wnd, &rc) && rc.right > 0 && rc.bottom > 0) { *w = (float)rc.right; *h = (float)rc.bottom; } else { *w = 1280; *h = 800; } }
static HRESULT WINAPI nv_rect(void *t, float *v) { (void)t; if (!v) return E_POINTER; v[0] = v[1] = 0; view_size(&v[2], &v[3]); return S_OK; }
static HRESULT WINAPI nv_viewbounds(void *t, int *v) { float w, h; (void)t; if (!v) return E_POINTER; memset(v, 0, 36); view_size(&w, &h); v[0] = (int)w; v[1] = (int)h; return S_OK; }
static HRESULT WINAPI nv_activate(void *t, void *window) {
  (void)t; slog("[shell] NavigationClient::OnActivateWindow(%p)", window);
  if (g_hosted_wnd) { SendMessageW(g_hosted_wnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_INITIALIZE, 0), 0); PostMessageW(g_hosted_wnd, WM_APP + 0x270, 0, 0); }
  return S_OK; }
static HRESULT WINAPI nv_prepare(void *i, unsigned long long a, void **action) { (void)i; (void)a; if (!action) return E_POINTER; return make_done_action(action); }
static HRESULT WINAPI nav_qi(void *i, REFIID iid, void **o) {
  NavClient *n = (NavClient *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16)) *o = &n->vt_client;
  else if (!memcmp(iid, &SH_IID_INavPrepare, 16)) *o = &n->vt_prepare;
  else if (n->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return n->ftm->lpVtbl->QueryInterface(n->ftm, iid, o);
  else { char g[48]; guid_str(iid, g); slog("[shell] navigation client: no interface %s", g); *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&n->ref); return S_OK; }
static ULONG WINAPI nav_ar(void *i) { return InterlockedIncrement(&((NavClient *)i)->ref); }
static ULONG WINAPI nav_rl(void *i) { NavClient *n = (NavClient *)i; LONG r = InterlockedDecrement(&n->ref); if (!r) { if (n->ftm) n->ftm->lpVtbl->Release(n->ftm); free(n); } return r; }
static HRESULT WINAPI nav_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.Phone.UI.Core.ImmersiveNavigationClient"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI nav_p_qi(void *i, REFIID iid, void **o) { return nav_qi(nav_of_prepare(i), iid, o); }
static ULONG WINAPI nav_p_ar(void *i) { return nav_ar(nav_of_prepare(i)); }
static ULONG WINAPI nav_p_rl(void *i) { return nav_rl(nav_of_prepare(i)); }
static const void *nvt_client[138];
static const void *nvt_prepare[7];
static void nav_vt_init(void) {
  int k; static int done; if (done) return; done = 1;
  for (k = 6; k < 138; k++) nvt_client[k] = nv_ok;            /* put_ and void methods: S_OK, arguments ignored */
  nvt_client[0] = nav_qi; nvt_client[1] = nav_ar; nvt_client[2] = nav_rl; nvt_client[3] = la_iids; nvt_client[4] = nav_class; nvt_client[5] = la_trust;
  nvt_client[6] = nv_p0_ni; nvt_client[8] = nv_b1; nvt_client[10] = nv_i0; nvt_client[12] = nv_rect;
  nvt_client[13] = nvt_client[15] = nvt_client[17] = nvt_client[19] = nv_tok;
  nvt_client[21] = nv_i1; nvt_client[22] = nv_activate; nvt_client[25] = nv_i1; nvt_client[27] = nv_i1; nvt_client[28] = nv_viewbounds;
  nvt_client[30] = nv_i0; nvt_client[32] = nv_size_max; nvt_client[34] = nv_b0; nvt_client[36] = nv_d0; nvt_client[37] = nv_b0; nvt_client[38] = nv_d0; nvt_client[39] = nv_d0;
  nvt_client[40] = nv_notimpl; nvt_client[41] = nv_i0; nvt_client[42] = nv_b0;
  for (k = 44; k <= 66; k += 2) nvt_client[k] = nv_p0;       /* TitleBar colour overrides (get at even slots) */
  nvt_client[68] = nv_b0; nvt_client[72] = nv_notimpl; nvt_client[73] = nv_b0; nvt_client[74] = nv_b0;
  nvt_client[76] = nv_b1; nvt_client[78] = nv_b1; nvt_client[80] = nv_b1; nvt_client[82] = nv_b0; nvt_client[84] = nv_b0; nvt_client[86] = nv_b1;
  nvt_client[88] = nv_b0; nvt_client[90] = nv_b0; nvt_client[92] = nv_b1; nvt_client[94] = nv_b0; nvt_client[96] = nv_b0; nvt_client[98] = nv_b1;
  nvt_client[100] = nv_p0; nvt_client[102] = nv_p0; nvt_client[106] = nv_i1; nvt_client[107] = nv_i1; nvt_client[109] = nv_p0; nvt_client[110] = nv_guid0;
  nvt_client[112] = nv_b0; nvt_client[113] = nv_i0; nvt_client[114] = nv_p0; nvt_client[116] = nv_u64z; nvt_client[118] = nv_u64z;
  nvt_client[123] = nvt_client[125] = nv_tok; nvt_client[127] = nv_i0; nvt_client[128] = nv_b0; nvt_client[129] = nv_b0; nvt_client[130] = nv_b0; nvt_client[131] = nv_b1;
  nvt_client[132] = nv_size_max; nvt_client[133] = nv_size_zero; nvt_client[134] = nv_u100; nvt_client[135] = nv_i0; nvt_client[136] = nv_p0;
  nvt_prepare[0] = nav_p_qi; nvt_prepare[1] = nav_p_ar; nvt_prepare[2] = nav_p_rl; nvt_prepare[3] = la_iids; nvt_prepare[4] = nav_class; nvt_prepare[5] = la_trust; nvt_prepare[6] = nv_prepare;
}
static HRESULT make_nav_client(IUnknown **out) {
  NavClient *n; nav_vt_init(); n = (NavClient *)calloc(1, sizeof *n); if (!n) return E_OUTOFMEMORY;
  n->vt_client = nvt_client; n->vt_prepare = nvt_prepare; n->ref = 1; CoCreateFreeThreadedMarshaler((IUnknown *)&n->vt_client, &n->ftm);
  *out = (IUnknown *)&n->vt_client; return S_OK; }


/* ---------------- window client adapter (CoreUIComponents' NavigationClientWindowClientAdapter, Windows 10 19041 slot layout) ----------------
   twinapi's ApplicationView, CoreApplicationView and Windows.UI's WindowServer ask the shell for window facts through three cloaked interfaces installed with
   ICoreWindowAdapterInterop::SetWindowClientAdapter. Without one ApplicationView.GetForCurrentView() fails (no file-mapped view service) and the app's start-up
   dereferences the null. Shape and behaviour follow wine-uwp's clientadapter.c; slot order is this build's own vtables (PDB of CoreUIComponents.dll):
     IWindowClientAdapter {6d015416} 58 slots (IUnknown based), IApplicationViewClientAdapter {f02ee4c8} 35, ICoreApplicationViewClientAdapter {c86d297c} 7. */
static const GUID SH_IID_IWindowClientAdapter = {0x6D015416, 0x85E6, 0x48C2, {0x8B, 0x21, 0x66, 0xB2, 0xBE, 0x16, 0x6A, 0xEB}};
static const GUID SH_IID_IApplicationViewClientAdapter = {0xF02EE4C8, 0x92B3, 0x48C7, {0x9B, 0x48, 0x3D, 0x0A, 0x0D, 0x2F, 0x1B, 0xF4}};
static const GUID SH_IID_ICoreApplicationViewClientAdapter = {0xC86D297C, 0x6302, 0x41AA, {0x9D, 0x05, 0x72, 0x41, 0x29, 0x61, 0x1E, 0x32}};
static const GUID SH_IID_ICoreWindowAdapterInterop = {0x7A5B6FD1, 0xCD73, 0x4B6C, {0x9C, 0xF4, 0x2E, 0x86, 0x9E, 0xAF, 0x47, 0x0A}};
static const GUID SH_IID_IAsyncOperationBool = {0xCDB5EFB3, 0x5788, 0x509D, {0x9B, 0xE1, 0x71, 0xCC, 0xB8, 0xA3, 0x36, 0x2A}};
typedef struct { float w, h; } VSize;
typedef struct { const void *vt_window, *vt_view, *vt_core; LONG ref; IUnknown *ftm; HWND hwnd; void *title; unsigned char multiview, suppress; int launch_mode; VSize launch, minsz; } WinAdapter;
static LONG g_adapter_token;
#define WA_FROM(i, f) ((WinAdapter *)((char *)(i) - offsetof(WinAdapter, f)))
static HRESULT wa_qi(WinAdapter *a, REFIID iid, void **o) {
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IWindowClientAdapter, 16)) *o = &a->vt_window;
  else if (!memcmp(iid, &SH_IID_IApplicationViewClientAdapter, 16)) *o = &a->vt_view;
  else if (!memcmp(iid, &SH_IID_ICoreApplicationViewClientAdapter, 16)) *o = &a->vt_core;
  else if (a->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return a->ftm->lpVtbl->QueryInterface(a->ftm, iid, o);
  else { *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&a->ref); return S_OK; }
static ULONG wa_release(WinAdapter *a) { LONG r = InterlockedDecrement(&a->ref); if (!r) { if (p_hsdel) p_hsdel(a->title); if (a->ftm) a->ftm->lpVtbl->Release(a->ftm); free(a); } return r; }
/* IAsyncOperation<bool> born completed */
typedef struct { const void *vt_op, *vt_info; LONG ref; unsigned char result; IUnknown *handler; } DoneBool;
static HRESULT WINAPI db_qi(void *i, REFIID iid, void **o) { DoneBool *d = (DoneBool *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16) || !memcmp(iid, &SH_IID_IAsyncOperationBool, 16)) *o = &d->vt_op;
  else if (!memcmp(iid, &SH_IID_IAsyncInfoX, 16)) *o = &d->vt_info; else { *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&d->ref); return S_OK; }
static ULONG WINAPI db_ar(void *i) { return InterlockedIncrement(&((DoneBool *)i)->ref); }
static ULONG WINAPI db_rl(void *i) { DoneBool *d = (DoneBool *)i; LONG r = InterlockedDecrement(&d->ref); if (!r) { if (d->handler) d->handler->lpVtbl->Release(d->handler); free(d); } return r; }
static HRESULT WINAPI dbi_qi(void *i, REFIID iid, void **o) { return db_qi((char *)i - sizeof(void *), iid, o); }
static ULONG WINAPI dbi_ar(void *i) { return db_ar((char *)i - sizeof(void *)); }
static ULONG WINAPI dbi_rl(void *i) { return db_rl((char *)i - sizeof(void *)); }
static HRESULT WINAPI db_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.Foundation.IAsyncOperation`1<Boolean>"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI db_put_completed(void *i, IUnknown *h) { DoneBool *d = (DoneBool *)i; if (d->handler) d->handler->lpVtbl->Release(d->handler); if (!(d->handler = h)) return S_OK; h->lpVtbl->AddRef(h);
  return ((HRESULT (WINAPI *)(void *, void *, int))(*(void ***)h)[3])(h, i, 1); }
static HRESULT WINAPI db_get_completed(void *i, IUnknown **h) { DoneBool *d = (DoneBool *)i; if ((*h = d->handler)) d->handler->lpVtbl->AddRef(d->handler); return S_OK; }
static HRESULT WINAPI db_results(void *i, unsigned char *r) { *r = ((DoneBool *)i)->result; return S_OK; }
static const void *const dbvt_op[] = { db_qi, db_ar, db_rl, la_iids, db_class, la_trust, db_put_completed, db_get_completed, db_results };
static const void *const dbvt_info[] = { dbi_qi, dbi_ar, dbi_rl, la_iids, db_class, la_trust, da_id, da_status, da_errcode, da_ok, da_ok };
static HRESULT make_done_bool(unsigned char v, void **out) { DoneBool *d = (DoneBool *)calloc(1, sizeof *d); if (!d) return E_OUTOFMEMORY; d->vt_op = dbvt_op; d->vt_info = dbvt_info; d->ref = 1; d->result = v; *out = &d->vt_op; return S_OK; }

static HRESULT WINAPI wa_w_qi(void *i, REFIID iid, void **o) { return wa_qi(WA_FROM(i, vt_window), iid, o); }
static ULONG WINAPI wa_w_ar(void *i) { return InterlockedIncrement(&WA_FROM(i, vt_window)->ref); }
static ULONG WINAPI wa_w_rl(void *i) { return wa_release(WA_FROM(i, vt_window)); }
static HRESULT WINAPI wa_v_qi(void *i, REFIID iid, void **o) { return wa_qi(WA_FROM(i, vt_view), iid, o); }
static ULONG WINAPI wa_v_ar(void *i) { return InterlockedIncrement(&WA_FROM(i, vt_view)->ref); }
static ULONG WINAPI wa_v_rl(void *i) { return wa_release(WA_FROM(i, vt_view)); }
static HRESULT WINAPI wa_c_qi(void *i, REFIID iid, void **o) { return wa_qi(WA_FROM(i, vt_core), iid, o); }
static ULONG WINAPI wa_c_ar(void *i) { return InterlockedIncrement(&WA_FROM(i, vt_core)->ref); }
static ULONG WINAPI wa_c_rl(void *i) { return wa_release(WA_FROM(i, vt_core)); }
static HRESULT WINAPI wa_get_view(void *i, void **o) { WinAdapter *a = WA_FROM(i, vt_window); *o = &a->vt_view; InterlockedIncrement(&a->ref); return S_OK; }
static HRESULT WINAPI wa_get_core(void *i, void **o) { WinAdapter *a = WA_FROM(i, vt_window); *o = &a->vt_core; InterlockedIncrement(&a->ref); return S_OK; }
static HRESULT WINAPI wa_null(void *i, void **o) { (void)i; *o = NULL; return S_OK; }
static HRESULT WINAPI wa_ok(void *i) { (void)i; return S_OK; }
static HRESULT WINAPI wa_event_add(void *i, void *h, unsigned long long *tok) { (void)i; (void)h; *tok = (unsigned long long)InterlockedIncrement(&g_adapter_token); return S_OK; }
static HRESULT WINAPI wa_event_remove(void *i, unsigned long long tok) { (void)i; (void)tok; return S_OK; }
static HRESULT WINAPI wa_false(void *i, unsigned char *v) { (void)i; *v = 0; return S_OK; }
static HRESULT WINAPI wa_true(void *i, unsigned char *v) { (void)i; *v = 1; return S_OK; }
static HRESULT WINAPI wa_zero_i32(void *i, int *v) { (void)i; *v = 0; return S_OK; }
static HRESULT WINAPI wa_null_hs(void *i, void **v) { (void)i; *v = NULL; return S_OK; }
static float wa_scale(HWND h) { HDC dc = GetDC(NULL); int dpi = 96; (void)h; if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc); } return dpi / 96.0f; }
static volatile float g_sh_min_w, g_sh_min_h;   /* app's SetPreferredMinSize, DIPs (enforced by sh_frame_subclass) */
static int wa_client_dips(WinAdapter *a, float *x, float *y, float *w, float *h) { RECT rc; POINT o = {0, 0}; float s = wa_scale(a->hwnd); if (!a->hwnd || !GetClientRect(a->hwnd, &rc)) return 0;
  ClientToScreen(a->hwnd, &o); *x = o.x / s; *y = o.y / s; *w = (rc.right - rc.left) / s; *h = (rc.bottom - rc.top) / s; return 1; }
static HRESULT WINAPI wa_w_logical_size(void *i, VSize *v) { float x, y; if (!wa_client_dips(WA_FROM(i, vt_window), &x, &y, &v->w, &v->h)) return E_FAIL; return S_OK; }
/* VisibleBounds in the same space XAML uses for the window content: origin 0,0. With the client area's screen position here, XAML
   constrained every in-window popup (flyouts, context menus) to a rectangle offset by the window's screen position, so they were placed
   outside the window and culled (visible only with the window at the screen's top-left corner). */
static HRESULT WINAPI wa_w_visible_bounds(void *i, float *r)
{ if (!wa_client_dips(WA_FROM(i, vt_window), &r[0], &r[1], &r[2], &r[3])) return E_FAIL;
  r[0] = r[1] = 0.0f;
  return S_OK; }
static HRESULT WINAPI wa_w_is_visible(void *i, unsigned char *v) { WinAdapter *a = WA_FROM(i, vt_window); *v = a->hwnd && IsWindowVisible(a->hwnd); return S_OK; }
static HRESULT WINAPI wa_w_is_minimized(void *i, unsigned char *v) { WinAdapter *a = WA_FROM(i, vt_window); *v = a->hwnd && IsIconic(a->hwnd); return S_OK; }
static HRESULT WINAPI wa_w_get_min(void *i, VSize *v) { *v = WA_FROM(i, vt_window)->minsz; return S_OK; }
static HRESULT WINAPI wa_w_put_min(void *i, VSize v) { WA_FROM(i, vt_window)->minsz = v; g_sh_min_w = v.w; g_sh_min_h = v.h; slog("[shell] window adapter: PreferredMinSize %gx%g", v.w, v.h); return S_OK; }
static HRESULT WINAPI wa_w_get_title(void *i, void **v) { return p_hsdup(WA_FROM(i, vt_window)->title, v); }
static HRESULT WINAPI wa_w_put_title(void *i, void *v) { WinAdapter *a = WA_FROM(i, vt_window); p_hsdel(a->title); a->title = NULL; if (v) p_hsdup(v, &a->title); if (a->hwnd && v) { UINT32 n; SetWindowTextW(a->hwnd, p_raw(v, &n)); } return S_OK; }
static HRESULT wa_refused_log(const char *name) { slog("[shell] window adapter: %s not implemented", name); return E_NOTIMPL; }
#define WA_REFUSE(n, name) static HRESULT WINAPI wa_r_##n(void *i) { (void)i; return wa_refused_log(name); }
WA_REFUSE(4, "get_AppViewId") WA_REFUSE(9, "get_Id") WA_REFUSE(14, "get_Occlusion") WA_REFUSE(15, "get_PersistedStateId") WA_REFUSE(16, "put_PersistedStateId") WA_REFUSE(17, "get_Placement")
WA_REFUSE(20, "get_PhysicalBounds") WA_REFUSE(23, "get_SetWindowPosFlags") WA_REFUSE(24, "get_ShouldWaitForLayoutCompletion") WA_REFUSE(25, "put_ShouldWaitForLayoutCompletion")
WA_REFUSE(8, "get_HoloViewClientAdapter (desktop: none; WindowServer QIs an S_OK result unchecked)") WA_REFUSE(29, "get_TitleBarClientAdapter (ApplicationView tolerates a failure, not S_OK + null)") WA_REFUSE(31, "get_PrimaryDisplayRegion") WA_REFUSE(34, "Consolidate") WA_REFUSE(39, "TryShowAsync")
static void wa_vt_init(void);
/* SystemNavigationClientAdapter: WindowServer QIs the (unchecked) result, so it must be a real object. 14 slots, events at 8/10/12 (+1 remove). */
static const void *wa_sysnav_vt[14]; static const void *wa_sysnav_obj = wa_sysnav_vt;
static HRESULT WINAPI wa_sn_qi(void *i, REFIID iid, void **o) { (void)iid; *o = i; return S_OK; }
static ULONG WINAPI wa_sn_ref(void *i) { (void)i; return 2; }
static HRESULT WINAPI wa_get_sysnav(void *i, void **o) { (void)i; wa_vt_init(); *o = &wa_sysnav_obj; return S_OK; }
/* HoloViewClientAdapter: WindowServer QIs the result unchecked (a failure breaks twinapi's HolographicViewProperties, which DisplayInformation and so
   Pivot need). A null object: QI answers itself; every method succeeds with a zero result (presentation mode 0 = not holographic). */
static HRESULT WINAPI wa_ho_any(void *i, void *out) { (void)i; if ((ULONG_PTR)out > 0x10000 && !((ULONG_PTR)out & 3)) *(int *)out = 0; return S_OK; }
static const void *wa_holo_vt[24]; static const void *wa_holo_obj = wa_holo_vt;
static HRESULT WINAPI wa_get_holo(void *i, void **o) { (void)i; wa_vt_init(); *o = &wa_holo_obj; return S_OK; }
/* DisplayClientAdapter (window adapter slot 6): WindowManagementAPI's WindowLayout reads orientation and scale from it; without one
   CurrentOrientation is 0 and Pivot's GoToOrientationState dereferences a null state name. Slots as WindowLayout calls them:
   3/4 Get/PutAutoRotationPreferences, 5 CurrentOrientation, 6 NativeOrientation, 7/8 scale (float*), 10/11 Add/RemoveOrientationChanged,
   12/13 Add/RemoveScaleChanged. */
static const void *wa_disp_vt[24]; static const void *wa_disp_obj = wa_disp_vt;
static int wa_disp_orient(void) { int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN); return w >= h ? 1 : 2; }   /* Landscape / Portrait */
static HRESULT WINAPI wa_d_orient(void *i, int *v) { (void)i; *v = wa_disp_orient(); return S_OK; }
static HRESULT WINAPI wa_d_put(void *i, int v) { (void)i; (void)v; return S_OK; }
static HRESULT WINAPI wa_d_scale(void *i, float *v) { (void)i; *v = wa_scale(NULL); return S_OK; }
#define WA_DREF(n) static HRESULT WINAPI wa_d_##n(void *i) { (void)i; return wa_refused_log("DisplayClientAdapter slot " #n); }
WA_DREF(9) WA_DREF(14) WA_DREF(15) WA_DREF(16) WA_DREF(17) WA_DREF(18) WA_DREF(19) WA_DREF(20) WA_DREF(21) WA_DREF(22) WA_DREF(23)
static HRESULT WINAPI wa_get_display(void *i, void **o) { (void)i; wa_vt_init(); *o = &wa_disp_obj; return S_OK; }
/* TitleBarClientAdapter (window adapter slot 29): twinapi's CoreApplicationViewTitleBar asks it for Height (slot 25, double*), IsVisible
   (34, boolean*) and the system overlay insets (37 left, 38 right, double*). Without it the title bar reported Height 0 / invisible and
   NavigationView (Calculator's mode header row) laid its header out ~80 px too low. Our window keeps Win7's own frame and caption buttons
   outside the client area, so the extended title bar is 32 DIPs with no overlay. Other slots (colours, events) succeed and write nothing. */
static const void *wa_tb_vt[64]; static const void *wa_tb_obj = wa_tb_vt;
static HRESULT WINAPI wa_tb_height(void *i, double *v) { (void)i; if (v) *v = 32.0; return S_OK; }
static HRESULT WINAPI wa_tb_zero(void *i, double *v) { (void)i; if (v) *v = 0.0; return S_OK; }
static HRESULT WINAPI wa_tb_visible(void *i, unsigned char *v) { (void)i; if (v) *v = 1; return S_OK; }
static HRESULT WINAPI wa_get_titlebar(void *i, void **o) { (void)i; wa_vt_init(); *o = &wa_tb_obj; slog("[shell] window adapter: TitleBarClientAdapter handed out (height 32, visible, no overlay)"); return S_OK; }static const void *wa_window_vt[58];
/* IApplicationViewClientAdapter */
static int g_sh_placed; static void sh_place_default(HWND h, float minw, float minh);
static void wa_apply_launch_size(WinAdapter *a) { float s; RECT rc; DWORD st, ex; if (!a->hwnd || a->launch.w < 100 || a->launch.h < 100 || g_sh_placed) return; s = wa_scale(a->hwnd); g_sh_placed = 1;   /* a remembered placement wins over the app's PreferredLaunchViewSize */
  rc.left = rc.top = 0; rc.right = (LONG)(a->launch.w * s); rc.bottom = (LONG)(a->launch.h * s); st = (DWORD)GetWindowLongPtrW(a->hwnd, GWL_STYLE); ex = (DWORD)GetWindowLongPtrW(a->hwnd, GWL_EXSTYLE);
  AdjustWindowRectEx(&rc, st, FALSE, ex); SetWindowPos(a->hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); }
static HRESULT WINAPI wa_v_get_launch_mode(void *i, int *v) { *v = WA_FROM(i, vt_view)->launch_mode; return S_OK; }
static HRESULT WINAPI wa_v_put_launch_mode(void *i, int v) { WA_FROM(i, vt_view)->launch_mode = v; slog("[shell] ApplicationView.PreferredLaunchWindowingMode = %d", v); return S_OK; }
static HRESULT WINAPI wa_v_get_launch_size(void *i, VSize *v) { *v = WA_FROM(i, vt_view)->launch; return S_OK; }
static HRESULT WINAPI wa_v_put_launch_size(void *i, VSize v) { WinAdapter *a = WA_FROM(i, vt_view); a->launch = v; slog("[shell] ApplicationView.PreferredLaunchViewSize = %g x %g", v.w, v.h); wa_apply_launch_size(a); return S_OK; }
static HRESULT WINAPI wa_v_get_min(void *i, VSize *v) { *v = WA_FROM(i, vt_view)->minsz; return S_OK; }
static HRESULT WINAPI wa_v_put_min(void *i, VSize v) { WinAdapter *a = WA_FROM(i, vt_view); a->minsz = v; g_sh_min_w = v.w; g_sh_min_h = v.h; slog("[shell] ApplicationView.SetPreferredMinSize(%g x %g)", v.w, v.h); sh_place_default(a->hwnd, v.w, v.h); return S_OK; }
static HRESULT WINAPI wa_v_get_multiview(void *i, unsigned char *v) { (void)i; *v = 0; return S_OK; }
static HRESULT WINAPI wa_v_put_multiview(void *i, unsigned char v) { WA_FROM(i, vt_view)->multiview = v; return S_OK; }
static HRESULT WINAPI wa_v_get_suppress(void *i, unsigned char *v) { *v = WA_FROM(i, vt_view)->suppress; return S_OK; }
static HRESULT WINAPI wa_v_put_suppress(void *i, unsigned char v) { WA_FROM(i, vt_view)->suppress = v; return S_OK; }
static HRESULT WINAPI wa_v_is_full_screen(void *i, unsigned char *v) { WinAdapter *a = WA_FROM(i, vt_view); *v = a->hwnd && !(GetWindowLongW(a->hwnd, GWL_STYLE) & WS_CAPTION); return S_OK; }
static HRESULT WINAPI wa_v_get_title(void *i, void **v) { return p_hsdup(WA_FROM(i, vt_view)->title, v); }
static HRESULT WINAPI wa_v_put_title(void *i, void *v) { WinAdapter *a = WA_FROM(i, vt_view); UINT32 n; p_hsdel(a->title); a->title = NULL; if (v) p_hsdup(v, &a->title); if (a->hwnd && v) SetWindowTextW(a->hwnd, p_raw(v, &n)); return S_OK; }
static HRESULT WINAPI wa_v_is_mode_supported(void *i, int mode, unsigned char *v) { (void)i; *v = !mode; return S_OK; }
static HRESULT WINAPI wa_v_try_show_mode(void *i, int view, int mode, IUnknown *prefs, void **op) { (void)i; (void)view; (void)prefs; return make_done_bool(!mode, op); }
static HRESULT WINAPI wa_v_refused(void *i) { (void)i; return wa_refused_log("IApplicationViewClientAdapter method"); }
static const void *wa_view_vt[35];
static const void *wa_core_vt[7];
static void wa_vt_init(void) {
  static int d; int k; if (d) return; d = 1;
  for (k = 3; k < 58; k++) wa_window_vt[k] = wa_ok;
  wa_window_vt[0] = wa_w_qi; wa_window_vt[1] = wa_w_ar; wa_window_vt[2] = wa_w_rl;
  wa_window_vt[3] = wa_get_view; wa_window_vt[4] = wa_r_4; wa_window_vt[5] = wa_get_core; wa_window_vt[6] = wa_get_display; wa_window_vt[7] = wa_null; wa_window_vt[8] = wa_get_holo;
  wa_window_vt[9] = wa_r_9; wa_window_vt[10] = wa_true; wa_window_vt[11] = wa_ok; wa_window_vt[12] = wa_w_is_visible; wa_window_vt[13] = wa_w_logical_size; wa_window_vt[14] = wa_r_14;
  wa_window_vt[15] = wa_r_15; wa_window_vt[16] = wa_r_16; wa_window_vt[17] = wa_r_17; wa_window_vt[18] = wa_w_get_min; wa_window_vt[19] = wa_w_put_min; wa_window_vt[20] = wa_r_20;
  wa_window_vt[21] = wa_null; wa_window_vt[22] = wa_null; wa_window_vt[23] = wa_r_23; wa_window_vt[24] = wa_r_24; wa_window_vt[25] = wa_r_25; wa_window_vt[26] = wa_get_sysnav;
  wa_window_vt[27] = wa_w_get_title; wa_window_vt[28] = wa_w_put_title; wa_window_vt[29] = wa_get_titlebar; wa_window_vt[30] = wa_w_visible_bounds; wa_window_vt[31] = wa_r_31;
  wa_window_vt[32] = wa_ok; wa_window_vt[33] = wa_ok; wa_window_vt[34] = wa_r_34; /* 35..38 notifications: S_OK */ wa_window_vt[39] = wa_r_39;
  for (k = 40; k < 58; k += 2) { wa_window_vt[k] = wa_event_add; wa_window_vt[k + 1] = wa_event_remove; }
  for (k = 3; k < 64; k++) wa_tb_vt[k] = wa_ok; wa_tb_vt[0] = wa_sn_qi; wa_tb_vt[1] = wa_sn_ref; wa_tb_vt[2] = wa_sn_ref;
  wa_tb_vt[25] = wa_tb_height; wa_tb_vt[34] = wa_tb_visible; wa_tb_vt[37] = wa_tb_zero; wa_tb_vt[38] = wa_tb_zero;
  for (k = 3; k < 24; k++) wa_holo_vt[k] = wa_ho_any; wa_holo_vt[0] = wa_sn_qi; wa_holo_vt[1] = wa_sn_ref; wa_holo_vt[2] = wa_sn_ref;
  { static const void *const d[24] = { wa_sn_qi, wa_sn_ref, wa_sn_ref, wa_zero_i32, wa_d_put, wa_d_orient, wa_d_orient, wa_d_scale, wa_d_scale, wa_d_9, wa_event_add, wa_event_remove,
      wa_event_add, wa_event_remove, wa_d_14, wa_d_15, wa_d_16, wa_d_17, wa_d_18, wa_d_19, wa_d_20, wa_d_21, wa_d_22, wa_d_23 }; memcpy(wa_disp_vt, d, sizeof d); }
  for (k = 3; k < 14; k++) wa_sysnav_vt[k] = wa_ok;
  wa_sysnav_vt[0] = wa_sn_qi; wa_sysnav_vt[1] = wa_sn_ref; wa_sysnav_vt[2] = wa_sn_ref; wa_sysnav_vt[3] = wa_zero_i32; wa_sysnav_vt[7] = wa_false;
  for (k = 8; k < 14; k += 2) { wa_sysnav_vt[k] = wa_event_add; wa_sysnav_vt[k + 1] = wa_event_remove; }
  /* IApplicationViewClientAdapter */
  for (k = 3; k < 35; k++) wa_view_vt[k] = wa_ok;
  wa_view_vt[0] = wa_v_qi; wa_view_vt[1] = wa_v_ar; wa_view_vt[2] = wa_v_rl;
  wa_view_vt[3] = wa_zero_i32; wa_view_vt[4] = wa_zero_i32; wa_view_vt[5] = wa_false; wa_view_vt[6] = wa_false; wa_view_vt[7] = wa_ok; wa_view_vt[8] = wa_v_put_multiview;
  wa_view_vt[9] = wa_false; wa_view_vt[10] = wa_v_is_full_screen; wa_view_vt[11] = wa_null_hs; wa_view_vt[12] = wa_ok; wa_view_vt[13] = wa_v_get_launch_mode; wa_view_vt[14] = wa_v_put_launch_mode;
  wa_view_vt[15] = wa_v_get_launch_size; wa_view_vt[16] = wa_v_put_launch_size; wa_view_vt[17] = wa_v_get_min; wa_view_vt[18] = wa_v_put_min; wa_view_vt[19] = wa_v_get_suppress; wa_view_vt[20] = wa_v_put_suppress;
  wa_view_vt[21] = wa_v_get_title; wa_view_vt[22] = wa_v_put_title; wa_view_vt[23] = wa_zero_i32; wa_view_vt[24] = wa_ok; wa_view_vt[25] = wa_ok; wa_view_vt[26] = wa_v_is_mode_supported;
  wa_view_vt[27] = wa_v_refused; wa_view_vt[28] = wa_ok; wa_view_vt[29] = wa_v_refused; wa_view_vt[30] = wa_v_refused; wa_view_vt[31] = wa_v_refused; wa_view_vt[32] = wa_v_try_show_mode;
  wa_view_vt[33] = wa_event_add; wa_view_vt[34] = wa_event_remove;
  /* ICoreApplicationViewClientAdapter: PresentationLoading / Unloaded */
  wa_core_vt[0] = wa_c_qi; wa_core_vt[1] = wa_c_ar; wa_core_vt[2] = wa_c_rl; wa_core_vt[3] = wa_event_add; wa_core_vt[4] = wa_event_remove; wa_core_vt[5] = wa_event_add; wa_core_vt[6] = wa_event_remove;

}
static HRESULT attach_window_client_adapter(IUnknown *window, HWND hwnd) {
  struct { void **vt; } *interop = NULL; WinAdapter *a; HRESULT hr;
  hr = window->lpVtbl->QueryInterface(window, &SH_IID_ICoreWindowAdapterInterop, (void **)&interop);
  if (FAILED(hr)) { slog("[shell] CoreWindow has no ICoreWindowAdapterInterop: 0x%08lX", (unsigned long)hr); return hr; }
  wa_vt_init(); a = (WinAdapter *)calloc(1, sizeof *a); if (!a) { ((IUnknown *)interop)->lpVtbl->Release((IUnknown *)interop); return E_OUTOFMEMORY; }
  a->vt_window = wa_window_vt; a->vt_view = wa_view_vt; a->vt_core = wa_core_vt; a->ref = 1; a->hwnd = hwnd; a->launch.w = 500; a->launch.h = 320; a->minsz.w = 0; a->minsz.h = 0;
  CoCreateFreeThreadedMarshaler((IUnknown *)&a->vt_window, &a->ftm);
  hr = ((HRESULT (WINAPI *)(void *, IUnknown *))interop->vt[11])(interop, (IUnknown *)&a->vt_window);
  slog("[shell] SetWindowClientAdapter -> 0x%08lX", (unsigned long)hr);
  wa_release(a); ((IUnknown *)interop)->lpVtbl->Release((IUnknown *)interop); return hr; }
/* ---------------- ICoreWindowBroker / ICoreWindowFactory / IConfigureWindowFactory ---------------- */
typedef struct { const void *vt_broker, *vt_factory, *vt_configure; IUnknown *ftm; LONG ref; } WinFactory;
static WinFactory *wf_of_factory(void *i) { return (WinFactory *)((char *)i - offsetof(WinFactory, vt_factory)); }
static WinFactory *wf_of_configure(void *i) { return (WinFactory *)((char *)i - offsetof(WinFactory, vt_configure)); }
static HRESULT WINAPI wf_qi(void *i, REFIID iid, void **o) {
  WinFactory *w = (WinFactory *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_ICoreWindowBroker, 16)) *o = &w->vt_broker;
  else if (!memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_ICoreWindowFactory, 16)) *o = &w->vt_factory;
  else if (!memcmp(iid, &SH_IID_IConfigureWindowFactory, 16)) *o = &w->vt_configure;
  else if (w->ftm && (!memcmp(iid, &SH_IID_IMarshal, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16))) return w->ftm->lpVtbl->QueryInterface(w->ftm, iid, o);
  else { char g[48]; guid_str(iid, g); slog("[shell] window factory: no interface %s", g); *o = NULL; return E_NOINTERFACE; }
  InterlockedIncrement(&w->ref); return S_OK; }
static ULONG WINAPI wf_ar(void *i) { return InterlockedIncrement(&((WinFactory *)i)->ref); }
static ULONG WINAPI wf_rl(void *i) { WinFactory *w = (WinFactory *)i; LONG r = InterlockedDecrement(&w->ref); if (!r) { if (w->ftm) w->ftm->lpVtbl->Release(w->ftm); free(w); } return r; }
static HRESULT WINAPI wfb_query_service(void *i, const GUID *service, const GUID *iid, void **o) {
  static const GUID sid_wf = {0xD98FD14A, 0x522A, 0x4D59, {0xB8, 0x75, 0x81, 0x1E, 0x83, 0x91, 0x9A, 0x9E}}; (void)iid;
  if (service && !memcmp(service, &sid_wf, 16)) return wf_qi(i, &SH_IID_IUnknown, o);
  if (o) *o = NULL; return E_NOINTERFACE; }
static HRESULT WINAPI wfb_slot4(void *i, ULONG *n, GUID **ids) { return la_iids(i, n, ids); }
static HRESULT WINAPI wfb_from_cookie(void *i, unsigned long long cookie, void *opts, void **o) { (void)cookie; (void)opts; return wf_qi(i, &SH_IID_IUnknown, o); }
static const void *const wvt_broker[] = { wf_qi, wf_ar, wf_rl, wfb_query_service, wfb_slot4, wfb_from_cookie };
static HRESULT WINAPI wfa_qi(void *i, REFIID iid, void **o) { return wf_qi(wf_of_factory(i), iid, o); }
static ULONG WINAPI wfa_ar(void *i) { return wf_ar(wf_of_factory(i)); }
static ULONG WINAPI wfa_rl(void *i) { return wf_rl(wf_of_factory(i)); }
static HRESULT WINAPI wfa_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.UI.Core.UAPCoreWindowFactory"); return *hs ? S_OK : E_FAIL; }
static const GUID SH_IID_ICoreWindowStatic = {0x4D239005, 0x3C2A, 0x41B1, {0x90, 0x22, 0x53, 0x6B, 0xB9, 0xCF, 0x93, 0xB1}};
/* diagnostics: does CoreWindow.GetForCurrentThread() answer on this thread yet? (twinapi's CallerIdentity::GetCoreWindowForCurrentThread needs it) */
static void probe_get_for_current_thread(const char *where) {
  void *hs = hs_new(L"Windows.UI.Core.CoreWindow"), *st = NULL, *w = NULL; HRESULT hr = RoGetActivationFactory(hs, &SH_IID_ICoreWindowStatic, &st);
  if (SUCCEEDED(hr) && st) { hr = ((HRESULT (WINAPI *)(void *, void **))(*(void ***)st)[6])(st, &w); }
  slog("[shell] probe CoreWindow.GetForCurrentThread (%s) on tid %lu -> 0x%08lX window=%p", where, GetCurrentThreadId(), (unsigned long)hr, w);
}static HRESULT WINAPI wfa_reuse(void *i, unsigned char *v) { (void)i; if (!v) return E_POINTER; *v = 1; return S_OK; }   /* TRUE: activate the main view Run() already created instead of building a second view+thread */
/* Window size. Windows' shell picks the first-launch size of a PreferredLaunchWindowingMode=Auto app (Calculator) itself; this is ours, in
   DIPs: a portrait window like the shell's default for Calculator (the app's own minimum is 320x500). PreferredLaunchViewSize, when an app
   sets it, still wins (wa_apply_launch_size). The minimum the app asks for (SetPreferredMinSize) is enforced while resizing. */
#define SH_LAUNCH_W 360
#define SH_LAUNCH_H 600
static void sh_launch_size(UINT *w, UINT *h) { *w = SH_LAUNCH_W; *h = SH_LAUNCH_H; }   /* creation size; sh_place_* then size the window per app */
/* ---- WXR: window placement as the Win10 shell keeps it - per app (AUMID), per user: <root>\Users\<user>\window.ini [<AUMID>]
   Rect=l,t,r,b (normal position, pixels) Max=0|1. Saved on move/resize/close, restored at the next launch when it is still on a monitor. */
static int g_sh_placed;   /* the window got a remembered placement (or the user moved it): no automatic sizing any more */
static int sh_place_file(WCHAR *ini) { WCHAR user[128]; DWORD un = 128; pkg_load(); if (!g_wxr_on || !g_aumid[0] || !GetUserNameW(user, &un)) return 0;
  _snwprintf(ini, MAX_PATH, L"%ls\\Users\\%ls\\window.ini", g_wxr_root, user); return 1; }
static void sh_place_save(HWND h) {
  WINDOWPLACEMENT wp; WCHAR ini[MAX_PATH], v[96]; wp.length = sizeof wp;
  if (!sh_place_file(ini) || !GetWindowPlacement(h, &wp) || wp.showCmd == SW_SHOWMINIMIZED && wp.rcNormalPosition.right <= wp.rcNormalPosition.left) return;
  _snwprintf(v, 96, L"%ld,%ld,%ld,%ld", wp.rcNormalPosition.left, wp.rcNormalPosition.top, wp.rcNormalPosition.right, wp.rcNormalPosition.bottom);
  WritePrivateProfileStringW(g_aumid, L"Rect", v, ini); WritePrivateProfileStringW(g_aumid, L"Max", IsZoomed(h) ? L"1" : L"0", ini); }
/* returns SW_SHOW / SW_SHOWMAXIMIZED when a remembered placement was applied, 0 otherwise */
static int sh_place_restore(HWND h) {
  WCHAR ini[MAX_PATH], v[96]; RECT r; WINDOWPLACEMENT wp;
  if (!sh_place_file(ini) || !GetPrivateProfileStringW(g_aumid, L"Rect", L"", v, 96, ini)) return 0;
  if (swscanf(v, L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) != 4 || r.right - r.left < 100 || r.bottom - r.top < 100 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return 0;
  memset(&wp, 0, sizeof wp); wp.length = sizeof wp; wp.showCmd = SW_HIDE; wp.rcNormalPosition = r; SetWindowPlacement(h, &wp);
  g_sh_placed = 1; slog("[shell] window placement restored: %ls", v);
  return GetPrivateProfileIntW(g_aumid, L"Max", 0, ini) ? SW_SHOWMAXIMIZED : SW_SHOW; }
/* first launch (nothing remembered): size from the app's minimum - narrow apps (Calculator, min 320x500) a compact portrait window,
   wider ones (Store 500x500, Media Player 576x328) a desktop-sized one; centred, clamped to the work area */
static void sh_place_default(HWND h, float minw, float minh) {
  float s = wa_scale(h), w, hh; RECT rc, wk; int x, y;
  if (!g_wxr_on || g_sh_placed || !h) return;
  if (minw >= 400) { w = 1100; hh = 720; } else {   /* only really narrow apps (Calculator, 320) start compact; the Store asks for 500 */ w = minw + 40 > 360 ? minw + 40 : 360; hh = minh + 100 > 600 ? minh + 100 : 600; }
  SetRect(&rc, 0, 0, (LONG)(w * s), (LONG)(hh * s)); AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wk, 0)) return;
  if (rc.right - rc.left > (wk.right - wk.left) * 9 / 10) rc.right = rc.left + (wk.right - wk.left) * 9 / 10;
  if (rc.bottom - rc.top > (wk.bottom - wk.top) * 9 / 10) rc.bottom = rc.top + (wk.bottom - wk.top) * 9 / 10;
  x = wk.left + ((wk.right - wk.left) - (rc.right - rc.left)) / 2; y = wk.top + ((wk.bottom - wk.top) - (rc.bottom - rc.top)) / 2;
  SetWindowPos(h, NULL, x, y, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
  slog("[shell] first-launch window size %gx%g DIP (app minimum %gx%g)", w, hh, minw, minh); }
static LRESULT CALLBACK sh_frame_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
  (void)id; (void)ref;
  if (g_wxr_on) {
    if (msg == WM_SIZE || msg == WM_MOVE) wxr_splash_resize(h);
    if (msg == WM_EXITSIZEMOVE) { g_sh_placed = 1; sh_place_save(h); }
    else if (msg == WM_SIZE && (wp == SIZE_MAXIMIZED || wp == SIZE_RESTORED) && IsWindowVisible(h) && g_sh_placed) sh_place_save(h);
    else if (msg == WM_CLOSE || msg == WM_DESTROY) sh_place_save(h); }
  if (msg == WM_GETMINMAXINFO && g_sh_min_w > 0 && g_sh_min_h > 0) {
    LRESULT r = DefSubclassProc(h, msg, wp, lp); MINMAXINFO *mm = (MINMAXINFO *)lp; float s = wa_scale(h);
    RECT rc = {0, 0, (LONG)(g_sh_min_w * s + 0.5f), (LONG)(g_sh_min_h * s + 0.5f)};
    AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
    mm->ptMinTrackSize.x = rc.right - rc.left; mm->ptMinTrackSize.y = rc.bottom - rc.top; return r;
  }
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, sh_frame_subclass, 1);
  return DefSubclassProc(h, msg, wp, lp);
}
typedef HRESULT (WINAPI *PPrivateCreateCoreWindow)(DWORD type, const WCHAR *title, int x, int y, UINT w, UINT h, DWORD flags, HWND parent, const GUID *iid, void **out);
static HRESULT WINAPI wfa_create_core_window(void *i, void *title, void **window) {
  static PPrivateCreateCoreWindow pcreate; const WCHAR *str; UINT32 len = 0; HRESULT hr; (void)i;
  if (!window) return E_POINTER; *window = NULL;
  if (!pcreate) { int d; HMODULE ui = NULL; alock(1); act_init(); alock(0);
    for (d = 0; d < g_ndll; d++) if (!lstrcmpiW(g_dllname[d], L"Windows.UI.dll")) ui = act_load(d);
    if (!ui) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    pcreate = (PPrivateCreateCoreWindow)GetProcAddress(ui, (const char *)(ULONG_PTR)1500); if (!pcreate) return E_FAIL; }
  str = p_raw(title, &len); if (!str || !len) str = L"App";
  slog("[shell] CreateCoreWindow(%ls) on tid %lu", str, GetCurrentThreadId());
  { UINT lw, lh; sh_launch_size(&lw, &lh); hr = pcreate(1, str, 0, 0, lw, lh, 0, NULL, &SH_IID_ICoreWindow, window); }
  slog("[shell] PrivateCreateCoreWindow -> 0x%08lX window=%p hwnd=%p", (unsigned long)hr, *window, (void *)g_lastwnd_created);
  if (FAILED(hr)) return hr;
  { HWND h = g_lastwnd_created;
    if (h) { LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE); float s = wa_scale(h); UINT lw, lh; RECT rc, wk; int x = 0, y = 0; sh_launch_size(&lw, &lh); SetRect(&rc, 0, 0, (LONG)(lw * s), (LONG)(lh * s));
      st = (st & ~(WS_POPUP | WS_CHILD)) | WS_OVERLAPPEDWINDOW; SetWindowLongPtrW(h, GWL_STYLE, st);
      AdjustWindowRectEx(&rc, (DWORD)st, FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
      if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wk, 0)) { if (rc.right - rc.left > wk.right - wk.left) { rc.left = 0; rc.right = wk.right - wk.left; } if (rc.bottom - rc.top > wk.bottom - wk.top) { rc.top = 0; rc.bottom = wk.bottom - wk.top; } x = wk.left + ((wk.right - wk.left) - (rc.right - rc.left)) / 2; y = wk.top + ((wk.bottom - wk.top) - (rc.bottom - rc.top)) / 2; if (x < wk.left) x = wk.left; if (y < wk.top) y = wk.top; }
      SetWindowPos(h, NULL, x, y, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      SetWindowSubclass(h, sh_frame_subclass, 1, 0);
      wxr_window_decorate(h);   /* name, icon, taskbar identity from the shell data (wxr sync); WXR_WINDOW_TITLE overrides the name */
      g_hosted_wnd = h;
      if (g_wxr_on) wxr_splash_create(h);   /* WXR: the manifest's splash screen until the first frame */
      { int sc = g_wxr_on ? sh_place_restore(h) : 0; ShowWindow(h, sc ? sc : SW_SHOW); if (g_wxr_on) wxr_splash_resize(h); }   /* WXR: last placement of this app; splash shown over it */ SetForegroundWindow(h); notify_dcomp_window(h); } }
  probe_get_for_current_thread("after PrivateCreateCoreWindow");
  { struct NavHost { const void **vt; } *host = NULL; IUnknown *client = NULL;
    hr = ((IUnknown *)*window)->lpVtbl->QueryInterface((IUnknown *)*window, &SH_IID_ICoreWindowNavigation, (void **)&host);
    if (FAILED(hr)) { slog("[shell] CoreWindow has no navigation client property: 0x%08lX", (unsigned long)hr); return hr; }
    if (SUCCEEDED(hr = make_nav_client(&client))) {
      hr = ((HRESULT (WINAPI *)(void *, IUnknown *))host->vt[7])(host, client); slog("[shell] put_NavigationClient -> 0x%08lX", (unsigned long)hr); probe_get_for_current_thread("after put_NavigationClient");
      client->lpVtbl->Release(client); }
    ((IUnknown *)host)->lpVtbl->Release((IUnknown *)host); }
  attach_window_client_adapter((IUnknown *)*window, g_hosted_wnd ? g_hosted_wnd : g_lastwnd_created);
  return hr; }
static const void *const wvt_factory[] = { wfa_qi, wfa_ar, wfa_rl, la_iids, wfa_class, la_trust, wfa_create_core_window, wfa_reuse };
static HRESULT WINAPI wfc_qi(void *i, REFIID iid, void **o) { return wf_qi(wf_of_configure(i), iid, o); }
static ULONG WINAPI wfc_ar(void *i) { return wf_ar(wf_of_configure(i)); }
static ULONG WINAPI wfc_rl(void *i) { return wf_rl(wf_of_configure(i)); }
static HRESULT WINAPI wfc_splash(void *i, DWORD flags, void **s) { (void)i; (void)flags; if (s) *s = NULL; return S_OK; }
static HRESULT WINAPI wfc_appwnd(void *i, HWND *h) { (void)i; if (h) *h = NULL; return S_OK; }
static HRESULT WINAPI wfc_hassplash(void *i, BOOL *h) { (void)i; if (h) *h = FALSE; return S_OK; }
static const void *const wvt_configure[] = { wfc_qi, wfc_ar, wfc_rl, nv_ok, nv_ok, wfc_splash, wfc_appwnd, wfc_hassplash, nv_ok };
static HRESULT make_window_factory(const GUID *iid, void **o) {
  WinFactory *w = (WinFactory *)calloc(1, sizeof *w); HRESULT hr; if (!w) return E_OUTOFMEMORY;
  w->vt_broker = wvt_broker; w->vt_factory = wvt_factory; w->vt_configure = wvt_configure; w->ref = 1;
  CoCreateFreeThreadedMarshaler((IUnknown *)&w->vt_broker, &w->ftm);
  hr = wf_qi(w, iid, o); wf_rl(w); return hr; }

/* ---------------- Windows.UI.UIContext (Windows keeps it in dcomp.dll; under WXR wxrsys serves it) ----------------
   Shapes follow wine-uwp's dcomp/device.c. CoreWindow asks the private statics {97473389-..} slot 7 for a per-view identity token (IUIContext
   {f21e14c1-..}); callers compare it, and walk UIContext -> get_WindowContext -> IWindowHostPartner {84acbbfe-..} -> slot 7 -> slot 7 to reach the HWND. */
static const GUID SH_IID_IUIContextStatics = {0x97473389, 0x5B37, 0x5C96, {0x9C, 0x2B, 0x0F, 0xD2, 0x9D, 0x43, 0xA6, 0xC1}};
static const GUID SH_IID_IUIContext = {0xF21E14C1, 0xE669, 0x52AF, {0x99, 0xCD, 0x17, 0x1E, 0xEE, 0x8E, 0x94, 0x0D}};
static const GUID SH_IID_IWindowHostPartner = {0x84ACBBFE, 0x649C, 0x5AC7, {0x81, 0x30, 0x7B, 0x6B, 0x76, 0xDB, 0x46, 0x3C}};
static const GUID SH_IID_ICoreWindowInterop = {0x45D64A29, 0xA63E, 0x4CB6, {0xB4, 0x98, 0x57, 0x81, 0xD2, 0x98, 0xCB, 0x4F}};
typedef struct { const void *vt; LONG ref; HWND hwnd; int last; } WinHost;      /* window host hop; also reused as WindowContext / UIContext (different vtables) */
static HRESULT WINAPI wh_qi(void *i, REFIID iid, void **o) {
  WinHost *w = (WinHost *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16) || !memcmp(iid, &SH_IID_IWindowHostPartner, 16)) { InterlockedIncrement(&w->ref); *o = i; return S_OK; }
  *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI wh_ar(void *i) { return InterlockedIncrement(&((WinHost *)i)->ref); }
static ULONG WINAPI wh_rl(void *i) { WinHost *w = (WinHost *)i; LONG r = InterlockedDecrement(&w->ref); if (!r) free(w); return r; }
static HRESULT WINAPI wh_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.UI.WindowContext"); return *hs ? S_OK : E_FAIL; }
static HRESULT make_winhost(const void *vt, HWND h, int last, void **o) { WinHost *w = (WinHost *)calloc(1, sizeof *w); if (!w) return E_OUTOFMEMORY; w->vt = vt; w->ref = 1; w->hwnd = h; w->last = last; *o = w; return S_OK; }
static HRESULT WINAPI whs_8(void *i, void *a, void *b) { (void)i; (void)a; (void)b; log_callers("[shell] window host slot 8 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_9(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 9 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_10(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 10 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_11(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 11 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_12(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 12 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_13(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 13 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_14(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 14 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_15(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 15 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_16(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 16 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_17(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 17 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_18(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 18 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_19(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 19 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_20(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 20 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_21(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 21 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_22(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 22 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_23(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 23 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_24(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 24 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_25(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 25 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_26(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 26 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_27(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 27 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_28(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 28 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_29(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 29 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_30(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 30 -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI whs_31(void *i, void *a, void *b) { (void)i; (void)a; (void)b; slog("[shell] window host slot 31 -> E_NOTIMPL"); return E_NOTIMPL; }
/* slot 17 (MrmCoreR): float "resolution scale" (1.0 = 100%) of the view; it multiplies it by 100 */
static HRESULT WINAPI wh_scale(void *i, float *o) { HDC dc; int dpi = 96; (void)i; if (!o) return E_POINTER; dc = GetDC(NULL); if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc); } *o = dpi / 96.0f; return S_OK; }
static const void *const whs_tab[24] = { whs_8, whs_9, whs_10, whs_11, whs_12, whs_13, whs_14, whs_15, whs_16, whs_17, whs_18, whs_19, whs_20, whs_21, whs_22, whs_23, whs_24, whs_25, whs_26, whs_27, whs_28, whs_29, whs_30, whs_31 };
static const void *wh_vt[32];
static HRESULT WINAPI wh_slot6(void *i, void **o) { WinHost *w = (WinHost *)i; if (!o) return E_POINTER; return make_winhost(wh_vt, w->hwnd, 0, o); }
static HRESULT WINAPI wh_slot7(void *i, void **o) { WinHost *w = (WinHost *)i; if (!o) return E_POINTER;
  if (w->last) { *o = (void *)w->hwnd; return w->hwnd ? S_OK : E_FAIL; } return make_winhost(wh_vt, w->hwnd, 1, o); }
static const void *wh_vt_init(void) { static int d; static const void **v = (const void **)wh_vt; int k; if (!d) { d = 1; v[0] = wh_qi; v[1] = wh_ar; v[2] = wh_rl; v[3] = la_iids; v[4] = wh_class; v[5] = la_trust; v[6] = wh_slot6; v[7] = wh_slot7; for (k = 8; k < 32; k++) v[k] = whs_tab[k - 8]; v[17] = wh_scale; } return v; }
/* WindowContext object: QI(IWindowHostPartner) -> first hop */
static const void *wc_vt[6];
static HRESULT WINAPI wc_qi(void *i, REFIID iid, void **o) {
  WinHost *w = (WinHost *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16)) { InterlockedIncrement(&w->ref); *o = i; return S_OK; }
  if (!memcmp(iid, &SH_IID_IWindowHostPartner, 16)) { wh_vt_init(); return make_winhost(wh_vt, w->hwnd ? w->hwnd : GetActiveWindow(), 0, o); }
  *o = NULL; return E_NOINTERFACE; }
/* UIContext object */
static const void *uc_vt[8];
static HRESULT WINAPI uc_qi(void *i, REFIID iid, void **o) {
  WinHost *w = (WinHost *)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16) || !memcmp(iid, &SH_IID_IUIContext, 16)) { InterlockedIncrement(&w->ref); *o = i; return S_OK; }
  { char g[48]; guid_str(iid, g); slog("[shell] UIContext: no interface %s", g); } *o = NULL; return E_NOINTERFACE; }
static HRESULT WINAPI uc_class(void *i, void **hs) { (void)i; *hs = hs_new(L"Windows.UI.UIContext"); return *hs ? S_OK : E_FAIL; }
static HRESULT WINAPI uc_slot6(void *i, void **o) { (void)i; if (o) *o = NULL; slog("[shell] UIContext slot 6 (get_UIContentRoot) -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI uc_get_window_context(void *i, void **o) { WinHost *w = (WinHost *)i; if (!o) return E_POINTER; return make_winhost(wc_vt, w->hwnd, 0, o); }
static void uc_vt_init(void) { static int d; const void **c = (const void **)wc_vt, **u = (const void **)uc_vt; if (d) return; d = 1;
  c[0] = wc_qi; c[1] = wh_ar; c[2] = wh_rl; c[3] = la_iids; c[4] = wh_class; c[5] = la_trust;
  u[0] = uc_qi; u[1] = wh_ar; u[2] = wh_rl; u[3] = la_iids; u[4] = uc_class; u[5] = la_trust; u[6] = uc_slot6; u[7] = uc_get_window_context; }
static HWND hwnd_from_view(void *view) {
  HWND h = NULL; void **interop = NULL;
  /* ICoreWindowInterop is IUnknown-based: get_WindowHandle is slot 3. Slot 6 (as for an IInspectable) hit another entry of CoreWindow's
     vtable at +0x10 and bumped WindowServer+0x170 (CoreInputBase's PointerPoint factory pointer) from 0 to 1: the first CoreWindow
     pointer event then crashed in PopulatePointerEventArgs. */
  if (view && SUCCEEDED(((IUnknown *)view)->lpVtbl->QueryInterface((IUnknown *)view, &SH_IID_ICoreWindowInterop, (void **)&interop))) {
    if (FAILED(((HRESULT (WINAPI *)(void *, HWND *))(*(void ***)interop)[3])(interop, &h))) h = NULL;
    ((IUnknown *)interop)->lpVtbl->Release((IUnknown *)interop); }
  return h; }
static HRESULT WINAPI ucs_create_for_view(void *i, void *view, UINT32 flags, void **o) { HWND h; (void)i; (void)flags; if (!o) return E_POINTER; uc_vt_init(); h = hwnd_from_view(view); if (!h) h = g_lastwnd_created;
  slog("[shell] UIContext::CreateForView(view=%p) hwnd=%p", view, (void *)h); return make_winhost(uc_vt, h, 0, o); }
/* the factory: IActivationFactory + private statics, singleton */
static const void *ucf_obj[2];
static HRESULT WINAPI ucf_qi(void *i, REFIID iid, void **o) {
  (void)i;
  if (!memcmp(iid, &SH_IID_IUnknown, 16) || !memcmp(iid, &SH_IID_IInspectable, 16) || !memcmp(iid, &SH_IID_IAgileObject, 16) || !memcmp(iid, &SH_IID_IActivationFactory_, 16)) { *o = &ucf_obj[0]; return S_OK; }
  if (!memcmp(iid, &SH_IID_IUIContextStatics, 16)) { *o = &ucf_obj[1]; return S_OK; }
  { char g[48]; guid_str(iid, g); slog("[shell] UIContext factory: no interface %s", g); } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI ucf_ar(void *i) { (void)i; return 2; }
static ULONG WINAPI ucf_rl(void *i) { (void)i; return 1; }
static HRESULT WINAPI ucf_activate(void *i, void **o) { (void)i; if (o) *o = NULL; log_callers("UIContext ActivateInstance"); return E_NOTIMPL; }
static const void *const ucf_vt_act[7] = { ucf_qi, ucf_ar, ucf_rl, la_iids, uc_class, la_trust, ucf_activate };
static const void *const ucf_vt_st[8] = { ucf_qi, ucf_ar, ucf_rl, la_iids, uc_class, la_trust, nv_notimpl, ucs_create_for_view };
static void *shell_builtin_factory(const WCHAR *cls) {
  if (lstrcmpiW(cls, L"Windows.UI.UIContext")) return NULL;
  ucf_obj[0] = ucf_vt_act; ucf_obj[1] = ucf_vt_st; return &ucf_obj[0]; }
/* called from com_inproc(): built-in shell classes */
static HRESULT shell_create(const GUID *clsid, const GUID *iid, void **ppv) {
  if (!memcmp(clsid, &SH_CLSID_ActivatableApplicationRegistrar, 16)) return reg_qi(&g_registrar, iid, ppv);
  if (!memcmp(clsid, &SH_CLSID_CoreWindowBroker, 16)) return make_window_factory(iid, ppv);
  return REGDB_E_CLASSNOTREG;
}




















