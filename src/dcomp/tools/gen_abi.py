"""gen_abi.py - emit C headers (WinRT ABI) from .winmd metadata.

usage: python3 gen_abi.py <out.h> <root> [<root> ...]
  root = full type name (class / interface / delegate / struct / enum), or  "Windows.Foundation.Collections.IVector`1<Windows.UI.Xaml.UIElement>"
  A class root pulls in its default interface, statics and factory interfaces (and composable factories).
Full vtables are emitted only for the roots (plus their interfaces); every other referenced interface/delegate is a
forward typedef; structs and enums are always emitted in full (dependency-ordered).
"""
import sys, os, re, hashlib, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import winmd

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.environ.get('WXR_WINMD_ROOT', '.')
DIRS = [SCR + '/winmd_system', SCR]
CACHE = os.path.join(HERE, 'winmd.cache')

PRIMC = {'bool':'boolean','char16':'WCHAR','int8':'INT8','uint8':'UINT8','int16':'INT16','uint16':'UINT16','int32':'INT32',
         'uint32':'UINT32','int64':'INT64','uint64':'UINT64','float':'float','double':'double','intptr':'INT_PTR','uintptr':'UINT_PTR'}
PRIMSIG = {'bool':'b1','char16':'c2','int8':'i1','uint8':'u1','int16':'i2','uint16':'u2','int32':'i4','uint32':'u4','int64':'i8',
           'uint64':'u8','float':'f4','double':'f8'}
CKW = {'default','register','union','long','short','signed','unsigned','int','char','float','double','void','switch','case','const',
       'volatile','static','extern','struct','enum','return','this','auto','break','continue','do','else','for','goto','if','sizeof',
       'typedef','while','inline','restrict','new','delete','class','template','typename','operator'}

R = winmd.load_all(*DIRS, cache=CACHE)
T = R.types

def key(full):          # generic types are stored as ns.Name`1
    return full

def mangle(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s)

def split_args(rest):
    out, depth, cur = [], 0, ''
    for ch in rest:
        if ch == '<': depth += 1
        elif ch == '>': depth -= 1
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

PIID_NS = uuid.UUID('11f47ad5-7b73-42c0-abae-878b1e16adee')
def guid_from_sig(sig):
    h = hashlib.sha1(PIID_NS.bytes + sig.encode('utf8')).digest()[:16]
    b = bytearray(h); b[6] = (b[6] & 0x0F) | 0x50; b[8] = (b[8] & 0x3F) | 0x80
    return str(uuid.UUID(bytes=bytes(b))).upper()

def type_sig(t, args=None):
    k = t[0]
    if k == 'prim': return PRIMSIG[t[1]]
    if k == 'str': return 'string'
    if k == 'obj': return 'cinterface(IInspectable)'
    if k == 'guid': return 'g16'
    if k == 'var': return type_sig(args[t[1]])
    if k == 'type':
        d = T[t[1]]
        if d.kind == 'interface': return '{%s}' % d.iid.lower()
        if d.kind == 'delegate': return 'delegate({%s})' % d.iid.lower()
        if d.kind == 'enum': return 'enum(%s;%s)' % (d.full, 'i4' if d.enum_base == 'int32' else 'u4')
        if d.kind == 'struct': return 'struct(%s;%s)' % (d.full, ';'.join(type_sig(f[1]) for f in d.fields))
        if d.kind == 'class':
            di = d.default_iface
            return 'rc(%s;%s)' % (d.full, type_sig(di))
    if k == 'inst':
        d = T[t[1]]
        return 'pinterface({%s};%s)' % (d.iid.lower(), ';'.join(type_sig(a, args) for a in t[2]))
    raise ValueError(t)

def inst_iid(t):
    return guid_from_sig(type_sig(t))

def inst_name(t, args=None):
    if t[0] == 'var': return inst_name(args[t[1]])
    if t[0] == 'type': return mangle(t[1])
    if t[0] == 'prim': return t[1]
    if t[0] == 'str': return 'HSTRING'
    if t[0] == 'obj': return 'Object'
    if t[0] == 'guid': return 'Guid'
    if t[0] == 'inst': return mangle(t[1]) + '__' + '__'.join(inst_name(a, args) for a in t[2])
    return 'X'

class Gen:
    def __init__(s):
        s.fwd = set(); s.structs = []; s.struct_seen = set(); s.enums = {}; s.out_vt = []; s.emitted = set(); s.insts = {}
        s.iids = []
    # ---- C type of an ABI value
    def ctype(s, t, args=None):
        k = t[0]
        if k == 'prim': return PRIMC[t[1]]
        if k == 'str': return 'HSTRING'
        if k == 'obj': return 'IInspectable*'
        if k == 'guid': return 'GUID'
        if k == 'var': return s.ctype(args[t[1]])
        if k == 'type':
            d = T[t[1]]
            if d.kind == 'interface' or d.kind == 'delegate':
                n = mangle(d.full); s.fwd.add(n); return n + '*'
            if d.kind == 'class':
                di = d.default_iface
                if di is None: return 'IInspectable*'
                return s.ctype(di)
            if d.kind == 'enum':
                s.need_enum(d); return mangle(d.full)
            if d.kind == 'struct':
                s.need_struct(d); return mangle(d.full)
        if k == 'inst':
            n = mangle(t[1]) + '__' + '__'.join(inst_name(a, args) for a in (s.resolve(x, args) for x in t[2]))
            res = tuple(s.resolve(x, args) for x in t[2])
            s.insts[n] = ('inst', t[1], list(res)); s.fwd.add(n); return n + '*'
        raise ValueError(t)
    def resolve(s, t, args):
        if args is None: return t
        if t[0] == 'var': return args[t[1]]
        if t[0] == 'inst': return ('inst', t[1], [s.resolve(x, args) for x in t[2]])
        if t[0] == 'arr': return ('arr', s.resolve(t[1], args))
        return t
    def need_enum(s, d):
        if d.full in s.enums: return
        s.enums[d.full] = d
    def need_struct(s, d):
        if d.full in s.struct_seen: return
        s.struct_seen.add(d.full)
        for nm, ft in d.fields: s.ctype(ft)    # pulls dependencies first
        s.structs.append(d)
    # ---- params
    def params(s, m, args=None):
        out = []
        for p in m.params:
            ty = s.resolve(p.type, args); nm = p.name + ('_' if (p.name in CKW or p.name in ('result', 'resultSize')) else '')
            if ty[0] == 'byref':
                inner = ty[1]
                if inner[0] == 'arr':
                    out.append('UINT32* %sSize' % nm); out.append('%s** %s' % (s.ctype(inner[1], args), nm))
                else: out.append('%s* %s' % (s.ctype(inner, args), nm))
            elif ty[0] == 'arr':
                out.append('UINT32 %sSize' % nm); out.append('%s* %s' % (s.ctype(ty[1], args), nm))
            else: out.append('%s %s' % (s.ctype(ty, args), nm))
        r = s.resolve(m.ret, args)
        if r[0] == 'arr':
            out.append('UINT32* resultSize'); out.append('%s** result' % s.ctype(r[1], args))
        elif r[0] != 'void':
            out.append('%s* result' % s.ctype(r, args))
        return out
    # ---- vtable emission
    def emit_iface(s, d, name=None, args=None, iid=None):
        name = name or mangle(d.full)
        if name in s.emitted: return
        s.emitted.add(name); s.fwd.add(name)
        L = []
        L.append('typedef struct %sVtbl {' % name)
        L.append('    HRESULT (STDMETHODCALLTYPE *QueryInterface)(%s* This, REFIID riid, void** ppv);' % name)
        L.append('    ULONG (STDMETHODCALLTYPE *AddRef)(%s* This);' % name)
        L.append('    ULONG (STDMETHODCALLTYPE *Release)(%s* This);' % name)
        if d.kind == 'interface':
            L.append('    HRESULT (STDMETHODCALLTYPE *GetIids)(%s* This, ULONG* iidCount, IID** iids);' % name)
            L.append('    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(%s* This, HSTRING* className);' % name)
            L.append('    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(%s* This, TrustLevel* trustLevel);' % name)
        seen = {}
        for i, m in enumerate(d.methods):
            if m.name in ('.ctor', '.cctor'): continue
            if d.kind == 'delegate' and m.name != 'Invoke': continue
            fn = m.name
            seen[fn] = seen.get(fn, 0) + 1
            if seen[fn] > 1: fn = '%s_%d' % (fn, seen[fn])
            ps = s.params(m, args)
            L.append('    HRESULT (STDMETHODCALLTYPE *%s)(%s);' % (fn, ', '.join(['%s* This' % name] + ps)))
        L.append('} %sVtbl;' % name)
        L.append('struct %s { const %sVtbl* lpVtbl; };' % (name, name))
        if iid or d.iid:
            g = iid or d.iid; p = g.split('-')
            L.append('static const GUID IID_%s = {0x%s,0x%s,0x%s,{0x%s,0x%s,0x%s,0x%s,0x%s,0x%s,0x%s,0x%s}};' %
                     (name, p[0], p[1], p[2], p[3][:2], p[3][2:], p[4][0:2], p[4][2:4], p[4][4:6], p[4][6:8], p[4][8:10], p[4][10:12]))
        s.out_vt.append('\n'.join(L))
    def add_root(s, r):
        if '<' in r:
            base, rest = r.split('<', 1); args = split_args(rest[:-1])
            args = [parse_type(x) for x in args]
            t = ('inst', base, args)
            n = mangle(base) + '__' + '__'.join(inst_name(a) for a in args)
            s.emit_iface(T[base], name=n, args=args, iid=inst_iid(t))
            return
        d = T[r]
        if d.kind == 'class':
            for i in [d.default_iface[1] if d.default_iface else None] + list(d.statics) + list(d.composable) + [x for x in d.activatable if x]:
                if i: s.emit_iface(T[i])
            for ref in d.interfaces:
                if ref[0] == 'type': s.emit_iface(T[ref[1]])
        elif d.kind in ('interface', 'delegate'): s.emit_iface(d)
        elif d.kind == 'struct': s.need_struct(d)
        elif d.kind == 'enum': s.need_enum(d)
    def render(s):
        o = ['/* generated by gen_abi.py from .winmd metadata - do not edit */', '#pragma once',
             '#include <windows.h>', '#include <inspectable.h>', '#include <hstring.h>', '']
        for n in sorted(s.fwd): o.append('typedef struct %s %s;' % (n, n))
        o.append('')
        for n, d in sorted(s.enums.items()):
            m = mangle(d.full)
            o.append('typedef %s %s;' % ('UINT32' if d.enum_base == 'uint32' else 'INT32', m))
            for nm, v in d.enum_values: o.append('#define %s_%s ((%s)%d)' % (m, nm, m, v))
        o.append('')
        for d in s.structs:
            o.append('typedef struct %s {' % mangle(d.full))
            for nm, ft in d.fields: o.append('    %s %s;' % (s.ctype(ft), nm))
            o.append('} %s;' % mangle(d.full))
        o.append('')
        o += s.out_vt
        return '\n'.join(o) + '\n'

def parse_type(x):
    prim = {'int32':'int32','uint32':'uint32','int64':'int64','uint64':'uint64','double':'double','float':'float','bool':'bool','uint8':'uint8','int16':'int16','uint16':'uint16','char16':'char16'}
    if x in prim: return ('prim', prim[x])
    if x == 'string': return ('str',)
    if x == 'object': return ('obj',)
    if x == 'guid': return ('guid',)
    if '<' in x:
        base, rest = x.split('<', 1); return ('inst', base, [parse_type(y) for y in split_args(rest[:-1])])
    return ('type', x)

def main():
    out = sys.argv[1]; roots = sys.argv[2:]
    g = Gen()
    for r in roots: g.add_root(r)
    # an instance referenced by the emitted vtables that is not itself emitted stays opaque
    open(out, 'w').write(g.render())
    print('wrote', out, len(g.out_vt), 'interfaces,', len(g.structs), 'structs,', len(g.enums), 'enums,', len(g.fwd), 'forward typedefs')

if __name__ == '__main__':
    main()
