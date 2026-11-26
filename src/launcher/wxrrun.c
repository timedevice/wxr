/* wxrrun.c - launcher / diagnostic host for a UWP app on Windows 7.
 *
 *   wxrrun.exe <path\to\App.exe> [arguments]
 *
 * Debug launch of a WXR app (pristine files, loader layer): wxrrun [--root <dir>] <exe | AUMID> [args]. Starts the app as a debuggee (so crashes are caught with module+offset, registers and a stack scan), logs every DLL it
 * loads, OutputDebugString text and exceptions, and writes everything to wxrrun.log beside this exe.
 * The app's folder is its working directory. This is also where package identity will be set up later.
 * Run with VxKex enabled for the *app exe* (Properties > VxKex).
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

static FILE *g_log;
static void out(const char *f, ...)
{
    char b[2048]; va_list a; va_start(a, f); _vsnprintf(b, sizeof b - 1, f, a); va_end(a); b[sizeof b - 1] = 0;
    fputs(b, stdout); fflush(stdout);
    if (g_log) { fputs(b, g_log); fflush(g_log); }
}

typedef struct Mod { ULONG_PTR base; DWORD size; char name[64]; } Mod;
static Mod g_mods[512]; static int g_nmods;

static void add_module(HANDLE proc, ULONG_PTR base, HANDLE hfile, const char *fallback)
{
    Mod *m; IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; SIZE_T n; WCHAR path[MAX_PATH] = L""; char name[64] = "?";
    if (g_nmods >= 512) return;
    m = &g_mods[g_nmods]; m->base = base; m->size = 0x100000;
    if (ReadProcessMemory(proc, (void *)base, &dos, sizeof dos, &n) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        ReadProcessMemory(proc, (void *)(base + dos.e_lfanew), &nt, sizeof nt, &n))
        m->size = nt.OptionalHeader.SizeOfImage;
    if (hfile && GetFinalPathNameByHandleW(hfile, path, MAX_PATH, 0)) {
        WCHAR *b = wcsrchr(path, L'\\'); WideCharToMultiByte(CP_ACP, 0, b ? b + 1 : path, -1, name, sizeof name, NULL, NULL);
    } else if (fallback) lstrcpynA(name, fallback, sizeof name);
    lstrcpynA(m->name, name, sizeof m->name);
    g_nmods++;
}
static const char *desc(ULONG_PTR a, char *buf, size_t cap)
{
    int i; for (i = 0; i < g_nmods; i++) if (a >= g_mods[i].base && a < g_mods[i].base + g_mods[i].size) { snprintf(buf, cap, "%s+0x%lX", g_mods[i].name, (unsigned long)(a - g_mods[i].base)); return buf; }
    snprintf(buf, cap, "0x%llX", (unsigned long long)a); return buf;
}
static int in_module(ULONG_PTR a) { int i; for (i = 0; i < g_nmods; i++) if (a >= g_mods[i].base && a < g_mods[i].base + g_mods[i].size) return 1; return 0; }

static void dump_chain(HANDLE proc, HANDLE th, DWORD tid, ULONG_PTR rsp, ULONG_PTR rip);
static void dump_context(HANDLE proc, DWORD tid, int deep)
{
    HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid); CONTEXT c; char d[160]; ULONG_PTR stk[96]; SIZE_T n; int i, shown = 0;
    if (!th) return;
    memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(th, &c)) {
        out("      rip=%s rax=%llX rcx=%llX rdx=%llX r8=%llX r9=%llX rsp=%llX\n", desc((ULONG_PTR)c.Rip, d, sizeof d),
            (unsigned long long)c.Rax, (unsigned long long)c.Rcx, (unsigned long long)c.Rdx, (unsigned long long)c.R8, (unsigned long long)c.R9, (unsigned long long)c.Rsp);
        if (deep && ReadProcessMemory(proc, (void *)c.Rsp, stk, sizeof stk, &n)) {
            out("      stack scan:");
            for (i = 0; i < (int)(n / 8) && shown < 12; i++) if (in_module(stk[i])) { out(" %s", desc(stk[i], d, sizeof d)); shown++; }
            out("\n");
        }
        if (deep) dump_chain(proc, th, tid, (ULONG_PTR)c.Rsp, (ULONG_PTR)c.Rip);
    }
    CloseHandle(th);
}


/* does a return address look like it follows a call instruction? */
static int after_call(HANDLE proc, ULONG_PTR a)
{
    BYTE b[8]; SIZE_T n;
    if (!ReadProcessMemory(proc, (void *)(a - 8), b, 8, &n) || n != 8) return 0;
    if (b[3] == 0xE8) return 1;                                   /* call rel32 */
    if (b[6] == 0xFF && (b[7] & 0x38) == 0x10) return 1;          /* call r/m, 2 bytes */
    if (b[5] == 0xFF && (b[6] & 0x38) == 0x10) return 1;          /* call [reg+disp8]/sib, 3 bytes */
    if (b[4] == 0xFF && (b[5] & 0x38) == 0x10) return 1;
    if (b[2] == 0xFF && (b[3] & 0x38) == 0x10) return 1;
    if (b[1] == 0xFF && (b[2] & 0x38) == 0x10) return 1;          /* call [reg+disp32] etc. */
    return 0;
}
typedef LONG (NTAPI *PFN_NtQIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);
/* walks the whole used stack: prints the return-address chain (with repeats folded) and the most frequent callers */
static void dump_chain(HANDLE proc, HANDLE th, DWORD tid, ULONG_PTR rsp, ULONG_PTR rip)
{
    PFN_NtQIT q = (PFN_NtQIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    struct { LONG st; PVOID teb; ULONG_PTR pid, tid, aff; LONG prio, bprio; } tbi; ULONG_PTR base = 0, limit = 0, top; SIZE_T n; BYTE code[32];
    static ULONG_PTR *buf; static ULONG_PTR chain[200000]; int nc = 0, i, j; char d[160];
    struct { ULONG_PTR a; int c; } hist[64]; int nh = 0;
    memset(&tbi, 0, sizeof tbi);
    if (q && q(th, 0, &tbi, sizeof tbi, NULL) == 0 && tbi.teb) { ReadProcessMemory(proc, (BYTE *)tbi.teb + 8, &base, 8, &n); ReadProcessMemory(proc, (BYTE *)tbi.teb + 16, &limit, 8, &n); }
    out("      thread %lu stack: base=%llX limit=%llX rsp=%llX (used %llu KB)\n", tid, (unsigned long long)base, (unsigned long long)limit, (unsigned long long)rsp, base > rsp ? (unsigned long long)((base - rsp) / 1024) : 0ull);
    if (ReadProcessMemory(proc, (void *)rip, code, sizeof code, &n)) { out("      code at rip:"); for (i = 0; i < (int)n; i++) out(" %02X", code[i]); out("\n"); }
    top = base > rsp ? base : rsp + 0x100000;
    if (top - rsp > 0x400000) top = rsp + 0x400000;
    buf = (ULONG_PTR *)malloc((size_t)(top - rsp));
    if (!buf) return;
    if (!ReadProcessMemory(proc, (void *)rsp, buf, (size_t)(top - rsp), &n)) { out("      (cannot read the stack)\n"); free(buf); return; }
    for (i = 0; i < (int)(n / 8) && nc < 200000; i++) if (in_module(buf[i]) && after_call(proc, buf[i])) chain[nc++] = buf[i];
    out("      %d call-return addresses on the stack; innermost first (repeating cycles folded; first 40 and last 60 lines shown):\n", nc);
    {
        static char lines[6000][200]; int nl = 0, from;
        for (i = 0; i < nc && nl < 6000; ) {
            int per = 0, rep = 1; char *L = lines[nl];
            for (j = 1; j <= 12 && i + 2 * j <= nc; j++) { int k, same = 1; for (k = 0; k < j; k++) if (chain[i + k] != chain[i + j + k]) { same = 0; break; } if (same) { per = j; break; } }
            if (per) { int off = 0; while (i + (rep + 1) * per <= nc) { int k, same = 1; for (k = 0; k < per; k++) if (chain[i + k] != chain[i + rep * per + k]) { same = 0; break; } if (!same) break; rep++; }
                off += snprintf(L + off, 200 - off, "        [");
                for (j = 0; j < per && off < 150; j++) off += snprintf(L + off, 200 - off, "%s%s", j ? " <- " : "", desc(chain[i + j], d, sizeof d));
                snprintf(L + off, 200 - off, "]  x%d", rep); i += rep * per; }
            else { snprintf(L, 200, "        %s", desc(chain[i], d, sizeof d)); i++; }
            nl++;
        }
        for (i = 0; i < nl && i < 40; i++) out("%s\n", lines[i]);
        from = nl - 60; if (from < 40) from = 40;
        if (from > 40) out("        ... %d lines omitted ...\n", from - 40);
        for (i = from; i < nl; i++) out("%s\n", lines[i]);
    }
    for (i = 0; i < nc; i++) { for (j = 0; j < nh; j++) if (hist[j].a == chain[i]) { hist[j].c++; break; } if (j == nh && nh < 64) { hist[nh].a = chain[i]; hist[nh].c = 1; nh++; } }
    for (i = 0; i < nh; i++) for (j = i + 1; j < nh; j++) if (hist[j].c > hist[i].c) { ULONG_PTR ta = hist[i].a; int tc = hist[i].c; hist[i] = hist[j]; hist[j].a = ta; hist[j].c = tc; }
    out("      most frequent callers:"); for (i = 0; i < nh && i < 10; i++) out(" %s x%d", desc(hist[i].a, d, sizeof d), hist[i].c); out("\n");
    free(buf);
}

/* (The subsystem-version check is bypassed in memory at process creation: wxrlaunch.inc hk_ncup. Files are never modified.) */

/* one-shot breakpoints: WXR_BP="SharedLibrary.dll:8FEF50,mrt100_app.dll:1234" - logs registers + return address at the first hit */
static struct { char mod[64]; ULONG_PTR rva, addr; unsigned char orig; int armed, hascond, trace, nhit, tracert, isret; unsigned long long sr8, sr9, srdx; unsigned long long cond; } g_bp[160]; static int g_nbp; static int g_rearm = -1; static DWORD g_rearm_tid;
static void bp_parse(void) {
    char *e = getenv("WXR_BP"), *p, buf[1024]; if (!e) return; lstrcpynA(buf, e, sizeof buf);
    for (p = strtok(buf, ","); p && g_nbp < 80; p = strtok(NULL, ",")) { char *c = strchr(p, ':'); if (!c) continue; *c = 0; lstrcpynA(g_bp[g_nbp].mod, p, 64); g_bp[g_nbp].rva = (ULONG_PTR)strtoull(c + 1, NULL, 16); { char *q = strchr(c + 1, '*'); if (q) g_bp[g_nbp].trace = 1; q = strchr(c + 1, '^'); if (q) { g_bp[g_nbp].trace = 1; g_bp[g_nbp].tracert = 1; } } { char *q = strchr(c + 1, '='); if (q) { g_bp[g_nbp].hascond = 1; g_bp[g_nbp].cond = strtoull(q + 1, NULL, 16); } } g_nbp++; }
}
static void bp_arm(HANDLE proc, ULONG_PTR base, const char *name) {
    int i; for (i = 0; i < g_nbp; i++) if (!g_bp[i].armed && !_stricmp(g_bp[i].mod, name)) { SIZE_T r; unsigned char c = 0xCC; g_bp[i].addr = base + g_bp[i].rva;
        ReadProcessMemory(proc, (void *)g_bp[i].addr, &g_bp[i].orig, 1, &r); WriteProcessMemory(proc, (void *)g_bp[i].addr, &c, 1, &r); FlushInstructionCache(proc, NULL, 0); g_bp[i].armed = 1; out("  [bp]   armed %s+%lX\n", name, (unsigned long)g_bp[i].rva); }
}
static const char *read_hstr(HANDLE proc, unsigned long long hs, wchar_t *buf, int cap) {
    unsigned long long hdr[3] = {0}; SIZE_T r; unsigned int len; buf[0] = 0; if (hs < 0x10000 || !ReadProcessMemory(proc, (void *)hs, hdr, 24, &r)) return "";
    len = (unsigned int)(hdr[0] >> 32); if (len == 0 || len > 200) return ""; if (!ReadProcessMemory(proc, (void *)hdr[2], buf, len * 2, &r)) return ""; buf[len] = 0; if (buf[0] < 0x20 || buf[0] > 0x7e) { buf[0] = 0; return ""; } return "ok";
}static int bp_hit(HANDLE proc, DWORD tid, ULONG_PTR addr) {
    int i; for (i = 0; i < g_nbp; i++) if (g_bp[i].armed == 1 && g_bp[i].addr == addr) {
        HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); CONTEXT cx; SIZE_T r; ULONG_PTR ret = 0;
        if (g_bp[i].isret) { unsigned char o2 = g_bp[i].orig; WriteProcessMemory(proc, (void *)addr, &o2, 1, &r); FlushInstructionCache(proc, NULL, 0); g_bp[i].armed = 2; memset(&cx, 0, sizeof cx); cx.ContextFlags = CONTEXT_FULL;
            if (th && GetThreadContext(th, &cx)) { { unsigned long long o8 = 0, o9 = 0, od = 0; SIZE_T rr; if (g_bp[i].sr8 > 0x10000) ReadProcessMemory(proc, (void *)g_bp[i].sr8, &o8, 8, &rr); if (g_bp[i].sr9 > 0x10000) ReadProcessMemory(proc, (void *)g_bp[i].sr9, &o9, 8, &rr); if (g_bp[i].srdx > 0x10000) ReadProcessMemory(proc, (void *)g_bp[i].srdx, &od, 8, &rr);
              { wchar_t hv[220]; read_hstr(proc, od, hv, 210); out("  [ret]  %s+%lX returned rax=%llX (tid %lu)  *rdx=%llX *r8=%llX *r9=%llX  hstr(*rdx)=\"%ls\"\n", g_bp[i].mod, (unsigned long)g_bp[i].rva, (unsigned long long)cx.Rax, (unsigned long)tid, od, o8, o9, hv); } } cx.Rip = addr; SetThreadContext(th, &cx); } if (th) CloseHandle(th); return 1; } unsigned char o = g_bp[i].orig; memset(&cx, 0, sizeof cx); cx.ContextFlags = CONTEXT_FULL;
        WriteProcessMemory(proc, (void *)addr, &o, 1, &r); FlushInstructionCache(proc, NULL, 0);
        if (g_bp[i].trace && th && GetThreadContext(th, &cx)) { ULONG_PTR ret2 = 0; ReadProcessMemory(proc, (void *)cx.Rsp, &ret2, 8, &r); if (g_bp[i].nhit++ < 400) { wchar_t w1[220], w2[220], w3[220]; read_hstr(proc, cx.Rdx, w1, 210); read_hstr(proc, cx.R8, w2, 210); read_hstr(proc, cx.R9, w3, 210); { int q; unsigned long long sv[8]; SIZE_T r7; if (ReadProcessMemory(proc, (void *)(cx.Rsp + 8), sv, 64, &r7)) for (q = 0; q < 8; q++) { wchar_t ws[220]; read_hstr(proc, sv[q], ws, 210); if (ws[0]) out("  [str]  stack[%d]=\"%ls\"\n", q, ws); } }
            if (w1[0] || w2[0] || w3[0]) out("  [str]  %s+%lX rdx=\"%ls\" r8=\"%ls\" r9=\"%ls\"\n", g_bp[i].mod, (unsigned long)g_bp[i].rva, w1, w2, w3); }
            out("  [trc]  %s+%lX rax=%llX rcx=%llX rdx=%llX r8=%llX r9=%llX ret=%p tid=%lu t=%lu\n", g_bp[i].mod, (unsigned long)g_bp[i].rva, (unsigned long long)cx.Rax, (unsigned long long)cx.Rcx, (unsigned long long)cx.Rdx, (unsigned long long)cx.R8, (unsigned long long)cx.R9, (void *)ret2, (unsigned long)tid, (unsigned long)GetTickCount());
            if (g_bp[i].tracert && g_nbp < 150 && ret2 > 0x10000) { int j = g_nbp++; SIZE_T r3; unsigned char c3 = 0xCC; memset(&g_bp[j], 0, sizeof g_bp[j]); lstrcpynA(g_bp[j].mod, g_bp[i].mod, 64); g_bp[j].rva = g_bp[i].rva; g_bp[j].addr = ret2; g_bp[j].isret = 1; g_bp[j].armed = 1; g_bp[j].sr8 = cx.R8; g_bp[j].sr9 = cx.R9; g_bp[j].srdx = cx.Rdx; ReadProcessMemory(proc, (void *)ret2, &g_bp[j].orig, 1, &r3); WriteProcessMemory(proc, (void *)ret2, &c3, 1, &r3); }
            if (g_bp[i].nhit <= 40 && getenv("WXR_PEEK")) {   /* WXR_PEEK="rcx+458,rbx+228": the qword at register+hexoffset (and, if a pointer, the qword it points to) */
                char pk[256], *s2; lstrcpynA(pk, getenv("WXR_PEEK"), sizeof pk);
                for (s2 = strtok(pk, ","); s2; s2 = strtok(NULL, ",")) { const char *rn = s2; char *pl = strchr(s2, '+'); unsigned long long rv = 0, v = 0, v2 = 0; SIZE_T rq;
                    if (!pl) continue; *pl = 0;
                    if (!_stricmp(rn, "rax")) rv = cx.Rax; else if (!_stricmp(rn, "rbx")) rv = cx.Rbx; else if (!_stricmp(rn, "rcx")) rv = cx.Rcx; else if (!_stricmp(rn, "rdx")) rv = cx.Rdx;
                    else if (!_stricmp(rn, "rsi")) rv = cx.Rsi; else if (!_stricmp(rn, "rdi")) rv = cx.Rdi; else if (!_stricmp(rn, "rsp")) rv = cx.Rsp; else if (!_stricmp(rn, "rbp")) rv = cx.Rbp;
                    else if (!_stricmp(rn, "r8")) rv = cx.R8; else if (!_stricmp(rn, "r9")) rv = cx.R9; else if (!_stricmp(rn, "r10")) rv = cx.R10; else if (!_stricmp(rn, "r11")) rv = cx.R11; else if (!_stricmp(rn, "r12")) rv = cx.R12; else if (!_stricmp(rn, "r13")) rv = cx.R13; else if (!_stricmp(rn, "r14")) rv = cx.R14; else if (!_stricmp(rn, "r15")) rv = cx.R15;
                    rv += strtoull(pl + 1, NULL, 16);
                    if (rv >= 0x10000 && ReadProcessMemory(proc, (void *)rv, &v, 8, &rq)) { char dd[200]; if (v >= 0x10000) ReadProcessMemory(proc, (void *)v, &v2, 8, &rq);
                        out("  [peek] %s+%s = %llX  (-> %llX %s)\n", rn, pl + 1, v, v2, v2 >= 0x10000 ? desc((ULONG_PTR)v2, dd, sizeof dd) : ""); }
                    else out("  [peek] %s+%s unreadable\n", rn, pl + 1); } }
            if (g_bp[i].nhit <= 40 && getenv("WXR_HANDLETYPE")) {   /* rcx as a handle of the target: its object type and, for timers/events, the signal state */
                HANDLE dup = NULL; typedef LONG (WINAPI *PNQO)(HANDLE, int, void *, ULONG, ULONG *); static PNQO nqo;
                if (!nqo) nqo = (PNQO)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
                if (cx.Rcx && DuplicateHandle(proc, (HANDLE)cx.Rcx, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                    BYTE ti[512]; ULONG rl = 0; DWORD w = WaitForSingleObject(dup, 0);
                    if (nqo && nqo(dup, 2 /*ObjectTypeInformation*/, ti, sizeof ti, &rl) >= 0) { USHORT ulen = *(USHORT *)ti; const wchar_t *ubuf = *(const wchar_t **)(ti + 8); out("  [handle] %llX type %.*ls signaled=%d\n", (unsigned long long)cx.Rcx, ulen / 2, ubuf, w == WAIT_OBJECT_0); }
                    CloseHandle(dup); } else out("  [handle] %llX: DuplicateHandle failed %lu\n", (unsigned long long)cx.Rcx, GetLastError()); }
            if (g_bp[i].nhit <= 40 && getenv("WXR_DUMPHEX")) {   /* raw bytes at rcx/rdx/r8/r9 (strings that are not plain ASCII, empty tokens ...) */
                unsigned long long rr[4] = { cx.Rcx, cx.Rdx, cx.R8, cx.R9 }; const char *nm4[4] = { "rcx", "rdx", "r8", "r9" }; int q4, b4;
                for (q4 = 0; q4 < 4; q4++) { unsigned char hb[192]; SIZE_T got = 0; char line[400]; int n4 = 0;
                    if (rr[q4] < 0x10000 || !ReadProcessMemory(proc, (void *)rr[q4], hb, sizeof hb, &got)) continue;
                    for (b4 = 0; b4 < 192; b4++) n4 += sprintf(line + n4, "%02X", hb[b4]); out("  [hex]  %s: %s\n", nm4[q4], line); } }
            if (g_bp[i].nhit <= 40 && getenv("WXR_DUMPVT")) {   /* rcx's first 12 qwords: each one that points to an object with a vtable inside a module -> module+offset of that vtable */
                unsigned long long f[12]; SIZE_T gr; int k;
                if (cx.Rcx >= 0x10000 && ReadProcessMemory(proc, (void *)cx.Rcx, f, sizeof f, &gr))
                    for (k = 0; k < 12; k++) { unsigned long long vt = 0; SIZE_T g2; char d1[200], d2[200];
                        if (f[k] < 0x10000 || !ReadProcessMemory(proc, (void *)f[k], &vt, 8, &g2) || vt < 0x10000) continue;
                        desc((ULONG_PTR)vt, d1, sizeof d1); desc((ULONG_PTR)f[k], d2, sizeof d2);
                        if (strchr(d1, '+')) out("  [vt]   rcx+%02X -> %s : vtable %s\n", k * 8, d2, d1); } }
            { const char *sv = getenv("WXR_STACK_IF_EDX"); static int nmatch2;
              if (sv && (unsigned long)(cx.Rdx & 0xFFFFFFFFu) == strtoul(sv, NULL, 16) && ++nmatch2 <= 6) { out("  [edx-match] %s\n", sv); CloseHandle(th); dump_context(proc, tid, 1); th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); GetThreadContext(th, &cx); } }
            { const char *sv = getenv("WXR_STACK_IF_EAX"); static int nmatch3;   /* a return site traced with '*': stack only when eax (the HRESULT just returned) matches */
              if (sv && (unsigned long)(cx.Rax & 0xFFFFFFFFu) == strtoul(sv, NULL, 16) && ++nmatch3 <= 6) { out("  [eax-match] %s\n", sv); CloseHandle(th); dump_context(proc, tid, 1); th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); GetThreadContext(th, &cx); } }
            { const char *sv = getenv("WXR_STACK_IF_ECX"); static int nmatch;   /* stack only for hits whose ecx (an HRESULT argument) matches, first 6 */
              if (sv && (unsigned long)(cx.Rcx & 0xFFFFFFFFu) == strtoul(sv, NULL, 16) && ++nmatch <= 6) { out("  [ecx-match] %s\n", sv); CloseHandle(th); dump_context(proc, tid, 1); th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); GetThreadContext(th, &cx); } }
            if (g_bp[i].nhit <= (getenv("WXR_TRACE_STACK_MAX") ? atoi(getenv("WXR_TRACE_STACK_MAX")) : 3) && getenv("WXR_TRACE_STACK")) { CloseHandle(th); dump_context(proc, tid, 1); th = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); GetThreadContext(th, &cx); }
            cx.Rip = addr; cx.EFlags |= 0x100; SetThreadContext(th, &cx); g_rearm = i; g_rearm_tid = tid; CloseHandle(th); return 1; }
        if (th && GetThreadContext(th, &cx) && g_bp[i].hascond && (cx.Rax & 0xffffffffull) != g_bp[i].cond) { cx.Rip = addr; cx.EFlags |= 0x100; SetThreadContext(th, &cx); g_rearm = i; g_rearm_tid = tid; CloseHandle(th); return 1; }
        g_bp[i].armed = 2;
        if (th && GetThreadContext(th, &cx)) { ReadProcessMemory(proc, (void *)cx.Rsp, &ret, 8, &r); out("  [bp]   HIT %s+%lX rcx=%llX rdx=%llX r8=%llX r9=%llX rax=%llX rdi=%llX rsi=%llX rbx=%llX ret=%p\n", g_bp[i].mod, (unsigned long)g_bp[i].rva, (unsigned long long)cx.Rcx, (unsigned long long)cx.Rdx, (unsigned long long)cx.R8, (unsigned long long)cx.R9, (unsigned long long)cx.Rax, (unsigned long long)cx.Rdi, (unsigned long long)cx.Rsi, (unsigned long long)cx.Rbx, (void *)ret); { ULONG_PTR vt = 0, s0 = 0; SIZE_T r5; if (cx.Rdx > 0x10000 && ReadProcessMemory(proc, (void *)cx.Rdx, &vt, 8, &r5) && vt > 0x10000) { char dd[200]; out("  [bp]   obj=rdx %p vtable %p -> %s", (void *)cx.Rdx, (void *)vt, desc(vt, dd, sizeof dd)); for (int q = 0; q < 12; q++) { ULONG_PTR fp = 0; ReadProcessMemory(proc, (void *)(vt + q * 8), &fp, 8, &r5); out("  [%d]=%s", q, desc(fp, dd, sizeof dd)); } out("\n"); } }
          { ULONG_PTR vt = 0, f22 = 0; SIZE_T r6; char dd[200]; if (cx.Rcx > 0x10000 && ReadProcessMemory(proc, (void *)cx.Rcx, &vt, 8, &r6) && vt > 0x10000) { ReadProcessMemory(proc, (void *)(vt + 22 * 8), &f22, 8, &r6); out("  [bp]   obj=rcx %p vtable %s", (void *)cx.Rcx, desc(vt, dd, sizeof dd)); out("  [22]=%s\n", desc(f22, dd, sizeof dd)); } }
            cx.Rip = addr; SetThreadContext(th, &cx); }
        if (th) CloseHandle(th); return 1; }
    return 0;
}

#define WXR_OUT out
#include "wxrlaunch.inc"   /* WXR mode: version bypass, layer injection, AUMID resolution (shared with the broker) */

static DWORD g_sample[8], g_t0; static int g_nsample, g_nsampled;
static void sample_threads(HANDLE proc, DWORD pid)
{
    HANDLE snap; THREADENTRY32 te; DWORD now = GetTickCount() - g_t0;
    if (g_nsampled >= g_nsample || now < g_sample[g_nsampled]) return;
    g_nsampled++; out("  [smp]  sample at %lu ms\n", (unsigned long)now);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); if (snap == INVALID_HANDLE_VALUE) return;
    te.dwSize = sizeof te;
    if (Thread32First(snap, &te)) do {
        HANDLE th; if (te.th32OwnerProcessID != pid) continue;
        th = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID); if (!th) continue;
        SuspendThread(th); out("  [smp]  thread %lu\n", (unsigned long)te.th32ThreadID); dump_context(proc, te.th32ThreadID, 1); ResumeThread(th); CloseHandle(th);
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
}

int main(int argc, char **argv)
{
    char cmd[2048] = "", dir[MAX_PATH], self[MAX_PATH], logp[MAX_PATH]; STARTUPINFOA si; PROCESS_INFORMATION pi; DEBUG_EVENT ev; int i, alive = 1; HANDLE proc;
    DWORD exitcode = 0; struct { DWORD code; ULONG_PTR addr; int n; } seen[64]; int nseen = 0;
    GetModuleFileNameA(NULL, self, MAX_PATH); { char *s = strrchr(self, '\\'); if (s) *s = 0; }
    snprintf(logp, sizeof logp, "%s\\wxrrun.log", self); g_log = fopen(logp, "w");
    { char root[MAX_PATH] = "";   /* --wxr is accepted for old command lines; WXR is the only mode */
    while (argc > 1 && !strncmp(argv[1], "--", 2)) {
        if (!strcmp(argv[1], "--root") && argc > 2) { lstrcpynA(root, argv[2], MAX_PATH); argv++; argc--; }
        argv++; argc--; }
    if (argc < 2) { out("usage: wxrrun.exe [--root <dir>] <path\\to\\App.exe | AUMID> [args]   (debug launch of a WXR app)\n"); return 2; }
    if (!root[0] && !GetEnvironmentVariableA("WXR_ROOT", root, MAX_PATH)) strcpy(root, "C:\\WXR");
    {   /* debug launch of a WXR app: by exe path or by AUMID (PackageFamilyName!AppId) */
        char exe[MAX_PATH], server[200] = "", args[3000] = "", err[400];
        if (strchr(argv[1], '!') && !strchr(argv[1], '\\')) { if (!wxr_resolve_aumid(root, argv[1], exe, server, err, sizeof err)) { out("wxrrun: %s\n", err); return 7; } }
        else lstrcpynA(exe, argv[1], MAX_PATH);
        lstrcpyA(args, server); for (i = 2; i < argc; i++) { strcat(args, " \""); strcat(args, argv[i]); strcat(args, "\""); }
        out("wxrrun: starting %s %s\n  WXR mode, root %s (pristine files, loader layer)%s\n\n", exe, args, root, GetModuleHandleA("KexDll.dll") ? "; VxKex inherited from this launcher" : "");
        if (!wxr_start_exe(root, exe, args, 1, &pi, err, sizeof err)) { out("wxrrun: %s\n", err); MessageBoxA(NULL, err, "WXR", MB_ICONWARNING); return 6; }
    } }
    proc = pi.hProcess; bp_parse();
    /* WXR_SAMPLE_AT=ms[,ms...]: at those times after start, suspend every thread and print its stack (for freezes) */
    { const char *s = getenv("WXR_SAMPLE_AT"); while (s && *s && g_nsample < 8) { g_sample[g_nsample++] = strtoul(s, (char **)&s, 10); if (*s == ',') s++; else break; } }
    g_t0 = GetTickCount();
    while (alive) {
        DWORD status = DBG_CONTINUE; char d[200];
        if (!WaitForDebugEvent(&ev, g_nsample ? 100 : INFINITE)) { if (GetLastError() == ERROR_SEM_TIMEOUT) { sample_threads(proc, pi.dwProcessId); continue; } break; }
        sample_threads(proc, pi.dwProcessId);
        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            add_module(proc, (ULONG_PTR)ev.u.CreateProcessInfo.lpBaseOfImage, ev.u.CreateProcessInfo.hFile, "app.exe");
            out("  [exe]  %s\n", g_mods[g_nmods - 1].name);
            bp_arm(proc, (ULONG_PTR)ev.u.CreateProcessInfo.lpBaseOfImage, g_mods[g_nmods - 1].name);   /* WXR_BP=<exe name>:rva works too */
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            add_module(proc, (ULONG_PTR)ev.u.LoadDll.lpBaseOfDll, ev.u.LoadDll.hFile, NULL);
            out("  [dll]  %-34s @ %p\n", g_mods[g_nmods - 1].name, ev.u.LoadDll.lpBaseOfDll); bp_arm(proc, (ULONG_PTR)ev.u.LoadDll.lpBaseOfDll, g_mods[g_nmods - 1].name);
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
            break;
        case OUTPUT_DEBUG_STRING_EVENT: {
            OUTPUT_DEBUG_STRING_INFO *o = &ev.u.DebugString; char buf[1024]; SIZE_T n = 0; int len = o->nDebugStringLength; if (len > 1000) len = 1000;
            if (!o->fUnicode && ReadProcessMemory(proc, o->lpDebugStringData, buf, len, &n)) { buf[n < sizeof buf ? n : sizeof buf - 1] = 0; out("  [dbg]  %s%s", buf, strchr(buf, '\n') ? "" : "\n"); }
            break; }
        case EXCEPTION_DEBUG_EVENT: {
            EXCEPTION_RECORD *er = &ev.u.Exception.ExceptionRecord; DWORD c = er->ExceptionCode; int k, dup = 0, noisy;
            status = DBG_EXCEPTION_NOT_HANDLED;
            if (c == EXCEPTION_SINGLE_STEP && g_rearm >= 0 && ev.dwThreadId == g_rearm_tid) { SIZE_T r; unsigned char cc = 0xCC; WriteProcessMemory(proc, (void *)g_bp[g_rearm].addr, &cc, 1, &r); FlushInstructionCache(proc, NULL, 0); g_rearm = -1; status = DBG_CONTINUE; break; }
            if (c == EXCEPTION_BREAKPOINT && bp_hit(proc, ev.dwThreadId, (ULONG_PTR)er->ExceptionAddress)) { status = DBG_CONTINUE; break; }
            if (c == 0xC0000008 && ev.u.Exception.dwFirstChance) { static int ninv; if (ninv++ < 5) out("  [exc]  (invalid handle passed to NtClose/NtSetEvent..., continuing like a non-debugged process) at %s\n", desc((ULONG_PTR)er->ExceptionAddress, d, sizeof d)); status = DBG_CONTINUE; break; }
            if (c == 0x40080201 || c == 0x40080202) { static int nro; if (nro++ < 40) { DWORD kk; SIZE_T rr; WCHAR wb[200] = {0}; out("  [ro]   0x%lX nparams=%lu:", (unsigned long)c, (unsigned long)er->NumberParameters); for (kk = 0; kk < er->NumberParameters && kk < 6; kk++) out(" %llX", (unsigned long long)er->ExceptionInformation[kk]);
              for (kk = 0; kk < er->NumberParameters && kk < 6; kk++) { if (er->ExceptionInformation[kk] > 0x10000 && ReadProcessMemory(proc, (void *)er->ExceptionInformation[kk], wb, 190 * 2, &rr) && rr >= 8 && wb[0] > 0x1F && wb[0] < 0x7F && wb[1] > 0x1F && wb[1] < 0x7F) { wb[199] = 0; out("  str[%lu]=%ls", (unsigned long)kk, wb); } } out("\n"); } status = DBG_CONTINUE; break; }   /* WinRT RoOriginateError notification: debugger-only, Windows swallows it */
            if (c == 0x406D1388 || c == 0x40010006 || c == 0x4001000A || c == EXCEPTION_BREAKPOINT || c == 0x4000001F /*wow64 bp*/) { status = (c == EXCEPTION_BREAKPOINT) ? DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED; break; }
            for (k = 0; k < nseen; k++) if (seen[k].code == c && seen[k].addr == (ULONG_PTR)er->ExceptionAddress) { seen[k].n++; dup = 1;
                if (seen[k].n >= 8 && !(seen[k].n & (seen[k].n - 1))) out("  [exc]  repeat %dx 0x%08lX at %s (t=%lu)\n", seen[k].n, (unsigned long)c, desc((ULONG_PTR)er->ExceptionAddress, d, sizeof d), (unsigned long)(GetTickCount() - g_t0));
                if (seen[k].n == 64 || seen[k].n == 1024) dump_context(proc, ev.dwThreadId, 1);
                break; }
            if (!dup && nseen < 64) { seen[nseen].code = c; seen[nseen].addr = (ULONG_PTR)er->ExceptionAddress; seen[nseen].n = 1; nseen++; }
            noisy = dup && !ev.u.Exception.dwFirstChance == 0;
            if (dup && ev.u.Exception.dwFirstChance && !(c == 0xE06D7363 && getenv("WXR_CPP_ALL"))) break;    /* repeated first-chance exception from the same place: already shown */
            (void)noisy;
            out("  [exc]  %s chance 0x%08lX at %s", ev.u.Exception.dwFirstChance ? "first " : "SECOND", (unsigned long)c, desc((ULONG_PTR)er->ExceptionAddress, d, sizeof d));
            if (c == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) out("  %s %p", er->ExceptionInformation[0] == 0 ? "read of" : er->ExceptionInformation[0] == 1 ? "write to" : "execute at", (void *)er->ExceptionInformation[1]);
            if (c == 0xE06D7363) out("  (C++ exception)");
            out("\n");
            if (!ev.u.Exception.dwFirstChance || c == EXCEPTION_ACCESS_VIOLATION || c == 0xE06D7363 || c == 0xC0000139 || c == 0xC06D007E || c == 0xC06D007F || (getenv("WXR_EXC_STACK") && c == (DWORD)strtoul(getenv("WXR_EXC_STACK"), NULL, 16))) dump_context(proc, ev.dwThreadId, 1);
            break; }
        case EXIT_PROCESS_DEBUG_EVENT:
            exitcode = ev.u.ExitProcess.dwExitCode; alive = 0; out("\nwxrrun: the app exited with code 0x%08lX (%ld)\n", (unsigned long)exitcode, (long)exitcode); break;
        default: break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, status);
    }
    out("wxrrun: log written to %s\n", logp);
    if (g_log) fclose(g_log);
    return (int)exitcode;
}














