/* rt_storecfg.c - Windows.ApplicationModel.Store.Preview.StoreConfiguration.FilterUnsupportedSystemFeaturesAsync, hand-written.
 *
 * The Store hands in the hardware features a product asks for (StoreSystemFeature values) and gets back those THIS machine has;
 * whatever is missing from the answer is shown as "not met" under System requirements. The generated stand-in answered with an
 * empty list, so every product claimed e.g. "less than 2 GB of memory". Now answered from the
 * machine: installed memory, the first display adapter's dedicated memory, x86/x64, Direct3D up to 11. Not offered on purpose:
 * Direct3D 12 (Windows 7 has none), ARM, cameras and sensors (not looked up). */
#include "rt_core.h"
#include <stdio.h>

enum { F_X86 = 0, F_X64, F_ARM, F_DX9, F_DX10, F_DX11, F_D3D12_FL11, F_D3D12_FL12, F_MEM300MB, F_MEM750MB, F_MEM1GB, F_MEM2GB,
       F_CAMFRONT, F_CAMREAR, F_GYRO, F_HOVER, F_MAGNETO, F_NFC, F_RES720P, F_RESWVGA, F_RESWVGA_OR_720P, F_RESWXGA, F_RESWVGA_OR_WXGA,
       F_RESWXGA_OR_720P, F_MEM4GB, F_MEM6GB, F_MEM8GB, F_MEM12GB, F_MEM16GB, F_MEM20GB, F_VMEM2GB, F_VMEM4GB, F_VMEM6GB, F_VMEM1GB, F_ARM64 };

/* installed memory in MB: the SMBIOS figure when Windows has it (what Win10 reports), else what the OS can use */
static unsigned long long mem_mb(void)
{
    typedef BOOL (WINAPI *PGPISM)(PULONGLONG); ULONGLONG kb = 0; MEMORYSTATUSEX ms;
    PGPISM f = (PGPISM)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetPhysicallyInstalledSystemMemory");
    if (f && f(&kb) && kb) return kb / 1024;
    ms.dwLength = sizeof ms; if (!GlobalMemoryStatusEx(&ms)) return 0;
    return ms.ullTotalPhys / (1024 * 1024);
}
/* dedicated memory of the first display adapter in MB (IDXGIFactory::EnumAdapters slot 7, IDXGIAdapter::GetDesc slot 8); 0 if unknown */
static unsigned long long vmem_mb(void)
{
    static const GUID IID_IDXGIFactory_ = {0x7b7166ec, 0x21c7, 0x44ae, {0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69}};
    typedef HRESULT (WINAPI *PCF)(REFIID, void **);
    struct { WCHAR Description[128]; UINT VendorId, DeviceId, SubSysId, Revision; SIZE_T DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory; LUID AdapterLuid; } d;
    HMODULE m = LoadLibraryA("dxgi.dll"); PCF cf = m ? (PCF)GetProcAddress(m, "CreateDXGIFactory") : NULL; IUnknown *fac = NULL, *ad = NULL; unsigned long long mb = 0;
    if (!cf || FAILED(cf(&IID_IDXGIFactory_, (void **)&fac)) || !fac) return 0;
    if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, UINT, IUnknown **))(*(void ***)fac)[7])(fac, 0, &ad)) && ad) {
        memset(&d, 0, sizeof d);
        if (SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, void *))(*(void ***)ad)[8])(ad, &d))) mb = (unsigned long long)d.DedicatedVideoMemory / (1024 * 1024);
        ad->lpVtbl->Release(ad); }
    fac->lpVtbl->Release(fac);
    return mb;
}
/* "N GB" on the box is a little less in fact (firmware reservations, 4096 MB cards reporting 4040): 6 % tolerance */
#define AT_LEAST(have, mb) ((have) * 100 >= (unsigned long long)(mb) * 94)
static int supported(int f, unsigned long long mem, unsigned long long vmem)
{
    switch (f) {
    case F_X86: case F_X64: case F_DX9: case F_DX10: case F_DX11: return 1;
    case F_RES720P: case F_RESWVGA: case F_RESWVGA_OR_720P: case F_RESWXGA: case F_RESWVGA_OR_WXGA: case F_RESWXGA_OR_720P: return 1;
    case F_MEM300MB: return AT_LEAST(mem, 300);   case F_MEM750MB: return AT_LEAST(mem, 750);   case F_MEM1GB: return AT_LEAST(mem, 1024);
    case F_MEM2GB: return AT_LEAST(mem, 2048);    case F_MEM4GB: return AT_LEAST(mem, 4096);    case F_MEM6GB: return AT_LEAST(mem, 6144);
    case F_MEM8GB: return AT_LEAST(mem, 8192);    case F_MEM12GB: return AT_LEAST(mem, 12288);  case F_MEM16GB: return AT_LEAST(mem, 16384);
    case F_MEM20GB: return AT_LEAST(mem, 20480);
    case F_VMEM1GB: return AT_LEAST(vmem, 1024);  case F_VMEM2GB: return AT_LEAST(vmem, 2048);  case F_VMEM4GB: return AT_LEAST(vmem, 4096);
    case F_VMEM6GB: return AT_LEAST(vmem, 6144);
    default: return 0;   /* ARM, ARM64, Direct3D 12, cameras, sensors, values newer than this list */
    }
}

HRESULT STDMETHODCALLTYPE S_IStoreConfigurationStatics_FilterUnsupportedSystemFeaturesAsync(void *This, void *a0, void *a1)
{
    static unsigned long long mem, vmem; static int probed;
    int in[128], out[128], nin = 0, nout = 0, i; IUnknown *it = NULL; void *coll, *op;
    const RtColl *c = rt_coll_named("Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Store.Preview.StoreSystemFeature>");
    const RtAsync *a = rt_async_named("Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Store.Preview.StoreSystemFeature>>");
    (void)This; rt_called("IStoreConfigurationStatics.FilterUnsupportedSystemFeaturesAsync", __builtin_return_address(0));
    if (!a1) return E_POINTER; *(void **)a1 = NULL; if (!c || !a) return E_NOTIMPL;
    if (!probed) { mem = mem_mb(); vmem = vmem_mb(); probed = 1; rt_log("StoreConfiguration: %llu MB memory, %llu MB video memory", mem, vmem); }
    /* IIterable<StoreSystemFeature>::First (slot 6) -> IIterator: get_Current 6, get_HasCurrent 7, MoveNext 8 */
    if (a0 && SUCCEEDED(((HRESULT (STDMETHODCALLTYPE *)(void *, IUnknown **))(*(void ***)a0)[6])(a0, &it)) && it) {
        BYTE has = 0;
        ((HRESULT (STDMETHODCALLTYPE *)(void *, BYTE *))(*(void ***)it)[7])(it, &has);
        while (has && nin < 128) {
            int v = -1; if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, int *))(*(void ***)it)[6])(it, &v))) break;
            in[nin++] = v; has = 0;
            if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, BYTE *))(*(void ***)it)[8])(it, &has))) break; }
        it->lpVtbl->Release(it); }
    for (i = 0; i < nin; i++) if (supported(in[i], mem, vmem)) out[nout++] = in[i];
    { static LONG nlog; if (InterlockedIncrement(&nlog) <= 6) { char b[400]; int k = 0; b[0] = 0; for (i = 0; i < nin && k < 380; i++) k += sprintf(b + k, "%d%s ", in[i], supported(in[i], mem, vmem) ? "" : "(no)"); rt_log("StoreConfiguration.FilterUnsupportedSystemFeatures: %s", b); } }
    coll = rt_new_coll_ints(c, out, nout); if (!coll) return E_OUTOFMEMORY;
    op = rt_new_async_result(a, (IUnknown *)coll); rt_Release(coll);
    if (!op) return E_OUTOFMEMORY;
    *(void **)a1 = op; return S_OK;
}

/* ---- IStoreConfigurationStatics4.GetStoreWebAccountId: the signed-in account the Store would SHOW. Reports EMPTY on purpose.
 * The functional sign-in (the user ticket on the order) works through rt_onlineid/rt_msa regardless; this is only the cosmetic
 * "signed in as <you>" chip. Returning the account id here flips the Store into full signed-in mode, where it fans out to many
 * authenticated backend calls (its own Windows.Web.Http requests, an Xbox xsts token, the Graph profile) - our user token is
 * not accepted across all of those, so the Store concludes it is offline and locks the UI. Making the
 * chip show would require the token to satisfy those services too (deep, unbounded) for a cosmetic gain, so it stays empty. */
HRESULT STDMETHODCALLTYPE S_IStoreConfigurationStatics4_GetStoreWebAccountId(void *This, void *out)
{ (void)This; if (!out) return E_POINTER; *(void **)out = NULL; return S_OK; }
HRESULT STDMETHODCALLTYPE S_IStoreConfigurationStatics4_GetStoreWebAccountIdForUser(void *This, void *user, void *out)
{ (void)user; return S_IStoreConfigurationStatics4_GetStoreWebAccountId(This, out); }
