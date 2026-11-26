#pragma once
#include <windows.h>
/* Small XML tree over Win7's XmlLite. Names are local names (namespace prefixes dropped), so "uap:VisualElements" is "VisualElements". */
typedef struct XNode {
    WCHAR *name, *text;
    int nattr; WCHAR **an, **av;
    struct XNode *parent, *first, *last, *next;
} XNode;
XNode *xml_parse(const void *data, size_t n, WCHAR *err /* 256 */);
void   xml_free(XNode *n);
const WCHAR *xattr(const XNode *n, const WCHAR *name);               /* NULL if absent */
XNode *xchild(const XNode *n, const WCHAR *name);                     /* first child with that name */
XNode *xnext(const XNode *n, const WCHAR *name);                      /* next sibling with that name */
