/* wxrbroker.inc - app activation in the WXR broker (wxrsvc --wxr). Included by wxrsvc.c after L().
   Role: what Windows 10's activation service does for ApplicationActivationManager::ActivateApplication. wxr.exe (or any local client of
   the same user) sends "activate\t<AUMID>\t<arguments>" over the pipe \\.\pipe\wxr.broker.<session>; the broker looks the AUMID up in the
   root's registered packages and starts the app (wxrlaunch.inc). Reply: "ok <pid>" or "error <message>".
   Only packages registered in the root can be started; remote clients are refused; the pipe keeps the default DACL (this user, SYSTEM,
   administrators). Apps started here inherit VxKex from the broker. */
#include <stddef.h>
#define WXR_OUT(...) L(__VA_ARGS__)
#include "../launcher/wxrlaunch.inc"

static char g_broker_root[MAX_PATH];
static void broker_pipe_name(char *out, size_t cap)
{
    DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    _snprintf(out, cap, "\\\\.\\pipe\\wxr.broker.%lu", (unsigned long)sid);
}
static void broker_request(const char *req, char *reply, int cap)
{
    char aumid[400] = "", args[2000] = "", exe[MAX_PATH], server[200], err[600], full[2300]; const char *t1, *t2; DWORD pid;
    if (strncmp(req, "activate\t", 9)) { _snprintf(reply, cap, "error unknown request"); return; }
    t1 = req + 9; t2 = strchr(t1, '\t');
    if (t2) { int n = (int)(t2 - t1); if (n >= (int)sizeof aumid) n = sizeof aumid - 1; memcpy(aumid, t1, n); aumid[n] = 0; lstrcpynA(args, t2 + 1, sizeof args); }
    else lstrcpynA(aumid, t1, sizeof aumid);
    if (!wxr_resolve_aumid(g_broker_root, aumid, exe, server, err, sizeof err)) { L("activate %s: %s", aumid, err); _snprintf(reply, cap, "error %s", err); return; }
    _snprintf(full, sizeof full, "%s%s%s", server, args[0] ? " " : "", args);
    pid = wxr_start_exe(g_broker_root, exe, full, 0, NULL, err, sizeof err);
    if (!pid) { L("activate %s: %s", aumid, err); _snprintf(reply, cap, "error %s", err); return; }
    L("activate %s -> %s (pid %lu)", aumid, exe, (unsigned long)pid);
    _snprintf(reply, cap, "ok %lu", (unsigned long)pid);
}
static DWORD WINAPI broker_thread(LPVOID x)
{
    char name[100]; (void)x; broker_pipe_name(name, sizeof name);
    L("activation endpoint %s", name);
    for (;;) {
        HANDLE p = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | 0x8 /* PIPE_REJECT_REMOTE_CLIENTS */,
                                    PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL);
        char req[4096], reply[1024]; DWORD n = 0;
        if (p == INVALID_HANDLE_VALUE) { L("CreateNamedPipe failed %lu", GetLastError()); Sleep(1000); continue; }
        if (ConnectNamedPipe(p, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            if (ReadFile(p, req, sizeof req - 1, &n, NULL)) { req[n] = 0; broker_request(req, reply, sizeof reply); WriteFile(p, reply, (DWORD)strlen(reply), &n, NULL); FlushFileBuffers(p); } }
        DisconnectNamedPipe(p); CloseHandle(p);
    }
    return 0;
}
/* idle exit: once no process from <root>\WindowsApps has run for 60 s (and the broker itself is older than that), the broker stops, wxr.exe
   starts it again on the next activation. Counts every WXR app, also the ones wxrrun starts. WXR_BROKER_STAY=1 keeps it running. */
#include <tlhelp32.h>
static int broker_apps_running(void) {
    char dir[MAX_PATH + 16]; size_t dl; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 pe; int n = 0;
    BOOL (WINAPI *qfp)(HANDLE, DWORD, LPSTR, PDWORD) = (BOOL (WINAPI *)(HANDLE, DWORD, LPSTR, PDWORD))GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryFullProcessImageNameA");
    if (s == INVALID_HANDLE_VALUE || !qfp) return 1;   /* cannot tell: stay */
    _snprintf(dir, sizeof dir, "%s\\WindowsApps\\", g_broker_root); dl = strlen(dir);
    pe.dwSize = sizeof pe;
    if (Process32First(s, &pe)) do { HANDLE p; char img[MAX_PATH]; DWORD il = MAX_PATH;
        if (pe.th32ProcessID == GetCurrentProcessId() || !(p = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pe.th32ProcessID))) continue;
        if (qfp(p, 0, img, &il) && !_strnicmp(img, dir, dl)) n++;
        CloseHandle(p); } while (!n && Process32Next(s, &pe));
    CloseHandle(s); return n; }
static DWORD WINAPI broker_idle_thread(LPVOID x) {
    DWORD idle = 0; (void)x;
    for (;;) { Sleep(10000);
        if (broker_apps_running()) { idle = 0; continue; }
        if ((idle += 10) < 60) continue;
        { HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE, "Local\\wxrsvc.stop"); L("no WXR app running for %lu s: broker exits", (unsigned long)idle); if (e) { SetEvent(e); CloseHandle(e); } return 0; } } }
static void broker_start(const char *root) {
    lstrcpynA(g_broker_root, root, MAX_PATH); CloseHandle(CreateThread(NULL, 0, broker_thread, NULL, 0, NULL));
    if (!GetEnvironmentVariableA("WXR_BROKER_STAY", NULL, 0)) CloseHandle(CreateThread(NULL, 0, broker_idle_thread, NULL, 0, NULL)); }
