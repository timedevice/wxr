import pefile, glob, os, collections, subprocess, sys
# genordmap.py [root]: builds data\ordmap\<dll>.txt ("ordinal name") for every ordinal imported (normal + delay) by the Win10 DLLs in
# <root>\Windows\System32 (default C:\WXR), named from the PDBs of the Win10 originals (getpdb.py + out\tools\symname.exe).
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYS32 = os.path.join(sys.argv[1] if len(sys.argv) > 1 else r'C:\WXR', 'Windows', 'System32')
ORDMAP = os.path.join(REPO, 'data', 'ordmap')
need = collections.defaultdict(set)
for f in glob.glob(os.path.join(SYS32, '*.dll')):
    pe = pefile.PE(f, fast_load=True); pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'], pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
    for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []) + getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', []):
        for i in e.imports:
            if not i.name: need[e.dll.decode().lower()].add(i.ordinal)
os.makedirs(ORDMAP, exist_ok=True)
ALIAS = (('api-ms-win-rtcore-ntuser', 'user32.dll'), ('ext-ms-win-ntuser', 'user32.dll'), ('ext-ms-win-rtcore-ntuser', 'user32.dll'),
         ('ext-ms-win-dwmapi', 'dwmapi.dll'), ('ext-ms-win-uxtheme', 'uxtheme.dll'), ('api-ms-win-shcore', 'shcore.dll'),
         ('api-ms-win-shlwapi-winrt-storage', 'shlwapi.dll'), ('api-ms-win-shell-shdirectory', 'shell32.dll'))
for dll, ords in sorted(need.items()):
    real = dll
    for pre, tgt in ALIAS:
        if dll.startswith(pre): real = tgt
    src = os.path.join(SYS32, real)
    if not os.path.exists(src): print('no reference dll for', dll, '(', real, ')', sorted(ords)); continue
    pe = pefile.PE(src); ex = {e.ordinal: e for e in pe.DIRECTORY_ENTRY_EXPORT.symbols}
    pdbdir = os.path.join(REPO, 'deps', 'symbols')
    try: subprocess.check_output([sys.executable, '-I', os.path.join(REPO, 'tools', 'getpdb.py'), src, pdbdir], stderr=subprocess.STDOUT)
    except Exception as ex_: print('pdb download failed for', real, ex_)
    rv = {o: ex[o].address for o in ords if o in ex and not ex[o].forwarder}
    out = subprocess.check_output([os.path.join(REPO, 'out', 'tools', 'symname.exe'), src, pdbdir] + ['%x' % r for r in rv.values()]).decode().splitlines() if rv else []
    names = dict(zip(rv.keys(), out))
    with open(os.path.join(ORDMAP, '%s.txt' % os.path.splitext(dll)[0]), 'w') as fh:
        for o in sorted(ords):
            e = ex.get(o)
            if not e: nm = '?NOTEXPORTED'
            elif e.name: nm = e.name.decode()
            elif e.forwarder: nm = '=>' + e.forwarder.decode()
            else:
                s = names.get(o, '?'); nm = s.split(' ', 1)[-1].split('+')[0] if ' ' in s else '?'
            fh.write('%d %s\n' % (o, nm)); print(dll, o, nm)
