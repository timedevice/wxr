/* wxrcab.inc - the Windows 8 Compression API (cabinet.dll #40 CreateDecompressor, #43 Decompress, #45 CloseDecompressor), MSZIP only.
   Included by wxrsys.c. Win10's d2d1 keeps its built-in effect shaders compressed (CTrackedShader::DecompressShader, algorithm 2 = MSZIP);
   Win7's cabinet.dll has no Compression API, the ordinals failed (ERROR_INVALID_ORDINAL) and NO Direct2D built-in effect could be created
   (Flood, Composite, AlphaMask, blur ...). WinUI 3's compositor builds rounded fills, nine-grids, masks and acrylic from them.
   Buffer format: header { DWORD magic 0x0A51E5C0; BYTE header size (0x18); BYTE 0; BYTE version; BYTE algorithm; UINT64 uncompressed size;
   UINT64 chunk size }, then chunks { DWORD compressed size; "CK" + raw deflate } (MSZIP: deflate history continues across CK blocks).
   Ordinals reach these functions through Layer\ordmap\cabinet.txt. */

typedef struct { const BYTE *in; SIZE_T inlen, inpos; UINT bitbuf; int bitcnt; BYTE *out; SIZE_T outlen, outpos; } Inf_;
static int inf_bits(Inf_ *s, int n) {
  UINT v = s->bitbuf;
  while (s->bitcnt < n) { if (s->inpos >= s->inlen) return -1; v |= (UINT)s->in[s->inpos++] << s->bitcnt; s->bitcnt += 8; }
  s->bitbuf = v >> n; s->bitcnt -= n; return (int)(v & ((1u << n) - 1)); }
typedef struct { short count[16]; short symbol[288]; } InfHuff_;
static int inf_build(InfHuff_ *h, const short *len, int n) {
  short offs[16]; int i, left = 1;
  for (i = 0; i < 16; i++) h->count[i] = 0; for (i = 0; i < n; i++) h->count[len[i]]++;
  if (h->count[0] == n) return 0;
  for (i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return left; }
  offs[1] = 0; for (i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
  for (i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = (short)i;
  return left; }
static int inf_decode(Inf_ *s, const InfHuff_ *h) {
  int code = 0, first = 0, index = 0, len, count, b;
  for (len = 1; len < 16; len++) { if ((b = inf_bits(s, 1)) < 0) return -1; code |= b; count = h->count[len];
    if (code - count < first) return h->symbol[index + (code - first)]; index += count; first += count; first <<= 1; code <<= 1; }
  return -2; }
static const short inf_lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short inf_lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short inf_dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short inf_dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static int inf_codes(Inf_ *s, const InfHuff_ *lc, const InfHuff_ *dc) {
  int sym, len, e; SIZE_T dist;
  for (;;) {
    if ((sym = inf_decode(s, lc)) < 0) return sym;
    if (sym < 256) { if (s->outpos >= s->outlen) return -3; s->out[s->outpos++] = (BYTE)sym; }
    else if (sym == 256) return 0;
    else { sym -= 257; if (sym >= 29) return -4;
      if ((e = inf_bits(s, inf_lext[sym])) < 0) return -1; len = inf_lbase[sym] + e;
      if ((sym = inf_decode(s, dc)) < 0 || sym >= 30) return -5;
      if ((e = inf_bits(s, inf_dext[sym])) < 0) return -1; dist = (SIZE_T)(inf_dbase[sym] + e);
      if (dist > s->outpos) return -6;
      if (s->outpos + (SIZE_T)len > s->outlen) return -3;
      while (len--) { s->out[s->outpos] = s->out[s->outpos - dist]; s->outpos++; } } } }
static int inf_stored(Inf_ *s) {
  UINT len; s->bitbuf = 0; s->bitcnt = 0;
  if (s->inpos + 4 > s->inlen) return -1;
  len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8); s->inpos += 4;
  if (s->inpos + len > s->inlen || s->outpos + len > s->outlen) return -3;
  memcpy(s->out + s->outpos, s->in + s->inpos, len); s->inpos += len; s->outpos += len; return 0; }
static int inf_fixed(Inf_ *s) {
  static InfHuff_ lc, dc; static int done; short l[288]; int i;
  if (!done) { for (i = 0; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8; inf_build(&lc, l, 288);
    for (i = 0; i < 30; i++) l[i] = 5; inf_build(&dc, l, 30); done = 1; }
  return inf_codes(s, &lc, &dc); }
static int inf_dynamic(Inf_ *s) {
  static const short ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15}; short l[320]; InfHuff_ lc, dc; int nlen, ndist, ncode, i, sym, len, rep;
  if ((nlen = inf_bits(s, 5)) < 0 || (ndist = inf_bits(s, 5)) < 0 || (ncode = inf_bits(s, 4)) < 0) return -1;
  nlen += 257; ndist += 1; ncode += 4; if (nlen > 286 || ndist > 30) return -7;
  for (i = 0; i < 19; i++) l[ord[i]] = 0;
  for (i = 0; i < ncode; i++) { int b = inf_bits(s, 3); if (b < 0) return -1; l[ord[i]] = (short)b; }
  if (inf_build(&lc, l, 19) != 0) return -8;
  for (i = 0; i < nlen + ndist; ) {
    if ((sym = inf_decode(s, &lc)) < 0) return sym;
    if (sym < 16) l[i++] = (short)sym;
    else { len = 0; if (sym == 16) { if (!i) return -9; len = l[i - 1]; if ((rep = inf_bits(s, 2)) < 0) return -1; rep += 3; }
      else if (sym == 17) { if ((rep = inf_bits(s, 3)) < 0) return -1; rep += 3; } else { if ((rep = inf_bits(s, 7)) < 0) return -1; rep += 11; }
      if (i + rep > nlen + ndist) return -10; while (rep--) l[i++] = (short)len; } }
  if (inf_build(&lc, l, nlen) < 0 && (lc.count[0] + lc.count[1] != nlen)) return -11;
  if (inf_build(&dc, l + nlen, ndist) < 0 && (dc.count[0] + dc.count[1] != ndist)) return -12;
  return inf_codes(s, &lc, &dc); }
/* one raw deflate stream (until its final block) into s->out at s->outpos (earlier output = history) */
static int inf_stream(Inf_ *s) {
  int last, type, r;
  s->bitbuf = 0; s->bitcnt = 0;
  do { if ((last = inf_bits(s, 1)) < 0 || (type = inf_bits(s, 2)) < 0) return -1;
    r = type == 0 ? inf_stored(s) : type == 1 ? inf_fixed(s) : type == 2 ? inf_dynamic(s) : -13; if (r) return r; } while (!last);
  return 0; }
/* MSZIP data of one chunk: "CK" + deflate, repeated */
static int mszip_chunk(const BYTE *in, SIZE_T inlen, BYTE *out, SIZE_T outlen, SIZE_T *outpos) {
  Inf_ s; int r; memset(&s, 0, sizeof s); s.in = in; s.inlen = inlen; s.out = out; s.outlen = outlen; s.outpos = *outpos;
  while (s.inpos + 2 <= s.inlen && s.outpos < s.outlen) {
    if (s.in[s.inpos] != 'C' || s.in[s.inpos + 1] != 'K') return -20; s.inpos += 2;
    if ((r = inf_stream(&s)) != 0) return r;
    if (s.bitcnt >= 8) { s.inpos -= (SIZE_T)(s.bitcnt / 8); } s.bitcnt = 0; s.bitbuf = 0; }   /* the next CK block starts on a byte boundary */
  *outpos = s.outpos; return 0; }

#define CAB_DECOMP_MAGIC 0x55434450u
typedef struct { UINT magic; UINT alg; } CabDecomp_;
__declspec(dllexport) BOOL WINAPI CreateDecompressor(DWORD alg, void *routines, HANDLE *h) {
  CabDecomp_ *d; (void)routines;
  if (!h) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } *h = NULL;
  if ((alg & 0xFF) != 2 /* MSZIP */) { static LONG nl; if (InterlockedIncrement(&nl) <= 4) slog("[cab] CreateDecompressor algorithm %lu not supported (MSZIP only)", (unsigned long)alg); SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
  d = (CabDecomp_ *)HeapAlloc(GetProcessHeap(), 0, sizeof *d); if (!d) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
  d->magic = CAB_DECOMP_MAGIC; d->alg = alg; *h = d; return TRUE; }
__declspec(dllexport) BOOL WINAPI CloseDecompressor(HANDLE h) { CabDecomp_ *d = (CabDecomp_ *)h; if (d && d->magic == CAB_DECOMP_MAGIC) { d->magic = 0; HeapFree(GetProcessHeap(), 0, d); } return TRUE; }
__declspec(dllexport) BOOL WINAPI Decompress(HANDLE h, const void *inv, SIZE_T inlen, void *outv, SIZE_T outlen, SIZE_T *outdata) {
  const BYTE *in = (const BYTE *)inv; UINT64 total, chunk; SIZE_T pos = 0, ip; int r; static LONG nl; CabDecomp_ *d = (CabDecomp_ *)h;
  if (!d || d->magic != CAB_DECOMP_MAGIC || !in || inlen < 0x18) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
  if (*(const UINT *)in != 0xC0E5510Au || in[7] != 2) {   /* bytes 0A 51 E5 C0 */
    static LONG nh; if (InterlockedIncrement(&nh) <= 4) slog("[cab] Decompress: not an MSZIP buffer (%02x %02x %02x %02x, algorithm %u)", in[0], in[1], in[2], in[3], in[7]);
    SetLastError(ERROR_BAD_COMPRESSION_BUFFER); return FALSE; }
  total = *(const UINT64 *)(in + 8); chunk = *(const UINT64 *)(in + 16); ip = in[4] ? in[4] : 0x18;
  if (outdata) *outdata = (SIZE_T)total;
  if (!outv || outlen < total) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
  while (pos < total && ip + 4 <= inlen) {
    UINT csz = *(const UINT *)(in + ip); ip += 4; if (ip + csz > inlen) break;
    { SIZE_T end = pos + (SIZE_T)(chunk && chunk < total - pos ? chunk : total - pos);
      if ((r = mszip_chunk(in + ip, csz, (BYTE *)outv, end, &pos)) != 0) { if (InterlockedIncrement(&nl) <= 4) slog("[cab] MSZIP chunk failed (%d) at output %Iu of %llu", r, pos, (unsigned long long)total); SetLastError(ERROR_BAD_COMPRESSION_BUFFER); return FALSE; } }
    ip += csz; }
  if (pos != total) { if (InterlockedIncrement(&nl) <= 4) slog("[cab] MSZIP: %Iu of %llu bytes", pos, (unsigned long long)total); SetLastError(ERROR_BAD_COMPRESSION_BUFFER); return FALSE; }
  { static LONG ok; if (InterlockedIncrement(&ok) <= 2) slog("[cab] Decompress MSZIP %Iu -> %llu bytes (Compression API emulated)", inlen, (unsigned long long)total); }
  if (outdata) *outdata = pos; return TRUE; }

