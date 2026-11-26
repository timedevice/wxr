/* wxrwait.inc - WaitForMultipleObjectsEx on I/O completion ports. Included by wxrsys.c.
   Win10 lets a thread wait on an I/O completion port handle (signaled while packets are queued; CoreMessaging waits on its port);
   Win7's IoCompletion object is not waitable and WaitFor* returns WAIT_OBJECT_0 at once. Emulated: the other handles are waited on in
   1 ms slices and the port's depth is polled. */
typedef NTSTATUS (NTAPI *PFN_QObj)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static void obj_type_name(HANDLE h, char *out, size_t n) {
  static PFN_QObj q; unsigned char buf[256]; ULONG len = 0; NTSTATUS st;
  if (!q) q = (PFN_QObj)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
  out[0] = 0; if (!q) { strncpy(out, "?", n); return; }
  st = q(h, 2, buf, sizeof buf, &len);   /* ObjectTypeInformation: UNICODE_STRING TypeName first */
  if (st) { _snprintf(out, n, "qobj=0x%08lX", (unsigned long)st); return; }
  { USHORT l = *(USHORT *)buf / 2; WCHAR *w = *(WCHAR **)(buf + 8); size_t i; for (i = 0; i < l && i + 1 < n; i++) out[i] = (char)w[i]; out[i] = 0; }
}
static int depth_of_iocp(HANDLE h) {
  static PFN_QObj q; ULONG info[1] = {0}, len = 0; NTSTATUS st;   /* IO_COMPLETION_BASIC_INFORMATION is a single LONG Depth */
  if (!q) q = (PFN_QObj)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryIoCompletion");
  st = q ? q(h, 0 /*IoCompletionBasicInformation*/, info, sizeof info, &len) : (NTSTATUS)0xC0000002;
  return st == 0 && info[0] > 0;
}
__declspec(dllexport) DWORD WINAPI WaitForMultipleObjectsEx(DWORD n, const HANDLE *h, BOOL all, DWORD ms, BOOL alert) {
  typedef DWORD (WINAPI *F)(DWORD, const HANDLE *, BOOL, DWORD, BOOL); static F f; static LONG logged;
  HANDLE oh[64]; DWORD map[64], no = 0, ni = 0, i, r, t0; char isio[64];
  if (!f) f = (F)GetProcAddress(GetModuleHandleA("kernel32.dll"), "WaitForMultipleObjectsEx");
  if (all || !n || n > 64) return f(n, h, all, ms, alert);
  for (i = 0; i < n; i++) {
    char tn[32]; obj_type_name(h[i], tn, sizeof tn); isio[i] = !strcmp(tn, "IoCompletion");
    if (isio[i]) ni++; else { oh[no] = h[i]; map[no++] = i; }
  }
  if (!ni) return f(n, h, all, ms, alert);
  if (InterlockedIncrement(&logged) <= 5) slog("[wait] WFMO emulating IoCompletion wait: n=%lu iocp=%lu ms=%lu", n, ni, ms);
  t0 = GetTickCount();
  for (;;) {
    for (i = 0; i < n; i++) if (isio[i] && depth_of_iocp(h[i])) return i;
    if (no) {
      r = f(no, oh, FALSE, 1, alert);
      if (r < no) return map[r];
      if (r >= WAIT_ABANDONED_0 && r < WAIT_ABANDONED_0 + no) return WAIT_ABANDONED_0 + map[r - WAIT_ABANDONED_0];
      if (r != WAIT_TIMEOUT) return r;
    } else if (SleepEx(1, alert) == WAIT_IO_COMPLETION) return WAIT_IO_COMPLETION;
    if (ms != INFINITE && GetTickCount() - t0 >= ms) return WAIT_TIMEOUT;
  }
}
