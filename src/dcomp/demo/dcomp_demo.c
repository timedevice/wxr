/* dcomp_demo.c - regression test for dcomp.dll: builds a tree through the *DirectComposition COM API only* (device -> target for HWND -> visuals -> surfaces drawn
 * with Direct2D 1.1 / DirectWrite -> commit).
 *   dcomp_demo.exe                        interactive (ESC closes)
 *   dcomp_demo.exe --snapshot out.png     render once, write the picture, exit
 *   dcomp_demo.exe --dll path\dcomp.dll   use that DLL (default: dcomp.dll next to the exe, never the system one)
 * The interfaces are called through raw vtable slots of the public IDCompositionDesktopDevice / IDCompositionVisual / IDCompositionSurface layouts. */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#define SLOT(o, n) ((*(void ***)(o))[n])
#define CALL(o, n, T, ...) (((T)SLOT(o, n))((o), ##__VA_ARGS__))
typedef HRESULT (STDMETHODCALLTYPE *F_v)(void *);
typedef HRESULT (STDMETHODCALLTYPE *F_pp)(void *, void **);
typedef HRESULT (STDMETHODCALLTYPE *F_f)(void *, float);
typedef HRESULT (STDMETHODCALLTYPE *F_p)(void *, void *);
typedef HRESULT (STDMETHODCALLTYPE *F_visual)(void *, void *, BOOL, void *);
typedef HRESULT (STDMETHODCALLTYPE *F_target)(void *, HWND, BOOL, void **);
typedef HRESULT (STDMETHODCALLTYPE *F_surface)(void *, UINT, UINT, DXGI_FORMAT, DXGI_ALPHA_MODE, void **);
typedef HRESULT (STDMETHODCALLTYPE *F_begin)(void *, const RECT *, REFIID, void **, POINT *);
typedef HRESULT (STDMETHODCALLTYPE *F_matrix)(void *, const D2D_MATRIX_3X2_F *);
typedef HRESULT (STDMETHODCALLTYPE *F_rect)(void *, const D2D_RECT_F *);
typedef HRESULT (STDMETHODCALLTYPE *F_content)(void *, IUnknown *);
typedef HRESULT (STDMETHODCALLTYPE *F_create2)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *F_png)(HWND, const WCHAR *);
typedef void (WINAPI *F_sethwnd)(HWND);
static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)
#define OK(e) do { HRESULT _h = (e); if (FAILED(_h)) { printf("FAIL line %d: %s -> 0x%08lX\n", __LINE__, #e, (unsigned long)_h); g_fail++; } } while (0)
static const GUID IID_Desktop = {0x5f4633fe, 0x1e08, 0x4cb8, {0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02}};
static const GUID IID_Device3_ = {0x0987cb06, 0xf916, 0x48bf, {0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1e, 0x6a}};
static const GUID IID_Visual_ = {0x4d93059d, 0x097b, 0x4651, {0x9a, 0x60, 0xf0, 0xf2, 0x51, 0x16, 0xe2, 0xf3}};
static const GUID IID_Visual3_ = {0x2775f462, 0xb6c1, 0x4015, {0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d}};
static const GUID IID_Surface_ = {0xbb8a4953, 0x2c99, 0x4f5a, {0x96, 0xf5, 0x48, 0x19, 0x02, 0x7f, 0xa3, 0xac}};
static const GUID IID_Target_ = {0xeacdd04c, 0x117e, 0x4e17, {0x88, 0xf4, 0xd1, 0xb1, 0x2b, 0x0e, 0x3d, 0x89}};
static const GUID IID_DC_ = {0xe8f7fe7a, 0x191c, 0x466d, {0xad, 0x95, 0x97, 0x56, 0x78, 0xbd, 0xa9, 0x98}};
static const GUID IID_DWFactory = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};

static void *g_dev;
static void *mkvisual(void) { void *v = NULL; OK(CALL(g_dev, 6, F_pp, &v)); return v; }                 /* IDCompositionDevice2/Desktop::CreateVisual = slot 6 */
static void setpos(void *v, float x, float y) { OK(CALL(v, 4, F_f, x)); OK(CALL(v, 6, F_f, y)); }          /* IDCompositionVisual::SetOffsetX(float) = 4, SetOffsetY(float) = 6 */
static void add(void *parent, void *child) { OK(CALL(parent, 16, F_visual, child, TRUE, NULL)); }         /* AddVisual(child, insertAbove, reference) = 16 */
static void *mksurface(UINT w, UINT h) { void *s = NULL; OK(CALL(g_dev, 8, F_surface, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &s)); return s; }   /* CreateSurface = slot 8 */
static ID2D1DeviceContext *begin(void *surf, POINT *off) { void *dc = NULL; OK(CALL(surf, 3, F_begin, NULL, &IID_DC_, &dc, off)); return (ID2D1DeviceContext *)dc; }
static void end(void *surf) { OK(CALL(surf, 4, F_v)); }
#define RT(dc) ((ID2D1RenderTarget *)(dc))
static void fill(ID2D1DeviceContext *dc, float l, float t, float r, float b, float R, float G, float B, float A)
{
    ID2D1SolidColorBrush *br = NULL; D2D1_COLOR_F c = { R, G, B, A }; D2D1_RECT_F rc = { l, t, r, b };
    OK(RT(dc)->lpVtbl->CreateSolidColorBrush(RT(dc), &c, NULL, &br)); RT(dc)->lpVtbl->FillRectangle(RT(dc), &rc, (ID2D1Brush *)br); ((IUnknown *)br)->lpVtbl->Release((IUnknown *)br);
}
static void with_surface(void *v, void *surf) { OK(CALL(v, 15, F_content, (IUnknown *)surf)); }              /* SetContent = slot 15 */

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
int main(int argc, char **argv)
{
    const char *snap = NULL; const char *dllp = NULL; int i; HMODULE dll; F_create2 create2; F_sethwnd sethwnd; F_png png; WNDCLASSW wc; RECT r = { 0, 0, 800, 600 }; HWND hwnd; MSG msg;
    void *target = NULL, *root, *va, *vb, *vc, *vd, *sa, *sb, *sc; POINT off; ID2D1DeviceContext *dc; WCHAR wsnap[MAX_PATH] = L"", self[MAX_PATH]; (void)create2;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 1; i < argc; i++) { if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snap = argv[++i]; else if (!strcmp(argv[i], "--dll") && i + 1 < argc) dllp = argv[++i]; }
    if (snap) MultiByteToWideChar(CP_ACP, 0, snap, -1, wsnap, MAX_PATH);
    if (!dllp) { char p[MAX_PATH], *s; GetModuleFileNameA(NULL, p, MAX_PATH); s = strrchr(p, '\\'); if (s) s[1] = 0; strcat(p, "dcomp.dll"); dll = LoadLibraryA(p); } else dll = LoadLibraryA(dllp);
    (void)self; if (!dll) { printf("cannot load dcomp.dll (%lu)\n", GetLastError()); return 2; }
    create2 = (F_create2)GetProcAddress(dll, "DCompositionCreateDevice2"); sethwnd = (F_sethwnd)GetProcAddress(dll, "WxrDcomp_SetTargetWindow"); png = (F_png)GetProcAddress(dll, "WxrDcomp_SnapshotToPng");
    if (!create2) { printf("DCompositionCreateDevice2 not found\n"); return 2; }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    memset(&wc, 0, sizeof wc); wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"DcompDemo"; wc.hCursor = LoadCursorW(NULL, IDC_ARROW); RegisterClassW(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);
    hwnd = CreateWindowW(L"DcompDemo", L"dcomp_demo", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 50, 50, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    (void)sethwnd;

    /* 1. device (public IDCompositionDesktopDevice) + target for the window */
    OK(create2(NULL, &IID_Desktop, &g_dev)); CHECK(g_dev != NULL); if (!g_dev) return 1;
    OK(CALL(g_dev, 24, F_target, hwnd, TRUE, &target));                                                 /* CreateTargetForHwnd = slot 24 of the desktop device */
    root = mkvisual();

    /* 2. surfaces drawn with Direct2D 1.1 + DirectWrite */
    sa = mksurface(300, 120); dc = begin(sa, &off);
    if (dc) { IDWriteFactory *dw = NULL; D2D1_COLOR_F bg = { 0.10f, 0.25f, 0.80f, 1 }; RT(dc)->lpVtbl->Clear(RT(dc), &bg);
        fill(dc, 10, 10, 290, 40, 1, 1, 0, 1);
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_DWFactory, (IUnknown **)&dw))) {
            IDWriteTextFormat *tf = NULL; ID2D1SolidColorBrush *wb = NULL; D2D1_COLOR_F white = { 1, 1, 1, 1 }; D2D1_RECT_F rc = { 12, 52, 290, 110 };
            if (SUCCEEDED(dw->lpVtbl->CreateTextFormat(dw, L"Arial", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-us", &tf))) {
                OK(RT(dc)->lpVtbl->CreateSolidColorBrush(RT(dc), &white, NULL, &wb)); RT(dc)->lpVtbl->DrawText(RT(dc), L"dcomp surface", 13, tf, &rc, (ID2D1Brush *)wb, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
                ((IUnknown *)wb)->lpVtbl->Release((IUnknown *)wb); tf->lpVtbl->Release(tf); }
            dw->lpVtbl->Release(dw); }
        ((IUnknown *)dc)->lpVtbl->Release((IUnknown *)dc); }
    end(sa);
    sb = mksurface(120, 120); dc = begin(sb, &off); if (dc) { D2D1_COLOR_F c = { 0.9f, 0.2f, 0.2f, 1 }; RT(dc)->lpVtbl->Clear(RT(dc), &c); fill(dc, 40, 40, 80, 80, 1, 1, 1, 1); ((IUnknown *)dc)->lpVtbl->Release((IUnknown *)dc); } end(sb);
    sc = mksurface(100, 100); dc = begin(sc, &off); if (dc) { D2D1_COLOR_F c = { 0.1f, 0.8f, 0.3f, 1 }; RT(dc)->lpVtbl->Clear(RT(dc), &c); ((IUnknown *)dc)->lpVtbl->Release((IUnknown *)dc); } end(sc);

    /* 3. visual tree */
    va = mkvisual(); with_surface(va, sa); setpos(va, 20, 20);                                          /* 300x120 at (20,20) */
    vb = mkvisual(); with_surface(vb, sb); setpos(vb, 400, 60);                                         /* rotated 20 deg about its centre via a 2x3 matrix */
    { float ca = cosf(20.0f * 3.14159265f / 180.0f), sa_ = sinf(20.0f * 3.14159265f / 180.0f), cx = 60, cy = 60;
      D2D_MATRIX_3X2_F m; m._11 = ca; m._12 = sa_; m._21 = -sa_; m._22 = ca; m._31 = cx - cx * ca + cy * sa_; m._32 = cy - cx * sa_ - cy * ca; OK(CALL(vb, 8, F_matrix, &m)); }   /* SetTransform(matrix) = 8 */
    vc = mkvisual(); with_surface(vc, sc); setpos(vc, 100, 250);                                        /* clipped to its left 60 px, half opaque (Visual3::SetOpacity = slot 26) */
    { D2D_RECT_F cr = { 0, 0, 60, 100 }; OK(CALL(vc, 14, F_rect, &cr)); }                              /* SetClip(rect) = 14 */
    { void *v3 = NULL; if (SUCCEEDED(((IUnknown *)vc)->lpVtbl->QueryInterface((IUnknown *)vc, &IID_Visual3_, &v3))) { OK(CALL(v3, 26, F_f, 0.5f)); ((IUnknown *)v3)->lpVtbl->Release((IUnknown *)v3); } else { printf("FAIL: no IDCompositionVisual3\n"); g_fail++; } }
    vd = mkvisual(); with_surface(vd, sc); setpos(vd, 90, 240);                                         /* opaque, drawn below vc (insertAbove = FALSE) */
    add(root, va); add(root, vb); add(root, vd); OK(CALL(root, 16, F_visual, vc, TRUE, vd));         /* vc above vd */
    OK(CALL(target, 3, F_p, root));                                                                      /* IDCompositionTarget::SetRoot = slot 3 */
    OK(CALL(g_dev, 3, F_v));                                                                             /* Commit */
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    for (i = 0; i < 4; i++) { while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(30); }
    if (snap) {
        if (!png) { printf("snapshot export missing\n"); return 2; }
        OK(png(hwnd, wsnap)); printf("%s\n", g_fail ? "DCOMP DEMO FAILED" : "DCOMP DEMO OK (snapshot written)"); return g_fail ? 1 : 0;
    }
    while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return g_fail ? 1 : 0;
}
