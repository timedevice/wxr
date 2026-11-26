/* gdk.inc - "wxrpkg gdk [--remove] [--force-update]": the runtime of GDK PC games (Microsoft Game Development Kit, e.g. Minecraft
 * for Windows). Part of wxrpkg.c, after store.inc.
 *
 * On Windows 10 these files come with Gaming Services and with each game's own redistributables; WXR takes them from the same places:
 *   - xgameruntime.dll, gamelaunchhelper.dll, gameconfighelper.dll: from Microsoft's Gaming Services package (Store product
 *     9MWPM2CQNLHN, downloaded through the delivery service like "wxrpkg store"), into <root>\Windows\System32, where Win10 has them.
 *     Only these three files are kept: the package's services and drivers cannot run on Windows 7 (the layer stands in for them).
 *   - GameInputRedist.dll: from the game's own Installers\GameInputRedist.msi by an administrative extract (msiexec /a: nothing is
 *     installed or registered, no service), into <root>\Program Files\Microsoft GameInput\x64; the newest version wins. The layer
 *     answers the game's HKLM\SOFTWARE\Microsoft\GameInput RedistDir lookup with that folder.
 * "wxrpkg gdk" does both (GameInput for every installed game); "wxrpkg add" does the GameInput part for a game it installs while the
 * gdk component is on (environment.ini [components] gdk=1). --remove deletes all of these files again. */
#define GAMING_SERVICES L"9MWPM2CQNLHN"
static const WCHAR *const k_gdk_files[] = { L"xgameruntime.dll", L"gamelaunchhelper.dll", L"gameconfighelper.dll" };
#define NGDK (sizeof k_gdk_files / sizeof k_gdk_files[0])

static int gdk_on(void) {
    WCHAR ini[MAX_PATH], v[8]; _snwprintf(ini, MAX_PATH, L"%ls\\environment.ini", g_root);
    GetPrivateProfileStringW(L"components", L"gdk", L"0", v, 8, ini); return v[0] == '1';
}
static ULONGLONG file_ver(const WCHAR *p) {
    DWORD h, n = GetFileVersionInfoSizeW(p, &h); BYTE *b; VS_FIXEDFILEINFO *fi; UINT l; ULONGLONG v = 0;
    if (!n || !(b = (BYTE *)malloc(n))) return 0;
    if (GetFileVersionInfoW(p, 0, n, b) && VerQueryValueW(b, L"\\", (void **)&fi, &l) && l >= sizeof *fi) v = ((ULONGLONG)fi->dwFileVersionMS << 32) | fi->dwFileVersionLS;
    free(b); return v;
}
static void ver_str(ULONGLONG v, WCHAR *o) { _snwprintf(o, 32, L"%u.%u.%u.%u", (unsigned)(v >> 48), (unsigned)(v >> 32) & 0xFFFF, (unsigned)(v >> 16) & 0xFFFF, (unsigned)v & 0xFFFF); }
/* one zip entry (a top-level file, any case) -> a file */
static int zip_save(Zip *z, const WCHAR *name, const WCHAR *dst, WCHAR *why) {
    size_t n; void *d = zip_read(z, name, &n); HANDLE h; DWORD w; WCHAR tmp[MAX_PATH]; int ok;
    if (!d) { _snwprintf(why, 256, L"%ls is not in the package", name); return 0; }
    _snwprintf(tmp, MAX_PATH, L"%ls.new", dst);
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok = h != INVALID_HANDLE_VALUE && WriteFile(h, d, (DWORD)n, &w, NULL) && w == n;
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    mz_free(d);
    if (ok) ok = MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING);
    if (!ok) { _snwprintf(why, 256, L"cannot write %ls (error %lu)", dst, GetLastError()); DeleteFileW(tmp); }
    return ok;
}

/* Gaming Services -> the three runtime files */
static int gdk_runtime(int force) {
    StList L; const StEnt *m; WCHAR why[256] = L"", dir[MAX_PATH], file[MAX_PATH], inner[MAX_PATH], dst[MAX_PATH], name[256], *us; Zip z; unsigned i; int have = 0, rc = 1;
    for (i = 0; i < NGDK; i++) { _snwprintf(dst, MAX_PATH, L"%ls\\Windows\\System32\\%ls", g_root, k_gdk_files[i]); if (exists(dst)) have++; }
    if (have == (int)NGDK && !force) { info(L"The GDK game runtime is installed (--force-update downloads it again)"); return 0; }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!st_list(GAMING_SERVICES, &L, why)) { err(L"%ls", why); return 1; }
    lstrcpynW(name, L.family, 256); if ((us = wcsrchr(name, '_'))) *us = 0;
    if (!(m = st_pick(&L, name, 0))) { err(L"the delivery service offers no Gaming Services package for this computer"); free(L.e); return 1; }
    info(L"GDK game runtime from %ls", m->moniker);
    _snwprintf(dir, MAX_PATH, L"%ls\\Temp\\gdk-%lu", g_root, GetCurrentProcessId()); mkdirs(dir);
    if (!st_fetch(m, dir, file, why)) { err(L"%ls", why); goto out; }
    if (m->bundle) {   /* the x64 package inside the bundle */
        WCHAR pick[400] = L""; FILE *f; int idx;
        if (!zip_open(&z, file, why)) { err(L"%ls", why); goto out; }
        for (i = 0; i < z.n; i++) { const WCHAR *e = wcsrchr(z.names[i], '.');
            if (e && (!_wcsicmp(e, L".msix") || !_wcsicmp(e, L".appx")) && StrStrIW(z.names[i], L"_x64")) { lstrcpynW(pick, z.names[i], 400); break; } }
        if (!pick[0] || (idx = zip_find(&z, pick)) < 0) { zip_close(&z); err(L"the Gaming Services bundle has no x64 package"); goto out; }
        _snwprintf(inner, MAX_PATH, L"%ls\\inner.msix", dir);
        if (!(f = _wfopen(inner, L"wb"))) { zip_close(&z); err(L"cannot create %ls", inner); goto out; }
        if (!mz_zip_reader_extract_to_cfile(&z.z, (mz_uint)idx, f, 0)) { fclose(f); zip_close(&z); err(L"cannot unpack %ls from the bundle", pick); goto out; }
        fclose(f); zip_close(&z);
    } else lstrcpynW(inner, file, MAX_PATH);
    if (!zip_open(&z, inner, why)) { err(L"%ls", why); goto out; }
    for (i = 0; i < NGDK; i++) {
        _snwprintf(dst, MAX_PATH, L"%ls\\Windows\\System32\\%ls", g_root, k_gdk_files[i]);
        if (!zip_save(&z, k_gdk_files[i], dst, why)) { zip_close(&z); err(L"%ls", why); goto out; }
        { WCHAR v[32]; ver_str(file_ver(dst), v); info(L"  %ls %ls", k_gdk_files[i], v); }
    }
    zip_close(&z);
    rc = 0;
out:
    rm_tree(dir); free(L.e);
    return rc;
}

/* a game's Installers\GameInputRedist.msi -> <root>\Program Files\Microsoft GameInput\x64\GameInputRedist.dll (if newer).
   0 = done or nothing to do, 1 = failed */
static int gdk_gameinput(const WCHAR *pkgdir) {
    WCHAR msi[MAX_PATH], tmp[MAX_PATH], src[MAX_PATH], dstd[MAX_PATH], dst[MAX_PATH], sys[MAX_PATH], exe[MAX_PATH], cmd[3 * MAX_PATH], v1[32], v2[32];
    STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD code = 1; ULONGLONG nv, ov;
    _snwprintf(msi, MAX_PATH, L"%ls\\Installers\\GameInputRedist.msi", pkgdir);
    if (!exists(msi)) return 0;
    _snwprintf(tmp, MAX_PATH, L"%ls\\Temp\\gameinput-%lu", g_root, GetCurrentProcessId()); rm_tree(tmp); mkdirs(tmp);
    GetSystemDirectoryW(sys, MAX_PATH); _snwprintf(exe, MAX_PATH, L"%ls\\msiexec.exe", sys);
    _snwprintf(cmd, 3 * MAX_PATH, L"\"%ls\" /a \"%ls\" /qn TARGETDIR=\"%ls\"", exe, msi, tmp);
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { warn(L"cannot run msiexec (error %lu)", GetLastError()); rm_tree(tmp); return 1; }
    if (WaitForSingleObject(pi.hProcess, 300000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    _snwprintf(src, MAX_PATH, L"%ls\\Microsoft GameInput\\x64\\GameInputRedist.dll", tmp);
    if (code || !exists(src)) { warn(L"could not unpack %ls (msiexec exit code %lu)", msi, code); rm_tree(tmp); return 1; }
    _snwprintf(dstd, MAX_PATH, L"%ls\\Program Files\\Microsoft GameInput\\x64", g_root); _snwprintf(dst, MAX_PATH, L"%ls\\GameInputRedist.dll", dstd);
    nv = file_ver(src); ov = exists(dst) ? file_ver(dst) : 0; ver_str(nv, v1); ver_str(ov, v2);
    if (ov && ov >= nv) info(L"GameInput %ls is set up (this game brings %ls)", v2, v1);
    else if (!mkdirs(dstd) || !CopyFileW(src, dst, FALSE)) { warn(L"cannot copy GameInputRedist.dll to %ls (error %lu)", dstd, GetLastError()); rm_tree(tmp); return 1; }
    else info(L"GameInput %ls set up from %ls", v1, msi);
    rm_tree(tmp);
    return 0;
}

static int cmd_gdk(int remove, int force) {
    WCHAR p[MAX_PATH], pat[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; unsigned i; int rc = 0;
    if (remove) {
        for (i = 0; i < NGDK; i++) { _snwprintf(p, MAX_PATH, L"%ls\\Windows\\System32\\%ls", g_root, k_gdk_files[i]);
            if (exists(p) && !DeleteFileW(p)) { err(L"cannot delete %ls (error %lu; is a game running?)", p, GetLastError()); rc = 1; } }
        _snwprintf(p, MAX_PATH, L"%ls\\Program Files\\Microsoft GameInput", g_root); rm_tree(p); RemoveDirectoryW(p);
        _snwprintf(p, MAX_PATH, L"%ls\\Program Files", g_root); RemoveDirectoryW(p);                     /* only if empty */
        if (!rc) info(L"The GDK game runtime is removed");
        return rc;
    }
    if (gdk_runtime(force)) return 1;
    /* GameInput from the games installed already */
    _snwprintf(pat, MAX_PATH, L"%ls\\WindowsApps\\*", g_root);
    if ((h = FindFirstFileW(pat, &fd)) != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
             _snwprintf(p, MAX_PATH, L"%ls\\WindowsApps\\%ls", g_root, fd.cFileName); if (gdk_gameinput(p)) rc = 1; } while (FindNextFileW(h, &fd));
        FindClose(h); }
    return rc;
}
