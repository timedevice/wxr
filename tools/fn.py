import sys, pefile, capstone
# usage: fn.py dll start_rva length  -> linear disassembly with import/call annotations
pe = pefile.PE(sys.argv[1]); base = pe.OPTIONAL_HEADER.ImageBase
start = int(sys.argv[2], 16); n = int(sys.argv[3], 16)
imps = {}
for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
    for i in e.imports: imps[i.address - base] = e.dll.decode() + '!' + (i.name or b'#%d' % i.ordinal).decode()
for e in getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', []):
    for i in e.imports: imps[i.address - base] = e.dll.decode() + '!' + (i.name or b'#%d' % i.ordinal).decode()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for ins in md.disasm(pe.get_data(start, n), start):
    note = ''
    if 'rip' in ins.op_str:
        t = ins.address + ins.size + int(ins.op_str.split('rip')[1].split(']')[0].replace(' ', ''), 16)
        note = '  ; ' + imps.get(t, '-> %x' % t)
    print('%x: %s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
