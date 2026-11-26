#pragma once
#include <windows.h>
#define UDF_MAX_EXT 64
typedef struct { HANDLE h; DWORD part_start, root_lb; } Udf;
typedef struct { DWORD lb, len; int recorded; } UdfExt;
typedef struct { ULONGLONG size; int is_dir, embedded; DWORD next; UdfExt ext[UDF_MAX_EXT]; BYTE emb[2048]; DWORD emb_len; } UdfFile;
int  udf_open(Udf *u, const WCHAR *path, WCHAR *msg);   /* msg: 256 WCHARs */
void udf_close(Udf *u);
int  udf_find(Udf *u, const WCHAR *path, UdfFile *out);
int  udf_read(Udf *u, const UdfFile *f, ULONGLONG offset, void *buf, DWORD n);   /* n bytes at offset inside the file */
int  udf_copy(Udf *u, const UdfFile *f, const WCHAR *dest, void (*progress)(ULONGLONG done, ULONGLONG total, void *ctx), void *ctx, WCHAR *msg);
