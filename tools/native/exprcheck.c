/* exprcheck <dcomp.dll> <file>: syntax check of composition expressions, one per line (UTF-8; '#' lines are comments), with the
   layer's own parser (dcomp.dll WxrDcomp_ExprCheck: unknown names and members are wildcards). Prints the lines that do not parse.
   Built by build.ps1 (target tools) as out\tools\exprcheck.exe. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    HMODULE m; int (WINAPI *chk)(const WCHAR *); FILE *f; char line[8192]; WCHAR w[8192]; int n = 0, bad = 0;
    if (argc < 3) { printf("usage: exprcheck <dcomp.dll> <file>\n"); return 2; }
    m = LoadLibraryA(argv[1]); if (!m) { printf("cannot load %s (%lu)\n", argv[1], GetLastError()); return 2; }
    chk = (int (WINAPI *)(const WCHAR *))GetProcAddress(m, "WxrDcomp_ExprCheck"); if (!chk) { printf("no WxrDcomp_ExprCheck export\n"); return 2; }
    f = fopen(argv[2], "r"); if (!f) { printf("cannot open %s\n", argv[2]); return 2; }
    while (fgets(line, sizeof line, f)) {
        int l = (int)strlen(line), r; while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!l || line[0] == '#') continue;
        MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 8192); n++;
        r = chk(w);
        if (r) { bad++; printf("FAIL at %d '%.20ls': %s\n", r - 1, w + r - 1, line); }
    }
    printf("%d expressions, %d failed\n", n, bad); return bad ? 1 : 0;
}
