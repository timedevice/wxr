/* wxrntdll.inc - ntdll: wait completion packets (user-mode, after wine-uwp) and small Win8+ functions. Included by wxrsys.c. */
/* ---------------- ntdll: wait completion packets [impl] (user-mode, after wine-uwp's sync.c) ---------------- */
typedef struct WCP { HANDLE ev, wait, iocp, timer; void *key, *apc; NTSTATUS st; ULONG_PTR info; volatile LONG pending, fired; struct WCP *next; } WCP;
static int cm_unlink_pending(HANDLE iocp, void *key);

/* Ports that InitThreadCoreMessagingIocp tied to a thread's message queue, with CoreMessaging's adapter window. On Win10 win32k
   wakes that thread's message wait whenever the port receives a packet. Polling the port from CoMsgWaitInProcessEvents alone is
   not that: the view thread also waits and pumps elsewhere, and a completed timer/event wait then sat in the port until some
   unrelated message made the pump come round (0.7 s .. 6 s measured) - CoreMessaging's timers and cross-thread wakes stalled,
   the Store's front page stayed on its skeleton. So a completion posted to such a port also posts 0x60/1 to the adapter window
   (= UserAdapter::AsyncPostMessage, runs DoWork, which drains the port). */
static struct { HANDLE iocp; HWND hwnd; } g_cmports[16]; static volatile LONG g_ncmports;
static void cm_port_wake(HANDLE iocp) {
  LONG i, n = g_ncmports;
  for (i = 0; i < n && i < 16; i++) if (g_cmports[i].iocp == iocp) { if (g_cmports[i].hwnd) PostMessageW(g_cmports[i].hwnd, 0x60, 1, 0); return; } }

static WCP *g_wcp; static CRITICAL_SECTION g_wcs; static LONG g_winit;
typedef NTSTATUS (NTAPI *PFN_SetIo)(HANDLE, PVOID, PVOID, NTSTATUS, ULONG_PTR);
static PFN_SetIo set_io(void) {
  static PFN_SetIo f; if (!f) f = (PFN_SetIo)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetIoCompletion"); return f;
}
static void wcp_lock(int on) {
  if (!g_winit) { if (InterlockedCompareExchange(&g_winit, 1, 0) == 0) { InitializeCriticalSection(&g_wcs); g_winit = 2; } while (g_winit != 2) Sleep(0); }
  if (on) EnterCriticalSection(&g_wcs); else LeaveCriticalSection(&g_wcs);
}
static WCP *wcp_find(HANDLE h) { WCP *p; for (p = g_wcp; p; p = p->next) if (p->ev == h) return p; return NULL; }
static VOID CALLBACK wcp_cb(PVOID ctx, BOOLEAN to) {
  WCP *p = (WCP *)ctx; (void)to;
  if (InterlockedExchange(&p->pending, 0) && set_io()) { p->fired = 1; set_io()(p->iocp, p->key, p->apc, p->st, p->info); cm_port_wake(p->iocp); }
}
/* Takes this packet's completion back out of its port (NtCancelWaitCompletionPacket with RemoveSignaledPacket): from the UI
   thread's not-yet-drained list, else by emptying the port and re-posting everything else. 0 = a consumer already has it. */
static int wcp_revoke(WCP *p) {
  typedef NTSTATUS (NTAPI *PFN_RemIo)(HANDLE, PVOID *, PVOID *, void *, LARGE_INTEGER *); static PFN_RemIo rem;
  struct { PVOID key, apc; struct { ULONG_PTR Status; ULONG_PTR Information; } io; } e[128]; int n = 0, i, found = 0; LARGE_INTEGER zero; zero.QuadPart = 0;
  if (cm_unlink_pending(p->iocp, p->key)) return 1;
  if (!rem) rem = (PFN_RemIo)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtRemoveIoCompletion");
  if (!rem || !set_io()) return 0;
  while (n < 128 && rem(p->iocp, &e[n].key, &e[n].apc, &e[n].io, &zero) == STATUS_SUCCESS) {
    if (!found && e[n].key == p->key && e[n].apc == p->apc) { found = 1; continue; }
    n++; }
  for (i = 0; i < n; i++) set_io()(p->iocp, e[i].key, e[i].apc, (NTSTATUS)e[i].io.Status, e[i].io.Information);
  return found;
}
static VOID CALLBACK wcp_timer_cb(PVOID ctx, BOOLEAN to) {
  WCP *p = (WCP *)ctx; (void)to;
  if (InterlockedExchange(&p->pending, 0) && set_io()) set_io()(p->iocp, p->key, p->apc, p->st, p->info);
}
__declspec(dllexport) NTSTATUS NTAPI NtCreateWaitCompletionPacket(PHANDLE h, ACCESS_MASK a, PVOID attr) {
  WCP *p; (void)a; (void)attr;
  if (!h) return STATUS_INVALID_PARAMETER;
  p = (WCP *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p); if (!p) return STATUS_NO_MEMORY;
  p->ev = CreateEventW(NULL, TRUE, FALSE, NULL); if (!p->ev) { HeapFree(GetProcessHeap(), 0, p); return STATUS_NO_MEMORY; }
  wcp_lock(1); p->next = g_wcp; g_wcp = p; wcp_lock(0);
  *h = p->ev; return STATUS_SUCCESS;
}
__declspec(dllexport) NTSTATUS NTAPI NtAssociateWaitCompletionPacket(HANDLE h, HANDLE iocp, HANDLE target, PVOID key, PVOID apc,
    NTSTATUS st, ULONG_PTR info, BOOLEAN *already) {
  WCP *p; wcp_lock(1); p = wcp_find(h); wcp_lock(0);
  if (!p) return STATUS_INVALID_HANDLE;
  if (p->wait) {
    if (p->pending) return STATUS_INVALID_PARAMETER;
    UnregisterWaitEx(p->wait, NULL); p->wait = NULL;
  }
  p->iocp = iocp; p->key = key; p->apc = apc; p->st = st; p->info = info; p->fired = 0;
  { char tn[48]; static LONG nlog; int lg = InterlockedIncrement(&nlog) <= 50;   /* the ALPC re-arm below repeats every few ms: log the first ones only */
    obj_type_name(target, tn, sizeof tn); if (lg) slog("[wcp] Associate packet=%p iocp=%p target=%p type=%s key=%p apc=%p", h, iocp, target, tn, key, apc);
    if (!strcmp(tn, "ALPC Port")) {
      /* ALPC ports are waitable on Win10 only.  Win7 can post a completion packet per message to an associated IOCP instead. */
      typedef NTSTATUS (NTAPI *PFN_AlpcSet)(HANDLE, ULONG, PVOID, ULONG); static PFN_AlpcSet as;
      struct { PVOID key; HANDLE port; } ai; NTSTATUS s2;
      if (!as) as = (PFN_AlpcSet)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAlpcSetInformation");
      ai.key = key; ai.port = iocp; s2 = as ? as(target, 2 /*AlpcAssociateCompletionPortInformation*/, &ai, sizeof ai) : STATUS_NOT_IMPLEMENTED;
      if (lg) slog("[wcp]   ALPC port %p associated with iocp %p key %p -> 0x%08lX", target, iocp, key, (unsigned long)s2);
      if (already) *already = FALSE;
      /* The Win7 kernel's one-shot completion is not re-armed reliably, so also re-post after a few ms.  A spurious packet is harmless: the
         registrar's handler does a non-blocking receive (STATUS_UNSUCCESSFUL when empty) and re-associates. */
      if (s2 == 0 || s2 == (NTSTATUS)0xC0000048 /*STATUS_PORT_ALREADY_SET: association persists*/) {
        if (!InterlockedExchange(&p->pending, 1) || !p->timer) {
          if (p->timer) { DeleteTimerQueueTimer(NULL, p->timer, NULL); p->timer = NULL; }
          if (!CreateTimerQueueTimer(&p->timer, NULL, wcp_timer_cb, p, 3, 0, WT_EXECUTEINTIMERTHREAD)) { p->pending = 0; slog("[wcp] timer create failed %lu", GetLastError()); }
        }
        return STATUS_SUCCESS;
      }
    } }
  if (already) *already = FALSE;
  if (already && WaitForSingleObject(target, 0) == WAIT_OBJECT_0) {
    *already = TRUE; if (set_io()) set_io()(iocp, key, apc, st, info); cm_port_wake(iocp); return STATUS_SUCCESS;
  }
  p->pending = 1;
  if (!RegisterWaitForSingleObject(&p->wait, target, wcp_cb, p, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD)) { p->pending = 0; return STATUS_INVALID_HANDLE; }
  return STATUS_SUCCESS;
}
__declspec(dllexport) NTSTATUS NTAPI NtCancelWaitCompletionPacket(HANDLE h, BOOLEAN remove) {
  /* The kernel's contract, which CoreMessaging's WaitCompletionPacket::TryCancel relies on:
       STATUS_SUCCESS    the wait was cancelled before the target was signalled (the target keeps its state), or - with
                         RemoveSignaledPacket - the already queued completion was taken back out of the port
       STATUS_PENDING    the wait was satisfied and its completion is queued (TryCancel then calls again with remove = TRUE and
                         handles the signal itself)
       STATUS_CANCELLED  nothing is associated
     The wait thread is stopped before `pending` is read: a signal that arrives during a cancel must not be consumed (auto-reset
     event) without a completion - that lost a wake-up of the UI thread's dispatcher (the Store's front page stuck on its skeleton). */
  WCP *p; HANDLE w; static volatile LONG nc[4];
  wcp_lock(1); p = wcp_find(h); wcp_lock(0);
  if (!p) return STATUS_INVALID_HANDLE;
  if ((w = p->wait)) { p->wait = NULL; UnregisterWaitEx(w, INVALID_HANDLE_VALUE); }   /* blocks until a running callback is through: fired or not is now final */
  if (InterlockedExchange(&p->pending, 0)) { InterlockedIncrement(&nc[0]); return STATUS_SUCCESS; }
  if (!p->fired) return STATUS_CANCELLED;
  if (!remove) { LONG c = InterlockedIncrement(&nc[1]); if (c <= 5 || !(c % 200)) slog("[wcp] Cancel packet=%p: already signalled (#%ld; cancelled in time %ld, taken back %ld, gone %ld)", h, c, nc[0], nc[2], nc[3]); return STATUS_PENDING; }
  p->fired = 0;
  if (wcp_revoke(p)) { InterlockedIncrement(&nc[2]); return STATUS_SUCCESS; }
  InterlockedIncrement(&nc[3]); return STATUS_PENDING;   /* a consumer already dequeued it */
}

/* ---------------- ntdll: small [impl]s ---------------- */
__declspec(dllexport) NTSTATUS NTAPI RtlSetThreadWorkOnBehalfTicket(PVOID t) { (void)t; return STATUS_SUCCESS; }
__declspec(dllexport) NTSTATUS NTAPI RtlClearThreadWorkOnBehalfTicket(void) { return STATUS_SUCCESS; }
__declspec(dllexport) BOOLEAN NTAPI RtlIsMultiSessionSku(void) { return FALSE; }
__declspec(dllexport) void NTAPI RtlFlushHeaps(void) {}
__declspec(dllexport) void NTAPI TpTrimPools(void) {}
__declspec(dllexport) NTSTATUS NTAPI RtlQueryUnbiasedInterruptTime(PULONGLONG t) {
  typedef BOOL (WINAPI *F)(PULONGLONG); static F f; if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryUnbiasedInterruptTime");
  if (!t) return STATUS_INVALID_PARAMETER; if (f && f(t)) return STATUS_SUCCESS; *t = (ULONGLONG)GetTickCount64() * 10000; return STATUS_SUCCESS;
}
/* QueryInterruptTimePrecise (kernelbase, Win8.1+): a monotonic 100 ns interrupt-time counter. The GDK runtime (xgameruntime) probes
   for it; Win7 has no exact equivalent, so the unbiased interrupt time (else the tick count) is a close monotonic stand-in. */
__declspec(dllexport) void WINAPI QueryInterruptTimePrecise(PULONGLONG t) {
  typedef BOOL (WINAPI *F)(PULONGLONG); static F f; static int tried;
  if (!tried) { f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryUnbiasedInterruptTime"); tried = 1; }
  if (!t) return; if (f && f(t)) return; *t = (ULONGLONG)GetTickCount64() * 10000;
}
