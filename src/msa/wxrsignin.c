/* wxrsignin.exe - WXR Sign-in: signs a Microsoft account in for WXR's Microsoft Store (the "Microsoft account sign-in" component).
 *
 * The OAuth 2.0 device-code flow against the consumer Microsoft-account service (login.live.com): this program shows a short code,
 * the user enters it at https://www.microsoft.com/link on any device, and Microsoft's own page does the password and two-step
 * verification. The account's password never passes through WXR. On success the account's REFRESH TOKEN is stored DPAPI-protected
 * (current Windows user only) in <root>\ProgramData\Identity\user.ini [usercred], the format src\rtstub\src\rt_msa.c reads; rt_msa
 * redeems it for the Store's user ticket and renews it silently from then on.
 *
 *   wxrsignin              sign in (replaces an account signed in before)
 *   wxrsignin --status     show whether an account is signed in
 *   wxrsignin --signout    forget the signed-in account
 * Root: the parent of this exe's folder (<root>\Layer\wxrsignin.exe), else %WXR_ROOT%.
 * Client 000000004C12AE6F (the Xbox app's consumer client) and scope service::www.microsoft.com::MBI_SSL: the combination that
 * issues the ticket the Store's order needs; rt_msa redeems the refresh token against the same client. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#define CLIENT "000000004C12AE6F"
#define SCOPE  "service::www.microsoft.com::MBI_SSL"

static WCHAR g_root[MAX_PATH], g_ini[MAX_PATH], g_identity[MAX_PATH];

/* POST application/x-www-form-urlencoded to login.live.com<path>; answer HeapAlloc'ed, NULL on failure */
static char *post_form(const WCHAR *path, const char *body, DWORD *status)
{
    HINTERNET ses, con = NULL, req = NULL; DWORD prot = 0x00000800 | 0x00000200 /* TLS 1.2 | 1.1: off by default in Win7's WinHTTP */, st = 0, sl = sizeof st, n, len = 0, cap = 0;
    char *buf = NULL, chunk[8192];
    *status = 0;
    if (!(ses = WinHttpOpen(L"WXR-Sign-in/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0))) return NULL;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot); WinHttpSetTimeouts(ses, 15000, 15000, 30000, 30000);
    if ((con = WinHttpConnect(ses, L"login.live.com", INTERNET_DEFAULT_HTTPS_PORT, 0)) &&
        (req = WinHttpOpenRequest(con, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) &&
        WinHttpSendRequest(req, L"Content-Type: application/x-www-form-urlencoded", (DWORD)-1, (void *)body, (DWORD)strlen(body), (DWORD)strlen(body), 0) &&
        WinHttpReceiveResponse(req, NULL)) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sl, WINHTTP_NO_HEADER_INDEX); *status = st;
        while (WinHttpReadData(req, chunk, sizeof chunk, &n) && n) {
            if (len + n + 1 > cap) { char *nb; cap = (len + n + 1) * 2; nb = buf ? (char *)HeapReAlloc(GetProcessHeap(), 0, buf, cap) : (char *)HeapAlloc(GetProcessHeap(), 0, cap); if (!nb) break; buf = nb; }
            memcpy(buf + len, chunk, n); len += n; }
        if (buf) buf[len] = 0;
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return buf;
}
static void free_secret(char *s) { if (s) { SecureZeroMemory(s, strlen(s)); HeapFree(GetProcessHeap(), 0, s); } }
/* the string value of JSON "key":"value" (\/ -> /); 1 if found */
static int json_str(const char *json, const char *key, char *out, int cap)
{
    char pat[64]; const char *p; int k = 0;
    _snprintf(pat, sizeof pat, "\"%s\"", key); pat[63] = 0; if (cap) out[0] = 0;
    if (!(p = strstr(json, pat))) return 0;
    p += strlen(pat); while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return 0;
    for (p++; *p && *p != '"' && k < cap - 1; p++) { if (p[0] == '\\' && p[1] == '/') { out[k++] = '/'; p++; } else out[k++] = *p; }
    out[k] = 0; return 1;
}
static int json_int(const char *json, const char *key, int def)
{
    char pat[64]; const char *p;
    _snprintf(pat, sizeof pat, "\"%s\"", key); pat[63] = 0;
    if (!(p = strstr(json, pat))) return def;
    p += strlen(pat); while (*p == ' ' || *p == ':' || *p == '"') p++;
    return (*p >= '0' && *p <= '9') ? atoi(p) : def;
}
/* DPAPI (current user) -> hex, as rt_msa.c's protect_hex */
static WCHAR *protect_hex(const void *data, DWORD n)
{
    DATA_BLOB in, out; WCHAR *h; DWORD i; in.pbData = (BYTE *)data; in.cbData = n;
    if (!CryptProtectData(&in, L"WXR device identity", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return NULL;
    if ((h = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (out.cbData * 2 + 1) * sizeof(WCHAR)))) { for (i = 0; i < out.cbData; i++) _snwprintf(h + i * 2, 3, L"%02X", out.pbData[i]); h[out.cbData * 2] = 0; }
    LocalFree(out.pbData); return h;
}

static int is_root(const WCHAR *dir)
{
    WCHAR ini[MAX_PATH], m[64];
    _snwprintf(ini, MAX_PATH, L"%ls\\environment.ini", dir); ini[MAX_PATH - 1] = 0;
    GetPrivateProfileStringW(L"wxr", L"marker", L"", m, 64, ini); return !wcscmp(m, L"WXR environment root");
}
static int find_root(void)
{
    WCHAR *s;
    GetModuleFileNameW(NULL, g_root, MAX_PATH);
    if ((s = wcsrchr(g_root, L'\\'))) *s = 0;          /* <root>\Layer */
    if ((s = wcsrchr(g_root, L'\\'))) *s = 0;          /* <root> */
    if (!is_root(g_root) && (!GetEnvironmentVariableW(L"WXR_ROOT", g_root, MAX_PATH) || !is_root(g_root))) return 0;
    _snwprintf(g_identity, MAX_PATH, L"%ls\\ProgramData\\Identity", g_root);
    _snwprintf(g_ini, MAX_PATH, L"%ls\\user.ini", g_identity);
    return 1;
}
static int signed_in(WCHAR *id, int cap)
{
    WCHAR tok[16];
    GetPrivateProfileStringW(L"user", L"Id", L"", id, cap, g_ini);
    GetPrivateProfileStringW(L"usercred", L"Token", L"", tok, 16, g_ini);
    return tok[0] != 0;
}
static void forget(void)
{
    WCHAR p[MAX_PATH];
    DeleteFileW(g_ini);
    _snwprintf(p, MAX_PATH, L"%ls\\user.import", g_identity); DeleteFileW(p);
}
static void copy_to_clipboard(const char *s)
{
    size_t n = strlen(s) + 1; HGLOBAL g;
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    if ((g = GlobalAlloc(GMEM_MOVEABLE, n))) { memcpy(GlobalLock(g), s, n); GlobalUnlock(g); if (!SetClipboardData(CF_TEXT, g)) GlobalFree(g); }
    CloseClipboard();
}

static int sign_in(void)
{
    char body[2048], dc[2048], uc[64], uri[256], *resp; DWORD st; int interval, expires, waited = 0;
    WCHAR wuri[256];

    printf("Asking Microsoft for a sign-in code...\n");
    _snprintf(body, sizeof body, "client_id=%s&scope=%s&response_type=device_code", CLIENT, SCOPE); body[sizeof body - 1] = 0;
    if (!(resp = post_form(L"/oauth20_connect.srf", body, &st))) { printf("\nNo answer from login.live.com (HTTP %lu). Check the internet connection and try again.\n", st); return 1; }
    if (!json_str(resp, "device_code", dc, sizeof dc) || !json_str(resp, "user_code", uc, sizeof uc)) {
        char e[128]; json_str(resp, "error", e, sizeof e);
        printf("\nMicrosoft did not give a sign-in code (HTTP %lu%s%s).\n", st, e[0] ? ", " : "", e); HeapFree(GetProcessHeap(), 0, resp); return 1; }
    json_str(resp, "verification_uri", uri, sizeof uri); if (!uri[0]) strcpy(uri, "https://www.microsoft.com/link");
    interval = json_int(resp, "interval", 5); expires = json_int(resp, "expires_in", 900);
    HeapFree(GetProcessHeap(), 0, resp);
    copy_to_clipboard(uc);

    printf("\n  1. Open  %s  in a browser, on this PC or on your phone.\n", uri);
    printf("  2. Enter this code:  %s   (it is on the clipboard too)\n", uc);
    printf("  3. Sign in with your Microsoft account there.\n\n");
    printf("  The code works for %d minutes. Your password is entered on Microsoft's page only; WXR never sees it.\n\n", expires / 60);
    MultiByteToWideChar(CP_UTF8, 0, uri, -1, wuri, 256);
    ShellExecuteW(NULL, L"open", wuri, NULL, NULL, SW_SHOWNORMAL);
    printf("Waiting for you to finish signing in");

    _snprintf(body, sizeof body, "client_id=%s&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=%s", CLIENT, dc); body[sizeof body - 1] = 0;
    for (;;) {
        char e[128] = "";
        if (interval < 1) interval = 5;
        Sleep(interval * 1000); waited += interval;
        if (!(resp = post_form(L"/oauth20_token.srf", body, &st))) { if (waited > expires) break; printf("!"); continue; }
        if (json_str(resp, "error", e, sizeof e) && (!strcmp(e, "authorization_pending") || !strcmp(e, "slow_down"))) {
            if (!strcmp(e, "slow_down")) interval += 5;
            HeapFree(GetProcessHeap(), 0, resp); printf("."); fflush(stdout);
            if (waited > expires) break;
            continue; }
        if (e[0]) {
            printf("\n\nSign-in did not complete (%s).\n", !strcmp(e, "authorization_declined") ? "you declined it" : !strcmp(e, "expired_token") ? "the code expired" : e);
            free_secret(resp); return 1; }
        {   /* signed in: keep the refresh token (DPAPI) and the account id; tickets of an account signed in before are dropped */
            char *rt = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 8192), id[160]; WCHAR wid[160], *hex;
            if (!rt || !json_str(resp, "refresh_token", rt, 8192) || !rt[0]) { printf("\n\nMicrosoft's answer has no refresh token; nothing was stored.\n"); free_secret(rt); free_secret(resp); return 1; }
            json_str(resp, "user_id", id, sizeof id); MultiByteToWideChar(CP_UTF8, 0, id, -1, wid, 160);
            free_secret(resp);
            CreateDirectoryW(g_identity, NULL);
            forget();
            if (!(hex = protect_hex(rt, (DWORD)strlen(rt) + 1))) { printf("\n\nCannot protect the token (DPAPI error %lu); nothing was stored.\n", GetLastError()); free_secret(rt); return 1; }
            free_secret(rt);
            WritePrivateProfileStringW(L"user", L"Id", wid, g_ini);
            WritePrivateProfileStringW(L"usercred", L"Token", hex, g_ini);
            WritePrivateProfileStringW(L"usercred", L"Expiry", L"0", g_ini);
            SecureZeroMemory(hex, lstrlenW(hex) * sizeof(WCHAR)); HeapFree(GetProcessHeap(), 0, hex);
            printf("\n\nSigned in (account %s).\n", id[0] ? id : "?");
            printf("If the Microsoft Store is open, close it and start it again.\n");
            return 0;
        }
    }
    printf("\n\nThe code expired before the sign-in was finished. Run WXR Sign-in again for a new code.\n");
    return 1;
}

int main(int argc, char **argv)
{
    WCHAR id[160], env[MAX_PATH], v[8]; int rc = 0; DWORD procs[4];
    int status = argc > 1 && !strcmp(argv[1], "--status"), signout = argc > 1 && !strcmp(argv[1], "--signout");
    SetConsoleTitleW(L"WXR Sign-in");
    printf("WXR Sign-in - a Microsoft account for WXR's Microsoft Store (experimental)\n\n");
    if (argc > 1 && !status && !signout) { printf("usage: wxrsignin [--status | --signout]\n"); return 2; }
    if (!find_root()) { printf("This program belongs in the Layer folder of a WXR installation (or set WXR_ROOT).\n"); rc = 1; goto done; }
    _snwprintf(env, MAX_PATH, L"%ls\\environment.ini", g_root);
    GetPrivateProfileStringW(L"components", L"msa", L"1", v, 8, env);
    if (v[0] == '0' && !signout) { printf("Microsoft account sign-in is not installed in %ls. Add it with WXR Setup (Change).\n", g_root); rc = 1; goto done; }
    if (signed_in(id, 160)) printf("Signed in now: account %ls\n", id[0] ? id : L"?"); else printf("No account is signed in.\n");
    if (status) goto done;
    if (signout) { forget(); printf("The account is forgotten: WXR's Store is signed out.\n"); goto done; }
    printf("\nWhat signing in does: the Store can show your library and install free apps and apps your account owns\n"
           "(some free apps still fail). Buying paid apps and showing the account in the Store do not work yet.\n\n");
    rc = sign_in();
done:
    if (GetConsoleProcessList(procs, 4) <= 1) { printf("\nPress Enter to close."); getchar(); }   /* own console window (Start menu) */
    return rc;
}
