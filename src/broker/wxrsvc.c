/* wxrsvc.exe - hosts the CoreMessagingRegistrar service (ServiceMain of the image's CoreMessaging.dll) in an ordinary process on Windows 7.
   usage: wxrsvc.exe <systemdir>      leave it running; stop with Ctrl+C or by creating the event "Local\wxrsvc.stop" (wxrsvc.exe <systemdir> stop). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include <dbghelp.h>
static FILE *lf;
#ifdef WXR_RETAIL   /* retail build: no Logs\wxrsvc.log, no console output */
static void L(const char *f, ...) { (void)f; }
#else
static void L(const char *f, ...) { char b[600]; va_list a; va_start(a, f); _vsnprintf(b, 599, f, a); va_end(a); b[599] = 0; printf("%s\n", b); fflush(stdout); if (lf) { fprintf(lf, "%s\n", b); fflush(lf); } }
#endif
#include <stdarg.h>
static LONG WINAPI vh(EXCEPTION_POINTERS *e) {
  DWORD c = e->ExceptionRecord->ExceptionCode;
  if (c >= 0xC0000000u || c == 0xE0464645) { HMODULE m = NULL; char nm[MAX_PATH] = "?"; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m); if (m) GetModuleFileNameA(m, nm, MAX_PATH);
    L("EXCEPTION 0x%08lX at %p (%s+0x%llx) info0=%llx info1=%llx", c, e->ExceptionRecord->ExceptionAddress, nm, (unsigned long long)((char *)e->ExceptionRecord->ExceptionAddress - (char *)m),
      e->ExceptionRecord->NumberParameters > 0 ? (unsigned long long)e->ExceptionRecord->ExceptionInformation[0] : 0ull, e->ExceptionRecord->NumberParameters > 1 ? (unsigned long long)e->ExceptionRecord->ExceptionInformation[1] : 0ull); }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void (WINAPI *g_smain)(DWORD, LPWSTR *); static LPWSTR *g_sv; static DWORD g_smain_tid;
static DWORD WINAPI smain_thread(LPVOID x) { (void)x; g_smain(1, g_sv); L("ServiceMain function returned"); return 0; }
static void dump_thread(DWORD tid) {
  HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); CONTEXT c; STACKFRAME64 sf; int i; if (!th) { L("  thread %lu: cannot open", tid); return; }
  if (tid != GetCurrentThreadId()) SuspendThread(th); memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(th, &c)) { L("  thread %lu: no context", tid); ResumeThread(th); CloseHandle(th); return; }
  memset(&sf, 0, sizeof sf); sf.AddrPC.Offset = c.Rip; sf.AddrPC.Mode = AddrModeFlat; sf.AddrFrame.Offset = c.Rbp; sf.AddrFrame.Mode = AddrModeFlat; sf.AddrStack.Offset = c.Rsp; sf.AddrStack.Mode = AddrModeFlat;
  L("  thread %lu:", tid);
  for (i = 0; i < 30; i++) { HMODULE m = NULL; char nm[MAX_PATH] = "?", *b;
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), th, &sf, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL) || !sf.AddrPC.Offset) break;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)sf.AddrPC.Offset, &m);
    if (m) { GetModuleFileNameA(m, nm, MAX_PATH); b = strrchr(nm, '\\'); if (b) memmove(nm, b + 1, strlen(b)); }
    L("      #%d %s+0x%llx", i, nm, (unsigned long long)(sf.AddrPC.Offset - (ULONG_PTR)m)); }
  { ULONG_PTR buf[400]; SIZE_T got = 0; int shown = 0, k; L("      raw stack scan (return-address candidates):");
    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)c.Rsp, buf, sizeof buf, &got)) for (k = 0; k < (int)(got / 8) && shown < 40; k++) { HMODULE m = NULL; char nm[MAX_PATH] = "?", *b;
      if (buf[k] < 0x10000) continue; if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)buf[k], &m) || !m) continue;
      GetModuleFileNameA(m, nm, MAX_PATH); b = strrchr(nm, '\\'); if (b) memmove(nm, b + 1, strlen(b)); L("        [rsp+0x%x] %s+0x%llx", k * 8, nm, (unsigned long long)(buf[k] - (ULONG_PTR)m)); shown++; } }
  if (tid != GetCurrentThreadId()) ResumeThread(th); CloseHandle(th);
}
static void dump_all(void) { HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); THREADENTRY32 t; t.dwSize = sizeof t; SymInitialize(GetCurrentProcess(), NULL, FALSE);
  if (Thread32First(s, &t)) do { if (t.th32OwnerProcessID == GetCurrentProcessId() && t.th32ThreadID != GetCurrentThreadId()) dump_thread(t.th32ThreadID); } while (Thread32Next(s, &t)); CloseHandle(s); }
static LONG CALLBACK svc_veh(PEXCEPTION_POINTERS e) {
  DWORD c = e->ExceptionRecord->ExceptionCode; ULONG_PTR *sp; HMODULE m = NULL; char nm[MAX_PATH] = "?", *b; int k, shown = 0; static LONG busy;
  if (c < 0xC0000000 || c == 0xC0000008 || c == 0xC0000194) return EXCEPTION_CONTINUE_SEARCH; if (InterlockedIncrement(&busy) > 8) return EXCEPTION_CONTINUE_SEARCH;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m);
  if (m) { GetModuleFileNameA(m, nm, MAX_PATH); b = strrchr(nm, '\\'); if (b) memmove(nm, b + 1, strlen(b)); }
  L("EXCEPTION 0x%08lX tid %lu at %s+0x%llx (addr %p, info0=%llx info1=%llx)", c, GetCurrentThreadId(), nm, m ? (unsigned long long)((ULONG_PTR)e->ExceptionRecord->ExceptionAddress - (ULONG_PTR)m) : 0ULL, e->ExceptionRecord->ExceptionAddress,
    e->ExceptionRecord->NumberParameters > 0 ? (unsigned long long)e->ExceptionRecord->ExceptionInformation[0] : 0ULL, e->ExceptionRecord->NumberParameters > 1 ? (unsigned long long)e->ExceptionRecord->ExceptionInformation[1] : 0ULL);
  sp = (ULONG_PTR *)e->ContextRecord->Rsp;
  for (k = 0; k < 600 && shown < 30; k++) { HMODULE mm = NULL; char n2[MAX_PATH] = "?", *b2; ULONG_PTR v; if (IsBadReadPtr(sp + k, 8)) break; v = sp[k]; if (v < 0x10000) continue;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mm) || !mm) continue;
    GetModuleFileNameA(mm, n2, MAX_PATH); b2 = strrchr(n2, '\\'); if (b2) memmove(n2, b2 + 1, strlen(b2)); L("    [rsp+0x%x] %s+0x%llx", k * 8, n2, (unsigned long long)(v - (ULONG_PTR)mm)); shown++; }
  return EXCEPTION_CONTINUE_SEARCH; }
#include "wxrbroker.inc"   /* WXR: activation by AUMID */
int main(int argc, char **argv) {
  AddVectoredExceptionHandler(1, svc_veh); SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  char sysd[MAX_PATH], p[MAX_PATH]; HMODULE us, cm; void (WINAPI *push)(void *); void (WINAPI *smain)(DWORD, LPWSTR *); HANDLE stop;
  static void *globals[64]; WELL_KNOWN_SID_TYPE t[] = { WinNullSid, WinWorldSid, WinLocalSid, WinNetworkSid, WinLocalSystemSid, WinLocalServiceSid, WinNetworkServiceSid, WinBuiltinDomainSid,
    WinAuthenticatedUserSid, WinAnonymousSid, WinBuiltinAdministratorsSid, WinBuiltinUsersSid, WinBuiltinGuestsSid, WinBuiltinPowerUsersSid, WinBuiltinAccountOperatorsSid,
    WinBuiltinSystemOperatorsSid, WinBuiltinPrintOperatorsSid, WinBuiltinBackupOperatorsSid }; int i; LPWSTR sv[2] = { L"CoreMessagingRegistrar", NULL };
  /* wxrsvc --wxr [<root>] [stop]: the pristine CoreMessaging.dll of <root>\Windows\System32, bound by the loader layer in <root>\Layer\wxrsys.dll,
     plus the activation endpoint (wxrbroker.inc). Root: the argument, else WXR_ROOT, else C:\WXR. One broker per session. */
  { char root[MAX_PATH] = "C:\\WXR"; int stopa = 2;
    if (argc < 2 || strcmp(argv[1], "--wxr")) { puts("usage: wxrsvc --wxr [<root>] [stop]"); return 2; }
    if (argc > 2 && strcmp(argv[2], "stop")) { lstrcpynA(root, argv[2], MAX_PATH); stopa = 3; } else if (GetEnvironmentVariableA("WXR_ROOT", p, MAX_PATH)) lstrcpynA(root, p, MAX_PATH);
    if (argc > stopa && !strcmp(argv[stopa], "stop")) { HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE, "Local\\wxrsvc.stop"); if (e) { SetEvent(e); puts("stop signalled"); } else puts("not running"); return 0; }
      { HANDLE e = OpenEventA(SYNCHRONIZE, FALSE, "Local\\wxrsvc.stop"); if (e) { puts("already running"); CloseHandle(e); return 0; } }   /* one broker per session */
#ifndef WXR_RETAIL
      _snprintf(p, MAX_PATH, "%s\\Logs\\wxrsvc.log", root); lf = fopen(p, "w");
#endif
      lstrcpynA(g_broker_root, root, MAX_PATH);
      AddVectoredExceptionHandler(1, vh);
      SetEnvironmentVariableA("WXR_LOADER", "1"); SetEnvironmentVariableA("WXR_ROOT", root);
      _snprintf(sysd, MAX_PATH, "%s\\Layer", root); SetDllDirectoryA(sysd);
      { static char path[8192]; DWORD n = GetEnvironmentVariableA("PATH", path, sizeof path - 300); if (n < sizeof path - 300) { _snprintf(path + n, 299, ";%s\\Windows\\System32", root); SetEnvironmentVariableA("PATH", path); } }   /* forwarders / by-name loads of image DLLs */
      _snprintf(p, MAX_PATH, "%s\\Layer\\wxrsys.dll", root); us = LoadLibraryExA(p, 0, LOAD_WITH_ALTERED_SEARCH_PATH); L("wxrsys.dll (loader layer): %s", us ? "loaded" : "FAILED");
      _snprintf(p, MAX_PATH, "%s\\Windows\\System32\\CoreMessaging.dll", root); cm = LoadLibraryExA(p, 0, 0); L("CoreMessaging (%s): %s (error %lu)", p, cm ? "loaded" : "FAILED", GetLastError());
  }
  if (!cm) return 1;
  push = (void *)GetProcAddress(cm, "SvchostPushServiceGlobals"); smain = (void *)GetProcAddress(cm, "ServiceMain"); L("SvchostPushServiceGlobals=%p ServiceMain=%p", (void *)push, (void *)smain);
  for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) { DWORD sz = SECURITY_MAX_SID_SIZE; PSID s = LocalAlloc(LPTR, sz); if (CreateWellKnownSid(t[i], NULL, s, &sz)) globals[i] = s; else globals[i] = NULL; }
  if (push) { L("calling SvchostPushServiceGlobals(globals)"); push(globals); }
  stop = CreateEventA(NULL, TRUE, FALSE, "Local\\wxrsvc.stop");
  g_smain = smain; g_sv = sv; { HANDLE th = CreateThread(NULL, 0, smain_thread, NULL, 0, &g_smain_tid); L("ServiceMain running on thread %lu", g_smain_tid);
    if (WaitForSingleObject(th, 6000) == WAIT_TIMEOUT) { L("ServiceMain did NOT return within 6 s: dumping all threads"); dump_all(); } else L("ServiceMain returned; registrar should be listening on \\BaseNamedObjects\\CoreMessagingRegistrar"); }
  if (g_broker_root[0]) broker_start(g_broker_root);   /* after the registrar is up: apps may start now */
  { MSG m; HANDLE h[1]; h[0] = stop; for (;;) { DWORD r = MsgWaitForMultipleObjects(1, h, FALSE, INFINITE, QS_ALLINPUT); if (r == WAIT_OBJECT_0) break; while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); } } }
  L("stop requested"); return 0;
}

