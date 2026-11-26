# fnrefs.py <dll> <pdb dir> <function name> [max bytes, hex]: disassembles a function and names every RIP-relative data
# reference and direct call through the PDB (Windows dbghelp; e.g. deps\symbols filled by getpdb.py). Needs pefile + capstone.
import sys, ctypes, ctypes.wintypes as W, os, pefile, capstone
dll, pdbdir, fname = sys.argv[1], sys.argv[2], sys.argv[3]
maxlen = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x1000
dh = ctypes.WinDLL(r'C:\Windows\System32\dbghelp.dll'); h = W.HANDLE(0x4321)
dh.SymSetOptions(0x2 | 0x4); dh.SymInitializeW.argtypes = [W.HANDLE, W.LPCWSTR, W.BOOL]; dh.SymInitializeW(h, pdbdir, False)
dh.SymLoadModuleExW.restype = ctypes.c_uint64
dh.SymLoadModuleExW.argtypes = [W.HANDLE, W.HANDLE, W.LPCWSTR, W.LPCWSTR, ctypes.c_uint64, W.DWORD, ctypes.c_void_p, W.DWORD]
B = 0x10000000; dh.SymLoadModuleExW(h, None, dll, None, B, os.path.getsize(dll), None, 0)
class SI(ctypes.Structure):
    _fields_ = [('SizeOfStruct', W.ULONG), ('TypeIndex', W.ULONG), ('Reserved', ctypes.c_uint64 * 2), ('Index', W.ULONG), ('Size', W.ULONG),
                ('ModBase', ctypes.c_uint64), ('Flags', W.ULONG), ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64), ('Register', W.ULONG),
                ('Scope', W.ULONG), ('Tag', W.ULONG), ('NameLen', W.ULONG), ('MaxNameLen', W.ULONG), ('Name', ctypes.c_wchar * 2000)]
dh.SymFromAddrW.argtypes = [W.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SI)]
dh.SymFromNameW.argtypes = [W.HANDLE, W.LPCWSTR, ctypes.POINTER(SI)]
def name(rva):
    s = SI(); s.SizeOfStruct = 88; s.MaxNameLen = 2000; d = ctypes.c_uint64()
    return (s.Name + ('+0x%x' % d.value if d.value else '')) if dh.SymFromAddrW(h, B + rva, ctypes.byref(d), ctypes.byref(s)) else None
s = SI(); s.SizeOfStruct = 88; s.MaxNameLen = 2000
assert dh.SymFromNameW(h, fname, ctypes.byref(s)), 'no symbol ' + fname
start = s.Address - B; size = s.Size or maxlen
pe = pefile.PE(dll); data = pe.get_data(start, min(size, maxlen))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
print('%s rva %x size %x' % (fname, start, size))
for ins in md.disasm(data, start):
    note = ''
    if 'rip' in ins.op_str:
        try:
            t = ins.address + ins.size + int(ins.op_str.split('rip')[1].split(']')[0].replace(' ', ''), 16); n = name(t)
            if n: note = '  ; ' + n
        except Exception: pass
    elif ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
        n = name(int(ins.op_str, 16)); note = '  ; ' + (n or '')
    if note or ins.mnemonic == 'call': print('%x: %s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
