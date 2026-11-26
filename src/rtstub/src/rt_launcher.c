/* rt_launcher.c - Windows.System.Launcher stand-in. Win10 hands every launch to a broker (Win10 shell infrastructure) that Win7 does not
 * have; Media Player's "open file location" did nothing. Here launches go to Win7's shell on the caller's thread:
 *   LaunchFileAsync            -> ShellExecute "open" on the file's path
 *   LaunchUriAsync (all forms) -> ShellExecute on the absolute URI (default browser / protocol handler)
 *   LaunchFolderAsync / LaunchFolderPathAsync -> Explorer on the folder; with FolderLauncherOptions.ItemsToSelect, explorer /select,<item>
 * Results: IAsyncOperation<bool>, true when the shell accepted it. Query/Find/ForResults stay generated stubs. */
#include "rt_core.h"
#include <shellapi.h>

void *lop_new(const char *type, IUnknown *obj, int is_bool, int b);   /* rt_library.c: completed operation */
#define OP_BOOL "Windows.Foundation.IAsyncOperation`1<bool>"
#define VT(o, k, T) ((T)(*(void ***)(o))[k])
static const GUID IID_IStorageItem_l2 = { 0x4207A996, 0xCA2F, 0x42F7, { 0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30 } };
static const GUID IID_IFolderLauncherOptions_ = { 0xBB91C27D, 0x6B87, 0x432A, { 0xBD, 0x04, 0x77, 0x6C, 0x6F, 0x5F, 0xB2, 0xAB } };

static const WCHAR *hraw(void *hs) { typedef const WCHAR *(WINAPI *P)(void *, UINT32 *); static P p; if (!hs) return L""; if (!p) p = (P)rt_winrt_fn("WindowsGetStringRawBuffer"); return p ? p(hs, NULL) : L""; }
static void hdel(void *hs) { typedef HRESULT (WINAPI *P)(void *); static P p; if (!hs) return; if (!p) p = (P)rt_winrt_fn("WindowsDeleteString"); if (p) p(hs); }
/* path of an IStorageItem (file or folder object) into buf */
static int item_path(void *obj, WCHAR *buf, int cap) {
    void *it = NULL, *hs = NULL; int ok = 0; HRESULT hq, hp = 0; buf[0] = 0;
    hq = obj ? ((IUnknown *)obj)->lpVtbl->QueryInterface((IUnknown *)obj, &IID_IStorageItem_l2, &it) : E_POINTER;
    if (SUCCEEDED(hq) && it) {
        if (SUCCEEDED(hp = VT(it, 12, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(it, &hs)) && hs) { lstrcpynW(buf, hraw(hs), cap); ok = buf[0] != 0; hdel(hs); }
        ((IUnknown *)it)->lpVtbl->Release((IUnknown *)it); }
    rt_log("launcher: item %p QI 0x%08lX get_Path 0x%08lX -> %ls", obj, (unsigned long)hq, (unsigned long)hp, buf);
    return ok;
}
/* first ItemsToSelect entry of a FolderLauncherOptions (IFolderLauncherOptions slot 6: get_ItemsToSelect -> IVector<IStorageItem>) */
static int select_path(void *opts, WCHAR *buf, int cap) {
    void *fo = NULL, *vec = NULL, *item = NULL; UINT32 n = 0; int ok = 0; buf[0] = 0;
    if (opts && SUCCEEDED(((IUnknown *)opts)->lpVtbl->QueryInterface((IUnknown *)opts, &IID_IFolderLauncherOptions_, &fo)) && fo) {
        if (SUCCEEDED(VT(fo, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(fo, &vec)) && vec) {
            if (SUCCEEDED(VT(vec, 7, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32 *))(vec, &n)) && n &&
                SUCCEEDED(VT(vec, 6, HRESULT (STDMETHODCALLTYPE *)(void *, UINT32, void **))(vec, 0, &item)) && item) {   /* GetAt(0) */
                ok = item_path(item, buf, cap); ((IUnknown *)item)->lpVtbl->Release((IUnknown *)item); }
            ((IUnknown *)vec)->lpVtbl->Release((IUnknown *)vec); }
        ((IUnknown *)fo)->lpVtbl->Release((IUnknown *)fo); }
    return ok;
}
static int shell_open(const WCHAR *what, const WCHAR *params) {
    INT_PTR r = (INT_PTR)ShellExecuteW(NULL, params ? NULL : L"open", params ? L"explorer.exe" : what, params, NULL, SW_SHOWNORMAL);
    rt_log("launcher: %ls %ls -> %d", params ? L"explorer.exe" : what, params ? params : L"", (int)r);
    return r > 32;
}
static HRESULT done(void **out, int ok) { if (!out) return E_POINTER; *out = lop_new(OP_BOOL, NULL, 1, ok); return *out ? S_OK : E_OUTOFMEMORY; }
static HRESULT launch_folder(const WCHAR *folder, void *opts, void **out) {
    WCHAR sel[MAX_PATH * 2], p[MAX_PATH * 2 + 16]; rt_log("launcher: folder %ls, options %p", folder ? folder : L"(none)", opts);
    if (select_path(opts, sel, MAX_PATH * 2)) { wsprintfW(p, L"/select,\"%s\"", sel); return done(out, shell_open(NULL, p)); }
    if (!folder || !folder[0]) return done(out, 0);
    wsprintfW(p, L"\"%s\"", folder); return done(out, shell_open(NULL, p));
}
static HRESULT launch_uri(void *uri, void **out) {   /* IUriRuntimeClass slot 6: get_AbsoluteUri */
    void *hs = NULL; int ok = 0; rt_log("launcher: uri %p", uri);
    if (uri && SUCCEEDED(VT(uri, 6, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(uri, &hs)) && hs) { ok = shell_open(hraw(hs), NULL); hdel(hs); }
    return done(out, ok);
}

HRESULT STDMETHODCALLTYPE S_ILauncherStatics_LaunchFileAsync(void *This, void *file, void *out) { WCHAR p[MAX_PATH * 2]; (void)This; return done((void **)out, item_path(file, p, MAX_PATH * 2) && shell_open(p, NULL)); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics_LaunchFileAsync_2(void *This, void *file, void *opt, void *out) { (void)opt; return S_ILauncherStatics_LaunchFileAsync(This, file, out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics_LaunchUriAsync(void *This, void *uri, void *out) { (void)This; return launch_uri(uri, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics_LaunchUriAsync_2(void *This, void *uri, void *opt, void *out) { (void)This; (void)opt; return launch_uri(uri, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics2_LaunchUriAsync(void *This, void *uri, void *opt, void *data, void *out) { (void)This; (void)opt; (void)data; return launch_uri(uri, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics4_LaunchUriForUserAsync(void *This, void *user, void *uri, void *out) { (void)This; (void)user; return launch_uri(uri, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics3_LaunchFolderAsync(void *This, void *folder, void *out) { WCHAR p[MAX_PATH * 2]; (void)This; item_path(folder, p, MAX_PATH * 2); return launch_folder(p, NULL, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics3_LaunchFolderAsync_2(void *This, void *folder, void *opt, void *out) { WCHAR p[MAX_PATH * 2]; (void)This; item_path(folder, p, MAX_PATH * 2); return launch_folder(p, opt, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics5_LaunchFolderPathAsync(void *This, void *path, void *out) { (void)This; return launch_folder(hraw(path), NULL, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics5_LaunchFolderPathAsync_2(void *This, void *path, void *opt, void *out) { (void)This; return launch_folder(hraw(path), opt, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics5_LaunchFolderPathForUserAsync(void *This, void *user, void *path, void *out) { (void)This; (void)user; return launch_folder(hraw(path), NULL, (void **)out); }
HRESULT STDMETHODCALLTYPE S_ILauncherStatics5_LaunchFolderPathForUserAsync_2(void *This, void *user, void *path, void *opt, void *out) { (void)This; (void)user; return launch_folder(hraw(path), opt, (void **)out); }
