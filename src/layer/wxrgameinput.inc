/* ---- GameInput stand-in (included from wxrsys.c) -------------------------------------------------------------------------------
   GDK titles (Minecraft Bedrock) load GameInputRedist.dll (HKLM\SOFTWARE\Microsoft\GameInput RedistDir: the layer answers it with
   <root>\Program Files\Microsoft GameInput\x64, where "wxrpkg gdk" puts the game's own redistributable; see gi_redist_dir in wxrdx.inc)
   and call GameInputInitialize(riid, &IGameInput) / GameInputCreate(&IGameInput). The real redist talks to GameInputRedistService.exe
   (a Win10 SYSTEM service, not running under WXR) and fails with 0x838AD009 -> "Your game is missing a required component".
   The entry points resolved at run time (WxrGetProcAddress) are wrapped: the REAL one is called first, so a working GameInput always
   wins; only when it fails does the app get this stand-in.

   The stand-in speaks GameInput v2 (Bedrock asks GameInputInitialize for the v0 IID {11BE2A7E} and then QIs v2 {BBAA66D2}); layouts
   come from Microsoft's public GameInput.h (NuGet Microsoft.GameInput, include\v2). It has ONE device: a virtual mouse fed by Win32 raw
   input (own message-only window and thread; relative motion, buttons, wheel; only while a window of this process has the focus).
   Keyboard is not offered (Bedrock reads it from window messages; it registers GameInput device callbacks for Mouse|Gamepad only).
   Bedrock takes its mouse from GameInput once GameInput is there, so a stand-in without a mouse device left the mouse dead. */
typedef HRESULT (WINAPI *PGIInit_)(REFIID, void **);
typedef HRESULT (WINAPI *PGICreate_)(void **);
static PGIInit_ g_gi_init_real; static PGICreate_ g_gi_create_real;
#define GI_KIND_MOUSE 0x20u
#define GI_E_READING_NOT_FOUND ((HRESULT)0x838A0003)
#define GI_E_DEVICE_NOT_FOUND  ((HRESULT)0x838A0001)
static int gi_logon(void) { return 0; }
static UINT64 gi_now(void) {
  LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
  return (UINT64)(c.QuadPart / f.QuadPart) * 1000000ull + (UINT64)(c.QuadPart % f.QuadPart) * 1000000ull / (UINT64)f.QuadPart; }

/* ---- mouse state (raw input thread writes, readings snapshot) ---- */
typedef struct { UINT32 buttons, positions; INT64 x, y, ax, ay, wx, wy; } GiMouseState;   /* = GameInputMouseState v2 (56 bytes) */
static CRITICAL_SECTION g_gi_cs; static volatile LONG g_gi_cs_init; static GiMouseState g_gi_ms; static UINT64 g_gi_seq, g_gi_mts;
static void gi_lock(int on) { if (!g_gi_cs_init) { if (!InterlockedCompareExchange(&g_gi_cs_init, 1, 0)) { InitializeCriticalSection(&g_gi_cs); g_gi_cs_init = 2; } while (g_gi_cs_init != 2) Sleep(0); }
  if (on) EnterCriticalSection(&g_gi_cs); else LeaveCriticalSection(&g_gi_cs); }

/* ---- objects ---- */
typedef struct GiObj { const void *const *vt; LONG ref; int kind; GiMouseState ms; UINT64 seq, ts; } GiObj;   /* kind 0 = IGameInput, 1 = device, 2 = reading */
static const GUID IID_GiInput0 = {0x11BE2A7E, 0x4254, 0x445A, {0x9C, 0x09, 0xFF, 0xC4, 0x0F, 0x00, 0x69, 0x18}};
static const GUID IID_GiInput2 = {0xBBAA66D2, 0x837A, 0x40F7, {0xA3, 0x03, 0x91, 0x7D, 0x50, 0x09, 0x55, 0xF4}};
static const GUID IID_GiReading2 = {0x65F06483, 0xDB76, 0x40B7, {0xB7, 0x45, 0xF5, 0x91, 0xFA, 0xE5, 0x5F, 0xC9}};
static const GUID IID_GiDevice2 = {0x39A0C9F2, 0xF055, 0x4089, {0x96, 0xE7, 0x25, 0xB2, 0x75, 0x6D, 0x30, 0x77}};
static GiObj *g_gi_mouse;   /* the one device (process lifetime) */
static HRESULT STDMETHODCALLTYPE gi_qi(void *t, REFIID iid, void **o) {
  static const GUID iu = {0, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}}; GiObj *g = (GiObj *)t; int ok;
  if (!o) return E_POINTER; *o = NULL;
  ok = IsEqualGUID(iid, &iu) || (g->kind == 0 && (IsEqualGUID(iid, &IID_GiInput0) || IsEqualGUID(iid, &IID_GiInput2)))
    || (g->kind == 1 && IsEqualGUID(iid, &IID_GiDevice2)) || (g->kind == 2 && IsEqualGUID(iid, &IID_GiReading2));
  if (!ok) { char gg[48]; static LONG n; guid_str(iid, gg); if (InterlockedIncrement(&n) <= 12) slog("[gameinput] QI(%s) on kind %d -> E_NOINTERFACE", gg, g->kind); return E_NOINTERFACE; }
  *o = t; InterlockedIncrement(&g->ref); return S_OK; }
static ULONG STDMETHODCALLTYPE gi_ar(void *t) { return (ULONG)InterlockedIncrement(&((GiObj *)t)->ref); }
static ULONG STDMETHODCALLTYPE gi_rl(void *t) { GiObj *g = (GiObj *)t; LONG r = InterlockedDecrement(&g->ref); if (r <= 0 && g != g_gi_mouse) { free(g); return 0; } return (ULONG)(r < 0 ? 0 : r); }
static GiObj *gi_alloc(const void *const *vt, int kind) { GiObj *o = (GiObj *)calloc(1, sizeof *o); if (o) { o->vt = vt; o->ref = 1; o->kind = kind; } return o; }

/* ---- IGameInputReading (v2) ---- */
static UINT32 STDMETHODCALLTYPE gr_kind(void *t) { (void)t; return GI_KIND_MOUSE; }
static UINT64 STDMETHODCALLTYPE gr_ts(void *t) { return ((GiObj *)t)->ts; }
static void STDMETHODCALLTYPE gr_dev(void *t, void **dev) { (void)t; if (dev) { *dev = g_gi_mouse; if (g_gi_mouse) gi_ar(g_gi_mouse); } }
static UINT32 STDMETHODCALLTYPE gr_zero0(void *t) { (void)t; return 0; }
static UINT32 STDMETHODCALLTYPE gr_zero2(void *t, UINT32 n, void *a) { (void)t; (void)n; (void)a; return 0; }
static BOOLEAN STDMETHODCALLTYPE gr_mouse(void *t, void *st) { if (st) memcpy(st, &((GiObj *)t)->ms, sizeof(GiMouseState)); return 1; }
static BOOLEAN STDMETHODCALLTYPE gr_false(void *t, void *st) { (void)t; (void)st; return 0; }
static const void *const g_gr_vt[] = { gi_qi, gi_ar, gi_rl, gr_kind, gr_ts, gr_dev, gr_zero0, gr_zero2, gr_zero0, gr_zero2, gr_zero0, gr_zero2,
  gr_zero0, gr_zero2, gr_mouse, gr_false, gr_false, gr_false, gr_false, gr_false, gr_false };
static GiObj *gi_reading(void) {
  GiObj *r = gi_alloc(g_gr_vt, 2); if (!r) return NULL;
  gi_lock(1); r->ms = g_gi_ms; r->seq = g_gi_seq; r->ts = g_gi_mts ? g_gi_mts : gi_now(); gi_lock(0); return r; }

/* ---- IGameInputDevice (v2): a virtual HID mouse ---- */
static BYTE g_gi_devinfo[272], g_gi_mouseinfo[12];
static void gi_devinfo_init(void) {
  BYTE *d = g_gi_devinfo; static const char nm[] = "WXR mouse"; static const char pnp[] = "WXR\\GameInput\\Mouse"; UINT32 u; int i;
  *(UINT16 *)(d + 6) = 0x0001; *(UINT16 *)(d + 8) = 0x0002;           /* usage: Generic Desktop / Mouse */
  for (i = 0; i < 32; i++) { d[26 + i] = (BYTE)(0x57 + i * 13); d[58 + i] = d[26 + i]; }   /* deviceId / deviceRootId (stable) */
  u = 3; memcpy(d + 92, &u, 4);                                       /* deviceFamily = GameInputFamilyHid */
  u = GI_KIND_MOUSE; memcpy(d + 96, &u, 4);                           /* supportedInput */
  *(const char **)(d + 128) = nm; *(const char **)(d + 136) = pnp;
  { UINT32 b = 0x1F, rate = 125; memcpy(g_gi_mouseinfo, &b, 4); memcpy(g_gi_mouseinfo + 4, &rate, 4); g_gi_mouseinfo[8] = 0; g_gi_mouseinfo[9] = 1; }
  *(BYTE **)(d + 152) = g_gi_mouseinfo; }
static HRESULT STDMETHODCALLTYPE gd_info(void *t, const void **info) { (void)t; if (!info) return E_POINTER; *info = g_gi_devinfo; return S_OK; }
static HRESULT STDMETHODCALLTYPE gd_haptic(void *t, void *info) { (void)t; (void)info; return E_NOTIMPL; }
static UINT32 STDMETHODCALLTYPE gd_status(void *t) { (void)t; return 1; }   /* GameInputDeviceConnected */
static HRESULT STDMETHODCALLTYPE gd_ffb(void *t, UINT32 m, const void *p, void **o) { (void)t; (void)m; (void)p; if (o) *o = NULL; return E_NOTIMPL; }
static BOOLEAN STDMETHODCALLTYPE gd_ffbon(void *t, UINT32 m) { (void)t; (void)m; return 0; }
static void STDMETHODCALLTYPE gd_gain(void *t, UINT32 m, float g) { (void)t; (void)m; (void)g; }
static void STDMETHODCALLTYPE gd_rumble(void *t, const void *p) { (void)t; (void)p; }
static HRESULT STDMETHODCALLTYPE gd_escape(void *t, UINT32 c, const void *i, UINT32 is, void *o, UINT32 os, UINT32 *w) { (void)t; (void)c; (void)i; (void)is; (void)o; (void)os; if (w) *w = 0; return E_NOTIMPL; }
static const void *const g_gd_vt[] = { gi_qi, gi_ar, gi_rl, gd_info, gd_haptic, gd_status, gd_ffb, gd_ffbon, gd_gain, gd_rumble, gd_escape };

/* ---- callbacks ---- */
typedef void (CALLBACK *GiReadingCb)(UINT64 tok, void *ctx, void *reading);
typedef void (CALLBACK *GiDeviceCb)(UINT64 tok, void *ctx, void *dev, UINT64 ts, UINT32 cur, UINT32 prev);
typedef struct { UINT64 tok; int type; UINT32 kind; void *ctx; void *cb; volatile LONG live; } GiCb;   /* type 1 = reading, 2 = device */
static GiCb g_gi_cbs[32]; static volatile LONG64 g_gi_tok;
static UINT64 gi_add_cb(int type, UINT32 kind, void *ctx, void *cb) {
  int i; UINT64 tok = (UINT64)InterlockedIncrement64(&g_gi_tok);
  gi_lock(1); for (i = 0; i < 32; i++) if (!g_gi_cbs[i].live) { g_gi_cbs[i].tok = tok; g_gi_cbs[i].type = type; g_gi_cbs[i].kind = kind; g_gi_cbs[i].ctx = ctx; g_gi_cbs[i].cb = cb; g_gi_cbs[i].live = 1; break; } gi_lock(0);
  return tok; }
/* a mouse event: reading callbacks that asked for mouse input get a fresh reading (called on the raw input thread) */
static void gi_fire_readings(void) {
  GiCb cs[32]; int i, n = 0; gi_lock(1); for (i = 0; i < 32; i++) if (g_gi_cbs[i].live && g_gi_cbs[i].type == 1 && (g_gi_cbs[i].kind & GI_KIND_MOUSE)) cs[n++] = g_gi_cbs[i]; gi_lock(0);
  for (i = 0; i < n; i++) { GiObj *r = gi_reading(); if (!r) break; ((GiReadingCb)cs[i].cb)(cs[i].tok, cs[i].ctx, r); gi_rl(r); } }

/* ---- raw input thread ---- */
static int gi_focused(void) { HWND f = GetForegroundWindow(); DWORD pid = 0; if (!f) return 0; GetWindowThreadProcessId(f, &pid); return pid == GetCurrentProcessId(); }
static LRESULT CALLBACK gi_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_INPUT) {
    RAWINPUT ri; UINT sz = sizeof ri;
    if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE) {
      RAWMOUSE *rm = &ri.data.mouse; USHORT bf = rm->usButtonFlags; int foc = gi_focused(); POINT pt;
      gi_lock(1);
      if (foc) {
        if (!(rm->usFlags & MOUSE_MOVE_ABSOLUTE)) { g_gi_ms.x += rm->lLastX; g_gi_ms.y += rm->lLastY; }
        if (bf & RI_MOUSE_WHEEL) g_gi_ms.wy += (SHORT)rm->usButtonData;
        if (bf & 0x0800 /* RI_MOUSE_HWHEEL */) g_gi_ms.wx += (SHORT)rm->usButtonData;
        if (bf & RI_MOUSE_LEFT_BUTTON_DOWN) g_gi_ms.buttons |= 1;   if (bf & RI_MOUSE_LEFT_BUTTON_UP) g_gi_ms.buttons &= ~1u;
        if (bf & RI_MOUSE_RIGHT_BUTTON_DOWN) g_gi_ms.buttons |= 2;  if (bf & RI_MOUSE_RIGHT_BUTTON_UP) g_gi_ms.buttons &= ~2u;
        if (bf & RI_MOUSE_MIDDLE_BUTTON_DOWN) g_gi_ms.buttons |= 4; if (bf & RI_MOUSE_MIDDLE_BUTTON_UP) g_gi_ms.buttons &= ~4u;
        if (bf & RI_MOUSE_BUTTON_4_DOWN) g_gi_ms.buttons |= 8;      if (bf & RI_MOUSE_BUTTON_4_UP) g_gi_ms.buttons &= ~8u;
        if (bf & RI_MOUSE_BUTTON_5_DOWN) g_gi_ms.buttons |= 16;     if (bf & RI_MOUSE_BUTTON_5_UP) g_gi_ms.buttons &= ~16u;
      } else g_gi_ms.buttons = 0;   /* focus lost: no buttons held */
      { HWND f = GetForegroundWindow(); if (foc && f && GetCursorPos(&pt) && ScreenToClient(f, &pt)) { g_gi_ms.ax = pt.x; g_gi_ms.ay = pt.y; } }
      g_gi_ms.positions = 3;   /* GameInputMouseAbsolutePosition | GameInputMouseRelativePosition */
      g_gi_seq++; g_gi_mts = gi_now();
      gi_lock(0);
      if (foc) gi_fire_readings(); }
    return DefWindowProcW(h, m, w, l); }
  return DefWindowProcW(h, m, w, l); }
static DWORD WINAPI gi_raw_thread(void *p) {
  WNDCLASSW wc; HWND h; RAWINPUTDEVICE rid; MSG msg; (void)p;
  memset(&wc, 0, sizeof wc); wc.lpfnWndProc = gi_wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"WxrGameInputMouse"; RegisterClassW(&wc);
  h = CreateWindowExW(0, L"WxrGameInputMouse", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
  rid.usUsagePage = 1; rid.usUsage = 2; rid.dwFlags = RIDEV_INPUTSINK; rid.hwndTarget = h;
  slog("[gameinput] raw mouse sink window %p: RegisterRawInputDevices -> %d (err %lu)", (void *)h, h ? (int)RegisterRawInputDevices(&rid, 1, sizeof rid) : 0, GetLastError());
  while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return 0; }
static void gi_mouse_start(void) {
  static volatile LONG started; if (InterlockedCompareExchange(&started, 1, 0)) return;
  gi_devinfo_init(); g_gi_mouse = gi_alloc(g_gd_vt, 1); if (g_gi_mouse) g_gi_mouse->ref = 1 << 20;   /* never freed */
  { HANDLE t = CreateThread(NULL, 0, gi_raw_thread, NULL, 0, NULL); if (t) CloseHandle(t); } }
static int gi_has_mouse(void) { return g_gi_mouse != NULL; }

/* device-arrival delivery (async enumeration: after RegisterDeviceCallback returned, from another thread) */
typedef struct { UINT64 tok; void *ctx; void *cb; } GiArrive;
static DWORD WINAPI gi_arrive_thread(void *p) {
  GiArrive a = *(GiArrive *)p; free(p); Sleep(1);
  ((GiDeviceCb)a.cb)(a.tok, a.ctx, g_gi_mouse, gi_now(), 1 /* Connected */, 0);
  slog("[gameinput] device callback %llu: mouse connected", (unsigned long long)a.tok); return 0; }

/* ---- IGameInput (v2) ---- */
static UINT64 STDMETHODCALLTYPE gi_ts(void *t) { (void)t; return gi_now(); }
/* 4 GetCurrentReading(kind, device, IGameInputReading **) */
static HRESULT STDMETHODCALLTYPE gi_m4(void *t, UINT32 kind, void *dev, void **out) { GiObj *r; (void)t;
  if (!out) return E_POINTER; *out = NULL;
  if (!(kind & GI_KIND_MOUSE) || !gi_has_mouse() || (dev && dev != g_gi_mouse)) return GI_E_READING_NOT_FOUND;
  if (!(r = gi_reading())) return E_OUTOFMEMORY; *out = r; return S_OK; }
/* 5 GetNextReading(ref, kind, device, IGameInputReading **): the current state when it is newer than ref */
static HRESULT STDMETHODCALLTYPE gi_m5(void *t, void *ref, UINT32 kind, void *dev, void **out) { GiObj *r; UINT64 s; (void)t;
  if (!out) return E_POINTER; *out = NULL;
  if (!(kind & GI_KIND_MOUSE) || !gi_has_mouse() || (dev && dev != g_gi_mouse) || !ref) return GI_E_READING_NOT_FOUND;
  gi_lock(1); s = g_gi_seq; gi_lock(0); if (((GiObj *)ref)->kind == 2 && ((GiObj *)ref)->seq >= s) return GI_E_READING_NOT_FOUND;
  if (!(r = gi_reading())) return E_OUTOFMEMORY; *out = r; return S_OK; }
/* 6 GetPreviousReading: no history */
static HRESULT STDMETHODCALLTYPE gi_m6(void *t, void *ref, UINT32 kind, void *dev, void **out) { (void)t; (void)ref; (void)kind; (void)dev; if (out) *out = NULL; return GI_E_READING_NOT_FOUND; }
/* 7 RegisterReadingCallback(device, kind, context, callback, token *) */
static HRESULT STDMETHODCALLTYPE gi_m7(void *t, void *dev, UINT32 kind, void *ctx, void *cb, UINT64 *tok) { UINT64 k; (void)t; (void)dev;
  if (!cb) return E_INVALIDARG; k = gi_add_cb(1, kind, ctx, cb); if (tok) *tok = k;
  slog("[gameinput] RegisterReadingCallback(dev %p, kind 0x%x) -> token %llu", dev, kind, (unsigned long long)k); return S_OK; }
/* 8 RegisterDeviceCallback(device, kind, statusFilter, enumerationKind, context, callback, token *) */
static HRESULT STDMETHODCALLTYPE gi_m8(void *t, void *dev, UINT32 kind, UINT32 status, UINT32 en, void *ctx, void *cb, UINT64 *tok) {
  UINT64 k; (void)t; if (!cb) return E_INVALIDARG; k = gi_add_cb(2, kind, ctx, cb); if (tok) *tok = k;
  slog("[gameinput] RegisterDeviceCallback(dev %p, kind 0x%x, status 0x%x, enum %u) -> token %llu%s", dev, kind, status, en, (unsigned long long)k,
       (kind & GI_KIND_MOUSE) && gi_has_mouse() ? ", announcing the mouse" : "");
  if ((kind & GI_KIND_MOUSE) && gi_has_mouse() && (status & 1) && en != 0 && (!dev || dev == g_gi_mouse)) {
    if (en == 2) ((GiDeviceCb)cb)(k, ctx, g_gi_mouse, gi_now(), 1, 0);   /* blocking enumeration: before returning */
    else { GiArrive *a = (GiArrive *)malloc(sizeof *a); HANDLE th; if (a) { a->tok = k; a->ctx = ctx; a->cb = cb; th = CreateThread(NULL, 0, gi_arrive_thread, a, 0, NULL); if (th) CloseHandle(th); else free(a); } } }
  return S_OK; }
/* 9 RegisterSystemButtonCallback(device, filter, context, callback, token *) / 10 RegisterKeyboardLayoutCallback(device, context, callback, token *) */
static HRESULT STDMETHODCALLTYPE gi_m9(void *t, void *dev, UINT32 f, void *ctx, void *cb, UINT64 *tok) { (void)t; (void)dev; (void)f; if (tok) *tok = gi_add_cb(3, 0, ctx, cb); return S_OK; }
static HRESULT STDMETHODCALLTYPE gi_m10(void *t, void *dev, void *ctx, void *cb, UINT64 *tok) { (void)t; (void)dev; if (tok) *tok = gi_add_cb(4, 0, ctx, cb); return S_OK; }
/* 11 StopCallback(token) / 12 UnregisterCallback(token) -> bool */
static void gi_drop(UINT64 tok) { int i; gi_lock(1); for (i = 0; i < 32; i++) if (g_gi_cbs[i].live && g_gi_cbs[i].tok == tok) g_gi_cbs[i].live = 0; gi_lock(0); }
static void STDMETHODCALLTYPE gi_m11(void *t, UINT64 tok) { (void)t; gi_drop(tok); }
static BOOLEAN STDMETHODCALLTYPE gi_m12(void *t, UINT64 tok) { (void)t; gi_drop(tok); return 1; }
/* 13 CreateDispatcher(IGameInputDispatcher **) */
static HRESULT STDMETHODCALLTYPE gi_m13(void *t, void **out) { (void)t; if (out) *out = NULL; return E_NOTIMPL; }
/* 14 FindDeviceFromId(APP_LOCAL_DEVICE_ID *, IGameInputDevice **) / 15 FindDeviceFromPlatformString(LPCWSTR, IGameInputDevice **) */
static HRESULT STDMETHODCALLTYPE gi_m14(void *t, const BYTE *id, void **out) { (void)t; if (!out) return E_POINTER; *out = NULL;
  if (id && gi_has_mouse() && !memcmp(id, g_gi_devinfo + 26, 32)) { *out = g_gi_mouse; gi_ar(g_gi_mouse); return S_OK; } return GI_E_DEVICE_NOT_FOUND; }
static HRESULT STDMETHODCALLTYPE gi_m15(void *t, const WCHAR *s, void **out) { (void)t; (void)s; if (out) *out = NULL; return GI_E_DEVICE_NOT_FOUND; }
/* 16 SetFocusPolicy(policy) */
static void STDMETHODCALLTYPE gi_m16(void *t, UINT32 p) { (void)t; slog("[gameinput] SetFocusPolicy(0x%x)", p); }
static const void *const g_gi_vt[] = { gi_qi, gi_ar, gi_rl, gi_ts, gi_m4, gi_m5, gi_m6, gi_m7, gi_m8, gi_m9, gi_m10, gi_m11, gi_m12, gi_m13, gi_m14, gi_m15, gi_m16 };

static HRESULT gi_standin(void **pp, HRESULT real) {
  GiObj *o; if (!pp) return E_POINTER;
  if (!(o = gi_alloc(g_gi_vt, 0))) return E_OUTOFMEMORY;
  gi_mouse_start();
  *pp = o;
  slog("[gameinput] real GameInput failed 0x%08lX (no GameInput service under WXR) -> stand-in IGameInput v2 %p (%s)", (unsigned long)real, (void *)o, gi_has_mouse() ? "raw-input mouse" : "no devices");
  return S_OK; }
static HRESULT WINAPI WxrGameInputInitialize(REFIID riid, void **pp) {
  HRESULT hr = g_gi_init_real ? g_gi_init_real(riid, pp) : E_NOTIMPL;
  if (SUCCEEDED(hr)) return hr;
  { char g[48]; guid_str(riid, g); slog("[gameinput] GameInputInitialize(%s)", g); }
  return gi_standin(pp, hr); }
static HRESULT WINAPI WxrGameInputCreate(void **pp) {
  HRESULT hr = g_gi_create_real ? g_gi_create_real(pp) : E_NOTIMPL;
  return SUCCEEDED(hr) ? hr : gi_standin(pp, hr); }
/* called by WxrGetProcAddress for every successful lookup: wraps the two entry points of GameInput.dll / GameInputRedist.dll */
static FARPROC gi_wrap(HMODULE m, LPCSTR name, FARPROC r) {
  WCHAR p[MAX_PATH]; const WCHAR *b;
  if (!r || !name || (ULONG_PTR)name < 0x10000 || strncmp(name, "GameInput", 9)) return r;
  if (strcmp(name, "GameInputInitialize") && strcmp(name, "GameInputCreate")) return r;
  if (!GetModuleFileNameW(m, p, MAX_PATH) || !(b = wcsrchr(p, L'\\'))) return r; b++;
  if (lstrcmpiW(b, L"GameInputRedist.dll") && lstrcmpiW(b, L"GameInput.dll")) return r;
  if (!strcmp(name, "GameInputInitialize")) { g_gi_init_real = (PGIInit_)r; r = (FARPROC)WxrGameInputInitialize; }
  else { g_gi_create_real = (PGICreate_)r; r = (FARPROC)WxrGameInputCreate; }
  slog("[gameinput] %s of %ls wrapped (stand-in if the real one fails)", name, b);
  return r; }
