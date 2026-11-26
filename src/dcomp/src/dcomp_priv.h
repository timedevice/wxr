#pragma once
/* dcomp_priv.h - internals of dcomp.dll (front end over the composition scene graph, rendering with Direct2D 1.1 on a Direct3D 11 device) */
#define COBJMACROS
#include "comp_types.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <float.h>


/* mingw's D2D 1.1 header only generates macros for the methods ID2D1DeviceContext adds; the inherited ID2D1RenderTarget ones go through Base */
#define DCB_(m, dc, ...) ((dc)->lpVtbl->Base.m((ID2D1RenderTarget *)(dc), ##__VA_ARGS__))
#define DCB_BeginDraw(dc) DCB_(BeginDraw, dc)
#define DCB_EndDraw(dc, a, b) DCB_(EndDraw, dc, a, b)
#define DCB_Clear(dc, c) DCB_(Clear, dc, c)
#define DCB_SetTransform(dc, m) DCB_(SetTransform, dc, m)
#define DCB_SetAntialiasMode(dc, m) DCB_(SetAntialiasMode, dc, m)
#define DCB_FillRectangle(dc, r, b) DCB_(FillRectangle, dc, r, b)
#define DCB_FillGeometry(dc, g, b, o) DCB_(FillGeometry, dc, g, b, o)
#define DCB_DrawGeometry(dc, g, b, w, s) DCB_(DrawGeometry, dc, g, b, w, s)
#define DCB_PushAxisAlignedClip(dc, r, m) DCB_(PushAxisAlignedClip, dc, r, m)
#define DCB_PopAxisAlignedClip(dc) DCB_(PopAxisAlignedClip, dc)
#define DCB_CreateLayer(dc, s, l) DCB_(CreateLayer, dc, s, l)
#define DCB_PushLayer(dc, p, l) DCB_(PushLayer, dc, p, l)
#define DCB_PopLayer(dc) DCB_(PopLayer, dc)
#define DCB_CreateSolidColorBrush(dc, c, p, b) DCB_(CreateSolidColorBrush, dc, c, p, b)
#define DCB_CreateLinearGradientBrush(dc, a, b, c, d) DCB_(CreateLinearGradientBrush, dc, a, b, c, d)
#define DCB_DrawBitmap(dc, bmp, d, o, m, s) DCB_(DrawBitmap, dc, (ID2D1Bitmap *)(bmp), d, o, m, s)
#define UREL(x) IUnknown_Release((IUnknown *)(x))

/* ---- tiny COM framework for objects that have no WinRT metadata (transforms, clips, animations, factories) ---- */
typedef struct HObj HObj;
typedef struct HView { const void *vt; HObj *obj; } HView;               /* same layout as CView */
typedef struct HIface { const GUID *iid; const void *vt; } HIface;
struct HObj { LONG ref; int cls /* always -1: tells HObj from CObj */; DWORD flags; const char *name; const HIface *ifs; int nifs; HView views[6]; void (*dtor)(HObj *); };
HObj *hobj_new(size_t size, const char *name, const HIface *ifs, int nifs, void (*dtor)(HObj *));
HRESULT hobj_return(HObj *o, REFIID iid, void **out);                   /* QI + release the creation reference */
HRESULT STDMETHODCALLTYPE hv_QI(void *This, REFIID r, void **p);        /* work for HObj views and CObj views alike */
ULONG STDMETHODCALLTYPE hv_AddRef(void *This);
ULONG STDMETHODCALLTYPE hv_Release(void *This);
HRESULT dc_stub(const char *table, int slot, const char *name, HRESULT hr);   /* logging stub body */
const char *dc_guid(const GUID *g, char *buf);

/* ---- shared devices ---- */
typedef struct DcDev { ID3D11Device *d3d; IDXGIDevice *dxgi; ID2D1Factory1 *f1; ID2D1Device *d2d; IDXGIFactory2 *dxf; BOOL given; } DcDev;
DcDev *dc_dev(void);                                                    /* the primary device (created on demand) */
HRESULT dc_set_rendering_device(IUnknown *dev);                         /* the device XAML hands us: becomes the primary one if nothing is using the default yet */
ID2D1Factory *comp_d2d_factory(void);
void dc_note_client_device(CObj *comp, IUnknown *dev);                  /* a dcomp device made with a D3D device other than the primary */
ID3D11Device *dc_client_device(CObj *comp);                             /* that device for a compositor, or NULL (no reference added) */
extern CRITICAL_SECTION g_dc_lock;
extern HWND g_dc_target_hwnd;

/* ---- IIDs (hand declared, they have no metadata) ---- */
#include "dcomp_iids.h"

/* ---- engine hooks implemented in dcomp_*.c ---- */
void dc_register(void);
Visual *dc_visual_from(void *view);
HRESULT dc_visual_qi_extra(CObj *o, REFIID r, void **p);
HRESULT dc_compositor_qi_extra(CObj *o, REFIID r, void **p);
HRESULT dc_kfanim_qi_extra(CObj *o, REFIID r, void **p);
HRESULT dc_partner_qi_extra(CObj *o, REFIID r, void **p);
void dc_text_hooks_install(void *dc);
HRESULT dc_surface_qi_extra(CObj *o, REFIID r, void **p);
HRESULT dc_target_qi_extra(CObj *o, REFIID r, void **p);
CObj *dc_create_surface(CObj *comp, int w, int h, DXGI_FORMAT fmt, DXGI_ALPHA_MODE am, int virt);
CObj *dc_surface_from_unknown(IUnknown *u);
CObj *dc_create_target(CObj *comp, HWND hwnd, BOOL topmost);
void dc_target_set_root(CObj *target, Visual *root);
void dc_note_visual(Visual *v, int created);                            /* registry of interop visuals (root fallback heuristic) */
void dc_commit(CObj *comp);
HRESULT dc_desktop_interop_qi(CObj *o, REFIID r, void **p);
HRESULT dc_compositor_interop_qi(CObj *o, REFIID r, void **p);
void dc_register_render(void);
void dc_surface_register(void);
CObj *comp_create_compositor(void);
HRESULT dc_snapshot_png(HWND hwnd, const WCHAR *path);                  /* exported as WxrDcomp_SnapshotToPng */
#include "dcomp_tables.h"

HMODULE dcomp_module_by_export_name(const char *name);   /* dcomp_textproxy.c: a loaded DLL by the name in its export directory */
