/* wxrsys.c - user-mode implementations of Windows 8/10 system functions that Microsoft's real WinRT/XAML DLLs
   import and that Windows 7 (+VxKex) lacks, and the loader layer that runs those DLLs (wxrldr.inc). x64 only.
   Honest rules: nothing silently pretends to work.  Real implementations are marked [impl]; the rest are [stub]
   and log every call (wxrsys.log next to the process exe) so we learn which ones matter. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <objbase.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

typedef LONG NTSTATUS;
#define STATUS_SUCCESS            ((NTSTATUS)0)
#define STATUS_NOT_IMPLEMENTED    ((NTSTATUS)0xC0000002)
#undef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define STATUS_CANCELLED          ((NTSTATUS)0xC0000120)
#define STATUS_NOT_FOUND          ((NTSTATUS)0xC0000225)
#define STATUS_BUFFER_TOO_SMALL   ((NTSTATUS)0xC0000023)
#undef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017)
#define ERROR_NO_PACKAGE_ID       15700

/* ---------------- logging ----------------
   Retail builds (build.ps1 retail, -DWXR_RETAIL) log nothing: slog returns before formatting, so wxrsys.log is never written. */
static CRITICAL_SECTION g_cs; static int g_cs_init;
/* keeps the caller's last error: opening the log file sets ERROR_ALREADY_EXISTS, which turned CreateStateSubcontainer's "not found"
   into "already exists" (Clock's ApplicationData containers could not be created) */
static void slog(const char *fmt, ...) {
  static LONG n; char b[512]; va_list ap; FILE *f; char path[MAX_PATH]; char *p; DWORD le;
#ifdef WXR_RETAIL
  (void)fmt; return;
#endif
  le = GetLastError();
  if (InterlockedIncrement(&n) > 40000) return;
  va_start(ap, fmt); _vsnprintf(b, sizeof b - 1, fmt, ap); va_end(ap); b[sizeof b - 1] = 0;
  if (!strncmp(b, "[shell]", 7) || !strncmp(b, "[win]", 5) || !strncmp(b, "[rgaf]", 6)) { char d[560]; _snprintf(d, sizeof d - 2, "%s\n", b); OutputDebugStringA(d); }   /* shows up inline in wxrrun's log */
  { HMODULE self = NULL; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)slog, &self);
    GetModuleFileNameA(self, path, MAX_PATH); p = strrchr(path, '\\'); if (p) *p = 0; strcat(path, "\\wxrsys.log"); }
  { static DWORD t0; DWORD t = GetTickCount(); if (!t0) t0 = t; t -= t0;   /* "[tid] s.mmm text": time since the first log line */
    f = fopen(path, "a"); if (f) { fprintf(f, "[%lu] %lu.%03lu %s\n", GetCurrentThreadId(), t / 1000, t % 1000, b); fclose(f); } }
  SetLastError(le);
}
__declspec(dllexport) void WINAPI WxrLog(const char *s) { if (s) slog("%s", s); }   /* wxrstub's rt_log writes here too */
#define STUB_LOG(n) slog("[stub] %s called", n)

/* Called by the loader's logging thunks for imports nobody implements (r11 = name). Returns 0. */
__declspec(dllexport) ULONG_PTR WxrUnimplC(const char *name) {
  slog("[unimpl] %s called (generated thunk)", name ? name : "?");
  if (name && (!strncmp(name, "Nt", 2) || !strncmp(name, "Zw", 2) || !strncmp(name, "Rtl", 3))) return (ULONG_PTR)STATUS_NOT_IMPLEMENTED;
  return 0;
}
/* asm entry: the thunk did  lea r11,[name] ; jmp [IAT]  so r11 = name.  Keep rcx,rdx,r8,r9. */
__asm__(".text\n.globl WxrUnimpl\nWxrUnimpl:\n"
  "push %rcx\n push %rdx\n push %r8\n push %r9\n sub $40,%rsp\n"
  "mov %r11,%rcx\n call WxrUnimplC\n add $40,%rsp\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n ret\n");

static void obj_type_name(HANDLE h, char *out, size_t n);
#include "wxrntdll.inc"
#include "wxrdevquery.inc"
#include "wxrapisets.inc"
#include "wxruser.inc"
#include "wxrcom.inc"
#include "wxrgdk.inc"
#include "wxrmarshal.inc"
#include "wxrwin8.inc"
#include "wxractivation.inc"
#include "wxrdx.inc"   /* D3D 11.2/11.3 tear-offs over Win7's D3D 11.1 (Win10 d2d1) */
#ifndef ERROR_BAD_COMPRESSION_BUFFER
#define ERROR_BAD_COMPRESSION_BUFFER 605L
#endif
#include "wxrcab.inc"  /* Compression API (cabinet.dll #40/#43/#45), MSZIP: Win10 d2d1's compressed built-in effect shaders */
#include "wxrshellapi.inc"
#include "wxrstream.inc"
#include "wxruri.inc"
#include "wxrerror.inc"
#include "wxrinet.inc"
#include "wxrcompat.inc"
#include "wxraudio.inc"
#include "wxrmisc.inc"
#include "wxrclasses.inc"
#include "wxrbuiltin.inc"
#include "wxrscm.inc"
#include "wxrpackage.inc"
#include "wxrldr.inc"       /* the loader layer: runs the image's Win10 files */
#include "wxrstorage.inc"   /* StorageFolder stand-in for Package.InstalledLocation */
#include "wxrprop.inc"      /* MF property handler for tags, propsys lookup, no third-party in-proc COM */
#include "wxrgameinput.inc"   /* GameInput stand-in when the real one (its Win10 service) fails */
#include "wxrgetproc.inc"
#include "wxrwait.inc"
/* mrt100.dll (Windows' own .NET Native runtime stub, delay-loaded by mrt100_app.dll; Win7 has none): mrt100_app asks for an optional
   "managed runtime service" inside a __try that only swallows exception 0x505 and then uses its built-in defaults. */
/* user32 private #2633/#2647/#2648 (ordmap names): InputHost's ActivationController for each CoreWindow (19041.3803 InputHost:
   BOOL CreateActivationObject(kind, in, HANDLE *out), FALSE -> WIL fail-fast in Windows.UI's WndProc). Win7 has no activation objects:
   each one is an unsignalled event, configure/destroy succeed (the event is left open). */
__declspec(dllexport) BOOL WINAPI NtUserCreateActivationObject(int kind, void *in, HANDLE *out) { (void)in; if (out) *out = CreateEventW(NULL, TRUE, FALSE, NULL); slog("[stub] NtUserCreateActivationObject(kind %d) -> event %p", kind, out ? *out : NULL); return TRUE; }
__declspec(dllexport) BOOL WINAPI NtUserConfigureActivationObject(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; return TRUE; }
__declspec(dllexport) BOOL WINAPI NtUserDestroyActivationObject(void *h) { (void)h; return TRUE; }
__declspec(dllexport) void *WINAPI GetManagedRuntimeService(const GUID *id, void *arg) { (void)id; (void)arg; RaiseException(0x505, 0, 0, NULL); return NULL; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)x; if (r == DLL_PROCESS_ATTACH) { g_self = h; wxr_start(h); } return TRUE; }

















#include "wxrpump.inc"
#include "wxrshell.inc"
