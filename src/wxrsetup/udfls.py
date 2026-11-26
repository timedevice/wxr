# Minimal UDF (1.02, as on Microsoft ISOs) reader.
# usage: udfls.py image.iso list [maxdepth]          -> tree (dirs + files)
#        udfls.py image.iso get <path> <outfile>      -> extract one file (path like \sources\install.esd)
import sys, struct
SEC = 2048
f = open(sys.argv[1], 'rb')
def rd(lba, n=SEC):
    f.seek(lba * SEC); return f.read(n)
def tag(d): return struct.unpack('<H', d[0:2])[0]
avdp = rd(256); assert tag(avdp) == 2, 'no AVDP'
vds_len, vds_loc = struct.unpack('<II', avdp[16:24])
part_start = None; fsd_lb = None
for i in range(vds_len // SEC):
    d = rd(vds_loc + i); t = tag(d)
    if t == 5: part_start = struct.unpack('<I', d[188:192])[0]          # Partition Descriptor: starting location
    if t == 6: fsd_lb = struct.unpack('<I', d[252:256])[0]              # LVD: logical volume contents use = long_ad of FSD
    if t == 8: break
P = lambda lb: part_start + lb
fsd = rd(P(fsd_lb)); assert tag(fsd) == 256, 'no FSD'
root_lb = struct.unpack('<I', fsd[404:408])[0]                          # root directory ICB long_ad (len 400, lb 404)
def dstr(b):
    if not b: return ''
    if b[0] == 8: return b[1:].decode('latin-1')
    if b[0] == 16: return b[1:].decode('utf-16-be')
    return b[1:].decode('latin-1', 'replace')
def fentry(lb):
    d = rd(P(lb)); t = tag(d)
    if t == 261: off = 176; L_EA, L_AD = struct.unpack('<II', d[168:176]); size = struct.unpack('<Q', d[56:64])[0]
    elif t == 266: off = 216; L_EA, L_AD = struct.unpack('<II', d[208:216]); size = struct.unpack('<Q', d[56:64])[0]  # extended FE
    else: raise ValueError('bad FE tag %d at %d' % (t, lb))
    icbflags = struct.unpack('<H', d[34:36])[0] & 7; ftype = d[27]
    ads = d[off + L_EA: off + L_EA + L_AD]
    ext = []
    if icbflags == 0:      # short_ad
        for j in range(0, len(ads) - 7, 8):
            ln, pos = struct.unpack('<II', ads[j:j + 8]); ln &= 0x3FFFFFFF
            if ln: ext.append((pos, ln))
    elif icbflags == 1:    # long_ad
        for j in range(0, len(ads) - 15, 16):
            ln, pos = struct.unpack('<II', ads[j:j + 8]); ln &= 0x3FFFFFFF
            if ln: ext.append((pos, ln))
    elif icbflags == 3:    # data embedded
        return ftype, size, None, ads
    return ftype, size, ext, None
def read_all(ext, emb, size):
    if emb is not None: return emb[:size]
    out = bytearray()
    for pos, ln in ext: out += rd(P(pos), ln)
    return bytes(out[:size])
def listdir(lb):
    ftype, size, ext, emb = fentry(lb); data = read_all(ext, emb, size); i = 0; res = []
    while i + 38 <= len(data):
        if tag(data[i:]) != 257: break
        chars = data[i + 18]; L_FI = data[i + 19]; icb_lb = struct.unpack('<I', data[i + 24:i + 28])[0]
        L_IU = struct.unpack('<H', data[i + 36:i + 38])[0]
        name = dstr(data[i + 38 + L_IU: i + 38 + L_IU + L_FI])
        if not (chars & 8) and not (chars & 4):      # skip parent, deleted
            res.append((name, icb_lb, bool(chars & 2)))
        i += (38 + L_IU + L_FI + 3) & ~3
    return res
def walk(lb, path, depth, maxd):
    for name, icb, isdir in listdir(lb):
        p = path + '\\' + name
        if isdir:
            print(f'{"<dir>":>16}  {p}')
            if depth < maxd: walk(icb, p, depth + 1, maxd)
        else:
            _, size, _, _ = fentry(icb); print(f'{size:>16,}  {p}')
def find(path):
    lb = root_lb
    for comp in [c for c in path.split('\\') if c]:
        m = [e for e in listdir(lb) if e[0].lower() == comp.lower()]
        if not m: raise FileNotFoundError(path)
        lb = m[0][1]
    return lb
if sys.argv[2] == 'list':
    walk(root_lb, '', 0, int(sys.argv[3]) if len(sys.argv) > 3 else 1)
elif sys.argv[2] == 'wiminfo':        # print the XML metadata of a WIM/ESD inside the image (no extraction)
    ftype, size, ext, emb = fentry(find(sys.argv[3]))
    def vread(off, n):      # read n bytes at file offset off across extents
        out = bytearray(); cur = 0
        for pos, ln in ext:
            if off < cur + ln and n > 0:
                s = max(off - cur, 0); k = min(ln - s, n); f.seek(P(pos) * SEC + s); out += f.read(k); off += k; n -= k
            cur += ln
        return bytes(out)
    h = vread(0, 208); assert h[:8] == b'MSWIM\0\0\0', h[:8]
    xsz = int.from_bytes(h[0x48:0x4F], 'little'); xoff = struct.unpack('<Q', h[0x50:0x58])[0]
    print('wim version %08X flags %08X images %d chunk %d' % (struct.unpack('<I', h[0xC:0x10])[0], struct.unpack('<I', h[0x10:0x14])[0], struct.unpack('<I', h[0x2C:0x30])[0], struct.unpack('<I', h[0x14:0x18])[0]))
    x = vread(xoff, xsz).decode('utf-16-le', 'replace')
    open(sys.argv[4], 'w', encoding='utf-8').write(x); print('xml', xsz, 'bytes ->', sys.argv[4])
elif sys.argv[2] == 'get':
    ftype, size, ext, emb = fentry(find(sys.argv[3]))
    with open(sys.argv[4], 'wb') as o:
        if emb is not None: o.write(emb[:size])
        else:
            left = size
            for pos, ln in ext:
                f.seek(P(pos) * SEC); n = min(ln, left)
                while n: b = f.read(min(n, 1 << 22)); o.write(b); n -= len(b); left -= len(b)
    print('extracted', sys.argv[3], size)
