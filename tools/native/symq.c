/* symq <pdb> <mask|@rva>... : prints RVA, size, name of matching symbols (dbghelp, PDB loaded at fake base 0x10000000) */
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
static BOOL CALLBACK cb(PSYMBOL_INFO s, ULONG sz, PVOID u) { (void)sz; (void)u; printf("%08llX %6lu %s\n", s->Address - 0x10000000ULL, s->Size, s->Name); return TRUE; }
int main(int argc, char **argv) {
  HANDLE p = GetCurrentProcess(); DWORD64 b; int i;
  if (argc < 3) return 1;
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_ANYTHING);
  if (!SymInitialize(p, NULL, FALSE)) return 2;
  b = SymLoadModuleEx(p, NULL, argv[1], NULL, 0x10000000, 0x2000000, NULL, 0);
  if (!b) { printf("load failed %lu\n", GetLastError()); return 3; }
  for (i = 2; i < argc; i++) {
    if (argv[i][0] == '@') { char buf[sizeof(SYMBOL_INFO) + 1024]; PSYMBOL_INFO s = (PSYMBOL_INFO)buf; DWORD64 d = 0; DWORD64 a = 0x10000000ULL + strtoull(argv[i] + 1, NULL, 16);
      memset(buf, 0, sizeof buf); s->SizeOfStruct = sizeof(SYMBOL_INFO); s->MaxNameLen = 1000;
      if (SymFromAddr(p, a, &d, s)) printf("%s = %s+0x%llX\n", argv[i] + 1, s->Name, d); else printf("%s = ?\n", argv[i] + 1); }
    else SymEnumSymbols(p, b, argv[i], cb, NULL); }
  return 0; }
