/* store.inc - "wxrpkg store <ProductId | PackageFamilyName>": fetch a product's packages from Microsoft's delivery service and
 * install them (what Win10's InstallService does for the Store's Get / update, for free unencrypted packages). Part of wxrpkg.c.
 *
 * The chain:
 *   1. catalogue  GET displaycatalog.mp.microsoft.com/v7.0/products/<id>   (or .../products/lookup?alternateId=PackageFamilyName&Value=..)
 *                 -> "WuCategoryId", "PackageFamilyName", "ProductTitle"
 *   2. cookie     POST fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx  GetCookie -> <EncryptedData>
 *   3. sync       POST same URL, SyncUpdates filtered to the category: every package of the product and of its frameworks, all
 *                 versions and architectures. <NewUpdates><UpdateInfo> = identity + PackageMoniker (downloadable ones carry
 *                 <SecuredFragment>); <ExtendedUpdateInfo><Update> = their <File FileName Digest Size InstallerSpecificIdentifier>.
 *   4. file URL   POST .../client.asmx/secured  GetExtendedUpdateInfo2(UpdateID, RevisionNumber) -> <FileLocation><FileDigest><Url>
 *   5. download   plain HTTP from the CDN; the package's own block map is verified by the normal install (cmd_add).
 * No account and no device ticket is sent: the WindowsUpdateTicketsToken carries an empty <User />, which the service accepts for
 * free packages. Encrypted packages (.eappx / .emsix: paid or protected content) are never chosen; they need the Store's licensing.
 *
 * The SyncUpdates request body is <layer>\data\wu_sync_request.xml: WUIDRequest.xml of StoreLib (github.com/StoreDev/StoreLib,
 * MPL-2.0; licence in <layer>\licenses\storelib), a captured Windows Update client request with placeholders {0} cookie,
 * {1} category id, {2} ticket. It is data, kept unmodified.
 * Progress for a caller that drives this as a child process (--progress): lines "@state <word>", "@bytes <done> <total>". */
#include <winhttp.h>

#define FE3_URL L"https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx"
static int g_progress;
static void st_state(const char *s) { if (g_progress) { printf("@state %s\n", s); fflush(stdout); } }

/* ---- HTTP: GET or POST; the answer goes to *body (malloc, 0-terminated) or into the file `out` ---- */
typedef struct { ULONGLONG done, total; DWORD last; } StProg;
static int st_http(const WCHAR *url, const char *post, const WCHAR *ctype, char **body, DWORD *blen, HANDLE out, ULONGLONG expect, WCHAR *why) {
    URL_COMPONENTS uc; WCHAR host[256], path[4096]; HINTERNET ses = NULL, con = NULL, req = NULL; DWORD status = 0, sl = sizeof status, n, w, prot = 0x00000800 | 0x00000200;   /* TLS 1.2 | 1.1: off by default in Win7's WinHTTP */
    char *buf = NULL; DWORD cap = 0, len = 0; int ok = 0; StProg pg; BYTE chunk[65536];
    memset(&uc, 0, sizeof uc); uc.dwStructSize = sizeof uc; uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    memset(&pg, 0, sizeof pg); pg.total = expect;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) { _snwprintf(why, 256, L"bad URL %.80ls", url); return 0; }
    if (!(ses = WinHttpOpen(L"wxrpkg/" WXR_PKG_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0))) goto fail;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof prot);
    WinHttpSetTimeouts(ses, 30000, 30000, 60000, 120000);
    if (!(con = WinHttpConnect(ses, host, uc.nPort, 0))) goto fail;
    if (!(req = WinHttpOpenRequest(con, post ? L"POST" : L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0))) goto fail;
    if (ctype) { WCHAR h[200]; _snwprintf(h, 200, L"Content-Type: %ls", ctype); WinHttpAddRequestHeaders(req, h, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD); }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, post ? (void *)post : WINHTTP_NO_REQUEST_DATA, post ? (DWORD)strlen(post) : 0, post ? (DWORD)strlen(post) : 0, 0) || !WinHttpReceiveResponse(req, NULL)) goto fail;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sl, WINHTTP_NO_HEADER_INDEX);
    for (;;) {
        if (!WinHttpReadData(req, chunk, sizeof chunk, &n)) goto fail;
        if (!n) break;
        if (out) { if (!WriteFile(out, chunk, n, &w, NULL) || w != n) { _snwprintf(why, 256, L"cannot write the download (error %lu)", GetLastError()); goto done; }
                   pg.done += n; if (g_progress && GetTickCount() - pg.last > 250) { pg.last = GetTickCount(); printf("@bytes %llu %llu\n", pg.done, pg.total); fflush(stdout); } }
        else { if (len + n + 1 > cap) { cap = (len + n + 1) * 2; buf = (char *)realloc(buf, cap); if (!buf) { _snwprintf(why, 256, L"out of memory"); goto done; } } memcpy(buf + len, chunk, n); len += n; }
    }
    if (status != 200) { _snwprintf(why, 256, L"%ls answered HTTP %lu%hs%.120hs", host, status, buf && len ? ": " : "", buf && len ? (buf[len] = 0, buf) : ""); goto done; }
    if (out && g_progress) { printf("@bytes %llu %llu\n", pg.done, pg.total); fflush(stdout); }
    if (out && expect && pg.done != expect) { _snwprintf(why, 256, L"download is %llu bytes, expected %llu", pg.done, expect); goto done; }
    if (body) { if (!buf) buf = (char *)calloc(1, 1); else buf[len] = 0; *body = buf; buf = NULL; if (blen) *blen = len; }
    ok = 1; goto done;
fail:
    _snwprintf(why, 256, L"no connection to %ls (WinHTTP error %lu)", host, GetLastError());
done:
    free(buf); if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); if (ses) WinHttpCloseHandle(ses);
    return ok;
}

/* ---- small text helpers (the answers are searched as text: JSON for one value, XML whose inner XML arrives entity-encoded) ---- */
static void st_unescape(char *s) {   /* &lt; &gt; &quot; &apos; &amp; in place */
    char *d = s;
    while (*s) {
        if (*s == '&') {
            if (!strncmp(s, "&lt;", 4)) { *d++ = '<'; s += 4; continue; }   if (!strncmp(s, "&gt;", 4)) { *d++ = '>'; s += 4; continue; }
            if (!strncmp(s, "&quot;", 6)) { *d++ = '"'; s += 6; continue; } if (!strncmp(s, "&apos;", 6)) { *d++ = '\''; s += 6; continue; }
            if (!strncmp(s, "&amp;", 5)) { *d++ = '&'; s += 5; continue; }
        }
        *d++ = *s++;
    }
    *d = 0;
}
/* value of key="..." / "key":"..." starting at p and before end; 0 if absent */
static int st_val(const char *p, const char *end, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key); const char *q = p, *e;
    out[0] = 0;
    while ((q = strstr(q, key)) && (!end || q < end)) {
        const char *v = q + kl;
        if (*v == '"') v++;                 /* JSON: "key":"value" was searched as  key":"  */
        e = strchr(v, '"');
        if (e && (!end || e <= end) && (size_t)(e - v) < cap) { memcpy(out, v, e - v); out[e - v] = 0; return 1; }
        q += kl;
    }
    return 0;
}
static int st_between(const char *p, const char *a, const char *b, char *out, size_t cap) {
    const char *s = strstr(p, a), *e; out[0] = 0; if (!s) return 0; s += strlen(a); e = strstr(s, b);
    if (!e || (size_t)(e - s) >= cap) return 0; memcpy(out, s, e - s); out[e - s] = 0; return 1;
}
static char *st_read_file(const WCHAR *path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); DWORD sz, rd = 0; char *b;
    if (h == INVALID_HANDLE_VALUE) return NULL; sz = GetFileSize(h, NULL); b = (char *)malloc(sz + 1);
    if (b) { ReadFile(h, b, sz, &rd, NULL); b[rd] = 0; } CloseHandle(h); return b;
}
/* replaces the first "{n}" */
static char *st_subst(char *tpl, const char *mark, const char *val) {
    char *p = strstr(tpl, mark), *r; size_t a, vl = strlen(val);
    if (!p) return tpl; a = p - tpl; r = (char *)malloc(strlen(tpl) + vl + 1);
    memcpy(r, tpl, a); memcpy(r + a, val, vl); strcpy(r + a + vl, p + strlen(mark)); free(tpl); return r;
}
static void st_now(char *created, char *expires) {   /* 2017-08-05T02:03:05.038Z */
    SYSTEMTIME t; FILETIME f; ULARGE_INTEGER u;
    GetSystemTime(&t); sprintf(created, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    SystemTimeToFileTime(&t, &f); u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime; u.QuadPart += 5ULL * 60 * 10000000; f.dwLowDateTime = u.LowPart; f.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&f, &t); sprintf(expires, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}
#define ST_TICKET "<User />"   /* anonymous fallback: no account, no device */
#define ST_TPL_OS "10.0.16184.1001"   /* the OS version inside the captured requests */
static char g_st_ticket[9200] = ST_TICKET;   /* the WindowsUpdateTicketsToken <User> content: anonymous, or the account's own ticket (st_load_user_ticket) */
static const char ST_HEAD[] =
    "<s:Envelope xmlns:a=\"http://www.w3.org/2005/08/addressing\" xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\"><s:Header>"
    "<a:Action s:mustUnderstand=\"1\">http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/%s</a:Action>"
    "<a:MessageID>urn:uuid:%s</a:MessageID><a:To s:mustUnderstand=\"1\">https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx%s</a:To>"
    "<o:Security s:mustUnderstand=\"1\" xmlns:o=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
    "<Timestamp xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\"><Created>%s</Created><Expires>%s</Expires></Timestamp>"
    "<wuws:WindowsUpdateTicketsToken wsu:id=\"ClientMSA\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\" "
    "xmlns:wuws=\"http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization\"><TicketType Name=\"MSA\" Version=\"1.0\" Policy=\"MBI_SSL\">%s</TicketType>"
    "</wuws:WindowsUpdateTicketsToken></o:Security></s:Header><s:Body>";
static void st_uuid(char *out) { GUID g; CoCreateGuid(&g); sprintf(out, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]); }
static char *st_envelope(const char *action, const char *to_suffix, const char *body) {
    char created[40], expires[40], id[40], *r = (char *)malloc(sizeof ST_HEAD + strlen(body) + strlen(g_st_ticket) + 400);
    st_now(created, expires); st_uuid(id);
    sprintf(r, ST_HEAD, action, id, to_suffix, created, expires, g_st_ticket); strcat(r, body); strcat(r, "</s:Body></s:Envelope>"); return r;
}

/* ---- authenticated lookup: the account's own user ticket (optional) --------------------------------------------
 * When the signed-in account's USER ticket (service www.microsoft.com / policy MBI_SSL) is present, send it in the
 * WindowsUpdateTicketsToken so the delivery service returns packages the account is ENTITLED to - owned apps and
 * games that are not anonymously downloadable. The ticket is minted and cached by the layer (rt_msa.c) in the
 * user.ini seam; the installer only READS it here (DPAPI, current user) and never speaks the login protocol.
 * Absent or expired -> stays anonymous (<User />). The ticket is never logged (only its length) and only ever
 * leaves for the Microsoft delivery endpoint it authenticates to, exactly as the Windows Store client sends it. */
static void *st_unprotect_hex(const WCHAR *hex, DWORD *n) {
    DATA_BLOB in, out; DWORD len = (DWORD)lstrlenW(hex) / 2, i; BYTE *b, *r = NULL; unsigned v; *n = 0;
    if (!len || !(b = (BYTE *)malloc(len))) return NULL;
    for (i = 0; i < len; i++) { WCHAR t[3] = { hex[i * 2], hex[i * 2 + 1], 0 }; swscanf(t, L"%x", &v); b[i] = (BYTE)v; }
    in.pbData = b; in.cbData = len;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        if ((r = (BYTE *)malloc(out.cbData + 1))) { memcpy(r, out.pbData, out.cbData); r[out.cbData] = 0; *n = out.cbData; }
        LocalFree(out.pbData); }
    free(b); return r;
}
static void st_load_user_ticket(void) {
    WCHAR ini[MAX_PATH], val[9200], *comma; FILETIME ft; ULONGLONG now, exp; DWORD n = 0; char *t;
    lstrcpyA(g_st_ticket, ST_TICKET);
    _snwprintf(ini, MAX_PATH, L"%ls\\ProgramData\\Identity\\user.ini", g_root);
    if (!GetPrivateProfileStringW(L"userticketcache", L"www.microsoft.com|mbi_ssl", L"", val, 9200, ini) || !(comma = wcschr(val, L','))) return;
    *comma = 0; exp = _wcstoui64(val, NULL, 16);
    GetSystemTimeAsFileTime(&ft); now = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    if (exp && exp <= now) { dbg(L"store: the account's cached ticket has expired - anonymous lookup"); return; }
    t = (char *)st_unprotect_hex(comma + 1, &n);
    if (t && t[0] && strlen(t) < sizeof g_st_ticket - 20) { sprintf(g_st_ticket, "<User>%s</User>", t); dbg(L"store: authenticated lookup with the account ticket (%d chars)", (int)strlen(t)); }
    if (t) { SecureZeroMemory(t, n ? n : (DWORD)strlen(t)); free(t); }
}

/* ---- the product's packages ---- */
typedef struct { WCHAR moniker[400], name[256], arch[16]; ULONGLONG ver; char update[40], rev[12], digest[64]; WCHAR file[128]; ULONGLONG size; int encrypted, bundle; long rank; } StEnt;   /* rank: the service's PackageRank - higher = the newer release of the product */
typedef struct { StEnt *e; int n; WCHAR title[200], family[300], product[32]; } StList;
static void st_split(StEnt *e) {   /* Name_Version_Arch_ResourceId_PublisherId */
    WCHAR t[400], *p1, *p2, *p3; lstrcpynW(t, e->moniker, 400);
    if ((p1 = wcschr(t, '_')) && (p2 = wcschr(p1 + 1, '_')) && (p3 = wcschr(p2 + 1, '_'))) { *p1 = *p2 = *p3 = 0; lstrcpynW(e->name, t, 256); parse_ver(p1 + 1, &e->ver); lstrcpynW(e->arch, p2 + 1, 16); }
}
static int st_list(const WCHAR *what, StList *L, WCHAR *why) {
    WCHAR url[1024], tplpath[MAX_PATH], exe[MAX_PATH], *sl; char *cat = NULL, *ck = NULL, *req = NULL, *sync = NULL, *tpl = NULL, *p, wu[64], fam[300], title[400], pid[64], cookie[2048], created[40], expires[40]; int cap = 0, i;
    memset(L, 0, sizeof *L); st_state("lookup"); st_load_user_ticket();
    if (wcschr(what, '_')) _snwprintf(url, 1024, L"https://displaycatalog.mp.microsoft.com/v7.0/products/lookup?alternateId=PackageFamilyName&Value=%ls&market=US&languages=en-US,neutral&fieldsTemplate=InstallAgent", what);   /* without the template the lookup leaves the fulfilment data out */
    else _snwprintf(url, 1024, L"https://displaycatalog.mp.microsoft.com/v7.0/products/%ls?market=US&languages=en-US,neutral", what);
    if (!st_http(url, NULL, NULL, &cat, NULL, NULL, 0, why)) return 0;
    if (!st_val(cat, NULL, "\"WuCategoryId\":", wu, sizeof wu) || !wu[0]) { _snwprintf(why, 256, L"the Store catalogue has no downloadable package for %ls (not an app, or not found)", what); free(cat); return 0; }
    st_val(cat, NULL, "\"PackageFamilyName\":", fam, sizeof fam); st_val(cat, NULL, "\"ProductTitle\":", title, sizeof title); st_val(cat, NULL, "\"ProductId\":", pid, sizeof pid); free(cat);
    MultiByteToWideChar(CP_UTF8, 0, fam, -1, L->family, 300); MultiByteToWideChar(CP_UTF8, 0, title, -1, L->title, 200); MultiByteToWideChar(CP_UTF8, 0, pid, -1, L->product, 32);
    dbg(L"store: %ls = \"%ls\", family %ls, category %hs", what, L->title, L->family, wu);
    /* cookie */
    st_now(created, expires);
    { char body[600]; sprintf(body, "<GetCookie xmlns=\"http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService\"><oldCookie></oldCookie><lastChange>2015-10-21T17:01:07.1472913Z</lastChange><currentTime>%s</currentTime><protocolVersion>1.40</protocolVersion></GetCookie>", created);
      req = st_envelope("GetCookie", "", body); }
    if (!st_http(FE3_URL, req, L"application/soap+xml; charset=utf-8", &ck, NULL, NULL, 0, why)) { free(req); return 0; }
    free(req); req = NULL;
    if (!st_between(ck, "<EncryptedData>", "</EncryptedData>", cookie, sizeof cookie)) { _snwprintf(why, 256, L"the delivery service gave no cookie"); free(ck); return 0; }
    free(ck);
    /* sync */
    GetModuleFileNameW(NULL, exe, MAX_PATH); if ((sl = wcsrchr(exe, '\\'))) *sl = 0;
    _snwprintf(tplpath, MAX_PATH, L"%ls\\data\\wu_sync_request.xml", exe);
    if (!(tpl = st_read_file(tplpath))) { _snwprintf(why, 256, L"%ls is missing (part of the layer)", tplpath); return 0; }
    if (!strncmp(tpl, "\xEF\xBB\xBF", 3)) memmove(tpl, tpl + 3, strlen(tpl + 3) + 1);
    tpl = st_subst(tpl, "{0}", cookie); tpl = st_subst(tpl, "{1}", wu); tpl = st_subst(tpl, "{2}", g_st_ticket);
    /* the captured request says Windows 10.0.16184: the service then offers only packages that old build can run (Media Player
       stopped at 10.22031). Say what this environment provides instead. */
    { char osv[40]; WideCharToMultiByte(CP_UTF8, 0, g_osver, -1, osv, sizeof osv, NULL, NULL); while (strstr(tpl, ST_TPL_OS)) tpl = st_subst(tpl, ST_TPL_OS, osv); }
    st_state("sync");
    if (!st_http(FE3_URL, tpl, L"application/soap+xml; charset=utf-8", &sync, NULL, NULL, 0, why)) { free(tpl); return 0; }
    free(tpl); st_unescape(sync);
    for (p = sync; (p = strstr(p, "<UpdateInfo>")); ) {
        char *end = strstr(p, "</UpdateInfo>"), id[24], mon[400], tag[80], *x, *u; StEnt *e;
        if (!end) break;
        x = strstr(p, "<SecuredFragment>");
        if (x && x < end && st_between(p, "<ID>", "</ID>", id, sizeof id) && st_val(p, end, "PackageMoniker=", mon, sizeof mon) && (x = strstr(p, "<Xml><UpdateIdentity ")) && x < end) {
            if (L->n == cap) { cap = cap ? cap * 2 : 64; L->e = (StEnt *)realloc(L->e, cap * sizeof(StEnt)); }
            e = &L->e[L->n]; memset(e, 0, sizeof *e);
            MultiByteToWideChar(CP_UTF8, 0, mon, -1, e->moniker, 400); st_split(e);
            st_val(x, end, "UpdateID=", e->update, sizeof e->update); st_val(x, end, "RevisionNumber=", e->rev, sizeof e->rev);
            { char rk[16]; if (st_val(x, end, "PackageRank=", rk, sizeof rk)) e->rank = atol(rk); }
            /* its file: <Update><ID>id</ID> ... <File FileName= Digest= Size= > (the Abm_*.cab beside it is the block map) */
            sprintf(tag, "<Update><ID>%s</ID>", id);
            if ((u = strstr(sync, tag))) { char *ue = strstr(u, "</Update>"), *f = u, fn[128], sz[24];
                while (ue && (f = strstr(f, "<File ")) && f < ue) { char *fe = strchr(f, '>');
                    if (fe && st_val(f, fe, "FileName=", fn, sizeof fn) && _strnicmp(fn, "Abm_", 4)) {
                        const char *ext = strrchr(fn, '.');
                        MultiByteToWideChar(CP_UTF8, 0, fn, -1, e->file, 128); st_val(f, fe, "Digest=", e->digest, sizeof e->digest);
                        if (st_val(f, fe, "Size=", sz, sizeof sz)) e->size = _strtoui64(sz, NULL, 10);
                        e->encrypted = ext && (ext[1] == 'e' || ext[1] == 'E'); e->bundle = ext && strstr(ext, "bundle") != NULL; break; }
                    f += 6; } }
            if (e->file[0] && e->update[0]) L->n++;
        }
        p = end;
    }
    free(sync);
    for (i = 0; i < L->n; i++) dbg(L"store:   %ls  rank %ld  %ls  %llu bytes%ls", L->e[i].moniker, L->e[i].rank, L->e[i].file, L->e[i].size, L->e[i].encrypted ? L" (encrypted)" : L"");
    if (!L->n) { _snwprintf(why, 256, L"the delivery service lists no package for \"%ls\"", L->title); return 0; }
    return 1;
}
/* downloads entry e into dir; path of the file in out */
static int st_fetch(const StEnt *e, const WCHAR *dir, WCHAR *out, WCHAR *why) {
    char body[1800], *req, *resp = NULL, *p, url[2048] = ""; WCHAR wurl[2048]; HANDLE h; int ok;
    sprintf(body, "<GetExtendedUpdateInfo2 xmlns=\"http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService\"><updateIDs><UpdateIdentity><UpdateID>%s</UpdateID><RevisionNumber>%s</RevisionNumber></UpdateIdentity></updateIDs>"
                  "<infoTypes><XmlUpdateFragmentType>FileUrl</XmlUpdateFragmentType><XmlUpdateFragmentType>FileDecryption</XmlUpdateFragmentType></infoTypes>"
                  "<deviceAttributes>BranchReadinessLevel=CB;CurrentBranch=rs_prerelease;OEMModel=Virtual Machine;FlightRing=WIS;AttrDataVer=21;SystemManufacturer=Microsoft Corporation;InstallLanguage=en-US;OSUILocale=en-US;InstallationType=Client;FlightingBranchName=external;FirmwareVersion=Hyper-V UEFI Release v2.5;SystemProductName=Virtual Machine;OSSkuId=48;FlightContent=Branch;App=WU;OEMName_Uncleaned=Microsoft Corporation;AppVer=10.0.16184.1001;OSArchitecture=AMD64;SystemSKU=None;UpdateManagementGroup=2;IsFlightingEnabled=1;IsDeviceRetailDemo=0;TelemetryLevel=3;OSVersion=10.0.16184.1001;DeviceFamily=Windows.Desktop;</deviceAttributes></GetExtendedUpdateInfo2>", e->update, e->rev);
    { char osv[40], *b2 = _strdup(body); WideCharToMultiByte(CP_UTF8, 0, g_osver, -1, osv, sizeof osv, NULL, NULL); while (strstr(b2, ST_TPL_OS)) b2 = st_subst(b2, ST_TPL_OS, osv);
      req = st_envelope("GetExtendedUpdateInfo2", "/secured", b2); free(b2); }
    ok = st_http(FE3_URL L"/secured", req, L"application/soap+xml; charset=utf-8", &resp, NULL, NULL, 0, why); free(req);
    if (!ok) return 0;
    for (p = resp; (p = strstr(p, "<FileLocation>")); p += 14) { char dg[64], *end = strstr(p, "</FileLocation>");
        if (end && st_between(p, "<FileDigest>", "</FileDigest>", dg, sizeof dg) && !strcmp(dg, e->digest) && st_between(p, "<Url>", "</Url>", url, sizeof url)) break; url[0] = 0; }
    if (!url[0]) dbg(L"store: no FileLocation with digest %hs for update %hs rev %hs in: %.1500hs", e->digest, e->update, e->rev, resp);
    free(resp);
    if (!url[0]) { _snwprintf(why, 256, L"the delivery service gave no download address for %ls", e->moniker); return 0; }
    st_unescape(url); MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048);
    _snwprintf(out, MAX_PATH, L"%ls\\%ls", dir, e->file);
    if ((h = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) { _snwprintf(why, 256, L"cannot create %ls (error %lu)", out, GetLastError()); return 0; }
    info(L"Downloading %ls (%.1f MB)", e->moniker, e->size / 1048576.0);
    ok = st_http(wurl, NULL, NULL, NULL, NULL, h, e->size, why); CloseHandle(h);
    if (!ok) DeleteFileW(out);
    return ok;
}
/* newest unencrypted entry called `name` that x64 can use (bundles are "neutral"); minver 0 = any.
   Newest = highest PackageRank, then highest version: bundle version numbers are NOT comparable across a product's history
   (Media Player: 2019.22031.x is the old Groove Music, 11.2609.x the current app; rank 30153 against 3xxxx+). */
static const StEnt *st_pick(const StList *L, const WCHAR *name, ULONGLONG minver) {
    const StEnt *best = NULL; int i;
    for (i = 0; i < L->n; i++) { const StEnt *e = &L->e[i];
        if (_wcsicmp(e->name, name) || e->encrypted || !arch_ok(e->arch) || e->ver < minver) continue;
        if (!best || e->rank > best->rank || (e->rank == best->rank && e->ver > best->ver)) best = e; }
    return best;
}

static int cmd_add(WCHAR **paths, int npaths, WCHAR **dpaths, int ndpaths, int force_update, int any_os);
/* is this exact Store package what the user already has? (bundles: the record's Bundle field; plain packages: the record itself) */
static int st_have(const StEnt *m) {
    WCHAR *l = user_list(g_user), *s; int r = 0;
    for (s = l; *s && !r; s += wcslen(s) + 1) { WCHAR bn[400]; rec_get(s, L"Bundle", bn, 400); if (!_wcsicmp(bn, m->moniker) || !_wcsicmp(s, m->moniker)) r = 1; }
    free(l); return r;
}
/* after an install: start menu entries, icons and display names (wxr sync, beside this exe in the layer) */
static void st_shell_sync(void) {
    static int done; WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16], *sl; STARTUPINFOW si; PROCESS_INFORMATION pi;
    if (done) return; done = 1;   /* once per run: wxrpkg store installs through cmd_add, which syncs already */
    GetModuleFileNameW(NULL, exe, MAX_PATH); if (!(sl = wcsrchr(exe, '\\'))) return; lstrcpyW(sl + 1, L"wxr.exe"); if (!exists(exe)) return;
    _snwprintf(cmd, MAX_PATH + 15, L"\"%ls\" sync", exe); memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { WaitForSingleObject(pi.hProcess, 60000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}
/* wxrpkg store <ProductId | PackageFamilyName> [--download-only | --check]
   --progress also prints "@product <id>", "@family <name>", "@package <full name>", "@size <bytes>", "@update yes|no" (no = the user has it). */
static int cmd_store(const WCHAR *what, int download_only, int force_update, int any_os) {
    StList L; WCHAR why[256] = L"", dir[MAX_PATH], depdir[MAX_PATH], tmp[MAX_PATH], mainfile[MAX_PATH], f[MAX_PATH], name[256], *us; const StEnt *m; Pkg *b; int nb = 0, i, j, rc = 1, nfw = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!st_list(what, &L, why)) { err(L"%ls", why); st_state("failed"); return 1; }
    lstrcpynW(name, L.family, 256); if ((us = wcsrchr(name, '_'))) *us = 0;          /* family = Name_PublisherId */
    if (!(m = st_pick(&L, name, 0))) { err(L"\"%ls\" has no package this environment can install (only encrypted or other-architecture packages are offered)", L.title); st_state("failed"); free(L.e); return 1; }
    info(L"%ls: %ls", L.title, m->moniker);
    { int have = st_have(m);
      if (g_progress) { printf("@product %ls\n@family %ls\n@package %ls\n@size %llu\n@update %s\n", L.product, L.family, m->moniker, m->size, have ? "no" : "yes"); fflush(stdout); }
      if (have && !force_update) { info(L"%ls is installed and up to date", L.title); st_state("done"); free(L.e); return 0; }
      if (download_only == 2) { info(L"%ls can be installed or updated", L.title); st_state("done"); free(L.e); return 0; } }   /* --check */
    _snwprintf(dir, MAX_PATH, L"%ls\\Temp\\store-%lu", g_root, GetCurrentProcessId()); _snwprintf(depdir, MAX_PATH, L"%ls\\deps", dir); _snwprintf(tmp, MAX_PATH, L"%ls\\x", dir);
    mkdirs(depdir); mkdirs(tmp);
    st_state("download");
    if (!st_fetch(m, dir, mainfile, why)) { err(L"%ls", why); goto out; }
    /* frameworks the package names and the environment does not have yet */
    b = (Pkg *)calloc(MAXB, sizeof(Pkg));
    if (!load_input(mainfile, 0, 0, tmp, b, &nb, why)) { err(L"%ls", why); free(b); goto out; }
    for (i = 0; i < nb; i++) for (j = 0; j < b[i].ndeps; j++) { const Dep *d = &b[i].deps[j]; WCHAR have[400]; ULONGLONG mv = 0; const StEnt *fe;
        if (resolve_dep(d, NULL, 0, have)) continue;
        parse_ver(d->minver, &mv);
        if (!(fe = st_pick(&L, d->name, mv))) { warn(L"%ls needs %ls %ls or newer, which the Store does not offer with it", b[i].fullname, d->name, d->minver); continue; }
        _snwprintf(f, MAX_PATH, L"%ls\\%ls", depdir, fe->file); if (exists(f)) continue;
        if (!st_fetch(fe, depdir, f, why)) { err(L"%ls", why); free(b); goto out; } nfw++; }
    free(b);
    if (download_only) { info(L"Downloaded to %ls", dir); st_state("done"); free(L.e); return 0; }
    st_state("install");
    { WCHAR *paths[1], *deps[1]; paths[0] = mainfile; deps[0] = depdir; rc = cmd_add(paths, 1, deps, nfw ? 1 : 0, force_update, any_os); }
out:
    rm_tree(dir); free(L.e);
    if (!rc) st_shell_sync();
    st_state(rc ? "failed" : "done");
    return rc;
}
