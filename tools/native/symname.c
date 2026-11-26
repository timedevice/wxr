/* symname <dll> <symdir> <hexrva>...   prints the PDB symbol covering each RVA (dbghelp, no symbol server) */
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  HANDLE p = GetCurrentProcess(); DWORD64 base; int i; char buf[sizeof(SYMBOL_INFO) + 512]; SYMBOL_INFO *s = (SYMBOL_INFO *)buf;
  if (argc < 4) { puts("usage: symname <dll> <symdir> <hexrva>..."); return 1; }
  SymSetOptions(SYMOPT_UNDNAME * 0 | SYMOPT_DEFERRED_LOADS * 0 | SYMOPT_LOAD_ANYTHING);
  if (!SymInitialize(p, argv[2], FALSE)) { printf("SymInitialize %lu\n", GetLastError()); return 1; }
  base = SymLoadModuleEx(p, NULL, argv[1], NULL, 0x10000000, 0, NULL, 0);
  if (!base) { printf("SymLoadModuleEx %lu\n", GetLastError()); return 1; }
  for (i = 3; i < argc; i++) {
    DWORD64 rva = strtoull(argv[i], NULL, 16), disp = 0;
    memset(buf, 0, sizeof buf); s->SizeOfStruct = sizeof(SYMBOL_INFO); s->MaxNameLen = 500;
    if (SymFromAddr(p, base + rva, &disp, s)) printf("%s %s+0x%llx\n", argv[i], s->Name, (unsigned long long)disp);
    else printf("%s ? (err %lu)\n", argv[i], GetLastError());
  }
  return 0;
}
