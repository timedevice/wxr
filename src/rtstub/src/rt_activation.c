/* rt_activation.c - Windows.ApplicationModel.AppInstance.GetActivatedEventArgs for packaged (Windows App SDK / WinUI 3) apps.
 *
 * WinUI 3 and packaged Win32 apps (App Installer, the winget UI) do NOT use CoreApplication::RegisterActivatableApplication
 * (the UWP path the loader layer's shell broker drives). They call AppInstance.GetActivatedEventArgs() to learn how they were
 * started. The generated stand-in returned NULL -> the app saw "no activation args" and treated every launch as a plain launch
 * (App Installer then sat on "Hang on while we get your app's information..."). Here we hand back the FileActivatedEventArgs the
 * loader layer builds from the command-line file (when the app was opened with an .appx/.msix(bundle)), so opening a package
 * reaches the app. The args object and its real Windows.Storage item live in wxrsys (the one place that can make a storage item);
 * this only forwards. S_OK with a NULL out keeps the prior "plain launch" behaviour. */
#include "rt_core.h"

HRESULT STDMETHODCALLTYPE S_IAppInstanceStatics_GetActivatedEventArgs(void *This, void *out)
{
    typedef HRESULT (WINAPI *PGA)(void **); static PGA fn; static int tried;
    void **result = (void **)out; HRESULT hr = S_FALSE;
    (void)This;
    if (!result) return S_OK;
    *result = NULL;
    if (!tried) { HMODULE m = GetModuleHandleW(L"wxrsys.dll"); fn = m ? (PGA)GetProcAddress(m, "WxrGetActivatedEventArgs") : NULL; tried = 1; }
    if (fn) hr = fn(result);
    rt_log("AppInstance.GetActivatedEventArgs -> %s", (hr == S_OK && *result) ? "FileActivatedEventArgs" : "none (plain launch)");
    return S_OK;
}
