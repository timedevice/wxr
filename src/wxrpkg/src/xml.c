#define COBJMACROS
#include "xml.h"
#include <objbase.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdio.h>

/* mingw's libxmllite.a has no IID data */
static const GUID IID_IXmlReader_ = { 0x7279FC81, 0x709D, 0x4095, { 0xB6, 0x3D, 0x69, 0xFE, 0x4B, 0x0D, 0x90, 0x30 } };
static WCHAR *dup(const WCHAR *s, UINT n) { WCHAR *r = (WCHAR *)malloc((n + 1) * sizeof(WCHAR)); memcpy(r, s, n * sizeof(WCHAR)); r[n] = 0; return r; }
static XNode *add(XNode *parent, const WCHAR *name, UINT n) {
    XNode *x = (XNode *)calloc(1, sizeof *x); x->name = dup(name, n); x->parent = parent;
    if (parent) { if (parent->last) parent->last->next = x; else parent->first = x; parent->last = x; }
    return x;
}
XNode *xml_parse(const void *data, size_t n, WCHAR *err) {
    IStream *st; IXmlReader *r = NULL; XmlNodeType t; XNode *root = NULL, *cur = NULL; HRESULT hr;
    st = SHCreateMemStream((const BYTE *)data, (UINT)n);
    if (!st) { _snwprintf(err, 256, L"out of memory"); return NULL; }
    hr = CreateXmlReader(&IID_IXmlReader_, (void **)&r, NULL);
    if (FAILED(hr)) { IStream_Release(st); _snwprintf(err, 256, L"XmlLite is not available (0x%08lX)", (unsigned long)hr); return NULL; }
    IXmlReader_SetProperty(r, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    IXmlReader_SetInput(r, (IUnknown *)st);
    while ((hr = IXmlReader_Read(r, &t)) == S_OK) {
        if (t == XmlNodeType_Element) {
            const WCHAR *nm; UINT nl; BOOL empty = IXmlReader_IsEmptyElement(r); XNode *x;
            IXmlReader_GetLocalName(r, &nm, &nl);
            x = add(cur, nm, nl); if (!root) root = x;
            if (IXmlReader_MoveToFirstAttribute(r) == S_OK) {
                do {
                    const WCHAR *an, *av, *pf; UINT al, vl, pl;
                    IXmlReader_GetPrefix(r, &pf, &pl); IXmlReader_GetLocalName(r, &an, &al);
                    if ((pl == 5 && !wcsncmp(pf, L"xmlns", 5)) || (pl == 0 && al == 5 && !wcsncmp(an, L"xmlns", 5))) continue;
                    IXmlReader_GetValue(r, &av, &vl);
                    x->an = (WCHAR **)realloc(x->an, (x->nattr + 1) * sizeof(WCHAR *)); x->av = (WCHAR **)realloc(x->av, (x->nattr + 1) * sizeof(WCHAR *));
                    x->an[x->nattr] = dup(an, al); x->av[x->nattr] = dup(av, vl); x->nattr++;
                } while (IXmlReader_MoveToNextAttribute(r) == S_OK);
                IXmlReader_MoveToElement(r);
            }
            if (!empty) cur = x;
        } else if (t == XmlNodeType_EndElement) {
            if (cur) cur = cur->parent;
        } else if ((t == XmlNodeType_Text || t == XmlNodeType_CDATA) && cur) {
            const WCHAR *v; UINT vl; size_t old = cur->text ? wcslen(cur->text) : 0;
            IXmlReader_GetValue(r, &v, &vl);
            cur->text = (WCHAR *)realloc(cur->text, (old + vl + 1) * sizeof(WCHAR)); memcpy(cur->text + old, v, vl * sizeof(WCHAR)); cur->text[old + vl] = 0;
        }
    }
    IXmlReader_Release(r); IStream_Release(st);
    if (FAILED(hr)) { UINT line = 0; _snwprintf(err, 256, L"XML error 0x%08lX", (unsigned long)hr); (void)line; xml_free(root); return NULL; }
    if (!root) _snwprintf(err, 256, L"empty XML document");
    return root;
}
void xml_free(XNode *n) {
    while (n) { XNode *nx = n->next; int i; xml_free(n->first); for (i = 0; i < n->nattr; i++) { free(n->an[i]); free(n->av[i]); } free(n->an); free(n->av); free(n->name); free(n->text); free(n); n = nx; }
}
const WCHAR *xattr(const XNode *n, const WCHAR *name) { int i; if (!n) return NULL; for (i = 0; i < n->nattr; i++) if (!wcscmp(n->an[i], name)) return n->av[i]; return NULL; }
XNode *xchild(const XNode *n, const WCHAR *name) { XNode *c; if (!n) return NULL; for (c = n->first; c; c = c->next) if (!wcscmp(c->name, name)) return c; return NULL; }
XNode *xnext(const XNode *n, const WCHAR *name) { XNode *c; if (!n) return NULL; for (c = n->next; c; c = c->next) if (!wcscmp(c->name, name)) return c; return NULL; }
