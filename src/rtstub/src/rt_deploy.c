/* rt_deploy.c - Windows.Management.Deployment: installing a package FILE (App Installer, any app using PackageManager.AddPackage*).
 *
 * Win10's PackageManager hands the file to AppXSvc (RPC), which Win7 lacks. Here the install is a child process of the WXR deployment
 * engine:   <root>\Layer\wxrpkg.exe add "<package>" [--dependency "<file>"]... [--force-update]
 * (the same engine the Store's install queue uses, rt_install.c). The operation is pending while it runs and completes with a
 * DeploymentResult; a failed install completes as AsyncStatus Error with wxrpkg's own message as ErrorText.
 *
 *   IPackageManager9.AddPackageByUriAsync(Uri, AddPackageOptions)   App Installer's path (it creates AddPackageOptions first)
 *   IPackageManager.AddPackageAsync(Uri, IIterable<Uri> deps, DeploymentOptions)   the classic overload
 *   AddPackageOptions: flags kept per object; DependencyPackageUris is ONE vector per options object (the generated getter gave a new
 *                      empty vector on every call, so the caller's Append went nowhere)
 *   DeploymentResult: ErrorText / ExtendedErrorCode / ActivityId / IsRegistered from the run
 * Not done: progress events (DeploymentProgress) - the caller just waits. Missing framework dependencies are not fetched here: wxrpkg add
 * reports them and the install fails with that message. */
#include "rt_core.h"
#include <stdio.h>

#define ERROR_INSTALL_FAILED_ 15603          /* HRESULT_FROM_WIN32 -> 0x80073CF3, what AppXSvc reports for a failed install */
static const GUID IID_IUriRuntimeClass_ = { 0x9E365E57, 0x48B2, 0x4160, { 0x95, 0x6F, 0xC7, 0x38, 0x51, 0x20, 0xBB, 0xFC } };

static const WCHAR *raw(void *hs)
{
    static const WCHAR *(WINAPI *p)(void *, UINT32 *);
    if (!hs) return L"";
    if (!p) p = (const WCHAR *(WINAPI *)(void *, UINT32 *))rt_winrt_fn("WindowsGetStringRawBuffer");
    return p ? p(hs, NULL) : L"";
}
static void hs_free(void *hs) { static HRESULT (WINAPI *d)(void *); if (!hs) return; if (!d) d = (HRESULT (WINAPI *)(void *))rt_winrt_fn("WindowsDeleteString"); if (d) d(hs); }
static int wxr_root(WCHAR *out)   /* "C:\WXR" (this DLL is <root>\Layer\wxrstub.dll) */
{
    HMODULE m = NULL; WCHAR *b;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)wxr_root, &m) || !GetModuleFileNameW(m, out, MAX_PATH)) return 0;
    if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0; if (!(b = wcsrchr(out, L'\\'))) return 0; *b = 0;
    return 1;
}
/* a Windows.Foundation.Uri (file:///E:/Downloads/x.msixbundle) as a local path; 0 if it is not a file URI */
static int uri_path(IUnknown *uri, WCHAR *out, DWORD cap)
{
    typedef HRESULT (WINAPI *PCF)(PCWSTR, PWSTR, DWORD *, DWORD); static PCF pcf; IUnknown *u = NULL; void *hs = NULL; DWORD n = cap; int ok = 0;
    out[0] = 0; if (!uri) return 0;
    if (FAILED(uri->lpVtbl->QueryInterface(uri, &IID_IUriRuntimeClass_, (void **)&u)) || !u) return 0;
    if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)u)[6])(u, &hs)) && hs) {   /* get_AbsoluteUri */
        const WCHAR *s = raw(hs);
        if (!pcf) { HMODULE m = LoadLibraryA("shlwapi.dll"); pcf = m ? (PCF)GetProcAddress(m, "PathCreateFromUrlW") : NULL; }
        if (!_wcsnicmp(s, L"file:", 5) && pcf && SUCCEEDED(pcf(s, out, &n, 0))) ok = 1;
        else rt_log("deploy: not a file URI: %ls", s);
        hs_free(hs); }
    u->lpVtbl->Release(u);
    return ok;
}

/* ---- AddPackageOptions ---- */
typedef struct { BYTE force_update, force_shutdown, defer, dev, allow_unsigned, all_resources, retain, stage_in_place, limit; int stub; void *deps; } Opts;
static Opts *opts_of(void *This) { Opts *o = (Opts *)rt_user(This); if (!o && (o = (Opts *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *o))) rt_set_user(This, o); return o; }
#define BOOLPROP(Name, field) \
    HRESULT STDMETHODCALLTYPE S_IAddPackageOptions_get_##Name(void *This, void *out) { Opts *o = opts_of(This); if (!out) return E_POINTER; *(BYTE *)out = o ? o->field : 0; return S_OK; } \
    HRESULT STDMETHODCALLTYPE S_IAddPackageOptions_put_##Name(void *This, void *v) { Opts *o = opts_of(This); if (o) o->field = (BYTE)(UINT_PTR)v; return S_OK; }
BOOLPROP(ForceUpdateFromAnyVersion, force_update)
BOOLPROP(ForceAppShutdown, force_shutdown)
BOOLPROP(DeferRegistrationWhenPackagesAreInUse, defer)
BOOLPROP(DeveloperMode, dev)
BOOLPROP(AllowUnsigned, allow_unsigned)
BOOLPROP(InstallAllResources, all_resources)
BOOLPROP(RetainFilesOnFailure, retain)
BOOLPROP(StageInPlace, stage_in_place)
HRESULT STDMETHODCALLTYPE S_IAddPackageOptions_get_DependencyPackageUris(void *This, void *out)
{
    Opts *o = opts_of(This); const RtColl *c;
    if (!out) return E_POINTER; *(void **)out = NULL; if (!o) return E_OUTOFMEMORY;
    if (!o->deps && (c = rt_coll_named("Windows.Foundation.Collections.IVector`1<Windows.Foundation.Uri>"))) o->deps = rt_new_coll(c);
    if (o->deps) { rt_AddRef(o->deps); *(void **)out = o->deps; }
    return o->deps ? S_OK : E_OUTOFMEMORY;
}

/* ---- DeploymentResult ---- */
typedef struct { HRESULT ext; BYTE registered; GUID activity; WCHAR text[512]; } DRes;
static int g_cls_dres = -2;
static void *new_result(HRESULT ext, int registered, const WCHAR *text)
{
    void *r; DRes *d;
    if (g_cls_dres == -2) g_cls_dres = rt_class_index(L"Windows.Management.Deployment.DeploymentResult");
    if (g_cls_dres < 0 || !(r = rt_new_object(g_cls_dres, NULL))) return NULL;
    if ((d = (DRes *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *d))) {
        d->ext = ext; d->registered = (BYTE)registered; CoCreateGuid(&d->activity); if (text) lstrcpynW(d->text, text, 512); rt_set_user(r, d); }
    return r;
}
HRESULT STDMETHODCALLTYPE S_IDeploymentResult_get_ErrorText(void *This, void *out) { DRes *d = (DRes *)rt_user(This); if (!out) return E_POINTER; *(void **)out = d && d->text[0] ? rt_hstring(d->text) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IDeploymentResult_get_ActivityId(void *This, void *out) { DRes *d = (DRes *)rt_user(This); if (!out) return E_POINTER; if (d) *(GUID *)out = d->activity; else memset(out, 0, sizeof(GUID)); return S_OK; }
HRESULT STDMETHODCALLTYPE S_IDeploymentResult_get_ExtendedErrorCode(void *This, void *out) { DRes *d = (DRes *)rt_user(This); if (!out) return E_POINTER; *(HRESULT *)out = d ? d->ext : S_OK; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IDeploymentResult2_get_IsRegistered(void *This, void *out) { DRes *d = (DRes *)rt_user(This); if (!out) return E_POINTER; *(BYTE *)out = d ? d->registered : 0; return S_OK; }

/* ---- the install: wxrpkg add on a worker thread ---- */
typedef struct { void *op; WCHAR cmd[8192]; WCHAR what[MAX_PATH]; } Job;
static DWORD WINAPI job_thread(void *p)
{
    Job *j = (Job *)p; WCHAR root[MAX_PATH], exe[MAX_PATH], err[512] = L""; HANDLE rd = NULL, wr = NULL; SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD code = 1, n; char buf[4096], line[1024]; int ll = 0; void *res;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!wxr_root(root) || _snwprintf(exe, MAX_PATH, L"%s\\Layer\\wxrpkg.exe", root) < 0 || !CreatePipe(&rd, &wr, &sa, 0)) { lstrcpyW(err, L"The WXR deployment engine (wxrpkg.exe) could not be started."); goto done; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    rt_log("deploy: %ls", j->cmd);
    if (!CreateProcessW(exe, j->cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        _snwprintf(err, 512, L"The WXR deployment engine could not be started (error %lu).", GetLastError()); CloseHandle(wr); wr = NULL; goto done; }
    CloseHandle(wr); wr = NULL; CloseHandle(pi.hThread);
    while (ReadFile(rd, buf, sizeof buf, &n, NULL) && n) { DWORD i;   /* every line goes to the log; the last "ERROR" line is the ErrorText */
        for (i = 0; i < n; i++) { char c = buf[i];
            if (c == '\n' || ll == (int)sizeof line - 1) { line[ll] = 0; if (ll && line[ll - 1] == '\r') line[--ll] = 0;
                if (ll) { rt_log("deploy| %s", line); if (!_strnicmp(line, "ERROR", 5)) { const char *m = line + 5; while (*m == ':' || *m == ' ') m++; MultiByteToWideChar(CP_UTF8, 0, m, -1, err, 512); } }
                ll = 0; }
            else line[ll++] = c; } }
    WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess);
done:
    if (rd) CloseHandle(rd); if (wr) CloseHandle(wr);
    if (code == 0) { res = new_result(S_OK, 1, NULL); rt_log("deploy: %ls installed", j->what); rt_async_complete(j->op, (IUnknown *)res, NULL, 0); }
    else { HRESULT hr = HRESULT_FROM_WIN32(ERROR_INSTALL_FAILED_);
        if (!err[0]) _snwprintf(err, 512, L"The WXR deployment engine failed (exit code %lu).", code);
        res = new_result(hr, 0, err); rt_log("deploy: %ls failed (exit %lu): %ls", j->what, code, err); rt_async_fail(j->op, hr, (IUnknown *)res); }
    if (res) rt_Release(res);
    rt_Release(j->op); HeapFree(GetProcessHeap(), 0, j);
    return 0;
}
/* starts the install; deps = the dependency files (already paths), n of them */
static HRESULT start_add(IUnknown *uri, WCHAR deps[][MAX_PATH], int ndeps, int force, void **out)
{
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperationWithProgress`2<Windows.Management.Deployment.DeploymentResult,Windows.Management.Deployment.DeploymentProgress>");
    WCHAR path[MAX_PATH]; Job *j; int i; size_t len; HANDLE th;
    if (!out) return E_POINTER; *out = NULL; if (!a) return E_NOTIMPL;
    if (!uri_path(uri, path, MAX_PATH)) return E_INVALIDARG;
    if (!(j = (Job *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *j))) return E_OUTOFMEMORY;
    lstrcpynW(j->what, path, MAX_PATH);
    len = _snwprintf(j->cmd, 8192, L"wxrpkg.exe add \"%s\"", path);
    for (i = 0; i < ndeps && len < 7600; i++) len += _snwprintf(j->cmd + len, 8192 - len, L" --dependency \"%s\"", deps[i]);
    if (force && len < 8100) len += _snwprintf(j->cmd + len, 8192 - len, L" --force-update");
    if (!(j->op = rt_new_async_pending(a))) { HeapFree(GetProcessHeap(), 0, j); return E_OUTOFMEMORY; }
    rt_AddRef(j->op); *out = j->op;   /* one reference for the caller, one for the worker */
    if (!(th = CreateThread(NULL, 0, job_thread, j, 0, NULL))) { rt_async_fail(j->op, E_FAIL, NULL); rt_Release(j->op); HeapFree(GetProcessHeap(), 0, j); return S_OK; }
    CloseHandle(th);
    return S_OK;
}

/* IPackageManager9.AddPackageByUriAsync(Uri packageUri, AddPackageOptions options) */
HRESULT STDMETHODCALLTYPE S_IPackageManager9_AddPackageByUriAsync(void *This, void *uri, void *options, void *out)
{
    static WCHAR deps[16][MAX_PATH]; int nd = 0, force = 0; Opts *o = NULL; HRESULT hr;
    (void)This;
    if (options) { o = (Opts *)rt_user(options); }
    if (o) { force = o->force_update;
        if (o->deps) { IUnknown **items = NULL; int k, n = rt_coll_items(o->deps, &items); for (k = 0; k < n && nd < 16; k++) if (uri_path(items[k], deps[nd], MAX_PATH)) nd++; } }
    hr = start_add((IUnknown *)uri, deps, nd, force, (void **)out);
    rt_log("PackageManager.AddPackageByUriAsync(%d dependency file(s), force %d) -> 0x%08lX", nd, force, (unsigned long)hr);
    return hr;
}
/* IPackageManager.AddPackageAsync(Uri packageUri, IIterable<Uri> dependencyPackageUris, DeploymentOptions options) */
HRESULT STDMETHODCALLTYPE S_IPackageManager_AddPackageAsync(void *This, void *uri, void *depsIt, void *options, void *out)
{
    static WCHAR deps[16][MAX_PATH]; int nd = 0; HRESULT hr; IUnknown *it = NULL;
    (void)This;
    if (depsIt && SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, IUnknown **))(*(void ***)depsIt)[6])(depsIt, &it)) && it) {   /* IIterable.First */
        BYTE has = 0;
        while (nd < 16 && SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, BYTE *))(*(void ***)it)[7])(it, &has)) && has) {   /* get_HasCurrent */
            IUnknown *u = NULL;
            if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, IUnknown **))(*(void ***)it)[6])(it, &u)) && u) { if (uri_path(u, deps[nd], MAX_PATH)) nd++; u->lpVtbl->Release(u); }   /* get_Current */
            if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, BYTE *))(*(void ***)it)[8])(it, &has))) break; }   /* MoveNext */
        it->lpVtbl->Release(it); }
    hr = start_add((IUnknown *)uri, deps, nd, ((UINT_PTR)options & 0x40000) != 0 /* DeploymentOptions.ForceUpdateFromAnyVersion */, (void **)out);
    rt_log("PackageManager.AddPackageAsync(%d dependency file(s), options 0x%lx) -> 0x%08lX", nd, (unsigned long)(UINT_PTR)options, (unsigned long)hr);
    return hr;
}
