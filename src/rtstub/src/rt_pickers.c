/* rt_pickers.c - Windows.Storage.Pickers stand-ins. Win10's pickers run in a separate broker (PickerHost.exe) we do not have; here they
 * show Win7's own Explorer dialog (IFileOpenDialog) on the caller's thread and turn the chosen paths into real StorageFile / StorageFolder
 * objects through Win10 Windows.Storage (StorageFile.GetFileFromPathAsync / StorageFolder.GetFolderFromPathAsync).
 * The picker keeps what the app set (FileTypeFilter, SuggestedStartLocation, CommitButtonText) and the dialog uses it: the app's file
 * types plus "All files", the start folder as the dialog's default folder (Win7 then remembers the last one, as Win10's picker does),
 * the commit text as the OK button. Cancel gives what Win10 gives: a NULL file / folder, an empty file list. FileSavePicker stays a stub (cancel). */
#define COBJMACROS
#include "rt_core.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <knownfolders.h>

typedef HRESULT (WINAPI *PRGAF)(void *cls, REFIID iid, void **f);
typedef HRESULT (WINAPI *PDEL)(void *s);
static const GUID IID_IStorageFileStatics_ = { 0x5984C710, 0xDAF2, 0x43C8, { 0x8B, 0xB4, 0xA4, 0xD3, 0xEA, 0xCF, 0xD0, 0x3F } };
static const GUID IID_IStorageFolderStatics_ = { 0x08F327FF, 0x85D5, 0x48B9, { 0xAE, 0xE9, 0x28, 0x51, 0x1E, 0x33, 0x9F, 0x9F } };
static const GUID IID_IAsyncInfo_p = { 0x00000036, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

static FARPROC winrt_fn(const char *name)
{
    static const char *const mods[] = { "wxrsys.dll", "combase.dll", "api-ms-win-core-winrt-l1-1-0.dll", "api-ms-win-core-winrt-string-l1-1-0.dll" }; int i;
    for (i = 0; i < 4; i++) { HMODULE m = GetModuleHandleA(mods[i]); FARPROC f = m ? GetProcAddress(m, name) : NULL; if (f) return f; }
    return NULL;
}
/* statics of a real Windows.Storage class, slot 6 = Get{File,Folder}FromPathAsync(HSTRING path, IAsyncOperation<T> **op) */
HRESULT from_path(const WCHAR *cls, const GUID *iid, const WCHAR *path, IUnknown **op)   /* also used by rt_accesslist.c */
{
    PRGAF rgaf = (PRGAF)winrt_fn("RoGetActivationFactory"); PDEL del = (PDEL)winrt_fn("WindowsDeleteString");
    void *hc = rt_hstring(cls), *hp = rt_hstring(path), *st = NULL; HRESULT hr = E_FAIL;
    *op = NULL;
    if (rgaf && hc && hp && SUCCEEDED(hr = rgaf(hc, iid, &st)) && st) {
        hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *, IUnknown **))(*(void ***)st)[6])(st, hp, op);
        ((IUnknown *)st)->lpVtbl->Release((IUnknown *)st);
    }
    if (del) { if (hc) del(hc); if (hp) del(hp); }
    rt_log("picker: %ls from %ls -> 0x%08lX", cls, path, (unsigned long)hr);
    return hr;
}
/* waits (pumping messages: the operation may need this thread) and takes the result of an IAsyncOperation<T> (T an object) */
void rt_mru_add_path(const WCHAR *path, int dir);   /* rt_accesslist.c: Win10's pickers add what the user picked to the app's MostRecentlyUsedList */
IUnknown *await_obj(IUnknown *op)
{
    IUnknown *info = NULL, *r = NULL; int st = 0; MSG m;
    if (!op || FAILED(op->lpVtbl->QueryInterface(op, &IID_IAsyncInfo_p, (void **)&info))) return NULL;
    for (;;) {
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, int *))(*(void ***)info)[7])(info, &st)) || st != 0) break;   /* get_Status: 0 = Started */
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
    if (st != 1) { HRESULT e = 0; ((HRESULT (STDMETHODCALLTYPE *)(void *, HRESULT *))(*(void ***)info)[8])(info, &e); rt_log("picker: storage operation error 0x%08lX", (unsigned long)e); }
    info->lpVtbl->Release(info);
    if (st == 1) ((HRESULT (STDMETHODCALLTYPE *)(void *, IUnknown **))(*(void ***)op)[8])(op, &r);   /* GetResults */
    else rt_log("picker: storage operation ended with status %d", st);
    return r;
}

static BOOL CALLBACK find_own(HWND h, LPARAM lp) { DWORD pid = 0; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { *(HWND *)lp = h; return FALSE; } return TRUE; }
static HWND owner_window(void)
{
    HWND h = GetForegroundWindow(); DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    if (!h || pid != GetCurrentProcessId()) { h = NULL; EnumWindows(find_own, (LPARAM)&h); }
    return h;
}

/* ---- what the app set on a FileOpenPicker / FolderPicker (per picker object) ---- */
typedef struct { int start; WCHAR commit[80]; } PickState;   /* start: PickerLocationId + 1 (0 = not set) */
static PickState *pstate(void *This)
{
    PickState *p = (PickState *)rt_user(This);
    if (!p && (p = (PickState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *p))) rt_set_user(This, p);
    return p;
}
static const WCHAR *hs_text(void *hs)
{
    static const WCHAR *(WINAPI *raw)(void *, UINT32 *); const WCHAR *t;
    if (!raw) raw = (const WCHAR *(WINAPI *)(void *, UINT32 *))winrt_fn("WindowsGetStringRawBuffer");
    t = hs && raw ? raw(hs, NULL) : NULL; return t ? t : L"";
}
static HRESULT get_filter(void *This, void **out)
{
    IUnknown *v;
    if (!out) return E_POINTER;
    if (!(v = rt_held(This))) {
        if (!(v = (IUnknown *)rt_new_coll(rt_coll_named("Windows.Foundation.Collections.IVector`1<String>")))) return E_OUTOFMEMORY;
        rt_hold(This, v); v->lpVtbl->Release(v); }
    v->lpVtbl->AddRef(v); *out = v; return S_OK;   /* the same vector every time: the app appends its file types to it */
}
static HRESULT put_start(void *This, void *a0) { PickState *p = pstate(This); if (p) p->start = (int)(INT_PTR)a0 + 1; rt_log("picker: SuggestedStartLocation %d", (int)(INT_PTR)a0); return S_OK; }
static HRESULT get_start(void *This, void *a0) { PickState *p = (PickState *)rt_user(This); if (!a0) return E_POINTER; *(int *)a0 = p && p->start ? p->start - 1 : 0; return S_OK; }
static HRESULT put_commit(void *This, void *a0) { PickState *p = pstate(This); if (p) lstrcpynW(p->commit, hs_text(a0), 80); return S_OK; }
static HRESULT get_commit(void *This, void *a0) { PickState *p = (PickState *)rt_user(This); if (!a0) return E_POINTER; *(void **)a0 = p && p->commit[0] ? rt_hstring(p->commit) : NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_get_FileTypeFilter(void *This, void *a0) { return get_filter(This, (void **)a0); }
HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_put_SuggestedStartLocation(void *This, void *a0) { return put_start(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_get_SuggestedStartLocation(void *This, void *a0) { return get_start(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_put_CommitButtonText(void *This, void *a0) { return put_commit(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_get_CommitButtonText(void *This, void *a0) { return get_commit(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFolderPicker_get_FileTypeFilter(void *This, void *a0) { return get_filter(This, (void **)a0); }
HRESULT STDMETHODCALLTYPE S_IFolderPicker_put_SuggestedStartLocation(void *This, void *a0) { return put_start(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFolderPicker_get_SuggestedStartLocation(void *This, void *a0) { return get_start(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFolderPicker_put_CommitButtonText(void *This, void *a0) { return put_commit(This, a0); }
HRESULT STDMETHODCALLTYPE S_IFolderPicker_get_CommitButtonText(void *This, void *a0) { return get_commit(This, a0); }

/* the app's types (".mp3", ".flac", ... or "*") -> "*.mp3;*.flac;..." ; NULL when there is no list or it has "*" (then: all files) */
static WCHAR *filter_spec(void *picker)
{
    IUnknown **it = NULL; int n = rt_coll_items(rt_held(picker), &it), i; size_t len = 1; WCHAR *s;
    if (n <= 0) return NULL;
    for (i = 0; i < n; i++) { const WCHAR *e = hs_text(it[i]); if (!e[0] || !lstrcmpW(e, L"*")) return NULL; len += lstrlenW(e) + 3; }
    if (!(s = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof *s))) return NULL;
    for (i = 0; i < n; i++) { const WCHAR *e = hs_text(it[i]); if (i) lstrcatW(s, L";"); lstrcatW(s, L"*"); if (e[0] != L'.') lstrcatW(s, L"."); lstrcatW(s, e); }
    return s;
}
/* PickerLocationId -> known folder (Objects3D / Unspecified: none) */
static const KNOWNFOLDERID *start_folder(int id)
{
    switch (id) { case 0: return &FOLDERID_DocumentsLibrary; case 1: return &FOLDERID_ComputerFolder; case 2: return &FOLDERID_Desktop;
                  case 3: return &FOLDERID_Downloads; case 4: return &FOLDERID_HomeGroup; case 5: return &FOLDERID_MusicLibrary;
                  case 6: return &FOLDERID_PicturesLibrary; case 7: return &FOLDERID_VideosLibrary; default: return NULL; }
}
/* shows the dialog; returns the number of chosen paths (0: cancelled / failed). paths: CoTaskMemFree each. picker: the app's
   FileOpenPicker / FolderPicker (its settings), or NULL. */
static int run_picker(void *picker, int folders, int multi, WCHAR **paths, int max)
{
    IFileOpenDialog *dlg = NULL; HRESULT hr, hrinit; FILEOPENDIALOGOPTIONS op = 0; int n = 0; WCHAR *spec = NULL;
    PickState *ps = picker ? (PickState *)rt_user(picker) : NULL;
    hrinit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* normally already initialised (S_FALSE / RPC_E_CHANGED_MODE) */
    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&dlg);
    rt_log("picker: CoInitializeEx 0x%08lX, FileOpenDialog -> 0x%08lX (thread %lu)", (unsigned long)hrinit, (unsigned long)hr, GetCurrentThreadId());
    if (FAILED(hr)) goto out;
    IFileOpenDialog_GetOptions(dlg, &op);
    op |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST) | (multi ? FOS_ALLOWMULTISELECT : 0);
    IFileOpenDialog_SetOptions(dlg, op);
    if (!folders) { COMDLG_FILTERSPEC f[2] = { { L"Supported files", NULL }, { L"All files", L"*.*" } };
        spec = picker ? filter_spec(picker) : NULL;
        if (spec) { f[0].pszSpec = spec; IFileOpenDialog_SetFileTypes(dlg, 2, f); } else IFileOpenDialog_SetFileTypes(dlg, 1, f + 1);
        IFileOpenDialog_SetFileTypeIndex(dlg, 1);
        rt_log("picker: file types %ls", spec ? spec : L"(all)"); }
    if (ps && ps->start) { const KNOWNFOLDERID *kf = start_folder(ps->start - 1); PIDLIST_ABSOLUTE pidl = NULL; IShellItem *si = NULL;
        if (kf && SUCCEEDED(SHGetKnownFolderIDList(kf, 0, NULL, &pidl))) {
            if (SUCCEEDED(SHCreateItemFromIDList(pidl, &IID_IShellItem, (void **)&si))) { IFileOpenDialog_SetDefaultFolder(dlg, si); IShellItem_Release(si); }
            CoTaskMemFree(pidl); } }
    if (ps && ps->commit[0]) IFileOpenDialog_SetOkButtonLabel(dlg, ps->commit);
    { HWND ow = owner_window(); rt_log("picker: showing the dialog, owner %p", (void *)ow); hr = IFileOpenDialog_Show(dlg, ow); }
    if (FAILED(hr)) { rt_log("picker: dialog closed (0x%08lX)", (unsigned long)hr); goto out; }
    if (multi) {
        IShellItemArray *arr = NULL; DWORD c = 0, i;
        if (SUCCEEDED(IFileOpenDialog_GetResults(dlg, &arr))) {
            IShellItemArray_GetCount(arr, &c);
            for (i = 0; i < c && n < max; i++) { IShellItem *it = NULL; WCHAR *p = NULL;
                if (SUCCEEDED(IShellItemArray_GetItemAt(arr, i, &it))) { if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p))) paths[n++] = p; IShellItem_Release(it); } }
            IShellItemArray_Release(arr);
        }
    } else {
        IShellItem *it = NULL; WCHAR *p = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(dlg, &it))) { if (SUCCEEDED(IShellItem_GetDisplayName(it, SIGDN_FILESYSPATH, &p))) paths[n++] = p; IShellItem_Release(it); }
    }
out:
    if (spec) HeapFree(GetProcessHeap(), 0, spec);
    if (dlg) IFileOpenDialog_Release(dlg);
    if (SUCCEEDED(hrinit)) CoUninitialize();
    return n;
}
int run_dialog(int folders, int multi, WCHAR **paths, int max) { return run_picker(NULL, folders, multi, paths, max); }   /* also used by rt_library.c */
/* cancelled: a completed operation whose result is NULL, as on Win10. A plain rt_new_async would hand out an inert stand-in
   StorageFolder / StorageFile, which the app takes for a real pick (Media Player fail-fast E_POINTER on folder cancel). */
static void *null_op(const char *name)
{
    void *op = rt_new_async_pending(rt_async_named(name));
    if (op) rt_async_complete(op, NULL, NULL, 0);
    return op;
}

HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_PickSingleFileAsync(void *This, void *a0)
{
    WCHAR *p[1]; IUnknown *op = NULL;
    if (!a0) return E_POINTER; *(void **)a0 = NULL;
    rt_log("FileOpenPicker.PickSingleFileAsync");
    if (run_picker(This, 0, 0, p, 1) == 1) { from_path(L"Windows.Storage.StorageFile", &IID_IStorageFileStatics_, p[0], &op); if (op) rt_mru_add_path(p[0], 0); CoTaskMemFree(p[0]); }
    if (op) { *(void **)a0 = op; return S_OK; }   /* the real operation: same IAsyncOperation<StorageFile> */
    *(void **)a0 = null_op("Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>");
    return *(void **)a0 ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE S_IFileOpenPicker_PickMultipleFilesAsync(void *This, void *a0)
{
    enum { MAXF = 4096 }; WCHAR **p = (WCHAR **)HeapAlloc(GetProcessHeap(), 0, MAXF * sizeof *p); IUnknown **files = NULL; int n = 0, k = 0, i; void *vec;
    if (!a0) return E_POINTER; *(void **)a0 = NULL;
    rt_log("FileOpenPicker.PickMultipleFilesAsync");
    if (p) n = run_picker(This, 0, 1, p, MAXF);
    if (n) files = (IUnknown **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof *files);
    for (i = 0; i < n; i++) { IUnknown *op = NULL;
        if (files && SUCCEEDED(from_path(L"Windows.Storage.StorageFile", &IID_IStorageFileStatics_, p[i], &op)) && op) { IUnknown *f = await_obj(op); if (f) { files[k++] = f; rt_mru_add_path(p[i], 0); } op->lpVtbl->Release(op); }
        CoTaskMemFree(p[i]); }
    if (p) HeapFree(GetProcessHeap(), 0, p);
    vec = rt_new_coll_items(rt_coll_named("Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFile>"), files, k);
    for (i = 0; i < k; i++) files[i]->lpVtbl->Release(files[i]);
    if (files) HeapFree(GetProcessHeap(), 0, files);
    rt_log("picker: %d of %d file(s)", k, n);
    if (!vec) return E_OUTOFMEMORY;
    *(void **)a0 = rt_new_async_result(rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFile>>"), (IUnknown *)vec);
    ((IUnknown *)vec)->lpVtbl->Release((IUnknown *)vec);
    return *(void **)a0 ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE S_IFolderPicker_PickSingleFolderAsync(void *This, void *a0)
{
    WCHAR *p[1]; IUnknown *op = NULL;
    if (!a0) return E_POINTER; *(void **)a0 = NULL;
    rt_log("FolderPicker.PickSingleFolderAsync");
    if (run_picker(This, 1, 0, p, 1) == 1) { from_path(L"Windows.Storage.StorageFolder", &IID_IStorageFolderStatics_, p[0], &op); if (op) rt_mru_add_path(p[0], 1); CoTaskMemFree(p[0]); }
    /* waited here (the app got E_UNEXPECTED from the real operation's result); the app gets a completed operation with the folder */
    if (op) { IUnknown *fo = await_obj(op); op->lpVtbl->Release(op); rt_log("picker: folder object %p", (void *)fo);
        if (fo) { *(void **)a0 = rt_new_async_result(rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>"), fo); fo->lpVtbl->Release(fo); if (*(void **)a0) return S_OK; } }
    *(void **)a0 = null_op("Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>");
    return *(void **)a0 ? S_OK : E_OUTOFMEMORY;
}
