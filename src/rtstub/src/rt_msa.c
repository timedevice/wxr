/* rt_msa.c - the environment's Microsoft DEVICE identity (what wlidsvc keeps on Win10): tickets for OnlineIdSystemAuthenticator.
 *
 * A device identity is a random member name ("02" + 18 letters) and password registered once with Microsoft's login service; with them
 * the service issues service tickets ("t=...&p=...", urn:passport:compact, valid 24 h) that tell a Microsoft service "this request
 * comes from device <PUID>". Nothing here is bound to hardware or the kernel, and no user account is involved.
 *   register  POST https://login.live.com/ppsecure/deviceaddcredential.srf   <DeviceAddRequest> -> <puid>
 *   ticket    POST https://login.live.com/RST2.srf   WS-Trust RequestSecurityToken, wsse:UsernameToken wsu:Id="devicesoftware",
 *             AppliesTo = the service (the Store: "www.microsoft.com"), PolicyReference URI = the policy ("MBI_SSL")
 *             -> <wsse:BinarySecurityToken Id="Compact0">t=...</wsse:BinarySecurityToken>
 * (protocol reference: github.com/minecraft-linux/msa, MIT)
 * State: <root>\ProgramData\Identity\device.ini  [device] Membername, Puid, Password (DPAPI, current user, hex)
 *                                               [tickets] <service>|<policy> = <expiry FILETIME hex>,<DPAPI hex of the ticket>
 * The identity is created on first use; a file device.import beside it (three lines: member name, password, puid) is taken over
 * instead and deleted (the prototype's identity). Tickets are reused for 12 hours, in memory and across processes.
 * WXR_NO_DEVICE_ID=1: never contact the login service (requests then fail as on a machine without an identity).
 * Log lines never contain the password or a ticket. */
#include "rt_core.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>

static SRWLOCK g_lk = SRWLOCK_INIT;
static struct { WCHAR key[520]; WCHAR *ticket; ULONGLONG expiry; } g_cache[8]; static int g_ncache;
static char g_member[64], g_pass[128], g_puid[40]; static int g_have;
static GUID g_app = { 0xD6D5A677, 0x0872, 0x4AB0, { 0x94, 0x42, 0xBB, 0x79, 0x2F, 0xCE, 0x85, 0xC5 } };   /* the requesting app; default = the Store's ApplicationId */
void msa_set_app(const GUID *id) { if (id) g_app = *id; }

static ULONGLONG now_ft(void) { FILETIME f; GetSystemTimeAsFileTime(&f); return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static int ident_path(WCHAR *out)
{
    HMODULE m = NULL; WCHAR *b;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ident_path, &m) || !GetModuleFileNameW(m, out, MAX_PATH)) return 0;
    if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0; if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0;
    if (lstrlenW(out) > MAX_PATH - 60) return 0;
    lstrcatW(out, L"\\ProgramData\\Identity"); CreateDirectoryW(out, NULL); lstrcatW(out, L"\\device.ini"); return 1;
}
/* DPAPI (current user) <-> hex text */
static WCHAR *protect_hex(const void *data, DWORD n)
{
    DATA_BLOB in, out; WCHAR *h; DWORD i; in.pbData = (BYTE *)data; in.cbData = n;
    if (!CryptProtectData(&in, L"WXR device identity", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return NULL;
    if ((h = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (out.cbData * 2 + 1) * sizeof(WCHAR)))) { for (i = 0; i < out.cbData; i++) _snwprintf(h + i * 2, 3, L"%02X", out.pbData[i]); h[out.cbData * 2] = 0; }
    LocalFree(out.pbData); return h;
}
static void *unprotect_hex(const WCHAR *hex, DWORD *n)
{
    DATA_BLOB in, out; DWORD len = (DWORD)lstrlenW(hex) / 2, i; BYTE *b, *r = NULL; unsigned v;
    if (!len || !(b = (BYTE *)HeapAlloc(GetProcessHeap(), 0, len))) return NULL;
    for (i = 0; i < len; i++) { WCHAR t[3] = { hex[i * 2], hex[i * 2 + 1], 0 }; swscanf(t, L"%x", &v); b[i] = (BYTE)v; }
    in.pbData = b; in.cbData = len;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) { if ((r = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, out.cbData + 2))) { memcpy(r, out.pbData, out.cbData); *n = out.cbData; } LocalFree(out.pbData); }
    HeapFree(GetProcessHeap(), 0, b); return r;
}

/* POST text/xml to login.live.com<path>; answer malloc'ed (HeapAlloc), NULL on failure */
static char *post(const WCHAR *path, const char *body, DWORD *status)
{
    HINTERNET ses, con = NULL, req = NULL; DWORD prot = 0x00000800 | 0x00000200, st = 0, sl = sizeof st, n, len = 0, cap = 0; char *buf = NULL, chunk[8192];
    *status = 0;
    if (!(ses = WinHttpOpen(L"WXR/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0))) return NULL;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot); WinHttpSetTimeouts(ses, 15000, 15000, 30000, 30000);
    if ((con = WinHttpConnect(ses, L"login.live.com", INTERNET_DEFAULT_HTTPS_PORT, 0)) && (req = WinHttpOpenRequest(con, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE))
        && WinHttpSendRequest(req, L"Content-Type: text/xml; charset=utf-8", (DWORD)-1, (void *)body, (DWORD)strlen(body), (DWORD)strlen(body), 0) && WinHttpReceiveResponse(req, NULL)) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX); *status = st;
        while (WinHttpReadData(req, chunk, sizeof chunk, &n) && n) {
            if (len + n + 1 > cap) { char *nb; cap = (len + n + 1) * 2; nb = buf ? (char *)HeapReAlloc(GetProcessHeap(), 0, buf, cap) : (char *)HeapAlloc(GetProcessHeap(), 0, cap); if (!nb) break; buf = nb; }
            memcpy(buf + len, chunk, n); len += n; }
        if (buf) buf[len] = 0;
    }
    if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return buf;
}
static void xml_esc(const char *s, char *out, size_t cap)
{
    size_t k = 0; for (; *s && k + 7 < cap; s++) { const char *r = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : *s == '\'' ? "&apos;" : NULL; if (r) { strcpy(out + k, r); k += strlen(r); } else out[k++] = *s; } out[k] = 0;
}
static void rnd(char *out, int n, const char *alphabet)
{
    HCRYPTPROV p = 0; BYTE b[64]; int i, al = (int)strlen(alphabet);
    if (!CryptAcquireContextW(&p, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) || !CryptGenRandom(p, n, b)) { for (i = 0; i < n; i++) b[i] = (BYTE)(rand() ^ (GetTickCount() >> (i & 7))); }
    if (p) CryptReleaseContext(p, 0);
    for (i = 0; i < n; i++) out[i] = alphabet[b[i] % al]; out[n] = 0;
}

/* loads the identity, imports the prototype's, or registers a new one. Caller holds the lock. */
static int identity(const WCHAR *ini)
{
    WCHAR w[600], imp[MAX_PATH], *hex; DWORD n = 0, st; char *p, *resp, body[900], esc[400];
    if (g_have) return 1;
    GetPrivateProfileStringW(L"device", L"Membername", L"", w, 64, ini); WideCharToMultiByte(CP_UTF8, 0, w, -1, g_member, sizeof g_member, NULL, NULL);
    GetPrivateProfileStringW(L"device", L"Puid", L"", w, 40, ini); WideCharToMultiByte(CP_UTF8, 0, w, -1, g_puid, sizeof g_puid, NULL, NULL);
    GetPrivateProfileStringW(L"device", L"Password", L"", w, 600, ini);
    if (g_member[0] && w[0] && (p = (char *)unprotect_hex(w, &n))) { lstrcpynA(g_pass, p, sizeof g_pass); SecureZeroMemory(p, n); HeapFree(GetProcessHeap(), 0, p); if (g_pass[0]) { g_have = 1; return 1; } }
    /* take over an identity made elsewhere (device.import: member name, password, puid - one per line) */
    lstrcpynW(imp, ini, MAX_PATH); lstrcpyW(wcsrchr(imp, L'\\') + 1, L"device.import");
    { HANDLE h = CreateFileW(imp, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL); char t[600]; DWORD rd = 0;
      if (h != INVALID_HANDLE_VALUE) { ReadFile(h, t, sizeof t - 1, &rd, NULL); CloseHandle(h); t[rd] = 0;
          if (sscanf(t, "%63[^\r\n]%*[\r\n]%127[^\r\n]%*[\r\n]%39[^\r\n]", g_member, g_pass, g_puid) >= 2) { g_have = 2; rt_log("device identity: imported %.4s... (puid %s)", g_member, g_puid); }
          SecureZeroMemory(t, sizeof t); DeleteFileW(imp); } }
    if (!g_have) {
        if (GetEnvironmentVariableA("WXR_NO_DEVICE_ID", NULL, 0)) return 0;
        g_member[0] = '0'; g_member[1] = '2'; rnd(g_member + 2, 18, "abcdefghijklmnopqrstuvwxyz");
        rnd(g_pass, 16, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^*()-_=+[]{}/?;:,.~");
        xml_esc(g_pass, esc, sizeof esc);
        _snprintf(body, sizeof body, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><DeviceAddRequest><ClientInfo name=\"IDCRL\" version=\"1.0\"><BinaryVersion>11</BinaryVersion></ClientInfo>"
                  "<Authentication><Membername>%s</Membername><Password>%s</Password></Authentication></DeviceAddRequest>", g_member, esc);
        resp = post(L"/ppsecure/deviceaddcredential.srf", body, &st); SecureZeroMemory(body, sizeof body);
        if (!resp || !(p = strstr(resp, "<puid>")) || sscanf(p + 6, "%39[0-9A-Fa-f]", g_puid) != 1) { rt_log("device identity: registration FAILED (HTTP %lu)", st); if (resp) HeapFree(GetProcessHeap(), 0, resp); g_member[0] = g_pass[0] = 0; return 0; }
        HeapFree(GetProcessHeap(), 0, resp); g_have = 2; rt_log("device identity: registered %.4s... with Microsoft's login service (puid %s)", g_member, g_puid);
    }
    if (g_have == 2) {   /* persist */
        MultiByteToWideChar(CP_UTF8, 0, g_member, -1, w, 64); WritePrivateProfileStringW(L"device", L"Membername", w, ini);
        MultiByteToWideChar(CP_UTF8, 0, g_puid, -1, w, 40); WritePrivateProfileStringW(L"device", L"Puid", w, ini);
        if ((hex = protect_hex(g_pass, (DWORD)strlen(g_pass) + 1))) { WritePrivateProfileStringW(L"device", L"Password", hex, ini); HeapFree(GetProcessHeap(), 0, hex); }
        g_have = 1;
    }
    return 1;
}
static char *request_ticket(const char *service, const char *policy)
{
    static const char *const NSX = "xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" "
        "xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\" xmlns:saml=\"urn:oasis:names:tc:SAML:1.0:assertion\" "
        "xmlns:wsp=\"http://schemas.xmlsoap.org/ws/2004/09/policy\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" "
        "xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" xmlns:wssc=\"http://schemas.xmlsoap.org/ws/2005/02/sc\" xmlns:wst=\"http://schemas.xmlsoap.org/ws/2005/02/trust\"";
    char *body = (char *)HeapAlloc(GetProcessHeap(), 0, 8192), esc[400], svc[900], pol[300], created[32], expires[32], polref[400] = "", *resp, *t, *e, *out = NULL; SYSTEMTIME a, b; FILETIME f; ULARGE_INTEGER u; DWORD st;
    if (!body) return NULL;
    GetSystemTime(&a); SystemTimeToFileTime(&a, &f); u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime; u.QuadPart += 300ULL * 10000000; f.dwLowDateTime = u.LowPart; f.dwHighDateTime = u.HighPart; FileTimeToSystemTime(&f, &b);
    sprintf(created, "%04u-%02u-%02uT%02u:%02u:%02uZ", a.wYear, a.wMonth, a.wDay, a.wHour, a.wMinute, a.wSecond); sprintf(expires, "%04u-%02u-%02uT%02u:%02u:%02uZ", b.wYear, b.wMonth, b.wDay, b.wHour, b.wMinute, b.wSecond);
    xml_esc(g_pass, esc, sizeof esc); xml_esc(service, svc, sizeof svc); xml_esc(policy, pol, sizeof pol);
    if (pol[0]) _snprintf(polref, sizeof polref, "<wsp:PolicyReference xmlns:wsp=\"http://schemas.xmlsoap.org/ws/2004/09/policy\" URI=\"%s\"/>", pol);
    _snprintf(body, 8191, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><s:Envelope %s><s:Header>"
        "<wsa:Action s:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/02/trust/RST/Issue</wsa:Action><wsa:To s:mustUnderstand=\"1\">https://login.live.com/RST2.srf</wsa:To><wsa:MessageID>%llu</wsa:MessageID>"
        "<ps:AuthInfo xmlns:ps=\"http://schemas.microsoft.com/Passport/SoapServices/PPCRL\" Id=\"PPAuthInfo\"><ps:BinaryVersion>11</ps:BinaryVersion><ps:DeviceType>Windows</ps:DeviceType>"
        "<ps:HostingApp>{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}</ps:HostingApp></ps:AuthInfo>"
        "<wsse:Security><wsse:UsernameToken wsu:Id=\"devicesoftware\"><wsse:Username>%s</wsse:Username><wsse:Password>%s</wsse:Password></wsse:UsernameToken>"
        "<wsu:Timestamp xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" wsu:Id=\"Timestamp\"><wsu:Created>%s</wsu:Created><wsu:Expires>%s</wsu:Expires></wsu:Timestamp></wsse:Security></s:Header>"
        "<s:Body><wst:RequestSecurityToken xmlns:wst=\"http://schemas.xmlsoap.org/ws/2005/02/trust\" Id=\"RST0\"><wst:RequestType>http://schemas.xmlsoap.org/ws/2005/02/trust/Issue</wst:RequestType>"
        "<wsp:AppliesTo xmlns:wsp=\"http://schemas.xmlsoap.org/ws/2004/09/policy\"><wsa:EndpointReference xmlns:wsa=\"http://www.w3.org/2005/08/addressing\"><wsa:Address>%s</wsa:Address></wsa:EndpointReference></wsp:AppliesTo>%s"
        "</wst:RequestSecurityToken></s:Body></s:Envelope>", NSX, now_ft() / 10000,
        g_app.Data1, g_app.Data2, g_app.Data3, g_app.Data4[0], g_app.Data4[1], g_app.Data4[2], g_app.Data4[3], g_app.Data4[4], g_app.Data4[5], g_app.Data4[6], g_app.Data4[7],
        g_member, esc, created, expires, svc, polref);
    body[8191] = 0;
    resp = post(L"/RST2.srf", body, &st); SecureZeroMemory(body, 8192); HeapFree(GetProcessHeap(), 0, body);
    if (!resp) { rt_log("device identity: no answer from the login service (HTTP %lu)", st); return NULL; }
    if ((t = strstr(resp, "<wsse:BinarySecurityToken")) && (t = strchr(t, '>')) && (e = strchr(t + 1, '<')) && e - t > 8) {
        char *d; *e = 0; t++;
        if ((out = (char *)HeapAlloc(GetProcessHeap(), 0, strlen(t) + 1))) { for (d = out; *t; ) { if (!strncmp(t, "&amp;", 5)) { *d++ = '&'; t += 5; } else *d++ = *t++; } *d = 0; }
    } else { char code[64] = ""; if ((t = strstr(resp, "<psf:value>"))) sscanf(t + 11, "%63[^<]", code); rt_log("device identity: the login service gave no ticket for this service (HTTP %lu, error %s)", st, code[0] ? code : "?"); }
    SecureZeroMemory(resp, strlen(resp)); HeapFree(GetProcessHeap(), 0, resp);
    return out;
}

/* ---- USER identity ---------------------------------------------------------------------------------------------
 * A persisted user credential (an OAuth REFRESH TOKEN for a Microsoft account, from the device-code sign-in), which this
 * code redeems for the per-service user RPS ticket the Store's order needs. Once the credential is present, no Win10 and
 * no re-import are involved: the ticket is fetched on demand and cached for its lifetime, and the refresh token itself
 * renews silently (no 2FA). Obtaining it the first time is the sign-in front end's job (the device-code flow of WXR Sign-in,
 * src\msa\wxrsignin.c, which hands it over as user.import); this file only persists and consumes it, so the auth surface
 * stays in one place.
 * State: <root>\ProgramData\Identity\user.ini
 *   [user]            Id = <the signed-in account's id>
 *   [usercred]        Token = <DPAPI hex of the refresh token>, Expiry = <FILETIME hex, 0 = unknown>
 *   [userticketcache] <service>|<policy> = <expiry FILETIME hex>,<DPAPI hex of the access ticket>   (reused until it expires)
 *   [usertickets]     <service>|<policy> = 0,<ticket>   (a ticket handed in directly, e.g. the old Win10 bridge; still honoured)
 * A file user.import beside it (id, refresh token, optional expiry FILETIME hex - one per line) is taken over into
 * [usercred] and deleted, so a credential obtained anywhere can be dropped in once. Log lines never contain the token or a ticket. */
static char g_usertoken[8192]; static ULONGLONG g_userexp; static WCHAR g_userid[80]; static int g_haveuser;   /* 1 = loaded, -1 = none */
static char g_userclient[40] = "";   /* the OAuth client the refresh token belongs to; empty = the device-code default */

static int user_ini(WCHAR *out) { if (!ident_path(out)) return 0; lstrcpyW(wcsrchr(out, L'\\') + 1, L"user.ini"); return 1; }
/* the "Microsoft account sign-in" component (environment.ini [components] msa, set by setup): when it is off, no account is used at
   all - no credential is imported, read or redeemed, and the Store runs signed out. Not recorded (older roots) = on. */
static int msa_enabled(void)
{
    WCHAR p[MAX_PATH], v[8], *b;
    if (!ident_path(p) || !(b = wcsstr(p, L"\\ProgramData\\Identity"))) return 1;
    lstrcpyW(b, L"\\environment.ini"); GetPrivateProfileStringW(L"components", L"msa", L"1", v, 8, p);
    return v[0] != '0';
}

/* store a credential into the globals + user.ini. Caller holds the lock. */
static void set_user_cred_locked(const WCHAR *ini, const char *token, ULONGLONG expiry, const WCHAR *id)
{
    WCHAR *hex, e[32];
    lstrcpynA(g_usertoken, token, sizeof g_usertoken); g_userexp = expiry; g_haveuser = 1;
    if (id && id[0]) { lstrcpynW(g_userid, id, 80); WritePrivateProfileStringW(L"user", L"Id", id, ini); }
    if ((hex = protect_hex(token, (DWORD)strlen(token) + 1))) { WritePrivateProfileStringW(L"usercred", L"Token", hex, ini); HeapFree(GetProcessHeap(), 0, hex); }
    _snwprintf(e, 32, L"%llX", expiry); e[31] = 0; WritePrivateProfileStringW(L"usercred", L"Expiry", e, ini);
}
/* persist a freshly obtained user credential (public entry for the sign-in front end). */
void msa_set_user_cred(const char *token, ULONGLONG expiry, const WCHAR *id)
{
    WCHAR ini[MAX_PATH];
    if (!token || !token[0] || !user_ini(ini)) return;
    AcquireSRWLockExclusive(&g_lk); set_user_cred_locked(ini, token, expiry, id); ReleaseSRWLockExclusive(&g_lk);
    rt_log("user identity: credential stored (id %ls)", id && id[0] ? id : L"?");
}
/* load the persisted credential (and take over user.import). Caller holds the lock. 1 = a non-expired token is in g_usertoken. */
static int user_cred(const WCHAR *ini)
{
    WCHAR w[9000], imp[MAX_PATH]; DWORD n = 0; char *p;
    if (g_haveuser) return g_haveuser > 0 && (!g_userexp || g_userexp > now_ft());
    lstrcpynW(imp, ini, MAX_PATH); lstrcpyW(wcsrchr(imp, L'\\') + 1, L"user.import");
    { HANDLE h = CreateFileW(imp, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL); char t[9000], idA[160], tok[8192]; DWORD rd = 0; unsigned long long ex = 0;
      if (h != INVALID_HANDLE_VALUE) { ReadFile(h, t, sizeof t - 1, &rd, NULL); CloseHandle(h); t[rd] = 0;
          if (sscanf(t, "%159[^\r\n]%*[\r\n]%8191[^\r\n]%*[\r\n]%llx", idA, tok, &ex) >= 2) { WCHAR idW[80]; MultiByteToWideChar(CP_UTF8, 0, idA, -1, idW, 80); set_user_cred_locked(ini, tok, (ULONGLONG)ex, idW); rt_log("user identity: imported credential (id %ls)", idW); }
          SecureZeroMemory(t, sizeof t); SecureZeroMemory(tok, sizeof tok); DeleteFileW(imp);
          if (g_haveuser > 0) return !g_userexp || g_userexp > now_ft(); } }
    GetPrivateProfileStringW(L"usercred", L"Token", L"", w, 9000, ini);
    if (w[0] && (p = (char *)unprotect_hex(w, &n))) { WCHAR e[32];
        lstrcpynA(g_usertoken, p, sizeof g_usertoken); SecureZeroMemory(p, n); HeapFree(GetProcessHeap(), 0, p);
        GetPrivateProfileStringW(L"usercred", L"Expiry", L"0", e, 32, ini); g_userexp = _wcstoui64(e, NULL, 16);
        GetPrivateProfileStringW(L"user", L"Id", L"", g_userid, 80, ini); g_haveuser = 1;
        return !g_userexp || g_userexp > now_ft(); }
    g_haveuser = -1; return 0;
}

/* the consumer-enabled client the device-code sign-in used; a refresh token must be redeemed against the same client. */
#define USER_CLIENT "000000004C12AE6F"

/* POST application/x-www-form-urlencoded to login.live.com<path>; answer malloc'ed, NULL on failure. */
static char *post_form(const WCHAR *path, const char *body, DWORD *status)
{
    HINTERNET ses, con = NULL, req = NULL; DWORD prot = 0x00000800 | 0x00000200, st = 0, sl = sizeof st, n, len = 0, cap = 0; char *buf = NULL, chunk[8192];
    *status = 0;
    if (!(ses = WinHttpOpen(L"WXR/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0))) return NULL;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot); WinHttpSetTimeouts(ses, 15000, 15000, 30000, 30000);
    if ((con = WinHttpConnect(ses, L"login.live.com", INTERNET_DEFAULT_HTTPS_PORT, 0)) && (req = WinHttpOpenRequest(con, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE))
        && WinHttpSendRequest(req, L"Content-Type: application/x-www-form-urlencoded", (DWORD)-1, (void *)body, (DWORD)strlen(body), (DWORD)strlen(body), 0) && WinHttpReceiveResponse(req, NULL)) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX); *status = st;
        while (WinHttpReadData(req, chunk, sizeof chunk, &n) && n) {
            if (len + n + 1 > cap) { char *nb; cap = (len + n + 1) * 2; nb = buf ? (char *)HeapReAlloc(GetProcessHeap(), 0, buf, cap) : (char *)HeapAlloc(GetProcessHeap(), 0, cap); if (!nb) break; buf = nb; }
            memcpy(buf + len, chunk, n); len += n; }
        if (buf) buf[len] = 0;
    }
    if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return buf;
}
/* copy the string value of JSON "key":"value" into out (unescaping \/ -> /). 1 if found. */
static int json_str(const char *json, const char *key, char *out, int cap)
{
    char pat[64]; const char *p; int k = 0; _snprintf(pat, sizeof pat, "\"%s\"", key); pat[63] = 0;
    if (cap) out[0] = 0; if (!(p = strstr(json, pat))) return 0;
    p += strlen(pat); while (*p == ' ' || *p == ':') p++; if (*p != '"') return 0;
    for (p++; *p && *p != '"' && k < cap - 1; p++) { if (p[0] == '\\' && p[1] == '/') { out[k++] = '/'; p++; } else out[k++] = *p; } out[k] = 0; return 1;
}
static int json_int(const char *json, const char *key, int def)
{
    char pat[64]; const char *p; _snprintf(pat, sizeof pat, "\"%s\"", key); pat[63] = 0;
    if (!(p = strstr(json, pat))) return def; p += strlen(pat); while (*p == ' ' || *p == ':' || *p == '"') p++;
    return (*p >= '0' && *p <= '9') ? atoi(p) : def;
}

/* Redeem the persisted refresh token for the service's USER access token (an RPS ticket, e.g. "EwAI...") at the OAuth
 * endpoint. The device-code sign-in gives, for scope service::www.microsoft.com::MBI_SSL, exactly the ticket the Store's
 * order needs - so no WS-Trust is involved on the user side. Caller holds the lock. *ttl = the ticket's lifetime (FILETIME
 * units) so it can be cached. A rotated refresh token in the answer is persisted. Answer malloc'ed, NULL on failure. */
static char *oauth_service_ticket(const WCHAR *ini, const char *service, const char *policy, ULONGLONG *ttl)
{
    char *body = (char *)HeapAlloc(GetProcessHeap(), 0, 2048), scope[700], *resp, *at = NULL; DWORD st; int expsec;
    const char *client = g_userclient[0] ? g_userclient : USER_CLIENT;
    if (!body) return NULL;
    _snprintf(scope, sizeof scope, "service::%s::%s", service, policy && policy[0] ? policy : "MBI_SSL"); scope[699] = 0;
    _snprintf(body, 2048, "client_id=%s&grant_type=refresh_token&scope=%s&refresh_token=%s", client, scope, g_usertoken); body[2047] = 0;
    resp = post_form(L"/oauth20_token.srf", body, &st); SecureZeroMemory(body, 2048); HeapFree(GetProcessHeap(), 0, body);
    if (!resp) { rt_log("user identity: no answer from the token endpoint (HTTP %lu)", st); return NULL; }
    { char *tmp = (char *)HeapAlloc(GetProcessHeap(), 0, 8192);
      if (tmp && json_str(resp, "access_token", tmp, 8192) && tmp[0]) { at = (char *)HeapAlloc(GetProcessHeap(), 0, strlen(tmp) + 1); if (at) strcpy(at, tmp); }
      if (tmp) { SecureZeroMemory(tmp, 8192); HeapFree(GetProcessHeap(), 0, tmp); } }
    if (at) {
        expsec = json_int(resp, "expires_in", 86400); if (expsec > 600) expsec -= 300;    /* a little slack */
        *ttl = (ULONGLONG)expsec * 10000000ULL;
        { char *nr = (char *)HeapAlloc(GetProcessHeap(), 0, 2048);                          /* persist a rotated refresh token */
          if (nr && json_str(resp, "refresh_token", nr, 2048) && nr[0] && strcmp(nr, g_usertoken)) set_user_cred_locked(ini, nr, 0, g_userid);
          if (nr) { SecureZeroMemory(nr, 2048); HeapFree(GetProcessHeap(), 0, nr); } }
    } else { char err[128] = ""; json_str(resp, "error", err, sizeof err); rt_log("user identity: the token endpoint gave no ticket (HTTP %lu, error %s)", st, err[0] ? err : "?"); }
    SecureZeroMemory(resp, strlen(resp)); HeapFree(GetProcessHeap(), 0, resp);
    return at;
}

/* 1 = a non-expired USER ticket for this service/policy is delivered. Order of preference:
 *   (1) a minted ticket still in the 12 h cache, (2) a ticket handed in directly ([usertickets], e.g. the old bridge),
 *   (3) mint a fresh one from the persisted user credential. The Store's order wants this (a user identity claim), not the device ticket. */
int msa_user_ticket(const WCHAR *service, const WCHAR *policy, WCHAR *ticket, int cap, WCHAR *id, int idcap)
{
    WCHAR ini[MAX_PATH], key[520], val[9000], *comma; char svc[800], pol[240], *tk; ULONGLONG now = now_ft(); const ULONGLONG KEEP = 12ULL * 3600 * 10000000; int ok = 0;
    if (cap > 0) ticket[0] = 0; if (idcap > 0) id[0] = 0;
    if (!service || !service[0] || !user_ini(ini)) return 0;
    if (!msa_enabled()) { static LONG nl; if (InterlockedIncrement(&nl) <= 2) rt_log("user identity: Microsoft account sign-in is not installed (setup component); signed out"); return 0; }
    _snwprintf(key, 520, L"%s|%s", service, policy ? policy : L""); key[519] = 0; CharLowerW(key);
    AcquireSRWLockExclusive(&g_lk);
    /* (1) cached minted ticket (DPAPI) */
    if (GetPrivateProfileStringW(L"userticketcache", key, L"", val, 9000, ini) && (comma = wcschr(val, L','))) {
        ULONGLONG exp = _wcstoui64(val, NULL, 16); DWORD n = 0; char *p;
        if (exp > now && (p = (char *)unprotect_hex(comma + 1, &n))) { MultiByteToWideChar(CP_UTF8, 0, p, -1, ticket, cap); SecureZeroMemory(p, n); HeapFree(GetProcessHeap(), 0, p); ok = ticket[0] != 0; }
    }
    /* (2) a ticket handed in directly */
    if (!ok && GetPrivateProfileStringW(L"usertickets", key, L"", val, 9000, ini) && val[0]) {
        comma = wcschr(val, L','); { const WCHAR *tv = comma ? comma + 1 : val; ULONGLONG exp = comma ? _wcstoui64(val, NULL, 16) : 0;
            if (exp && exp < now) rt_log("user ticket for '%ls' has expired", key); else { lstrcpynW(ticket, tv, cap); ok = ticket[0] != 0; } }
    }
    /* (3) mint from the persisted credential */
    if (!ok && user_cred(ini)) {
        ULONGLONG ttl = 0;
        WideCharToMultiByte(CP_UTF8, 0, service, -1, svc, sizeof svc, NULL, NULL); WideCharToMultiByte(CP_UTF8, 0, policy ? policy : L"", -1, pol, sizeof pol, NULL, NULL);
        if ((tk = oauth_service_ticket(ini, svc, pol, &ttl))) {
            WCHAR *hex; MultiByteToWideChar(CP_UTF8, 0, tk, -1, ticket, cap); ok = ticket[0] != 0;
            rt_log("user identity: new ticket for '%ls' (%d chars, kept %llu min)", key, (int)strlen(tk), (ttl ? ttl : KEEP) / (60ULL * 10000000));
            if ((hex = protect_hex(tk, (DWORD)strlen(tk) + 1)) && lstrlenW(hex) < 8900) { _snwprintf(val, 9000, L"%llX,%s", now + (ttl ? ttl : KEEP), hex); WritePrivateProfileStringW(L"userticketcache", key, val, ini); }
            if (hex) HeapFree(GetProcessHeap(), 0, hex);
            SecureZeroMemory(tk, strlen(tk)); HeapFree(GetProcessHeap(), 0, tk);
        }
    }
    if (idcap > 0) { if (g_userid[0]) lstrcpynW(id, g_userid, idcap); else GetPrivateProfileStringW(L"user", L"Id", L"", id, idcap, ini); }
    ReleaseSRWLockExclusive(&g_lk);
    return ok;
}

/* The signed-in account id for the Store's cosmetic "signed in" state (GetStoreWebAccountId). "" if nobody is signed in.
 * Cheap: the loaded credential's id, else user.ini [user] Id - no network, no ticket mint. */
int msa_user_id(WCHAR *out, int cap)
{
    WCHAR ini[MAX_PATH];
    if (cap > 0) out[0] = 0; else return 0;
    if (g_userid[0]) lstrcpynW(out, g_userid, cap);
    if (!out[0] && user_ini(ini)) GetPrivateProfileStringW(L"user", L"Id", L"", out, cap, ini);
    return out[0] != 0;
}

/* 1 = ticket (and the device id = its PUID) delivered */
int msa_device_ticket(const WCHAR *service, const WCHAR *policy, WCHAR *ticket, int cap, WCHAR *deviceId, int idcap)
{
    WCHAR ini[MAX_PATH], key[520], val[9000], *comma; char svc[800], pol[240], *tk = NULL; int i, ok = 0; ULONGLONG now = now_ft(); const ULONGLONG KEEP = 12ULL * 3600 * 10000000;
    if (cap > 0) ticket[0] = 0; if (idcap > 0) deviceId[0] = 0;
    if (!service || !service[0] || !ident_path(ini)) return 0;
    _snwprintf(key, 520, L"%s|%s", service, policy ? policy : L""); key[519] = 0; CharLowerW(key);
    AcquireSRWLockExclusive(&g_lk);
    if (!identity(ini)) goto done;
    MultiByteToWideChar(CP_UTF8, 0, g_puid, -1, deviceId, idcap);
    for (i = 0; i < g_ncache; i++) if (!lstrcmpW(g_cache[i].key, key) && g_cache[i].expiry > now) { lstrcpynW(ticket, g_cache[i].ticket, cap); ok = 1; goto done; }
    if (GetPrivateProfileStringW(L"tickets", key, L"", val, 9000, ini) && (comma = wcschr(val, L','))) {   /* another process got it already */
        ULONGLONG exp = _wcstoui64(val, NULL, 16); DWORD n = 0; char *p;
        if (exp > now && (p = (char *)unprotect_hex(comma + 1, &n))) { tk = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1); if (tk) { memcpy(tk, p, n); tk[n] = 0; } SecureZeroMemory(p, n); HeapFree(GetProcessHeap(), 0, p); now = exp - KEEP; }
    }
    if (!tk) {
        WCHAR *hex;
        if (GetEnvironmentVariableA("WXR_NO_DEVICE_ID", NULL, 0)) goto done;
        WideCharToMultiByte(CP_UTF8, 0, service, -1, svc, sizeof svc, NULL, NULL); WideCharToMultiByte(CP_UTF8, 0, policy ? policy : L"", -1, pol, sizeof pol, NULL, NULL);
        if (!(tk = request_ticket(svc, pol))) goto done;
        rt_log("device identity: new ticket for '%ls' (%d chars, kept 12 h)", key, (int)strlen(tk));
        if ((hex = protect_hex(tk, (DWORD)strlen(tk) + 1)) && lstrlenW(hex) < 8900) { _snwprintf(val, 9000, L"%llX,%s", now + KEEP, hex); WritePrivateProfileStringW(L"tickets", key, val, ini); }
        if (hex) HeapFree(GetProcessHeap(), 0, hex);
    }
    MultiByteToWideChar(CP_UTF8, 0, tk, -1, ticket, cap); ok = ticket[0] != 0;
    if (ok) { i = g_ncache < 8 ? g_ncache++ : 0; if (g_cache[i].ticket) HeapFree(GetProcessHeap(), 0, g_cache[i].ticket);
        lstrcpynW(g_cache[i].key, key, 520); g_cache[i].expiry = now + KEEP; g_cache[i].ticket = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(ticket) + 1) * sizeof(WCHAR)); if (g_cache[i].ticket) lstrcpyW(g_cache[i].ticket, ticket); else g_cache[i].expiry = 0; }
    SecureZeroMemory(tk, strlen(tk)); HeapFree(GetProcessHeap(), 0, tk);
done:
    ReleaseSRWLockExclusive(&g_lk);
    return ok;
}
