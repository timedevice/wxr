#pragma once
/* comp_base.h - types shared by the generated plumbing (comp_gen.c) and the hand-written implementation */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winstring.h>
#include <stdio.h>
typedef struct CObj CObj;
typedef struct CView { const void *vt; CObj *obj; } CView;          /* every COM interface pointer we hand out points at one of these */
typedef struct IfaceEntry { const GUID *iid; const void *vt; const char *name; } IfaceEntry;
typedef struct ClassDesc {
    const WCHAR *name; const char *sname; int base; const IfaceEntry *ifs; const IfaceEntry *statics;
    const char *def_iface; int activatable;
} ClassDesc;
HRESULT comp_unimpl(const char *what);
void comp_log(const char *fmt, ...);
HRESULT STDMETHODCALLTYPE cv_QI(void *This, REFIID r, void **p);
ULONG STDMETHODCALLTYPE cv_AddRef(void *This);
ULONG STDMETHODCALLTYPE cv_Release(void *This);
HRESULT STDMETHODCALLTYPE cv_GetIids(void *This, ULONG *c, IID **i);
HRESULT STDMETHODCALLTYPE cv_GetRuntimeClassName(void *This, HSTRING *n);
HRESULT STDMETHODCALLTYPE cv_GetTrustLevel(void *This, TrustLevel *t);
