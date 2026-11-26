import pefile, sys, os, struct, urllib.request
# usage: getpdb.py <dll> <outdir>  -> downloads the matching PDB from the Microsoft symbol server
dll, out = sys.argv[1], sys.argv[2]
pe = pefile.PE(dll, fast_load=True); pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DEBUG']])
for d in pe.DIRECTORY_ENTRY_DEBUG:
    if d.struct.Type == 2:
        raw = pe.get_data(d.struct.AddressOfRawData, d.struct.SizeOfData)
        assert raw[:4] == b'RSDS'
        d1, d2, d3 = struct.unpack_from('<IHH', raw, 4); d4 = raw[12:20]; age, = struct.unpack_from('<I', raw, 20)
        name = raw[24:].split(b'\0')[0].decode().replace('\\', '/').split('/')[-1]
        key = '%08X%04X%04X%s%d' % (d1, d2, d3, d4.hex().upper(), age)
        url = 'https://msdl.microsoft.com/download/symbols/%s/%s/%s' % (name, key, name)
        print(url)
        os.makedirs(out, exist_ok=True)
        req = urllib.request.Request(url, headers={'User-Agent': 'Microsoft-Symbol-Server/10.0.0.0'})
        data = urllib.request.urlopen(req, timeout=300).read()
        open(os.path.join(out, name), 'wb').write(data); print('saved', name, len(data))
