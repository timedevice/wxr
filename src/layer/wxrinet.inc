/* wxrinet.inc - WinINet: options and header flags Win7 rejects. Included by wxrsys.c. */
/* InternetSetOptionW (forced for WXR modules): Win7's WinINet (IE11) rejects options added in Win10 with ERROR_INVALID_PARAMETER /
   ERROR_INTERNET_INVALID_OPTION. Windows.Web.Http sets INTERNET_OPTION_ENABLE_HTTP_PROTOCOL (148, HTTP/2) on every new session and gave up on
   the failure - every Store request ended before a connection. Win10-only options are accepted and dropped (the request runs over HTTP/1.1);
   any other failure is logged and returned unchanged. */
__declspec(dllexport) BOOL WINAPI InternetSetOptionW(void *h, DWORD opt, void *buf, DWORD len) {
  typedef BOOL (WINAPI *F)(void *, DWORD, void *, DWORD); static F f; BOOL ok; DWORD e;
  if (!f) { HMODULE m = LoadLibraryW(L"wininet.dll"); f = m ? (F)GetProcAddress(m, "InternetSetOptionW") : NULL; } if (!f) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
  ok = f(h, opt, buf, len); if (ok) return TRUE; e = GetLastError();
  if ((e == ERROR_INVALID_PARAMETER || e == 12009 /* ERROR_INTERNET_INVALID_OPTION */) && (opt == 148 /* ENABLE_HTTP_PROTOCOL */ || opt == 149 /* HTTP_DECODING? */ || opt >= 150)) {
    static LONG n; if (InterlockedIncrement(&n) <= 20) slog("[inet] InternetSetOptionW(option %lu) not on Win7 (error %lu): accepted and ignored", opt, e); SetLastError(0); return TRUE; }
  { static LONG n; if (InterlockedIncrement(&n) <= 20) slog("[inet] InternetSetOptionW(option %lu, len %lu) failed %lu", opt, len, e); } SetLastError(e); return FALSE; }
/* HttpAddRequestHeadersA/W (forced for WXR modules): HTTP_ADDREQ_FLAG_ALLOW_EMPTY_VALUES (0x04000000) is Win10's; Win7's WinINet refuses the
   whole call with ERROR_INVALID_PARAMETER (Windows.Web.Http adds every request's headers with it). Retried without it. */
#define INET_ADDREQ_W10 0x04000000u
__declspec(dllexport) BOOL WINAPI HttpAddRequestHeadersA(void *h, const char *hd, DWORD len, DWORD mods) {
  typedef BOOL (WINAPI *F)(void *, const char *, DWORD, DWORD); static F f; BOOL ok;
  if (!f) { HMODULE m = LoadLibraryW(L"wininet.dll"); f = m ? (F)GetProcAddress(m, "HttpAddRequestHeadersA") : NULL; } if (!f) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
  ok = f(h, hd, len, mods); if (!ok && (mods & INET_ADDREQ_W10) && GetLastError() == ERROR_INVALID_PARAMETER) {
    /* without the flag Win7 still refuses a block holding an empty-valued header ("OSContentId:"): those lines are dropped */
    DWORD L = (len == (DWORD)-1) ? (DWORD)lstrlenA(hd) : len, i = 0, o = 0, dropped = 0; char *c = (char *)malloc(L + 1);
    if (c) { while (i < L) { DWORD s = i, e2, k; while (i < L && hd[i] != '\n') i++; e2 = i; if (i < L) i++;   /* line [s, e2) without its \n */
        { DWORD ve = e2; const char *colon; while (ve > s && (hd[ve - 1] == '\r' || hd[ve - 1] == ' ' || hd[ve - 1] == '\t')) ve--;
          colon = (const char *)memchr(hd + s, ':', ve - s); if (ve > s && colon && colon == hd + ve - 1) { dropped++; continue; } }
        for (k = s; k < i; k++) c[o++] = hd[k]; }
      c[o] = 0; ok = f(h, c, o, mods & ~INET_ADDREQ_W10); { DWORD e3 = GetLastError(); static LONG nd; if (dropped && InterlockedIncrement(&nd) <= 5) slog("[inet] HttpAddRequestHeadersA: %lu empty-valued header(s) dropped -> %d", dropped, ok); free(c); SetLastError(e3); } } }
  if (!ok) { DWORD e = GetLastError(); static LONG n; if (InterlockedIncrement(&n) <= 20) { char b[600]; DWORD k, L = len == (DWORD)-1 ? (DWORD)lstrlenA(hd) : len; for (k = 0; k < L && k < 599; k++) b[k] = (hd[k] == '\r') ? '|' : (hd[k] == '\n') ? '~' : hd[k]; b[k] = 0;
      slog("[inet] HttpAddRequestHeadersA(mods 0x%lx, len %lu) failed %lu: %s", mods, len, e, b); } SetLastError(e); } return ok; }
__declspec(dllexport) BOOL WINAPI HttpAddRequestHeadersW(void *h, const WCHAR *hd, DWORD len, DWORD mods) {
  typedef BOOL (WINAPI *F)(void *, const WCHAR *, DWORD, DWORD); static F f; BOOL ok;
  if (!f) { HMODULE m = LoadLibraryW(L"wininet.dll"); f = m ? (F)GetProcAddress(m, "HttpAddRequestHeadersW") : NULL; } if (!f) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
  ok = f(h, hd, len, mods); if (!ok && (mods & INET_ADDREQ_W10) && GetLastError() == ERROR_INVALID_PARAMETER) ok = f(h, hd, len, mods & ~INET_ADDREQ_W10);
  if (!ok) { DWORD e = GetLastError(); static LONG n; if (InterlockedIncrement(&n) <= 20) slog("[inet] HttpAddRequestHeadersW(mods 0x%lx) failed %lu", mods, e); SetLastError(e); } return ok; }
