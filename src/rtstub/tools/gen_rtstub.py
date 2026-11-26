"""gen_rtstub.py - generate wxrstub.dll's WinRT stand-in classes from .winmd metadata.

usage: python -I gen_rtstub.py <outdir> <classes.txt>
classes.txt: one full runtime class name per line (# comments). Classes returned by their methods are added automatically when they
live in one of the listed classes' namespaces. A line "~Class" generates the class WITHOUT an activation factory and without opening its
namespace (objects only hand-written code creates, e.g. Windows.ApplicationModel.Package: the real class keeps serving Package.Current).

Every method of every interface is a logged stub with one pointer-sized argument slot per ABI argument (x64: each argument has its own
register/stack slot whatever its type, so out pointers are always read correctly). What a stub does with its outputs:
  value types (numbers, enums, structs, GUID, strings)  -> zeroed, S_OK          (EventRegistrationToken of add_* -> 1)
  a generated class / an interface such a class has   -> a new inert object of that class
  IVectorView/IVector/IIterable/IMapView/IMap<...>     -> an empty collection
  IAsyncAction / IAsyncOperation<T> (+WithProgress)     -> an already completed operation whose result is zero / an inert object
  anything else                                         -> NULL
A hand-written method (src/*.c) replaces a stub when its name S_<Iface>_<Method> is listed in overrides.txt (PE weak symbols are not\nusable with this toolchain: every weak stub resolved to the first one).
Emits: rt_gen.c (IIDs, stubs, vtables, class/collection/async descriptor tables).
"""
import os, sys, re
_here = os.path.dirname(os.path.abspath(__file__))
_wxr = os.path.normpath(os.path.join(_here, '..', '..', '..'))
os.environ.setdefault('WXR_WINMD_ROOT', os.path.join(_wxr, 'deps', 'winmd'))
sys.path.insert(0, os.path.join(_wxr, 'src', 'dcomp', 'tools'))
import gen_abi as G
T = G.T

COLL = {'Windows.Foundation.Collections.IIterable`1': 'iterable', 'Windows.Foundation.Collections.IIterator`1': 'iterator',
        'Windows.Foundation.Collections.IVectorView`1': 'vectorview', 'Windows.Foundation.Collections.IVector`1': 'vector',
        'Windows.Foundation.Collections.IMapView`2': 'mapview', 'Windows.Foundation.Collections.IMap`2': 'map'}
ASYNC = {'Windows.Foundation.IAsyncOperation`1': 'op', 'Windows.Foundation.IAsyncOperationWithProgress`2': 'opp',
         'Windows.Foundation.IAsyncActionWithProgress`1': 'actp'}
PRIMSZ = {'bool': 1, 'char16': 2, 'int8': 1, 'uint8': 1, 'int16': 2, 'uint16': 2, 'int32': 4, 'uint32': 4, 'int64': 8, 'uint64': 8,
          'float': 4, 'double': 8, 'intptr': 8, 'uintptr': 8}

def mangle(s): return re.sub(r'[^A-Za-z0-9]', '_', s)
def short(full): return full.split('.')[-1]
def guid_c(g):
    p = g.split('-')
    return '{0x%s,0x%s,0x%s,{0x%s,0x%s,0x%s,0x%s,0x%s,0x%s,0x%s,0x%s}}' % (p[0], p[1], p[2], p[3][:2], p[3][2:], p[4][0:2], p[4][2:4], p[4][4:6], p[4][6:8], p[4][8:10], p[4][10:12])

def size_align(t):
    """byte size and alignment of a value-type ABI value"""
    k = t[0]
    if k == 'prim': s = PRIMSZ[t[1]]; return s, s
    if k == 'guid': return 16, 4
    if k in ('str', 'obj', 'inst'): return 8, 8
    if k == 'type':
        d = T[t[1]]
        if d.kind == 'enum': return 4, 4
        if d.kind == 'struct':
            off, al = 0, 1
            for _n, ft in d.fields:
                s, a = size_align(ft); off = (off + a - 1) // a * a + s; al = max(al, a)
            return (off + al - 1) // al * al, al
        return 8, 8
    return 8, 8

class Gen:
    def __init__(s, classes, hidden=()):
        s.classes = []          # full names, in table order
        s.listed = set(classes)  # named in classes.txt: only these get an activation factory
        s.cls_index = {}
        s.ns = set(c.rsplit('.', 1)[0] for c in classes)
        s.iid_names = {}        # guid -> C name
        s.ifaces = {}           # key -> (cname, decl, args, iid)
        s.iface_order = []
        s.colls = {}            # iid -> (cname, kind, ref)
        s.asyncs = {}           # iid -> (cname, kind, resref, ref)
        s.stubs = []
        for c in classes: s.add_class(c)
        for c in hidden: s.add_class(c)
    def add_class(s, full):
        if full in s.cls_index or full not in T or T[full].kind != 'class': return
        s.cls_index[full] = len(s.classes); s.classes.append(full)
    def iid_var(s, guid, hint):
        guid = guid.upper()
        if guid not in s.iid_names: s.iid_names[guid] = 'IID_%s' % mangle(hint)[:90] + ('_%d' % len(s.iid_names))
        return s.iid_names[guid]
    # ---- type of an out value: what to put there
    def out_action(s, t, args, fn):
        t = G.Gen().resolve(t, args) if args else t
        k = t[0]
        if k == 'type':
            d = T[t[1]]
            if d.kind == 'class':
                if d.full.rsplit('.', 1)[0] in s.ns: s.add_class(d.full); return ('obj', d.full, None)
                return ('null', d.full)
            if d.kind == 'interface':
                if d.full == 'Windows.Foundation.IAsyncAction': return ('async', s.async_desc(('type', d.full), 'act', None))
                cls = s.class_with_iface(d.full)
                if cls: return ('obj', cls, d.full)
                return ('null', d.full)
            if d.kind == 'delegate': return ('null',)
            sz, _a = size_align(t); return ('zero', sz)
        if k == 'inst':
            base = t[1]
            if base in COLL: return ('coll', s.coll_desc(t))
            if base in ASYNC: return ('async', s.async_desc(t, ASYNC[base], t[2][0]))
            return ('null', base)
        if k in ('str', 'obj'): return ('null',)
        sz, _a = size_align(t)
        if fn.startswith('add_') and k == 'type': return ('token',)
        return ('zero', sz)
    def class_with_iface(s, iface):
        for c in list(s.classes):
            if any(r[0] == 'type' and r[1] == iface for r in T[c].interfaces): return c
        ns = iface.rsplit('.', 1)[0]
        if ns in s.ns:
            for full, d in T.items():
                if d.kind == 'class' and full.startswith(ns + '.') and d.default_iface and d.default_iface[0] == 'type' and d.default_iface[1] == iface:
                    s.add_class(full); return full
        return None
    def coll_desc(s, t):
        iid = G.inst_iid(t); kind = COLL[t[1]]
        if iid not in s.colls:
            s.colls[iid] = ('coll_%d' % len(s.colls), kind, t)
            if kind in ('vectorview', 'vector', 'mapview', 'map'):   # also IIterable<T> (maps: IIterable<IKeyValuePair<K,V>>)
                el = t[2][0] if kind in ('vectorview', 'vector') else ('inst', 'Windows.Foundation.Collections.IKeyValuePair`2', t[2])
                s.coll_desc(('inst', 'Windows.Foundation.Collections.IIterable`1', [el]))
            if kind == 'iterable': s.coll_desc(('inst', 'Windows.Foundation.Collections.IIterator`1', t[2]))
        return s.colls[iid][0]
    def async_desc(s, t, kind, res):
        iid = G.inst_iid(t) if t[0] == 'inst' else T[t[1]].iid
        if iid.upper() not in s.asyncs:
            s.asyncs[iid.upper()] = ('async_%d' % len(s.asyncs), kind, res, t)
        return s.asyncs[iid.upper()][0]

    def emit_iface(s, ref, out):
        """emits stubs + vtable for one interface ref ('type', name); returns (vt name, iid var)"""
        d = T[ref[1]]
        cn = mangle(d.full)
        if cn in s.ifaces: return s.ifaces[cn]
        iv = s.iid_var(d.iid, short(d.full)); s.ifaces[cn] = ('VT_%s' % cn, iv)
        ents = []; seen = {}
        for m in d.methods:
            fn = m.name; seen[fn] = seen.get(fn, 0) + 1
            if seen[fn] > 1: fn = '%s_%d' % (fn, seen[fn])
            sym = 'S_%s_%s' % (short(d.full), fn)
            slots = []                                   # per ABI argument slot: None (input) or out action
            for p in m.params:
                ty = p.type
                if ty[0] == 'byref':
                    inner = ty[1]
                    if inner[0] == 'arr': slots += [('zero', 4), ('null',)]
                    else: slots.append(s.out_action(inner, None, fn))
                elif ty[0] == 'arr': slots += [None, None]
                else: slots.append(None)
            r = m.ret
            if r[0] == 'arr': slots += [('zero', 4), ('null',)]
            elif r[0] != 'void': slots.append(s.out_action(r, None, fn))
            s.stubs.append((sym, '%s.%s' % (short(d.full), fn), slots))
            ents.append(sym)
        out.append('static const void *const VT_%s[] = { RT_BASE%s };' % (cn, ''.join(', (void *)' + e for e in ents)))
        return s.ifaces[cn]

    def render(s):
        body = []; descs = []
        # classes (the list can grow while emitting: classes returned by methods)
        i = 0; cls_rows = []
        while i < len(s.classes):
            full = s.classes[i]; d = T[full]; i += 1
            inst = [s.emit_iface(r, body) for r in d.interfaces if r[0] == 'type']
            st = [s.emit_iface(('type', x), body) for x in list(d.statics) + [f for f in d.factories if f] + [f for f in d.composable if f] + [f for f in d.activatable if f]]   # factory interfaces sit in 'activatable'
            act = 1 if any(x is None for x in d.activatable) else 0
            di = d.default_iface[1] if d.default_iface and d.default_iface[0] == 'type' else None
            cls_rows.append((full, inst, st, act, s.ifaces[mangle(di)][1] if di and mangle(di) in s.ifaces else None))
        # stub bodies
        S = []
        ovr = set()
        op = os.path.join(os.path.dirname(os.path.abspath(LIST)), 'overrides.txt')
        if os.path.exists(op): ovr = set(l.strip() for l in open(op) if l.strip() and not l.startswith('#'))
        for sym, what, slots in s.stubs:
            if sym in ovr: continue                     # hand-written in src/
            n = len(slots)
            params = ''.join(', void *a%d' % k for k in range(n))
            nulls = [a[1] for a in slots if a and a[0] == 'null' and len(a) > 1]
            if nulls: what += ' -> NULL ' + ', '.join(nulls)
            b = ['HRESULT STDMETHODCALLTYPE %s(void *This%s) {' % (sym, params), '    (void)This; rt_called("%s", __builtin_return_address(0));' % what]
            for k, a in enumerate(slots):
                if a is None: b.append('    (void)a%d;' % k); continue
                if a[0] == 'zero': b.append('    if (a%d) memset(a%d, 0, %d);' % (k, k, a[1]))
                elif a[0] == 'token': b.append('    if (a%d) { memset(a%d, 0, 8); *(INT64 *)a%d = 1; }' % (k, k, k))
                elif a[0] == 'null': b.append('    if (a%d) *(void **)a%d = NULL;' % (k, k))
                elif a[0] == 'obj':
                    iv = s.ifaces.get(mangle(a[2]), (None, None))[1] if a[2] else None
                    b.append('    if (a%d) *(void **)a%d = rt_new_object(%d, %s);' % (k, k, s.cls_index[a[1]], '&' + iv if iv else 'NULL'))
                elif a[0] == 'coll': b.append('    if (a%d) *(void **)a%d = rt_new_coll(&%s);' % (k, k, a[1]))
                elif a[0] == 'async': b.append('    if (a%d) *(void **)a%d = rt_new_async(&%s);' % (k, k, a[1]))
            b.append('    return S_OK;\n}')
            S.append('\n'.join(b))
        # collections / async descriptors (need the element / result action)
        while True:   # async result types can bring new collections (IVectorView<StorageFile>): resolve them before anything is emitted
            n0 = (len(s.colls), len(s.asyncs))
            for iid, (cn, kind, res, t) in list(s.asyncs.items()):
                if res is not None: s.out_action(res, None, 'GetResults')
            for iid, (cn, kind, t) in list(s.colls.items()): s.coll_desc(t)
            if (len(s.colls), len(s.asyncs)) == n0: break
        C = ['extern const RtColl %s;' % v[0] for v in s.colls.values()]
        for iid, (cn, kind, t) in s.colls.items():
            iv = s.iid_var(iid, cn)
            # 4th field: an iterable's IIterator<T> descriptor; a vector's / map's IIterable descriptor
            if kind in ('vectorview', 'vector'): nxt = G.inst_iid(('inst', 'Windows.Foundation.Collections.IIterable`1', [t[2][0]]))
            elif kind in ('mapview', 'map'): nxt = G.inst_iid(('inst', 'Windows.Foundation.Collections.IIterable`1', [('inst', 'Windows.Foundation.Collections.IKeyValuePair`2', t[2])]))
            elif kind == 'iterable': nxt = G.inst_iid(('inst', 'Windows.Foundation.Collections.IIterator`1', t[2]))
            else: nxt = None
            nd = s.colls.get(nxt, [None])[0] if nxt else None
            # 5th field: what an element is (RT_EL_OBJ / RT_EL_STR / RT_EL_VAL) - IVector<String>.Append hands an HSTRING, not an object
            el = t[2][0] if kind in ('vectorview', 'vector', 'iterable', 'iterator') else ('obj',)
            ek = el[0]
            if ek == 'type': ek = 'val' if T[el[1]].kind in ('enum', 'struct') else 'obj'
            ek = {'str': 'STR', 'obj': 'OBJ', 'inst': 'OBJ'}.get(ek, 'VAL')
            C.append('const RtColl %s = { RT_%s, &%s, NULL, %s, RT_EL_%s };' % (cn, kind.upper(), iv, ('&' + nd) if nd else 'NULL', ek))
        for iid, (cn, kind, res, t) in s.asyncs.items():
            iv = s.iid_var(iid, cn)
            rsz, robj, riv = 8, 'NULL', 'NULL'
            if res is not None:
                a = s.out_action(res, None, 'GetResults')
                if a[0] == 'zero': rsz = a[1]
                elif a[0] == 'obj' and s.cls_index[a[1]] < len(cls_rows):
                    robj = str(s.cls_index[a[1]]); riv = ('&' + s.ifaces[mangle(a[2])][1]) if a[2] and mangle(a[2]) in s.ifaces else 'NULL'
            C.append('const RtAsync %s = { RT_A_%s, &%s, %d, %s, %s };' % (cn, kind.upper(), iv, rsz, robj if robj != 'NULL' else '-1', riv))
        # name -> descriptor tables (hand-written code in src/ looks descriptors up by type name: rt_coll_named / rt_async_named)
        def tname(t):
            if t[0] == 'inst': return '%s<%s>' % (t[1], ','.join(tname(x) for x in t[2]))
            if t[0] in ('str', 'obj'): return {'str': 'String', 'obj': 'Object'}[t[0]]
            return t[1] if len(t) > 1 and isinstance(t[1], str) else str(t)
        C.append('const RtNamed g_rtcoll_names[] = {' + ''.join(' { "%s", &%s },' % (tname(t), cn) for iid, (cn, kind, t) in s.colls.items()) + ' { 0, 0 } };')
        C.append('const RtNamed g_rtasync_names[] = {' + ''.join(' { "%s", &%s },' % (tname(t), cn) for iid, (cn, kind, res, t) in s.asyncs.items()) + ' { 0, 0 } };')
        # class table
        R = []
        for full, inst, st, act, div in cls_rows:
            cn = mangle(full)
            R.append('static const RtIface ifs_%s[] = { %s{ 0, 0 } };' % (cn, ''.join('{ &%s, %s }, ' % (iv, vt) for vt, iv in inst)))
            R.append('static const RtIface st_%s[] = { %s{ 0, 0 } };' % (cn, ''.join('{ &%s, %s }, ' % (iv, vt) for vt, iv in st)))
        R.append('const RtClass g_rtclass[] = {')
        for full, inst, st, act, div in cls_rows:
            cn = mangle(full)
            R.append('    { L"%s", ifs_%s, st_%s, %d, %d },' % (full, cn, cn, act, 1 if full in s.listed else 0))
        R.append('    { 0, 0, 0, 0, 0 } };')
        R.append('const int g_nrtclass = %d;' % len(cls_rows))
        H = ['/* generated by gen_rtstub.py - do not edit */', '#include "rt_core.h"', '']
        for g, n in s.iid_names.items(): H.append('static const GUID %s = %s;' % (n, guid_c(g)))
        H.append('')
        # forward declarations of the stubs (vtables reference them)
        fw = ['HRESULT STDMETHODCALLTYPE %s(void *This%s);' % (sym, ''.join(', void *a%d' % k for k in range(len(sl)))) for sym, _w, sl in s.stubs]
        fw += ['extern const RtColl %s;' % v[0] for v in s.colls.values()] + ['extern const RtAsync %s;' % v[0] for v in s.asyncs.values()]
        return '\n'.join(H + fw + [''] + S + [''] + body + [''] + C + [''] + R) + '\n'

def main():
    global LIST
    out, lst = sys.argv[1], sys.argv[2]; LIST = lst
    lines = [l.strip() for l in open(lst) if l.strip() and not l.startswith('#')]
    classes = [l for l in lines if not l.startswith('~')]; hidden = [l[1:].strip() for l in lines if l.startswith('~')]
    g = Gen(classes, hidden)
    src = g.render()
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'rt_gen.c'), 'w').write(src)
    print('classes %d, stubs %d, collections %d, async %d' % (len(g.classes), len(g.stubs), len(g.colls), len(g.asyncs)))
main()
