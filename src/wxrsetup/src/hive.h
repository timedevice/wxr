#pragma once
#include <windows.h>
/* Read-only reader for registry hive files (regf 1.x). Win7's RegLoadAppKey rejects Win10 hives (error 1009), so we parse them. */
typedef struct { BYTE *b; DWORD n; DWORD root; WCHAR err[256]; } Hive;
int  hive_open(Hive *h, const WCHAR *path);
void hive_close(Hive *h);
int  hive_find(Hive *h, const WCHAR *path, DWORD *nk);                    /* path relative to the hive root, '\' separated */
/* copies key nk (values + subkeys, recursively) into dst. counts are added to *keys / *values. returns 0 on failure (h->err) */
int  hive_copy(Hive *h, DWORD nk, HKEY dst, unsigned *keys, unsigned *values);
