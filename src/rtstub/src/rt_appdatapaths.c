/* rt_appdatapaths.c - Windows.Storage.AppDataPaths getters (IAppDataPaths). Win10's implementation derives the paths from package
 * infrastructure this system does not have (0x80073D54, Media Player's startup threw); here they are the user's ordinary shell folders. */
#include "rt_core.h"
#include <shlobj.h>

static HRESULT path_of(int csidl, const WCHAR *sub, void **out)
{
    WCHAR p[MAX_PATH + 32];
    if (!out) return E_POINTER; *out = NULL;
    if (FAILED(SHGetFolderPathW(NULL, csidl, NULL, 0, p))) return E_FAIL;
    if (sub) lstrcatW(p, sub);
    *out = rt_hstring(p); return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_Cookies(void *This, void **o) { (void)This; return path_of(CSIDL_COOKIES, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_Desktop(void *This, void **o) { (void)This; return path_of(CSIDL_DESKTOPDIRECTORY, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_Documents(void *This, void **o) { (void)This; return path_of(CSIDL_PERSONAL, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_Favorites(void *This, void **o) { (void)This; return path_of(CSIDL_FAVORITES, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_History(void *This, void **o) { (void)This; return path_of(CSIDL_HISTORY, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_InternetCache(void *This, void **o) { (void)This; return path_of(CSIDL_INTERNET_CACHE, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_LocalAppData(void *This, void **o) { (void)This; return path_of(CSIDL_LOCAL_APPDATA, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_ProgramData(void *This, void **o) { (void)This; return path_of(CSIDL_COMMON_APPDATA, NULL, o); }
HRESULT STDMETHODCALLTYPE S_IAppDataPaths_get_RoamingAppData(void *This, void **o) { (void)This; return path_of(CSIDL_APPDATA, NULL, o); }
