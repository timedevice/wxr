/* dcomp_textproxy.c - text on Win7.
 * Win10 d2d1 on the Win7 D3D runtime draws no glyphs at all (DrawText / DrawGlyphRun leave the target untouched; shapes work, DWrite's own
 * rasterizer works). XAML draws text both into the context dcomp's BeginDraw hands out and with its own context onto DXGI surfaces (large
 * text), so d2d1's two DrawGlyphRun implementations are inline-hooked (ID2D1RenderTarget slot 29 = d2d1+71790, ID2D1DeviceContext slot 82 =
 * d2d1+718f0, Win10 19041 d2d1; code bytes verified): the run is rasterized on the CPU with DWrite (grayscale, under the context's transform)
 * and the coverage painted with FillOpacityMask. */
#include "dcomp_priv.h"

/* A loaded Win10 DLL by its export-directory name ("DWrite.dll"): Win7's copy of the same name is loaded too, so the module is told apart
   by its export directory rather than by file name. */
HMODULE dcomp_module_by_export_name(const char *name)
{
    typedef BOOL (WINAPI *PEPM)(HANDLE, HMODULE *, DWORD, DWORD *); HMODULE mods[512], self = NULL; DWORD need = 0, i, n;
    PEPM epm = (PEPM)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules");
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)dcomp_module_by_export_name, &self);
    if (!epm || !epm(GetCurrentProcess(), mods, sizeof mods, &need)) return NULL;
    n = need / sizeof(HMODULE); if (n > 512) n = 512;
    {   /* Win7's own DLL of the same name (dwrite, d2d1) may also be loaded: the copy outside Win7's System32 wins (WXR keeps file names) */
        WCHAR sys[MAX_PATH], p[MAX_PATH]; UINT sl = GetSystemDirectoryW(sys, MAX_PATH); HMODULE first = NULL;
        for (i = 0; i < n; i++) {
            BYTE *b = (BYTE *)mods[i]; IMAGE_NT_HEADERS *nt; IMAGE_DATA_DIRECTORY *dd;
            if (mods[i] == self || ((IMAGE_DOS_HEADER *)b)->e_magic != IMAGE_DOS_SIGNATURE) continue;
            nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew); dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if (!dd->VirtualAddress || !dd->Size || lstrcmpiA((const char *)(b + ((IMAGE_EXPORT_DIRECTORY *)(b + dd->VirtualAddress))->Name), name)) continue;
            if (!GetModuleFileNameW(mods[i], p, MAX_PATH) || !sl || _wcsnicmp(p, sys, sl) || p[sl] != L'\\') return mods[i];
            if (!first) first = mods[i];
        }
        return first; }
}

/* ---- CPU glyph rasterization (IDWriteFactory2::CreateGlyphRunAnalysis, grayscale) ---- */
typedef struct { float m11, m12, m21, m22, dx, dy; } DWMatrix_;
static void *dw_factory2(void)
{
    static void *f; static LONG tried; static const GUID iid_f2 = {0x0439fc60, 0xca44, 0x4994, {0x8d, 0xee, 0x3a, 0x9a, 0xf7, 0xb7, 0x32, 0xec}};
    if (!f && !InterlockedExchange(&tried, 1)) {
        /* the DWrite XAML uses (Win10's). Win7's DWrite has the same export-directory name and may be loaded first (WXR loader: both keep
           their file names), so every module called DWrite.dll is tried until one gives IDWriteFactory2 (Win7's stops at IDWriteFactory1). */
        typedef HRESULT (WINAPI *PDW)(int, REFIID, void **); typedef BOOL (WINAPI *PEPM)(HANDLE, HMODULE *, DWORD, DWORD *);
        PEPM epm = (PEPM)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModules"); HMODULE mods[512]; DWORD need = 0, i, n = 0;
        if (epm && epm(GetCurrentProcess(), mods, sizeof mods, &need)) n = need / sizeof(HMODULE) > 512 ? 512 : need / sizeof(HMODULE);
        for (i = 0; i < n && !f; i++) {
            BYTE *b = (BYTE *)mods[i]; IMAGE_NT_HEADERS *nt; IMAGE_DATA_DIRECTORY *dd; PDW c;
            if (((IMAGE_DOS_HEADER *)b)->e_magic != IMAGE_DOS_SIGNATURE) continue;
            nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew); dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if (!dd->VirtualAddress || lstrcmpiA((const char *)(b + ((IMAGE_EXPORT_DIRECTORY *)(b + dd->VirtualAddress))->Name), "DWrite.dll")) continue;
            if ((c = (PDW)GetProcAddress(mods[i], "DWriteCreateFactory")) != NULL && SUCCEEDED(c(0, &iid_f2, &f)) && f) comp_log("text proxy: IDWriteFactory2 from module %p\n", (void *)mods[i]);
            else f = NULL; }
        if (!f) comp_log("text proxy: no IDWriteFactory2 in any loaded DWrite.dll\n");
    }
    return f;
}
#define VT(o, k, T) ((T)(*(void ***)(o))[k])
static void cpu_glyph_run(ID2D1DeviceContext *dc, D2D1_POINT_2F org, const void *run, ID2D1Brush *brush, int mm)
{
    void *f = dw_factory2(), *ana = NULL; D2D1_MATRIX_3X2_F m, id = { 1, 0, 0, 1, 0, 0 }; DWMatrix_ dm; RECT b; HRESULT hr; int mode, type = 0; UINT32 w, h, i; BYTE *buf;
    ID2D1Bitmap *bmp = NULL; D2D1_BITMAP_PROPERTIES bp; D2D1_SIZE_U sz; D2D1_RECT_F dst; D2D1_ANTIALIAS_MODE aa; static LONG nlog;
    if (!f || !run) return;
    DCB_(GetTransform, dc, &m);
    dm.m11 = m._11; dm.m12 = m._12; dm.m21 = m._21; dm.m22 = m._22; dm.dx = org.x * m._11 + org.y * m._21 + m._31; dm.dy = org.x * m._12 + org.y * m._22 + m._32;
    mode = mm == 1 ? 2 : mm == 2 ? 3 : 5;                                        /* GDI_CLASSIC / GDI_NATURAL / NATURAL_SYMMETRIC */
    /* IDWriteFactory2::CreateGlyphRunAnalysis (slot 30): run, transform, rendering mode, measuring mode, grid fit default, antialias grayscale, origin 0,0 */
    hr = VT(f, 30, HRESULT (WINAPI *)(void *, const void *, const DWMatrix_ *, int, int, int, int, float, float, void **))(f, run, &dm, mode, mm, 0, 1, 0.0f, 0.0f, &ana);
    if (FAILED(hr)) hr = VT(f, 30, HRESULT (WINAPI *)(void *, const void *, const DWMatrix_ *, int, int, int, int, float, float, void **))(f, run, &dm, 1, mm, 0, 1, 0.0f, 0.0f, &ana);   /* ALIASED */
    if (FAILED(hr) || !ana) { if (InterlockedIncrement(&nlog) <= 5) comp_log("text proxy: CreateGlyphRunAnalysis failed 0x%08lX\n", (unsigned long)hr); return; }
    memset(&b, 0, sizeof b);
    VT(ana, 3, HRESULT (WINAPI *)(void *, int, RECT *))(ana, type, &b);           /* GetAlphaTextureBounds(ALIASED_1x1) */
    if (b.right <= b.left || b.bottom <= b.top) {   /* blank glyphs (spaces) */
        ((IUnknown *)ana)->lpVtbl->Release((IUnknown *)ana); return; }
    w = (UINT32)(b.right - b.left); h = (UINT32)(b.bottom - b.top);
    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h); if (!buf) { ((IUnknown *)ana)->lpVtbl->Release((IUnknown *)ana); return; }
    hr = VT(ana, 4, HRESULT (WINAPI *)(void *, int, const RECT *, BYTE *, UINT32))(ana, type, &b, buf, w * h);   /* CreateAlphaTexture */
    ((IUnknown *)ana)->lpVtbl->Release((IUnknown *)ana);
    if (SUCCEEDED(hr)) {
        for (i = 0; i < w * h && !buf[i]; i++) ;
        {
          typedef struct { void *face; float em; UINT32 n; const UINT16 *idx; } GRHead_; const GRHead_ *g = (const GRHead_ *)run;
          if (InterlockedIncrement(&nlog) <= 8) comp_log("text proxy: glyph run %ux%u at (%ld,%ld) em %.1f glyphs %u first %u, coverage %s\n", w, h, b.left, b.top, g->em, g->n, g->n ? g->idx[0] : 0, i < w * h ? "yes" : "none"); }
        sz.width = w; sz.height = h; memset(&bp, 0, sizeof bp); bp.pixelFormat.format = DXGI_FORMAT_A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        DCB_(GetDpi, dc, &bp.dpiX, &bp.dpiY);
        if (SUCCEEDED(DCB_(CreateBitmap, dc, sz, buf, w, &bp, &bmp))) {
            float sx = bp.dpiX > 0 ? 96.0f / bp.dpiX : 1.0f, sy = bp.dpiY > 0 ? 96.0f / bp.dpiY : 1.0f;   /* device pixels -> DIPs */
            dst.left = b.left * sx; dst.top = b.top * sy; dst.right = b.right * sx; dst.bottom = b.bottom * sy;
            aa = DCB_(GetAntialiasMode, dc); DCB_SetTransform(dc, &id); DCB_SetAntialiasMode(dc, D2D1_ANTIALIAS_MODE_ALIASED);
            {   /* XAML's brush can come from another D2D factory than this context (Clock's A8 text surfaces: EndDraw failed with
                   D2DERR_WRONG_FACTORY and XAML fail-fast). A solid colour brush is re-made on this context, same colour and opacity. */
                static const GUID iid_scb = {0x2cd906a9, 0x12e2, 0x11dc, {0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9}};
                void *scb = NULL, *own = NULL; D2D1_COLOR_F col; D2D1_BRUSH_PROPERTIES bpr = { 1.0f, { 1, 0, 0, 1, 0, 0 } };
                if (brush && SUCCEEDED(((IUnknown *)brush)->lpVtbl->QueryInterface((IUnknown *)brush, &iid_scb, &scb)) && scb) {
                    VT(scb, 9, D2D1_COLOR_F *(WINAPI *)(void *, D2D1_COLOR_F *))(scb, &col);              /* GetColor (struct return) */
                    bpr.opacity = VT(scb, 6, float (WINAPI *)(void *))(scb);                             /* GetOpacity */
                    VT(dc, 8, HRESULT (WINAPI *)(void *, const D2D1_COLOR_F *, const D2D1_BRUSH_PROPERTIES *, void **))(dc, &col, &bpr, &own);   /* CreateSolidColorBrush */
                    ((IUnknown *)scb)->lpVtbl->Release((IUnknown *)scb); }
                DCB_(FillOpacityMask, dc, bmp, own ? (ID2D1Brush *)own : brush, D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL, &dst, NULL);
                if (own) ((IUnknown *)own)->lpVtbl->Release((IUnknown *)own); }
            DCB_SetAntialiasMode(dc, aa); DCB_SetTransform(dc, &m);
            ((IUnknown *)bmp)->lpVtbl->Release((IUnknown *)bmp);
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}
static void STDMETHODCALLTYPE hk_rt_drawglyphrun(ID2D1DeviceContext *dc, D2D1_POINT_2F org, const void *run, ID2D1Brush *brush, int mm) { cpu_glyph_run(dc, org, run, brush, mm); }
static void STDMETHODCALLTYPE hk_dc_drawglyphrun(ID2D1DeviceContext *dc, D2D1_POINT_2F org, const void *run, const void *desc, ID2D1Brush *brush, int mm) { (void)desc; cpu_glyph_run(dc, org, run, brush, mm); }

/* inline hooks: replace the first bytes of each implementation with `mov rax, hook; jmp rax` (the originals are never called again) */
typedef struct { DWORD rva; BYTE len; BYTE code[14]; } TextHook_;
static const TextHook_ g_th[2] = {
    { 0x71790, 14, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56 } },    /* RT::DrawGlyphRun */
    { 0x718f0, 13, { 0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57 } } };        /* DC::DrawGlyphRun */
/* called with a device context of the d2d1 that XAML uses (a surface's s->dc): finds that module and hooks it once */
void dc_text_hooks_install(void *dc)
{
    static LONG done; HMODULE hm = NULL; BYTE *base; DWORD size, old; int i; void *hooks[2] = { (void *)hk_rt_drawglyphrun, (void *)hk_dc_drawglyphrun };
    if (!dc || InterlockedExchange(&done, 1)) return;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)**(void ***)dc, &hm) || !hm) return;
    base = (BYTE *)hm; size = ((IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew))->OptionalHeader.SizeOfImage;
    { WCHAR p[MAX_PATH] = L"?"; GetModuleFileNameW(hm, p, MAX_PATH); comp_log("text hooks: context %p belongs to %ls\n", dc, p); }
    for (i = 0; i < 2; i++) {
        /* the implementation is found through the context's own vtable (slot 29 / 82), so any d2d1 build works (19041.6456: the
           fixed RVAs in g_th, 19041.3803 WXR image: elsewhere); jumps (incremental-link / import thunks) are followed */
        static const int slot[2] = { 29, 82 }; BYTE *f = (BYTE *)(*(void ***)dc)[slot[i]]; int hop;
        for (hop = 0; hop < 4 && f; hop++) {
            if (f[0] == 0xE9) f = f + 5 + *(INT32 *)(f + 1);
            else if (f[0] == 0xEB) f = f + 2 + (signed char)f[1];
            else if (f[0] == 0xFF && f[1] == 0x25) f = *(BYTE **)(f + 6 + *(INT32 *)(f + 2));
            else break; }
        if (!f || f < base || f + 16 > base + size) { comp_log("text hook %d: slot %d is outside d2d1, text stays blank\n", i, slot[i]); continue; }
        if ((DWORD)(f - base) == g_th[i].rva && memcmp(f, g_th[i].code, g_th[i].len)) { comp_log("text hook %d: unexpected code at d2d1+%lx, text stays blank\n", i, (unsigned long)g_th[i].rva); continue; }
        comp_log("text hook %d: slot %d -> d2d1+%lx (%02x %02x %02x %02x %02x %02x)\n", i, slot[i], (unsigned long)(f - base), f[0], f[1], f[2], f[3], f[4], f[5]);
        if (VirtualProtect(f, 12, PAGE_EXECUTE_READWRITE, &old)) {
            f[0] = 0x48; f[1] = 0xB8; *(void **)(f + 2) = hooks[i]; f[10] = 0xFF; f[11] = 0xE0;
            VirtualProtect(f, 12, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 12);
            comp_log("text hook %d armed at d2d1+%lx (glyph runs rasterized by DWrite on the CPU)\n", i, (unsigned long)(f - base));
        }
    }
}
