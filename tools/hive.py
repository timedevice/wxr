# hive.py <hive file> <key path> [...]: prints a key's values and subkey names from an offline registry hive (regf), read-only.
# Used to look up how Windows 10 registers something (Windows\System32\config\SOFTWARE from the ISO), without loading the hive.
import sys, struct
data = open(sys.argv[1], 'rb').read()
B = 0x1000
def cell(off): return B + off + 4
def nk(off):
    o = cell(off); assert data[o:o + 2] == b'nk', 'not nk'
    flags = struct.unpack_from('<H', data, o + 2)[0]
    nsub, subl = struct.unpack_from('<II', data, o + 0x14)[0], struct.unpack_from('<I', data, o + 0x1C)[0]
    nval, vall = struct.unpack_from('<II', data, o + 0x24)
    nlen = struct.unpack_from('<H', data, o + 0x48)[0]; raw = data[o + 0x4C:o + 0x4C + nlen]
    name = raw.decode('latin-1') if flags & 0x20 else raw.decode('utf-16le')
    return dict(name=name, nsub=nsub, subl=subl, nval=nval, vall=vall)
def subkeys(k):
    out = []
    def walk(lst):
        o = cell(lst); sig = data[o:o + 2]; n = struct.unpack_from('<H', data, o + 2)[0]
        if sig in (b'lf', b'lh'):
            for i in range(n): out.append(struct.unpack_from('<I', data, o + 4 + i * 8)[0])
        elif sig == b'li':
            for i in range(n): out.append(struct.unpack_from('<I', data, o + 4 + i * 4)[0])
        elif sig == b'ri':
            for i in range(n): walk(struct.unpack_from('<I', data, o + 4 + i * 4)[0])
    if k['nsub'] and k['subl'] != 0xFFFFFFFF: walk(k['subl'])
    return [(nk(x)['name'], x) for x in out]
def values(k):
    res = []
    if not k['nval'] or k['vall'] == 0xFFFFFFFF: return res
    lo = cell(k['vall'])
    for i in range(k['nval']):
        vo = cell(struct.unpack_from('<I', data, lo + i * 4)[0]); assert data[vo:vo + 2] == b'vk'
        nl, dl, doff, typ, fl = struct.unpack_from('<HIIIH', data, vo + 2)
        name = data[vo + 0x14:vo + 0x14 + nl].decode('latin-1' if fl & 1 else 'utf-16le') if nl else '(default)'
        if dl & 0x80000000: raw = struct.pack('<I', doff)[:dl & 0x7FFFFFFF]
        else: raw = data[cell(doff):cell(doff) + dl]
        if typ in (1, 2): v = raw.decode('utf-16le', 'replace').rstrip('\0')
        elif typ == 4: v = struct.unpack('<I', raw[:4])[0]
        else: v = raw[:64].hex()
        res.append((name, typ, v))
    return res
root = nk(struct.unpack_from('<I', data, 0x24)[0])
for path in sys.argv[2:]:
    k = root; ok = True
    for part in [p for p in path.split('\\') if p]:
        m = [x for n, x in subkeys(k) if n.lower() == part.lower()]
        if not m: ok = False; break
        k = nk(m[0])
    print('[%s]%s' % (path, '' if ok else '  (missing)'))
    if ok:
        for v in values(k): print('  %s = %s' % (v[0], v[2]))
        sk = [n for n, _ in subkeys(k)]
        if sk: print('  subkeys:', ', '.join(sk[:30]))
