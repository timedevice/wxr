/* wxrpump.inc - the UI thread pump: CoreDispatcher / CoreMessaging bridge. Included by wxrsys.c. */
/* ================= UI thread pump: CoreDispatcher / CoreMessaging bridge =================
   Windows.UI.dll drives its CoreDispatcher through combase's private pump family and CoreMessaging through private user32 calls
   that tie the thread's message queue to an I/O completion port. Win7 has neither; this is a polling emulation:
   CoMsgWaitInProcessEvents = wait on the handles (IoCompletion handles are polled by our WaitForMultipleObjectsEx) + GetQueueStatus(mask). */
static volatile DWORD g_view_tid;   /* the thread that owns the CoreDispatcher / DXamlCore (first to install a message dispatcher) */
__declspec(dllexport) HRESULT WINAPI CoSetMessageDispatcher(void *a, void *b) { (void)a; (void)b; if (!g_view_tid) g_view_tid = GetCurrentThreadId(); slog("[pump] CoSetMessageDispatcher (view tid %lu)", g_view_tid); return S_OK; }
__declspec(dllexport) HRESULT WINAPI CoBeginProcessEvents(void *ctx) { if (ctx) memset(ctx, 0, 16); return S_OK; }
__declspec(dllexport) HRESULT WINAPI CoEndProcessEvents(void *ctx) { (void)ctx; return S_OK; }
__declspec(dllexport) HRESULT WINAPI CoHandlePriorityEventsFromMessagePump(void) { return S_OK; }
static int cm_poll(void);
/* Win7's GetQueueStatus resets the "new since last call" bits (HIWORD) of whatever it reports. Win10's *Readonly variants do not,
   and CoMsgWait (without MWMO_INPUTAVAILABLE) wakes only on those bits: a read-only peek that consumed them made the pump sleep with
   the message still queued until some other input arrived (lost wake-ups: startup stalls, a skeleton page whose "data ready" work
   item never ran). Every GetQueueStatus done on the caller's behalf records the new bits here; the wait consumes them. */
static __declspec(thread) HANDLE t_cmiocp; static __declspec(thread) HWND t_cmhwnd; static __declspec(thread) void *t_cmpend;   /* CoreMessaging user integration, see cm_poll */
/* A call for the view thread, made from its dispatcher wait (where Win10 delivers the activation call of an ASTA). An APC is not
   a substitute: it runs inside ANY alertable wait, and .NET's waits are all alertable - Activate then re-entered the Store in the
   middle of its App constructor (a blocking managed wait) and deadlocked in twinapi.appcore (startup stall). */
typedef void (*ViewCall)(ULONG_PTR);
static void *volatile g_view_call; static volatile ULONG_PTR g_view_call_arg;
static void view_call_poll(void) {
  if (g_view_call && GetCurrentThreadId() == g_view_tid) { ViewCall f = (ViewCall)InterlockedExchangePointer((void *volatile *)&g_view_call, NULL); if (f) f(g_view_call_arg); } }
/* the queue state for CoreMessaging: GetQueueStatus is what is in the queue now (HIWORD); a peek cannot make the wait in
   CoMsgWaitInProcessEvents miss anything */
static DWORD qs_peek(DWORD mask) { return GetQueueStatus(mask); }
__declspec(dllexport) DWORD WINAPI CoMsgWaitInProcessEvents(void *ctx, DWORD n, const HANDLE *h, DWORD ms, DWORD mask, DWORD flags) {
  DWORD t0 = GetTickCount(), r; (void)ctx; { static volatile LONG nlog; if (InterlockedIncrement(&nlog) <= 6) slog("[pump] CoMsgWait args: tid %lu n=%lu ms=%lu mask=%lx flags=%lx", GetCurrentThreadId(), n, ms, mask, flags); } mask &= QS_ALLINPUT; if (!mask) mask = QS_ALLINPUT;   /* Win8 added QS_TOUCH/QS_POINTER; Win7 rejects unknown bits (returns 0) */
  for (;;) {
    DWORD el, slice;
    view_call_poll();
    cm_poll();   /* CoreMessaging port packets -> 0x60/1 posted to its adapter window, seen below as input */
    { DWORD qs = qs_peek(mask); static volatile LONG nret; if (((flags & 4) ? (DWORD)(qs | (qs >> 16)) : (DWORD)HIWORD(qs)) & mask) { if (InterlockedIncrement(&nret) <= 30) slog("[pump] CoMsgWait tid %lu: input qs=%08lx -> return %lu", GetCurrentThreadId(), qs, n); return WAIT_OBJECT_0 + n; } }
    el = GetTickCount() - t0; if (ms != INFINITE && el >= ms) return WAIT_TIMEOUT;
    slice = 2; if (ms != INFINITE && ms - el < slice) slice = ms - el;
    if (n) { r = WaitForMultipleObjectsEx(n, h, FALSE, slice, TRUE); if (r != WAIT_TIMEOUT) return r; } else SleepEx(slice, TRUE);
  }
}
__declspec(dllexport) DWORD WINAPI NtUserGetQueueStatusReadonly(DWORD mask) { return qs_peek(mask & QS_ALLINPUT); }
__declspec(dllexport) DWORD WINAPI GetQueueStatusForCoreMessaging(DWORD mask) { return qs_peek(mask & QS_ALLINPUT); }
__declspec(dllexport) DWORD WINAPI GetQueueStatusReadonlyForCoreMessaging(DWORD mask) { return qs_peek(mask & QS_ALLINPUT); }
/* CoreMessaging in user-integrated mode never waits on its port: it associates its timers' and waits' completion packets
   with the port InitThreadCoreMessagingIocp returns and relies on win32k to wake the thread's message wait. Here: CoMsgWait polls the port; a real packet (KeyContext = CoreMessaging's wait record,
   ApcContext bit 31 = the queue's own notification) is kept, 0x60/1 is posted to the adapter window (what
   UserAdapter::AsyncPostMessage posts to run DoWork) and input is reported. Drain hands the records to CoreMessaging
   on the list at TEB+0x9e8 -> +8 (next at +0). */

static int cm_unlink_pending(HANDLE iocp, void *key) {   /* a completion cm_poll already took from this thread's port */
  void **pp;
  if (!t_cmiocp || t_cmiocp != iocp) return 0;
  for (pp = &t_cmpend; *pp; pp = (void **)*pp) if (*pp == key) { *pp = *(void **)key; return 1; }
  return 0; }
static int cm_poll(void) {
  DWORD n; ULONG_PTR key; OVERLAPPED *ov; int got = 0;
  if (!t_cmiocp) return 0;
  while (GetQueuedCompletionStatus(t_cmiocp, &n, &key, &ov, 0) || ov) {
    if ((ULONG_PTR)ov & 0x80000000u || !key) {   /* the queue's own notification */
      static volatile LONG nn; LONG c = InterlockedIncrement(&nn);
      if (c <= 12 || !(c % 500)) slog("[pump] CoreMessaging notification packet #%ld: n=%lu key=%p ov=%p", c, n, (void *)key, (void *)ov);
      continue; }
    { void *p; int dup = 0;   /* a record completed again before the drain: linking it twice would point it at itself and drop the rest */
      for (p = t_cmpend; p; p = *(void **)p) if (p == (void *)key) { dup = 1; break; }
      if (dup) { static volatile LONG nd; if (InterlockedIncrement(&nd) <= 20) slog("[pump] CoreMessaging record %p completed twice before drain (kept once)", (void *)key); got = 1; continue; } }

    *(void **)key = t_cmpend; t_cmpend = (void *)key; got = 1; }
  if (got && t_cmhwnd) PostMessageW(t_cmhwnd, 0x60, 1, 0);
  return got; }
__declspec(dllexport) HANDLE WINAPI InitThreadCoreMessagingIocp(HWND hwnd) {
  if (!t_cmiocp) t_cmiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  { LONG i, n = g_ncmports; for (i = 0; i < n && i < 16; i++) if (g_cmports[i].iocp == t_cmiocp) break;
    if (i < 16) { g_cmports[i].hwnd = hwnd; g_cmports[i].iocp = t_cmiocp; if (i == n) InterlockedIncrement(&g_ncmports); } }
  t_cmhwnd = hwnd; slog("[pump] InitThreadCoreMessagingIocp(%p) -> %p", (void *)hwnd, (void *)t_cmiocp); return t_cmiocp;
}
__declspec(dllexport) DWORD WINAPI DrainThreadCoreMessagingCompletions(void) {
  void **shared = *(void ***)((BYTE *)NtCurrentTeb() + 0x9e8), *r;
  cm_poll();
  if (!shared) { if (t_cmpend) { static volatile LONG nd; if (InterlockedIncrement(&nd) <= 40) slog("[pump] Drain: no shared list at TEB+0x9e8, %s", "pending records KEPT for the next drain"); } return 1; }   /* was: dropped (t_cmpend = NULL) */
  while ((r = t_cmpend)) { t_cmpend = *(void **)r; *(void **)r = shared[1]; shared[1] = r; }
  return 1;   /* 0 is CoreMessaging's E_FAIL fail-fast (DrainIoCompletionPort) */
}
/* NtUserScheduleDispatchNotification answers 2 = "arm the dispatch timer" */
/* An input-priority dispatch request. "2" alone makes CoreMessaging SetTimer(hwnd, 1, 0): the WM_TIMER runs DoWork at timer
   priority, BELOW the input-priority item, which stays pending until some other DoWork happens to take it (and SetTimer from a
   thread that does not own the window fails outright). That was the Store's skeleton front page: "data ready" work items queued
   to the UI thread sat there. The message that runs DoWork at input priority is 0x60 with wParam 0: post it,
   still answer 2. */
__declspec(dllexport) DWORD WINAPI ScheduleDispatchNotification(void *a, DWORD b) {
  static volatile LONG nl; LONG c = InterlockedIncrement(&nl);
  if (c <= 5 || !(c % 1000)) slog("[pump] ScheduleDispatchNotification(%p, %lu) #%ld from tid %lu (view %lu) -> 2 + 0x60/0", a, b, c, GetCurrentThreadId(), g_view_tid);
  if (a) PostMessageW((HWND)a, 0x60, 0, 0);
  return 2; }
__declspec(dllexport) DWORD WINAPI HasDeferredInputForCoreDispatcher(void) { return 0; }
__declspec(dllexport) HRESULT WINAPI RoCreateNonAgilePropertySet(void **out) {
  void *hs = NULL; WCHAR *name = L"Windows.Foundation.Collections.PropertySet"; typedef HRESULT (WINAPI *PCr)(const WCHAR *, UINT32, void **); PCr cr;
  HRESULT hr; if (!out) return E_POINTER; *out = NULL;
  cr = (PCr)GetProcAddress(LoadLibraryA("combase.dll"), "WindowsCreateString"); if (!cr) cr = (PCr)GetProcAddress(LoadLibraryA("kxcom.dll"), "WindowsCreateString");
  if (!cr) return E_NOTIMPL; hr = cr(name, (UINT32)wcslen(name), &hs); if (FAILED(hr)) return hr;
  hr = RoActivateInstance(hs, out); slog("[impl] RoCreateNonAgilePropertySet -> 0x%08lX", (unsigned long)hr); return hr;
}
