/* wxrlaunch.inc - starting a packaged app of a WXR root on Windows 7 (shared by wxrrun --wxr and the broker wxrsvc --wxr).
   The includer defines WXR_OUT(fmt, ...) for log lines.
   1. Windows 7's CreateProcess refuses (error 193) an exe whose subsystem version is above 6.1. kernel32 checks the version it gets back from
      NtCreateUserProcess (PS_ATTRIBUTE_IMAGE_INFO -> SECTION_IMAGE_INFORMATION): kernel32's import of NtCreateUserProcess is hooked in this
      process and the reported version lowered to 6.1. The file is not touched. (Under VxKex, kernel32 is already patched by VxKex.)
   2. The app starts suspended; its in-memory import directory is replaced by one import of wxrsys.dll (found through PATH = <root>\Layer).
      wxrsys's DllMain binds the exe's real imports and every WXR module after it (wxrldr.inc).
   3. Activation by AUMID ("<PackageFamilyName>!<AppId>", as Win10's ApplicationActivationManager): the user's registered packages
      (<root>\Users\<user>\packages.ini) and package records (<root>\ProgramData\Packages\<full>.ini [applications] Id=Executable|EntryPoint). */
typedef LONG (NTAPI *PFN_NCUP_)(PHANDLE, PHANDLE, ACCESS_MASK, ACCESS_MASK, PVOID, PVOID, ULONG, ULONG, PVOID, PVOID, PVOID);
static PFN_NCUP_ g_ncup;
typedef struct { ULONG_PTR Attribute; SIZE_T Size; ULONG_PTR Value; PSIZE_T ReturnLength; } PSATTR_;
static LONG NTAPI hk_ncup(PHANDLE ph, PHANDLE th, ACCESS_MASK pa, ACCESS_MASK ta, PVOID poa, PVOID toa, ULONG pf, ULONG tf, PVOID pp, PVOID ci, PVOID al)
{
    LONG st = g_ncup(ph, th, pa, ta, poa, toa, pf, tf, pp, ci, al);
    if (st >= 0 && al) {
        SIZE_T total = *(SIZE_T *)al, n = (total - sizeof(SIZE_T)) / sizeof(PSATTR_), i; PSATTR_ *a = (PSATTR_ *)((BYTE *)al + sizeof(SIZE_T));
        for (i = 0; i < n; i++) if ((a[i].Attribute & 0x3FFFF) == 6 /* PsAttributeImageInfo */ && a[i].Value && a[i].Size >= 0x30) {
            USHORT *mi = (USHORT *)((BYTE *)a[i].Value + 0x24), *ma = mi + 1;
            if (*ma > 6 || (*ma == 6 && *mi > 1)) { WXR_OUT("  [wxr]  exe subsystem version %u.%u reported to kernel32 as 6.1 (file unchanged)\n", *ma, *mi); *ma = 6; *mi = 1; } }
    }
    return st;
}
static void *iat_patch(HMODULE mod, const char *dll, const char *fn, void *hook)
{
    BYTE *b = (BYTE *)mod; IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew); IMAGE_IMPORT_DESCRIPTOR *id; DWORD old;
    if (!nt->OptionalHeader.DataDirectory[1].VirtualAddress) return NULL;
    for (id = (IMAGE_IMPORT_DESCRIPTOR *)(b + nt->OptionalHeader.DataDirectory[1].VirtualAddress); id->Name; id++) {
        ULONGLONG *ilt, *iat; int i; if (_stricmp((const char *)(b + id->Name), dll)) continue;
        ilt = (ULONGLONG *)(b + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk)); iat = (ULONGLONG *)(b + id->FirstThunk);
        for (i = 0; ilt[i]; i++) if (!(ilt[i] >> 63) && !strcmp((const char *)(b + (DWORD)ilt[i] + 2), fn)) {
            void *o = (void *)iat[i]; if (!VirtualProtect(&iat[i], 8, PAGE_READWRITE, &old)) return NULL; iat[i] = (ULONGLONG)hook; VirtualProtect(&iat[i], 8, old, &old); return o; }
    }
    return NULL;
}
typedef LONG (NTAPI *PFN_NtQIP_)(HANDLE, int, PVOID, ULONG, PULONG);
static int wxr_inject(HANDLE proc, const char *dll)
{
    PFN_NtQIP_ q = (PFN_NtQIP_)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
    struct { LONG st; PVOID peb; ULONG_PTR aff, prio, pid, ppid; } pbi; ULONG_PTR base = 0; SIZE_T n; IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; BYTE blk[512]; BYTE *rb = NULL; DWORD rva, old;
    memset(&pbi, 0, sizeof pbi);
    if (!q || q(proc, 0, &pbi, sizeof pbi, NULL) < 0 || !ReadProcessMemory(proc, (BYTE *)pbi.peb + 0x10, &base, 8, &n)) { WXR_OUT("  [wxr]  cannot read the PEB\n"); return 0; }
    if (!ReadProcessMemory(proc, (void *)base, &dos, sizeof dos, &n) || !ReadProcessMemory(proc, (void *)(base + dos.e_lfanew), &nt, sizeof nt, &n)) { WXR_OUT("  [wxr]  cannot read the exe headers\n"); return 0; }
    /* the block goes into the slack at the end of a writable section (VxKex refuses import tables outside the image:
       STATUS_IMPORT_TABLE_TAMPERING_DETECTED; the loader writes the IAT, so the section must be writable) */
    { IMAGE_SECTION_HEADER sh[96]; int ns = nt.FileHeader.NumberOfSections, s; if (ns > 96) ns = 96;
      if (!ReadProcessMemory(proc, (void *)(base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader), sh, ns * sizeof sh[0], &n)) { WXR_OUT("  [wxr]  cannot read the section table\n"); return 0; }
      for (s = 0; s < ns && !rb; s++) { DWORD vs = sh[s].Misc.VirtualSize ? sh[s].Misc.VirtualSize : sh[s].SizeOfRawData, start = (sh[s].VirtualAddress + vs + 15) & ~15u, end = (sh[s].VirtualAddress + vs + 0xFFF) & ~0xFFFu;
        if ((sh[s].Characteristics & IMAGE_SCN_MEM_WRITE) && end > start && end - start >= sizeof blk) rb = (BYTE *)(base + start); } }
    if (!rb) { WXR_OUT("  [wxr]  no slack in a writable section of the exe for the import block\n"); return 0; }
    rva = (DWORD)((ULONG_PTR)rb - base); memset(blk, 0, sizeof blk);
    /* 0: 2 descriptors (40)  48: ILT  64: IAT  80: hint+name  112: dll name */
    { IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)blk; d->OriginalFirstThunk = rva + 48; d->Name = rva + 112; d->FirstThunk = rva + 64; }
    *(ULONGLONG *)(blk + 48) = rva + 80; *(ULONGLONG *)(blk + 64) = rva + 80; memcpy(blk + 82, "WxrUnimpl", 11); lstrcpynA((char *)blk + 112, dll, 200);
    if (!VirtualProtectEx(proc, rb, sizeof blk, PAGE_READWRITE, &old) || !WriteProcessMemory(proc, rb, blk, sizeof blk, &n)) { WXR_OUT("  [wxr]  cannot write the import block (%lu)\n", GetLastError()); return 0; }
    VirtualProtectEx(proc, rb, sizeof blk, old, &old);
    nt.OptionalHeader.DataDirectory[1].VirtualAddress = rva; nt.OptionalHeader.DataDirectory[1].Size = 40;
    nt.OptionalHeader.DataDirectory[11].VirtualAddress = 0; nt.OptionalHeader.DataDirectory[11].Size = 0;
    if (!VirtualProtectEx(proc, (void *)(base + dos.e_lfanew), sizeof nt, PAGE_READWRITE, &old) || !WriteProcessMemory(proc, (void *)(base + dos.e_lfanew), &nt, sizeof nt, &n)) { WXR_OUT("  [wxr]  cannot patch the exe headers (%lu)\n", GetLastError()); return 0; }
    VirtualProtectEx(proc, (void *)(base + dos.e_lfanew), sizeof nt, old, &old);
    WXR_OUT("  [wxr]  exe at %p: imports replaced in memory by %s (block at %p)\n", (void *)base, dll, rb);
    return 1;
}
/* VxKex is enabled per exact exe path (IFEO\<name>\VxKex_*\FilterFullPath + VerifierDlls=kexdll.dll), or inherited from a VxKex parent */
static int vxkex_enabled(const char *exe)
{
    char key[400], sub[200], v[MAX_PATH]; const char *b = strrchr(exe, '\\'); HKEY k, s; DWORD i, n, t; int ok = 0;
    if (GetModuleHandleA("KexDll.dll")) return 2;   /* this process runs under VxKex: children inherit it */
    snprintf(key, sizeof key, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\%s", b ? b + 1 : exe);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &k)) return 0;
    for (i = 0; !ok && (n = sizeof sub, RegEnumKeyExA(k, i, sub, &n, NULL, NULL, NULL, NULL)) == 0; i++) {
        if (RegOpenKeyExA(k, sub, 0, KEY_READ | KEY_WOW64_64KEY, &s)) continue;
        n = sizeof v; if (!RegQueryValueExA(s, "FilterFullPath", NULL, &t, (BYTE *)v, &n) && !_stricmp(v, exe)) {
            n = sizeof v; if (!RegQueryValueExA(s, "VerifierDlls", NULL, &t, (BYTE *)v, &n) && strstr(_strlwr(v), "kexdll")) ok = 1; }
        RegCloseKey(s); }
    RegCloseKey(k); return ok;
}
/* the environment every app of the root inherits (set once in this process) */
static int wxr_env(const char *root)
{
    static int done; char p[MAX_PATH], *path; DWORD n;
    if (done) return 1;
    snprintf(p, sizeof p, "%s\\Layer\\wxrsys.dll", root); if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) { WXR_OUT("  [wxr]  %s is missing (install the layer)\n", p); return 0; }
    SetEnvironmentVariableA("WXR_LOADER", "1"); SetEnvironmentVariableA("WXR_ROOT", root);
    snprintf(p, sizeof p, "%s\\WindowsApps", root); SetEnvironmentVariableA("WXR_PACKAGE_STORE", p);
    n = GetEnvironmentVariableA("PATH", NULL, 0); path = (char *)malloc(n + 2 * MAX_PATH + 8); if (!path) return 0;
    { char *old = (char *)malloc(n + 1); GetEnvironmentVariableA("PATH", old, n + 1); sprintf(path, "%s\\Layer;%s;%s\\Windows\\System32", root, old, root); free(old); }
    SetEnvironmentVariableA("PATH", path); free(path);
    if (!GetModuleHandleA("KexDll.dll")) {
        g_ncup = (PFN_NCUP_)iat_patch(GetModuleHandleA("kernel32.dll"), "ntdll.dll", "NtCreateUserProcess", (void *)hk_ncup);
        if (!g_ncup) WXR_OUT("  [wxr]  cannot hook kernel32's NtCreateUserProcess: exes with a Win10 subsystem version will fail with error 193\n"); }
    done = 1; return 1;
}
/* starts exe (suspended, layer injected, resumed). Returns the process id or 0; err gets a message. */
static DWORD wxr_start_exe(const char *root, const char *exe, const char *args, int debug, PROCESS_INFORMATION *pio, char *err, int ecap)
{
    char cmd[4096], dir[MAX_PATH], *s; STARTUPINFOA si; PROCESS_INFORMATION pi; int vk = vxkex_enabled(exe);
    if (!vk) { _snprintf(err, ecap, "VxKex is not enabled for %s (and the launcher does not run under VxKex)", exe); return 0; }
    if (!wxr_env(root)) { _snprintf(err, ecap, "the WXR layer is not installed in %s\\Layer", root); return 0; }
    _snprintf(cmd, sizeof cmd, "\"%s\"%s%s", exe, args && *args ? " " : "", args ? args : ""); cmd[sizeof cmd - 1] = 0;
    lstrcpynA(dir, exe, MAX_PATH); if ((s = strrchr(dir, '\\'))) *s = 0;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED | (debug ? DEBUG_ONLY_THIS_PROCESS : 0), NULL, dir, &si, &pi)) { _snprintf(err, ecap, "CreateProcess(%s) failed, error %lu", exe, GetLastError()); return 0; }
    if (!wxr_inject(pi.hProcess, "wxrsys.dll")) { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); _snprintf(err, ecap, "could not attach the layer to %s", exe); return 0; }
    ResumeThread(pi.hThread);
    if (pio) *pio = pi; else { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return pi.dwProcessId;
}
/* AUMID -> exe path and the -ServerName argument. Highest registered version of the family wins. */
static int wxr_resolve_aumid(const char *root, const char *aumid, char *exe, char *server, char *err, int ecap)
{
    char fam[200], app[100], user[128] = "", regf[MAX_PATH], keys[32768], *k, best[300] = "", bestv[64] = ""; DWORD un = sizeof user; const char *bang = strchr(aumid, '!');
    if (!bang || bang - aumid >= (int)sizeof fam) { _snprintf(err, ecap, "'%s' is not an AUMID (PackageFamilyName!AppId)", aumid); return 0; }
    memcpy(fam, aumid, bang - aumid); fam[bang - aumid] = 0; lstrcpynA(app, bang + 1, sizeof app);
    GetUserNameA(user, &un); _snprintf(regf, MAX_PATH, "%s\\Users\\%s\\packages.ini", root, user);
    if (!GetPrivateProfileStringA("registered", NULL, "", keys, sizeof keys, regf)) { _snprintf(err, ecap, "no packages registered for %s (%s)", user, regf); return 0; }
    for (k = keys; *k; k += strlen(k) + 1) {   /* Name_Version_Arch_ResourceId_PublisherId; family = Name_PublisherId */
        char nm[200], ver[64], arch[32], rid[64], pid[32], f2[300], rec[MAX_PATH], is[16];
        if (sscanf(k, "%199[^_]_%63[^_]_%31[^_]_%63[^_]_%31s", nm, ver, arch, rid, pid) < 4) {
            if (sscanf(k, "%199[^_]_%63[^_]_%31[^_]__%31s", nm, ver, arch, pid) != 4) continue; rid[0] = 0; }
        _snprintf(f2, sizeof f2, "%s_%s", nm, pid); if (_stricmp(f2, fam)) continue;
        _snprintf(rec, MAX_PATH, "%s\\ProgramData\\Packages\\%s.ini", root, k);
        GetPrivateProfileStringA("package", "IsResourcePackage", "", is, sizeof is, rec); if (!_stricmp(is, "True")) continue;
        GetPrivateProfileStringA("package", "IsFramework", "", is, sizeof is, rec); if (!_stricmp(is, "True")) continue;
        { int a[4] = {0}, b[4] = {0}, j, newer = !best[0]; sscanf(ver, "%d.%d.%d.%d", &a[0], &a[1], &a[2], &a[3]); sscanf(bestv, "%d.%d.%d.%d", &b[0], &b[1], &b[2], &b[3]);
          for (j = 0; j < 4 && !newer; j++) { if (a[j] != b[j]) { newer = a[j] > b[j]; break; } }
          if (newer) { lstrcpynA(best, k, sizeof best); lstrcpynA(bestv, ver, sizeof bestv); } }
    }
    if (!best[0]) { _snprintf(err, ecap, "package family %s is not registered for %s", fam, user); return 0; }
    { char rec[MAX_PATH], loc[MAX_PATH], ent[400], *bar;
      _snprintf(rec, MAX_PATH, "%s\\ProgramData\\Packages\\%s.ini", root, best);
      GetPrivateProfileStringA("package", "InstallLocation", "", loc, sizeof loc, rec);
      if (!GetPrivateProfileStringA("applications", app, "", ent, sizeof ent, rec)) { _snprintf(err, ecap, "%s has no application '%s'", best, app); return 0; }
      if ((bar = strchr(ent, '|'))) *bar = 0;
      if (!ent[0]) { _snprintf(err, ecap, "%s!%s has no executable (not supported yet)", best, app); return 0; }
      _snprintf(exe, MAX_PATH, "%s\\%s", loc, ent);
      /* GDK titles: the Application is GameLaunchHelper.exe, which needs the Win10 game-filter driver (gameflt) and exits with
         ERROR_NOT_SUPPORTED. Their content is already loose on disk, so start the game's own executable instead: the first
         <Executable Name="..."> of MicrosoftGame.config (Minecraft for Windows: Minecraft.Windows.exe). */
      if (!_stricmp(ent, "GameLaunchHelper.exe")) { char cfg[MAX_PATH], buf[16384], *p, *q; DWORD got = 0; HANDLE f;
        _snprintf(cfg, MAX_PATH, "%s\\MicrosoftGame.config", loc);
        f = CreateFileA(cfg, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) { ReadFile(f, buf, sizeof buf - 1, &got, NULL); CloseHandle(f); buf[got] = 0;
          if ((p = strstr(buf, "<Executable ")) && (p = strstr(p, "Name=\"")) && (q = strchr(p += 6, '"')) && q - p > 0 && q - p < 200) {
            *q = 0; _snprintf(exe, MAX_PATH, "%s\\%s", loc, p); } } }
      /* Win10 passes the app's registered WinRT server name; ours is made up the same shape (wxrshell accepts any) */
      _snprintf(server, 200, "-ServerName:%s.AppXwxr%.24s.mca", app, fam); }
    return 1;
}
