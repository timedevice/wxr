/* wxrinput.inc - mouse as pointer input on Win7, included by wxrsys.c.
   Win10's CoreWindow and XAML read only WM_POINTER* messages and GetPointerInfo (on Win10, mouse-in-pointer is always on for UWP processes);
   Win7 has neither. A WH_GETMESSAGE hook on the thread that enables mouse-in-pointer rewrites its mouse messages into WM_POINTER* before
   anyone sees them, and keeps the state GetPointerInfo & co. report: the mouse is pointer id 1, PT_MOUSE, primary.
   XAML also needs QuirkIsEnabled(0x20046) (wxrsys.c) to take its subclassed-CoreWindow input path. */
#define IN_POINTER_ID 1
/* POINTER_INFO.sourceDevice of the mouse: ninput's ProcessFrameHistory rejects a frame without one (0x80400003), and XAML then never sees
   a Tapped (nav menu items, pivot headers). Any non-NULL handle will do; it only identifies the device. */
#define IN_MOUSE_DEVICE ((HANDLE)(ULONG_PTR)0x55573701)
#define IN_PT_MOUSE 4
typedef struct InPointerInfo {            /* POINTER_INFO (Win8 SDK), 96 bytes on x64 */
  DWORD pointerType; UINT32 pointerId, frameId, pointerFlags; HANDLE sourceDevice; HWND hwndTarget;
  POINT ptPixelLocation, ptHimetricLocation, ptPixelLocationRaw, ptHimetricLocationRaw;
  DWORD dwTime; UINT32 historyCount; INT32 InputData; DWORD dwKeyStates; UINT64 PerformanceCount; DWORD ButtonChangeType;
} InPointerInfo;
static InPointerInfo g_in_pi; static CRITICAL_SECTION g_in_cs; static LONG g_in_cs_init; static volatile LONG g_in_log;
static void in_lock(int on) { if (!g_in_cs_init) { if (!InterlockedCompareExchange(&g_in_cs_init, 1, 0)) { InitializeCriticalSection(&g_in_cs); g_in_cs_init = 2; } while (g_in_cs_init != 2) Sleep(0); }
  if (on) EnterCriticalSection(&g_in_cs); else LeaveCriticalSection(&g_in_cs); }

/* pointer flags (POINTER_FLAG_*) and the WM_POINTER wParam high word (POINTER_MESSAGE_FLAG_*) */
#define PF_NEW 0x1
#define PF_INRANGE 0x2
#define PF_INCONTACT 0x4
#define PF_FIRSTBUTTON 0x10
#define PF_SECONDBUTTON 0x20
#define PF_THIRDBUTTON 0x40
#define PF_PRIMARY 0x2000
#define PF_CONFIDENCE 0x4000
#define PF_DOWN 0x10000
#define PF_UPDATE 0x20000
#define PF_UP 0x40000
#define PF_WHEEL 0x80000
#define WM_POINTERUPDATE_ 0x0245
#define WM_POINTERDOWN_ 0x0246
#define WM_POINTERUP_ 0x0247
#define WM_POINTERWHEEL_ 0x024E

/* rewrite one mouse message (in place) into its WM_POINTER equivalent and update the pointer state */
static void in_translate(MSG *m)
{
  UINT msg = m->message, newmsg, change = 0, ev; POINT pt; LARGE_INTEGER qpc; UINT32 buttons, flags; WORD hi;
  switch (msg) {
  case WM_MOUSEMOVE: newmsg = WM_POINTERUPDATE_; ev = PF_UPDATE; break;
  case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: newmsg = WM_POINTERDOWN_; ev = PF_DOWN; change = 1; break;
  case WM_LBUTTONUP: newmsg = WM_POINTERUP_; ev = PF_UP; change = 2; break;
  case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: newmsg = WM_POINTERDOWN_; ev = PF_DOWN; change = 3; break;
  case WM_RBUTTONUP: newmsg = WM_POINTERUP_; ev = PF_UP; change = 4; break;
  case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: newmsg = WM_POINTERDOWN_; ev = PF_DOWN; change = 5; break;
  case WM_MBUTTONUP: newmsg = WM_POINTERUP_; ev = PF_UP; change = 6; break;
  case WM_MOUSEWHEEL: newmsg = WM_POINTERWHEEL_; ev = PF_WHEEL; break;
  default: return;
  }
  /* mouse lParam is client coordinates except for the wheel; WM_POINTER lParam is screen coordinates */
  pt.x = (short)LOWORD(m->lParam); pt.y = (short)HIWORD(m->lParam); if (msg != WM_MOUSEWHEEL) ClientToScreen(m->hwnd, &pt);
  buttons = 0; if (GET_KEYSTATE_WPARAM(m->wParam) & MK_LBUTTON) buttons |= PF_FIRSTBUTTON; if (GET_KEYSTATE_WPARAM(m->wParam) & MK_RBUTTON) buttons |= PF_SECONDBUTTON;
  if (GET_KEYSTATE_WPARAM(m->wParam) & MK_MBUTTON) buttons |= PF_THIRDBUTTON;
  if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) buttons |= PF_FIRSTBUTTON; else if (msg == WM_LBUTTONUP) buttons &= ~PF_FIRSTBUTTON;   /* posted messages may lack MK_ bits */
  if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) buttons |= PF_SECONDBUTTON; else if (msg == WM_RBUTTONUP) buttons &= ~PF_SECONDBUTTON;
  if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) buttons |= PF_THIRDBUTTON; else if (msg == WM_MBUTTONUP) buttons &= ~PF_THIRDBUTTON;
  flags = PF_INRANGE | PF_PRIMARY | PF_CONFIDENCE | buttons | ev | (buttons ? PF_INCONTACT : 0) | (newmsg == WM_POINTERDOWN_ ? PF_NEW : 0);
  QueryPerformanceCounter(&qpc);
  in_lock(1);
  g_in_pi.pointerType = IN_PT_MOUSE; g_in_pi.pointerId = IN_POINTER_ID; g_in_pi.frameId++; g_in_pi.pointerFlags = flags; g_in_pi.sourceDevice = IN_MOUSE_DEVICE; g_in_pi.hwndTarget = m->hwnd;
  /* "himetric" = pixels on purpose: Windows.UI's PointerPoint maps the himetric location through GetPointerDeviceRects (device rect ->
     display rect), and both rects are the virtual screen in pixels. Real himetric made Position ~26x too large (slider seek jumped to the
     end, scrollbar thumbs dragged far too fast). */
  g_in_pi.ptPixelLocation = pt; g_in_pi.ptPixelLocationRaw = pt; g_in_pi.ptHimetricLocation = pt;
  g_in_pi.ptHimetricLocationRaw = g_in_pi.ptHimetricLocation; g_in_pi.dwTime = m->time; g_in_pi.historyCount = 1;
  g_in_pi.InputData = msg == WM_MOUSEWHEEL ? GET_WHEEL_DELTA_WPARAM(m->wParam) : 0;
  g_in_pi.dwKeyStates = ((GET_KEYSTATE_WPARAM(m->wParam) & MK_SHIFT) ? 0x4 : 0) | ((GET_KEYSTATE_WPARAM(m->wParam) & MK_CONTROL) ? 0x8 : 0);
  g_in_pi.PerformanceCount = (UINT64)qpc.QuadPart; g_in_pi.ButtonChangeType = change;
  in_lock(0);
  /* wParam: low word pointer id, high word POINTER_MESSAGE_FLAG_* (= the low 16 pointer-flag bits); the wheel carries its delta there instead */
  hi = msg == WM_MOUSEWHEEL ? (WORD)GET_WHEEL_DELTA_WPARAM(m->wParam) : (WORD)(flags & 0xFFFF);
  if ((msg != WM_MOUSEMOVE || g_in_log < 3) && InterlockedIncrement(&g_in_log) <= 30) slog("[input] mouse 0x%x -> pointer 0x%x at (%ld,%ld) flags 0x%x hwnd %p", msg, newmsg, pt.x, pt.y, flags, (void *)m->hwnd);
  /* the edited MSG is not what gets dispatched here (XAML's subclass still saw the mouse messages), so the pointer message is sent to the
     window directly: XAML's CoreWindowSubclassProc and CoreWindow's WindowServer both handle WM_POINTER* in the window procedure */
  m->message = newmsg; m->wParam = MAKEWPARAM(IN_POINTER_ID, hi); m->lParam = MAKELPARAM((short)pt.x, (short)pt.y);
}
/* per-thread state through TlsAlloc: __declspec(thread) variables turned out to be SHARED between threads in wxrsys (the UI thread saw another
   thread's hook and never installed its own) */
typedef struct { int sending, selective; HHOOK hook; } InTls_;
static DWORD g_in_tls = TLS_OUT_OF_INDEXES;
static InTls_ *in_tls(void) { InTls_ *p; if (g_in_tls == TLS_OUT_OF_INDEXES) { DWORD i = TlsAlloc(); if (InterlockedCompareExchange((LONG *)&g_in_tls, (LONG)i, (LONG)TLS_OUT_OF_INDEXES) != (LONG)TLS_OUT_OF_INDEXES) TlsFree(i); }
  p = (InTls_ *)TlsGetValue(g_in_tls); if (!p) { p = (InTls_ *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p); TlsSetValue(g_in_tls, p); } return p; }
#define t_in_sending (in_tls()->sending)
/* WinUI 3 (Microsoft.UI.Input): pointer input reaches the hidden InputSiteWindowClass child as WM_POINTER*, routed on Win10 by DWM through
   composition input sinks (ours are inert). Only the wheel is handled as a legacy mouse message there. Threads that create an input-site window
   get a SELECTIVE rewrite: mouse messages for a Microsoft.UI.Content.DesktopChildSiteBridge window are delivered as WM_POINTER* to its
   InputSiteWindowClass child, and those already aimed at an input site (capture) likewise; the title-bar windows (non-client pointer source,
   caption controls) keep their legacy mouse messages (dragging works that way). */
#define t_in_selective (in_tls()->selective)
static HWND in_site_target(HWND h) { WCHAR c[64];
  if (!h || !GetClassNameW(h, c, 64)) return NULL;
  if (!lstrcmpW(c, L"InputSiteWindowClass")) return h;
  if (!lstrcmpW(c, L"Microsoft.UI.Content.DesktopChildSiteBridge")) return FindWindowExW(h, NULL, L"InputSiteWindowClass", NULL);
  return NULL; }
static LRESULT CALLBACK in_getmsg_hook(int code, WPARAM wp, LPARAM lp)
{
  GUITHREADINFO gti; gti.cbSize = sizeof gti;
  if (code == HC_ACTION && wp == PM_REMOVE && lp && GetGUIThreadInfo(GetCurrentThreadId(), &gti) && (gti.flags & (GUI_INMOVESIZE | GUI_INMENUMODE | GUI_SYSTEMMENUMODE))) {
    static volatile LONG ns; MSG *mm = (MSG *)lp;
    if (mm->message >= WM_MOUSEFIRST && mm->message <= WM_MOUSELAST && InterlockedIncrement(&ns) <= 10) slog("[input] mouse 0x%x not translated: GUI thread flags 0x%lx (capture %p)", mm->message, gti.flags, (void *)gti.hwndCapture); }
  gti.cbSize = sizeof gti;
  /* Windows' own move/size loop pumps the mouse messages through this hook too; it must see them, or a drag freezes and cancels */
  if (code == HC_ACTION && wp == PM_REMOVE && lp && !t_in_sending && !(GetGUIThreadInfo(GetCurrentThreadId(), &gti) && (gti.flags & (GUI_INMOVESIZE | GUI_INMENUMODE | GUI_SYSTEMMENUMODE)))) {
    MSG *m = (MSG *)lp, p = *m; HWND site = NULL;
    /* WinUI 3 threads (selective): Microsoft.UI.Input emulates mouse-in-pointer ITSELF from legacy mouse messages at the input site
       (PointerInputObserverWinRT [+0x300], switched off by the first WM_POINTER with id 1) - so no translation there: mouse messages for the
       content window (DesktopChildSiteBridge) are forwarded unchanged to its input site, as Win10's input routing delivers them. */
    if (t_in_selective) {
      if (m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST && m->message != WM_MOUSEWHEEL && (site = in_site_target(m->hwnd)) && site != m->hwnd) {
        WNDPROC sp = (WNDPROC)GetWindowLongPtrW(site, GWLP_WNDPROC); POINT pt; static volatile LONG nf;
        pt.x = (short)LOWORD(m->lParam); pt.y = (short)HIWORD(m->lParam); MapWindowPoints(m->hwnd, site, &pt, 1);
        if (m->message != WM_MOUSEMOVE && InterlockedIncrement(&nf) <= 6) slog("[input] WinUI: mouse 0x%x forwarded %p -> input site %p at (%ld,%ld)", m->message, (void *)m->hwnd, (void *)site, pt.x, pt.y);
        if (sp) { t_in_sending = 1; CallWindowProcW(sp, site, m->message, m->wParam, MAKELPARAM((short)pt.x, (short)pt.y)); t_in_sending = 0; m->message = WM_NULL; } }
      return CallNextHookEx(NULL, code, wp, lp); }
    in_translate(&p);
    if (p.message != m->message && IsWindow(p.hwnd)) { LRESULT r; static volatile LONG n;
      /* call the window procedure chain directly: Win7's SendMessage goes through win32k, which does not deliver the Win8 message numbers intact */
      WNDPROC wproc = (WNDPROC)GetWindowLongPtrW(p.hwnd, GWLP_WNDPROC);
      t_in_sending = 1; r = wproc ? CallWindowProcW(wproc, p.hwnd, p.message, p.wParam, p.lParam) : 0; t_in_sending = 0; m->message = WM_NULL;
      if (p.message != WM_POINTERUPDATE_ && InterlockedIncrement(&n) <= 6) { void *wp = (void *)GetWindowLongPtrW(p.hwnd, GWLP_WNDPROC); HMODULE hm = NULL; WCHAR mn[MAX_PATH] = L"?";
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)wp, &hm); if (hm) GetModuleFileNameW(hm, mn, MAX_PATH);
        slog("[input] sent 0x%x -> %Id; window proc %p in %ls", p.message, r, wp, mn); } }
  }
  return CallNextHookEx(NULL, code, wp, lp);
}
/* install the message rewrite once per thread (the thread that owns the CoreWindow calls the mouse-in-pointer enablers) */
#define t_in_hook (in_tls()->hook)
static void in_install(const char *who)
{
  if (who && !strcmp(who, "InputSite")) { t_in_selective = 1; if (t_in_hook) { slog("[input] thread %lu: WinUI input site - selective mode (forward to input site, no WM_POINTER)", GetCurrentThreadId()); return; } }
  /* a full enabler (XAML's EnableMouseInPointer) on a WinUI input-site thread does not switch translation on: WinUI emulates it itself */
  if (t_in_hook) return;
  t_in_hook = SetWindowsHookExW(WH_GETMESSAGE, in_getmsg_hook, NULL, GetCurrentThreadId());
  slog("[input] %s: mouse -> WM_POINTER rewrite on thread %lu -> %p", who, GetCurrentThreadId(), (void *)t_in_hook);
}

/* ---- user32 pointer APIs (Win8) ---- */
static BOOL in_info(UINT32 id, InPointerInfo *out) { if (id != IN_POINTER_ID || !out) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } in_lock(1); *out = g_in_pi; in_lock(0); return TRUE; }
static void in_logcall(const char *fn, UINT32 id, void *ret) { static volatile LONG n; if (InterlockedIncrement(&n) <= 20) { HMODULE hm = NULL; WCHAR mn[MAX_PATH] = L"?";
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ret, &hm); if (hm) GetModuleFileNameW(hm, mn, MAX_PATH);
  slog("[input] %s(%u) from %ls+0x%Ix", fn, id, wcsrchr(mn, L'\\') ? wcsrchr(mn, L'\\') + 1 : mn, (ULONG_PTR)ret - (ULONG_PTR)hm); } }
__declspec(dllexport) BOOL WINAPI GetPointerInfo(UINT32 id, InPointerInfo *out) { in_logcall("GetPointerInfo", id, __builtin_return_address(0)); return in_info(id, out); }
__declspec(dllexport) BOOL WINAPI GetPointerType(UINT32 id, DWORD *type) { if (id != IN_POINTER_ID || !type) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *type = IN_PT_MOUSE; return TRUE; }
__declspec(dllexport) BOOL WINAPI GetPointerCursorId(UINT32 id, UINT32 *cursor) { if (id != IN_POINTER_ID || !cursor) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *cursor = 0; return TRUE; }
__declspec(dllexport) BOOL WINAPI GetPointerFrameInfo(UINT32 id, UINT32 *count, InPointerInfo *out)
{ in_logcall("GetPointerFrameInfo", id, __builtin_return_address(0)); if (!count) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } if (!out) { *count = 1; return TRUE; } if (*count < 1) { *count = 1; SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; } *count = 1; return in_info(id, out); }
__declspec(dllexport) BOOL WINAPI GetPointerInfoHistory(UINT32 id, UINT32 *count, InPointerInfo *out) { return GetPointerFrameInfo(id, count, out); }
__declspec(dllexport) BOOL WINAPI GetPointerFrameInfoHistory(UINT32 id, UINT32 *entries, UINT32 *pointers, InPointerInfo *out)
{ if (!entries || !pointers) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } if (!out) { *entries = 1; *pointers = 1; return TRUE; } *entries = 1; *pointers = 1; return in_info(id, out); }
__declspec(dllexport) BOOL WINAPI SkipPointerFrameMessages(UINT32 id) { (void)id; return TRUE; }
__declspec(dllexport) BOOL WINAPI EnableMouseInPointer(BOOL on) { if (on) in_install("EnableMouseInPointer"); return TRUE; }
__declspec(dllexport) BOOL WINAPI IsMouseInPointerEnabled(void) { return TRUE; }
/* the mouse is not a pointer *device* on Windows either: GetPointerDevice fails, there are no devices, transforms are identity (wine-uwp) */
__declspec(dllexport) BOOL WINAPI GetPointerDevice(HANDLE dev, void *out)
{ static volatile LONG n; (void)out; if (InterlockedIncrement(&n) <= 6) { char t[80]; wsprintfA(t, "[input] GetPointerDevice(%p) -> fails", dev); log_callers(t); }
  SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
__declspec(dllexport) BOOL WINAPI GetPointerDevices(UINT32 *count, void *devs) { (void)devs; if (!count) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *count = 0; return TRUE; }
/* Windows.UI's CPointerPointStatics::GetPointsHelper (PointerPoint.GetCurrentPoint) needs the mouse's rects (a failure becomes E_INVALIDARG
   in every Button's OnPointerMoved): the device and display rectangles are both the virtual screen */
__declspec(dllexport) BOOL WINAPI GetPointerDeviceRects(HANDLE dev, RECT *dr, RECT *sr)
{ RECT v; (void)dev; v.left = GetSystemMetrics(SM_XVIRTUALSCREEN); v.top = GetSystemMetrics(SM_YVIRTUALSCREEN); v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN); v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (!dr || !sr) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *dr = v; *sr = v; return TRUE; }
__declspec(dllexport) BOOL WINAPI GetPointerInputTransform(UINT32 id, UINT32 n, float *xf)
{ UINT32 i; if (!xf || !n) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }   /* INPUT_TRANSFORM: a 4x4 float matrix each */
  for (i = 0; i < n; i++) { float *m = xf + i * 16; memset(m, 0, 16 * sizeof(float)); m[0] = m[5] = m[10] = m[15] = 1.0f; } (void)id; return TRUE; }
