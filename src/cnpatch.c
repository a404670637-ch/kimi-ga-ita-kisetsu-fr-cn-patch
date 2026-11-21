/* cnpatch.dll - runtime Chinese text patch for rUGP (君がいた季節FR)
 * Injected by injector.exe into rugp.exe (see setup notes).
 * Hooks Vm60.dll+0x79B55 (text pointer slot) and GDI32 GetGlyphOutlineA tunnel chars.
 * Data: cnpatch_txt.bin / cnpatch_uni.bin next to this DLL.
 */
#include <windows.h>
#include "namemap.h"
#include <stdio.h>
#include <stdlib.h>

static void join_path(char* out, const char* name);
static void colour_hint(const char* s);
static void colour_restore_now(void);

/* ---------------- logging / config ---------------- */
static HMODULE g_self = NULL;
static char g_dir[MAX_PATH] = {0};
static HANDLE g_log = INVALID_HANDLE_VALUE;
static int g_log_enabled = 0;
int g_enabled = 1;
static int g_hotkey = 0x77; /* VK_F8 */
static int g_dump_miss = 0;
static HANDLE g_miss = INVALID_HANDLE_VALUE;
/* message log: every distinct text drawn by the engine (original SJIS bytes) */
static HANDLE g_msg = INVALID_HANDLE_VALUE;
static DWORD g_msg_bytes = 0;
static char g_hist[8][1024];
static DWORD g_histlen[8];
static int g_histn = 0, g_histpos = 0;
static void msg_log(const char* s) {
    if (g_msg == INVALID_HANDLE_VALUE) return;
    if (g_msg_bytes > (6u * 1024u * 1024u)) return;
    DWORD len = (DWORD)lstrlenA(s);
    if (len == 0 || len >= 1024) return;
    for (int i = 0; i < g_histn; i++) {
        int k = (g_histpos - g_histn + i + 8) & 7;
        if (g_histlen[k] == len && !memcmp(g_hist[k], s, len)) return;
    }
    {
        DWORD w;
        WriteFile(g_msg, &len, 4, &w, NULL);
        WriteFile(g_msg, s, len, &w, NULL);
        g_msg_bytes += len + 4;
    }
    memcpy(g_hist[g_histpos], s, len);
    g_histlen[g_histpos] = len;
    g_histpos = (g_histpos + 1) & 7;
    if (g_histn < 8) g_histn++;
}
static int g_trace = 0;
static int g_fontdiag = 0;
static int g_storefix = 1; /* keep engine-side copies of the line in Japanese */
static int g_keepzh = 1;   /* leave the live display buffer in Chinese (backlog reads it) */
static int g_matchdiag = 0; /* log every lookup decision (lookup.log) */
static HANDLE g_mlog = INVALID_HANDLE_VALUE;
static char g_fontface[64] = {0}; /* when set, rewrite CreateFont*A face name */
static char g_fontfile[MAX_PATH] = {0}; /* when set, redirect font-file opens to this file */
static HANDLE g_flog = INVALID_HANDLE_VALUE;
static int g_fl_n = 0;
static unsigned short g_last_ch = 0;
static HANDLE g_tlog = INVALID_HANDLE_VALUE;
static int g_tn = 0;
typedef struct { void* ra; unsigned ch; } GRing;
static GRing g_ring[512];
static int g_rpos = 0, g_rcnt = 0;
static void dump_ring(void) {
    char p[MAX_PATH]; join_path(p, "glyphring.log");
    HANDLE h = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[128]; int n = wsprintfA(buf, "cnt=%d vm60=%p univ=%p rvmm=%p gmfc=%p exe=%p\r\n", g_rcnt,
        GetModuleHandleA("Vm60.dll"), GetModuleHandleA("UnivUI.dll"), GetModuleHandleA("rvmm.dll"), GetModuleHandleA("GMfc.dll"), GetModuleHandleA(NULL));
    DWORD w; WriteFile(h, buf, n, &w, NULL);
    for (int i = 0; i < g_rcnt; i++) {
        int k = (g_rpos - g_rcnt + i + 512) & 511;
        n = wsprintfA(buf, "ra=%08lX ch=%04X\r\n", (unsigned long)g_ring[k].ra, g_ring[k].ch);
        WriteFile(h, buf, n, &w, NULL);
    }
    CloseHandle(h);
}
static int g_hits = 0, g_replaced = 0, g_tunnel = 0;
static HANDLE g_sd = INVALID_HANDLE_VALUE;
static DWORD g_sd_pages[512];
static int g_sd_n = 0;
static int g_sd_on = 0;
static int g_fetch_log = 0, g_fetch_n = 0;
__attribute__((used)) unsigned char* g_fetch_tramp = NULL;
static DWORD g_stackbuf[4096];
static int g_stackcnt = 0;
static DWORD g_stackesp = 0;
static int g_stackdone = 0;
static __attribute__((noinline)) void dump_stack_now(void) {
    if (g_stackdone) return;
    g_stackdone = 1;
    DWORD sp;
    __asm__ __volatile__("movl %%esp, %0" : "=r"(sp));
    DWORD* spd = (DWORD*)sp;
    g_stackesp = sp;
    int cnt = 0;
    for (int i = 0; i < 4096; i++) {
        if ((i & 255) == 0) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(&spd[i], &mbi, sizeof(mbi)) == 0) break;
            if (mbi.State != MEM_COMMIT) break;
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) break;
        }
        g_stackbuf[i] = spd[i]; cnt = i + 1;
    }
    g_stackcnt = cnt;
    char p[MAX_PATH]; join_path(p, "stackdump.log");
    HANDLE h = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[64]; int n = wsprintfA(buf, "esp=%08lX\r\n", (unsigned long)g_stackesp);
    DWORD w; WriteFile(h, buf, n, &w, NULL);
    for (int i = 0; i < g_stackcnt; i++) {
        n = wsprintfA(buf, "%d %08lX\r\n", i, (unsigned long)g_stackbuf[i]);
        WriteFile(h, buf, n, &w, NULL);
    }
    CloseHandle(h);
}


static void join_path(char* out, const char* name);

static void logline(const char* s) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(g_log, s, (DWORD)lstrlenA(s), &w, NULL);
    WriteFile(g_log, "\r\n", 2, &w, NULL);
}
static void logmsg(const char* fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap); va_end(ap);
    logline(buf);
}

static void join_path(char* out, const char* name) {
    lstrcpynA(out, g_dir, MAX_PATH);
    int n = lstrlenA(out);
    if (n && out[n-1] != '\\') { out[n] = '\\'; out[n+1] = 0; }
    lstrcatA(out, name);
}

static void load_config(void) {
    char p[MAX_PATH]; join_path(p, "config.ini");
    FILE* f = fopen(p, "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char* k = line; char* v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        while (*v == ' ' || *v == '\t') v++;
        char* e = v + lstrlenA(v);
        while (e > v && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!_stricmp(k, "enabled")) g_enabled = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "log")) g_log_enabled = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "scriptdump")) g_sd_on = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "hotkey")) g_hotkey = (int)strtol(v, NULL, 0);
        else if (!_stricmp(k, "misslog")) g_dump_miss = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "fetchlog")) g_fetch_log = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "trace")) g_trace = atoi(v);
        else if (!_stricmp(k, "fontdiag")) g_fontdiag = atoi(v);
        else if (!_stricmp(k, "fontface")) lstrcpynA(g_fontface, v, sizeof(g_fontface));
        else if (!_stricmp(k, "fontfile")) lstrcpynA(g_fontfile, v, sizeof(g_fontfile));
        else if (!_stricmp(k, "storefix")) g_storefix = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "keepzh")) g_keepzh = atoi(v) ? 1 : 0;
        else if (!_stricmp(k, "matchdiag")) g_matchdiag = atoi(v) ? 1 : 0;
    }
    fclose(f);
}

/* ---------------- text table ---------------- */
static BYTE* g_txt = NULL;
static DWORD g_txtsz = 0;
static DWORD* g_idx = NULL;
static DWORD g_nrec = 0;
static BYTE* g_base = NULL;

static int load_txt(void) {
    char p[MAX_PATH]; join_path(p, "cnpatch_txt.bin");
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { logmsg("missing %s", p); return 0; }
    DWORD sz = GetFileSize(h, NULL);
    BYTE* b = (BYTE*)malloc(sz);
    DWORD got = 0; ReadFile(h, b, sz, &got, NULL); CloseHandle(h);
    if (got != sz || sz < 12) { free(b); return 0; }
    if (*(DWORD*)b != 0x58544E43) { free(b); return 0; } /* 'CNTX' */
    g_nrec = *(DWORD*)(b + 4);
    if (g_nrec > 1000000) { g_nrec = 0; free(b); return 0; }
    g_txt = b; g_txtsz = sz;
    g_idx = (DWORD*)(b + 8);
    g_base = b + 8 + 4 * g_nrec;
    logmsg("txt loaded: %lu records", (unsigned long)g_nrec);
    return 1;
}

/* matchdiag: record which line could not be matched, with its exact bytes. */
static void mdiag(const char* s, unsigned klen, int idx, int ok) {
    if (!g_matchdiag || g_mlog == INVALID_HANDLE_VALUE) return;
    static int n = 0;
    if (n > 4000) return;
    n++;
    DWORD len = (DWORD)lstrlenA(s);
    if (len > 900) return;
    char head[128];
    int h = wsprintfA(head, "M %d ok=%d idx=%d klen=%u len=%lu hex=", n, ok, idx, klen, (unsigned long)len);
    DWORD w;
    WriteFile(g_mlog, head, h, &w, NULL);
    for (DWORD i = 0; i < len && i < 900; i++) {
        char b2[3]; b2[0] = "0123456789abcdef"[(unsigned char)s[i] >> 4];
        b2[1] = "0123456789abcdef"[(unsigned char)s[i] & 15]; b2[2] = 0;
        WriteFile(g_mlog, b2, 2, &w, NULL);
    }
    WriteFile(g_mlog, "~~", 2, &w, NULL);
    WriteFile(g_mlog, s, len, &w, NULL);
    WriteFile(g_mlog, "\r\n", 2, &w, NULL);
}

static const BYTE* find_rec(const char* s) {
    if (!g_nrec) return NULL;
    int lo = 0, hi = (int)g_nrec - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const BYTE* rec = g_base + g_idx[mid];
        unsigned klen = *(unsigned short*)rec;
        const BYTE* key = rec + 2;
        int cmp = 0;
        for (unsigned i = 0; i < klen; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c == 0) { cmp = -1; break; }
            if (c != key[i]) { cmp = (c < key[i]) ? -1 : 1; break; }
        }
        if (cmp == 0 && s[klen] != 0) cmp = 1;
        if (cmp == 0) { mdiag(s, klen, (int)mid, 1); return rec; }
        if (cmp < 0) hi = mid - 1; else lo = mid + 1;
    }
    mdiag(s, 0, -1, 0);
    return NULL;
}

/* ---------------- tunnel glyph table ---------------- */
typedef struct { unsigned short code; unsigned int uni; } TEnt;
static TEnt* g_tmap = NULL;
static int g_ntmap = 0;

typedef DWORD (WINAPI *pGetGlyphOutlineW)(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);
static pGetGlyphOutlineW real_GetGlyphOutlineW = NULL;

static int load_uni(void) {
    char p[MAX_PATH]; join_path(p, "cnpatch_uni.bin");
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { logmsg("missing %s", p); return 0; }
    DWORD sz = GetFileSize(h, NULL);
    BYTE* b = (BYTE*)malloc(sz);
    DWORD got = 0; ReadFile(h, b, sz, &got, NULL); CloseHandle(h);
    if (got != sz || sz < 8 || *(DWORD*)b != 0x4E554E43) { free(b); return 0; } /* 'CNUN' */
    int m = (int)*(DWORD*)(b + 4);
    if (m < 0 || m > 200000) { free(b); return 0; }
    g_tmap = (TEnt*)malloc(sizeof(TEnt) * (m ? m : 1));
    for (int i = 0; i < m; i++) {
        g_tmap[i].code = *(unsigned short*)(b + 8 + i * 6);
        g_tmap[i].uni = *(unsigned int*)(b + 8 + i * 6 + 2);
    }
    g_ntmap = m; free(b);
    logmsg("uni loaded: %d", m);
    return 1;
}

static int find_uni(unsigned short code, unsigned int* out) {
    int lo = 0, hi = g_ntmap - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned short c = g_tmap[mid].code;
        if (c == code) { *out = g_tmap[mid].uni; return 1; }
        if (c < code) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* Reverse map: when the engine converts SJIS to Unicode before asking for a glyph
 * (the UI text pipeline does this), a tunnel code arrives as the Unicode char that
 * CP932 maps it to.  Build cp932(code)->unicode so such calls can be recognised and
 * redirected to the intended Chinese glyph. */
typedef struct { unsigned int u; unsigned int uni; } REnt;
static REnt* g_rmap = NULL;
static int g_nrmap = 0;
static int cmp_r(const void* a, const void* b) {
    unsigned x = ((const REnt*)a)->u, y = ((const REnt*)b)->u;
    return x < y ? -1 : (x > y ? 1 : 0);
}
static int build_rmap(void) {
    if (!g_ntmap) return 0;
    g_rmap = (REnt*)malloc(sizeof(REnt) * g_ntmap);
    if (!g_rmap) return 0;
    int n = 0;
    for (int i = 0; i < g_ntmap; i++) {
        char two[2]; two[0] = (char)(g_tmap[i].code >> 8); two[1] = (char)(g_tmap[i].code & 0xFF);
        WCHAR w = 0;
        if (MultiByteToWideChar(932, 0, two, 2, &w, 1) == 1 && w) { g_rmap[n].u = w; g_rmap[n].uni = g_tmap[i].uni; n++; }
    }
    qsort(g_rmap, n, sizeof(REnt), cmp_r);
    g_nrmap = n;
    return n;
}
static int find_rev(unsigned int u, unsigned int* out) {
    int lo = 0, hi = g_nrmap - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (g_rmap[mid].u == u) { *out = g_rmap[mid].uni; return 1; }
        if (g_rmap[mid].u < u) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* font cache keyed by height/weight */
static HFONT GetCnFont(HDC hdc) {
    static HFONT fnt[8]; static int fh[8], fw[8]; static int n = 0;
    HFONT cur = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
    LOGFONTA lf; memset(&lf, 0, sizeof(lf));
    if (cur) GetObjectA(cur, sizeof(lf), &lf);
    int h = lf.lfHeight, w = lf.lfWeight;
    for (int i = 0; i < n; i++) if (fh[i] == h && fw[i] == w) return fnt[i];
    LOGFONTA nf; memset(&nf, 0, sizeof(nf));
    nf.lfHeight = h; nf.lfWidth = lf.lfWidth;
    nf.lfWeight = w ? w : FW_NORMAL;
    nf.lfItalic = lf.lfItalic;
    nf.lfCharSet = DEFAULT_CHARSET; nf.lfOutPrecision = OUT_TT_PRECIS;
    nf.lfQuality = ANTIALIASED_QUALITY; nf.lfPitchAndFamily = DEFAULT_PITCH;
    lstrcpyA(nf.lfFaceName, "SimSun");
    HFONT f = CreateFontIndirectA(&nf);
    if (f && n < 8) { fnt[n] = f; fh[n] = h; fw[n] = w; n++; }
    return f;
}

/* ---------------- GDI IAT hooks ---------------- */
typedef DWORD (WINAPI *pGetGlyphOutlineA)(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);
static pGetGlyphOutlineA real_glyph = NULL;

static HANDLE g_glyphlog = INVALID_HANDLE_VALUE;
static int g_gl_n = 0;
static void gly_log(const char* api, unsigned code, void* ra) {
    if (g_gl_n >= (g_trace == 3 ? 20000 : 800)) return;
    if (g_glyphlog == INVALID_HANDLE_VALUE) {
        char p[MAX_PATH]; join_path(p, "glyphlog.txt");
        g_glyphlog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (g_glyphlog == INVALID_HANDLE_VALUE) return;
    char buf[192];
    int n = wsprintfA(buf, "%s code=%04X ra=%08lX\r\n", api, code, (unsigned long)(ULONG_PTR)ra);
    DWORD w; WriteFile(g_glyphlog, buf, n, &w, NULL);
    g_gl_n++;
}
static DWORD WINAPI my_GetGlyphOutlineW(HDC hdc, UINT ch, UINT fmt, LPGLYPHMETRICS gm, DWORD cb, LPVOID buf, const MAT2* mat) {
    if (g_trace == 3) gly_log("Wall", (unsigned short)ch, __builtin_return_address(0));
    unsigned int uni;
    int hit = 0;
    if (g_enabled && g_ntmap) {
        if (find_uni((unsigned short)ch, &uni)) { hit = 1; gly_log("W", (unsigned short)ch, __builtin_return_address(0)); }
        else if (g_nrmap && find_rev(ch, &uni)) { hit = 2; gly_log("RW", (unsigned short)ch, __builtin_return_address(0)); }
    }
    if (hit) {
        g_tunnel++;
        HFONT cn = GetCnFont(hdc);
        HFONT old = cn ? (HFONT)SelectObject(hdc, cn) : NULL;
        DWORD r = real_GetGlyphOutlineW ? real_GetGlyphOutlineW(hdc, uni, fmt, gm, cb, buf, mat) : 0;
        if (old) SelectObject(hdc, old);
        return r;
    }
    return real_GetGlyphOutlineW ? real_GetGlyphOutlineW(hdc, ch, fmt, gm, cb, buf, mat) : 0;
}
static DWORD WINAPI my_GetGlyphOutlineA(HDC hdc, UINT ch, UINT fmt, LPGLYPHMETRICS gm, DWORD cb, LPVOID buf, const MAT2* mat) {
    if (g_trace == 3) gly_log("Aall", (unsigned short)ch, __builtin_return_address(0));
    if (g_trace == 2) {
        if (!g_stackdone) dump_stack_now();
        void* ra = __builtin_return_address(0);
        g_ring[g_rpos].ra = ra; g_ring[g_rpos].ch = ch;
        g_rpos = (g_rpos + 1) & 511; if (g_rcnt < 512) g_rcnt++;
    }
    unsigned int uni;
    int hit = 0;
    if (g_enabled && g_ntmap) {
        /* The A entry point is handed raw SJIS codes.  g_rmap is keyed by the Unicode
         * value of cp932(tunnel_code), so using it here is a type confusion: any native
         * character whose SJIS code happens to equal such a Unicode value was drawn as
         * the tunnel's Chinese glyph (that was the 【６りか】/ま→６ bug, and 159 such
         * pairings still exist for text outside the translated corpus).  Only the
         * forward map is meaningful for raw codes; the Unicode-keyed map stays for the
         * W entry point below.  (v1.20-dev, needs live A/B verification.) */
        if (find_uni((unsigned short)ch, &uni)) { hit = 1; gly_log("A", (unsigned short)ch, __builtin_return_address(0)); }
    }
    if (hit) {
        g_tunnel++;
        HFONT cn = GetCnFont(hdc);
        HFONT old = cn ? (HFONT)SelectObject(hdc, cn) : NULL;
        DWORD r = real_GetGlyphOutlineW ? real_GetGlyphOutlineW(hdc, uni, fmt, gm, cb, buf, mat) : 0;
        if (old) SelectObject(hdc, old);
        return r;
    }
    return real_glyph ? real_glyph(hdc, ch, fmt, gm, cb, buf, mat) : 0;
}

static int patch_iat(HMODULE mod, const char* dllname, const char* func, void* hookfn, void** orig) {
    if (!mod) return 0;
    BYTE* base = (BYTE*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    DWORD impRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!impRva) return 0;
    IMAGE_IMPORT_DESCRIPTOR* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + impRva);
    int patched = 0;
    for (; imp->Name; imp++) {
        const char* dll = (const char*)(base + imp->Name);
        if (_stricmp(dll, dllname) != 0) continue;
        IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA* iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; thunk->u1.Function; thunk++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + thunk->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, func) != 0) continue;
            if (*orig == NULL) *orig = (void*)iat->u1.Function;
            DWORD old;
            VirtualProtect(iat, sizeof(IMAGE_THUNK_DATA), PAGE_READWRITE, &old);
            iat->u1.Function = (ULONG_PTR)hookfn;
            VirtualProtect(iat, sizeof(IMAGE_THUNK_DATA), old, &old);
            patched++;
        }
    }
    return patched;
}


/* ---------------- GDI text: trace + tunnel -> Unicode substitution ----------------
 * The save/load screen (and other engine UI drawn by rvmm.dll) paints its text with
 * plain Win32 text APIs (DrawTextA / ExtTextOutA).  Those convert the SJIS bytes with
 * CP932 internally, so a tunnel-encoded Chinese string comes out as unrelated kanji.
 * The hooks below decode such strings ourselves, replacing tunnel codes with the real
 * Unicode characters, and hand the result to the wide API.
 */
static __attribute__((noinline)) int decode_tunnel(const char* s, int n, WCHAR* out, int cap, int* has) {
    int i = 0, o = 0;
    if (n < 0) n = (int)lstrlenA(s);
    while (i < n && o < cap - 1) {
        unsigned char b = (unsigned char)s[i];
        if (b < 0x80) { out[o++] = (WCHAR)b; i++; continue; }
        if (b >= 0xA1 && b <= 0xDF) { out[o++] = (WCHAR)(0xFF61 + (b - 0xA1)); i++; continue; }
        if (i + 1 < n) {
            unsigned short code = (unsigned short)(((unsigned)b << 8) | (unsigned char)s[i + 1]);
            unsigned int uni;
            if (g_ntmap && find_uni(code, &uni)) {
                out[o++] = (WCHAR)uni; i += 2; if (has) *has = 1; continue;
            }
            {
                char two[2]; two[0] = s[i]; two[1] = s[i + 1];
                WCHAR w = 0;
                int r = MultiByteToWideChar(932, 0, two, 2, &w, 1);
                out[o++] = r ? w : (WCHAR)0xFFFD;
                i += 2; continue;
            }
        }
        i++;
    }
    out[o] = 0;
    return o;
}
static WCHAR g_wbuf[4096];

/* ---------------- GDI text trace (diagnostic, trace=1) ---------------- */
typedef BOOL (WINAPI *pTextOutA)(HDC,int,int,LPCSTR,int);
typedef BOOL (WINAPI *pTextOutW)(HDC,int,int,LPCWSTR,int);
typedef BOOL (WINAPI *pExtTextOutA)(HDC,int,int,UINT,const RECT*,LPCSTR,UINT,const INT*);
typedef BOOL (WINAPI *pExtTextOutW)(HDC,int,int,UINT,const RECT*,LPCWSTR,UINT,const INT*);
static pTextOutA real_TextOutA = NULL;
static pTextOutW real_TextOutW = NULL;
static pExtTextOutA real_ExtTextOutA = NULL;
static pExtTextOutW real_ExtTextOutW = NULL;
typedef int (WINAPI *pDrawTextA)(HDC,LPCSTR,int,LPRECT,UINT);
typedef int (WINAPI *pDrawTextW)(HDC,LPCWSTR,int,LPRECT,UINT);
typedef int (WINAPI *pDrawTextExA)(HDC,LPSTR,int,LPRECT,UINT,LPDRAWTEXTPARAMS);
typedef int (WINAPI *pDrawTextExW)(HDC,LPWSTR,int,LPRECT,UINT,LPDRAWTEXTPARAMS);
static pDrawTextA real_DrawTextA = NULL;
static pDrawTextW real_DrawTextW = NULL;
static pDrawTextExA real_DrawTextExA = NULL;
static pDrawTextExW real_DrawTextExW = NULL;

static void trace_log(const char* fn, const char* s, int len) {
    if (g_tlog == INVALID_HANDLE_VALUE || g_tn >= 4000) return;
    if (!s || len <= 0) return;
    g_tn++;
    char hdr[128]; int n = wsprintfA(hdr, "[%d] %s len=%d: ", g_tn, fn, len);
    DWORD w; WriteFile(g_tlog, hdr, n, &w, NULL);
    int k = len > 1800 ? 1800 : len;
    WriteFile(g_tlog, s, k, &w, NULL);
    WriteFile(g_tlog, "\r\n", 2, &w, NULL);
}
static void trace_log_w(const char* fn, LPCWSTR s, int len) {
    if (g_tlog == INVALID_HANDLE_VALUE || g_tn >= 4000) return;
    if (!s || len <= 0) return;
    char buf[2048];
    int n = WideCharToMultiByte(932, 0, s, len, buf, sizeof(buf)-1, NULL, NULL);
    if (n > 0) { buf[n] = 0; trace_log(fn, buf, n); }
}
static BOOL WINAPI my_TextOutA(HDC h, int x, int y, LPCSTR s, int len) {
    if (g_trace) trace_log("TextOutA", s, len);
    return real_TextOutA ? real_TextOutA(h, x, y, s, len) : 0;
}
static BOOL WINAPI my_TextOutW(HDC h, int x, int y, LPCWSTR s, int len) {
    if (g_trace) trace_log_w("TextOutW", s, len);
    return real_TextOutW ? real_TextOutW(h, x, y, s, len) : 0;
}
static BOOL WINAPI my_ExtTextOutA(HDC h, int x, int y, UINT opt, const RECT* r, LPCSTR s, UINT len, const INT* dx) {
    if (g_trace) trace_log("ExtTextOutA", s, (int)len);
    if (g_enabled && g_ntmap && s && len) {
        int has = 0;
        int n = decode_tunnel(s, (int)len, g_wbuf, 4095, &has);
        if (has) {
            if (!real_ExtTextOutW) real_ExtTextOutW = (pExtTextOutW)GetProcAddress(GetModuleHandleA("gdi32.dll"), "ExtTextOutW");
            if (real_ExtTextOutW) return real_ExtTextOutW(h, x, y, opt, r, g_wbuf, (UINT)n, NULL);
        }
    }
    return real_ExtTextOutA ? real_ExtTextOutA(h, x, y, opt, r, s, len, dx) : 0;
}
static BOOL WINAPI my_ExtTextOutW(HDC h, int x, int y, UINT opt, const RECT* r, LPCWSTR s, UINT len, const INT* dx) {
    if (g_trace) trace_log_w("ExtTextOutW", s, (int)len);
    return real_ExtTextOutW ? real_ExtTextOutW(h, x, y, opt, r, s, len, dx) : 0;
}
static int WINAPI my_DrawTextA(HDC h, LPCSTR s, int n, LPRECT r, UINT fmt) {
    if (g_trace) trace_log("DrawTextA", s, n < 0 ? (int)(s ? lstrlenA(s) : 0) : n);
    if (g_enabled && g_ntmap && s && n != 0) {
        int has = 0;
        int len = decode_tunnel(s, n, g_wbuf, 4095, &has);
        if (has) {
            if (!real_DrawTextW) real_DrawTextW = (pDrawTextW)GetProcAddress(GetModuleHandleA("user32.dll"), "DrawTextW");
            if (real_DrawTextW) return real_DrawTextW(h, g_wbuf, len, r, fmt);
        }
    }
    return real_DrawTextA ? real_DrawTextA(h, s, n, r, fmt) : 0;
}
static int WINAPI my_DrawTextExA(HDC h, LPSTR s, int n, LPRECT r, UINT fmt, LPDRAWTEXTPARAMS p) {
    if (g_trace) trace_log("DrawTextExA", s, n < 0 ? (int)(s ? lstrlenA(s) : 0) : n);
    if (g_enabled && g_ntmap && s && n != 0) {
        int has = 0;
        int len = decode_tunnel(s, n, g_wbuf, 4095, &has);
        if (has) {
            if (!real_DrawTextExW) real_DrawTextExW = (pDrawTextExW)GetProcAddress(GetModuleHandleA("user32.dll"), "DrawTextExW");
            if (real_DrawTextExW) return real_DrawTextExW(h, g_wbuf, len, r, fmt, p);
        }
    }
    return real_DrawTextExA ? real_DrawTextExA(h, s, n, r, fmt, p) : 0;
}
/* ---- diagnostic: log font creation (fontdiag=1) ---- */
typedef HFONT (WINAPI *pCreateFontIndirectA)(const LOGFONTA*);
typedef HFONT (WINAPI *pCreateFontA)(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR);
static pCreateFontIndirectA real_cfia = NULL;
static pCreateFontA real_cfa = NULL;
static void fontlog_cf(const LOGFONTA* lf) {
    if (g_flog == INVALID_HANDLE_VALUE) {
        char p[MAX_PATH]; join_path(p, "fontlog.txt");
        g_flog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (g_flog == INVALID_HANDLE_VALUE) return;
    char buf[160];
    int n = wsprintfA(buf, "CF h=%d w=%d cs=%d face=%s\r\n", lf->lfHeight, lf->lfWeight, (int)lf->lfCharSet, lf->lfFaceName);
    DWORD w; WriteFile(g_flog, buf, n, &w, NULL);
}
static HFONT WINAPI my_CreateFontIndirectA(const LOGFONTA* lf) {
    if (g_fontdiag && lf) fontlog_cf(lf);
    if (g_fontface[0] && lf) {
        LOGFONTA cp = *lf;
        lstrcpynA(cp.lfFaceName, g_fontface, LF_FACESIZE);
        HFONT f = real_cfia ? real_cfia(&cp) : NULL;
        if (g_fontdiag) {
            if (g_flog == INVALID_HANDLE_VALUE) {
                char p[MAX_PATH]; join_path(p, "fontlog.txt");
                g_flog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            }
            char b[160]; int n2 = wsprintfA(b, "CFR h=%d %s -> %s = %p\r\n", lf->lfHeight, lf->lfFaceName, cp.lfFaceName, f);
            DWORD w2; if (g_flog != INVALID_HANDLE_VALUE) WriteFile(g_flog, b, n2, &w2, NULL);
        }
        return f;
    }
    return real_cfia ? real_cfia(lf) : NULL;
}
static HFONT WINAPI my_CreateFontA(int h,int w,int e,int o,int wt,DWORD it,DWORD un,DWORD so,DWORD cs,DWORD op,DWORD cp,DWORD q,DWORD pf,LPCSTR face) {
    if (g_fontdiag && face) {
        LOGFONTA lf; memset(&lf,0,sizeof(lf)); lf.lfHeight=h; lf.lfWeight=wt; lf.lfCharSet=(BYTE)cs;
        lstrcpynA(lf.lfFaceName, face, LF_FACESIZE);
        fontlog_cf(&lf);
    }
    if (g_fontface[0]) return real_cfa ? real_cfa(h,w,e,o,wt,it,un,so,cs,op,cp,q,pf,g_fontface) : NULL;
    return real_cfa ? real_cfa(h,w,e,o,wt,it,un,so,cs,op,cp,q,pf,face) : NULL;
}
static DWORD WINAPI eot_dump_thread(LPVOID p) {
    HANDLE h = (HANDLE)p;
    Sleep(2500);
    DWORD sz = GetFileSize(h, NULL);
    if (sz && sz < (16u << 20)) {
        BYTE* buf = (BYTE*)malloc(sz);
        if (buf) {
            DWORD got = 0;
            SetFilePointer(h, 0, NULL, FILE_BEGIN);
            if (ReadFile(h, buf, sz, &got, NULL) && got > 0) {
                char p2[MAX_PATH]; join_path(p2, "eot_dump.eot");
                HANDLE hw = CreateFileA(p2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hw != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(hw, buf, got, &wr, NULL); CloseHandle(hw); }
                logmsg("eot dumped: %lu bytes (size field %lu)", (unsigned long)got, (unsigned long)sz);
            }
            free(buf);
        }
    } else {
        logmsg("eot dump: size=%lu", (unsigned long)sz);
    }
    CloseHandle(h);
    return 0;
}
/* ---- global (export-level) CreateFileA/W hooks: find font file loads ---- */
typedef HANDLE (WINAPI *pCFA)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef HANDLE (WINAPI *pCFW)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static pCFA g_cfa_tr = NULL;
static pCFW g_cfw_tr = NULL;
static int looks_like_font(const char* s) {
    if (!s) return 0;
    char low[260]; int n = 0;
    for (; s[n] && n < 255; n++) { char c = s[n]; low[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    low[n] = 0;
    return strstr(low, ".ttf") || strstr(low, ".ttc") || strstr(low, ".otf") || strstr(low, ".eot") || strstr(low, ".fnt") || strstr(low, "\\fonts\\");
}
static void log_open(const char* name, DWORD da, DWORD cd, HANDLE h) {
    if (!g_fontdiag || !g_flog || g_flog == INVALID_HANDLE_VALUE) return;
    char b[420]; int n2 = wsprintfA(b, "GF %08X disp=%d -> %p : %s\r\n", (unsigned)da, (int)cd, h, name);
    DWORD w2; WriteFile(g_flog, b, n2, &w2, NULL);
}
static HANDLE WINAPI my_cfa_export(LPCSTR name, DWORD da, DWORD sm, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fa, HANDLE ht) {
    LPCSTR use = name;
    if (g_fontfile[0] && looks_like_font(name)) {
        int n = lstrlenA(name);
        if (n > 4 && (_stricmp(name + n - 4, ".ttf") == 0 || _stricmp(name + n - 4, ".ttc") == 0)) use = g_fontfile;
    }
    HANDLE h = g_cfa_tr ? g_cfa_tr(use, da, sm, sa, cd, fa, ht) : INVALID_HANDLE_VALUE;
    if (looks_like_font(name)) log_open(name, da, cd, h);
    return h;
}
static HANDLE WINAPI my_cfw_export(LPCWSTR name, DWORD da, DWORD sm, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fa, HANDLE ht) {
    LPCWSTR use = name;
    WCHAR wrep[MAX_PATH];
    if (g_fontfile[0] && name) {
        char ansi[300]; int n2 = WideCharToMultiByte(932, 0, name, -1, ansi, 299, NULL, NULL);
        if (n2 > 0 && looks_like_font(ansi)) {
            int n = lstrlenA(ansi);
            if (n > 4 && (_stricmp(ansi + n - 4, ".ttf") == 0 || _stricmp(ansi + n - 4, ".ttc") == 0)) {
                MultiByteToWideChar(932, 0, g_fontfile, -1, wrep, MAX_PATH);
                use = wrep;
            }
        }
    }
    HANDLE h = g_cfw_tr ? g_cfw_tr(use, da, sm, sa, cd, fa, ht) : INVALID_HANDLE_VALUE;
    if (g_fontdiag && name) {
        char ansi[300]; int n2 = WideCharToMultiByte(932, 0, name, -1, ansi, 299, NULL, NULL);
        if (n2 > 0 && looks_like_font(ansi)) log_open(ansi, da, cd, h);
    }
    return h;
}
static int hook_export5(BYTE* p, void* hookfn, void** tramp) {
    if (!p) return 0;
    if (!(p[0] == 0x8B && p[1] == 0xFF && p[2] == 0x55 && p[3] == 0x8B && p[4] == 0xEC)) {
        logmsg("export hook: unexpected prologue %02X %02X %02X at %p", p[0], p[1], p[2], p);
        return 0;
    }
    BYTE* tr = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return 0;
    memcpy(tr, p, 5);
    tr[5] = 0xE9;
    *(DWORD*)(tr + 6) = (DWORD)(p + 5) - (DWORD)(tr + 10);
    *tramp = tr;
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)(ULONG_PTR)hookfn - (DWORD)(ULONG_PTR)p - 5;
    VirtualProtect(p, 5, old, &old);
    logmsg("export hook at %p tramp %p", p, tr);
    return 1;
}
static BYTE* follow_thunk(BYTE* p) {
    if (p && p[0] == 0xFF && p[1] == 0x25) {
        DWORD slot = *(DWORD*)(p + 2);
        BYTE** pp = (BYTE**)slot;
        if (pp && *pp) return *pp;
    }
    return p;
}
static void install_createfile_hooks(void) {
    HMODULE k = GetModuleHandleA("kernel32.dll");
    if (!k) return;
    BYTE* pa = follow_thunk((BYTE*)GetProcAddress(k, "CreateFileA"));
    BYTE* pw = follow_thunk((BYTE*)GetProcAddress(k, "CreateFileW"));
    hook_export5(pa, (void*)my_cfa_export, (void**)&g_cfa_tr);
    hook_export5(pw, (void*)my_cfw_export, (void**)&g_cfw_tr);
}
typedef BOOL (WINAPI *pDeleteFileA)(LPCSTR);
static pDeleteFileA real_delfile = NULL;
static BOOL WINAPI my_DeleteFileA(LPCSTR name) {
    if (g_fontdiag && name) {
        if (g_flog == INVALID_HANDLE_VALUE) {
            char p[MAX_PATH]; join_path(p, "fontlog.txt");
            g_flog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (g_flog != INVALID_HANDLE_VALUE) {
            char b[300]; int n2 = wsprintfA(b, "DEL %s\r\n", name);
            DWORD w2; WriteFile(g_flog, b, n2, &w2, NULL);
        }
        int len = lstrlenA(name);
        if (len > 4 && _stricmp(name + len - 4, ".eot") == 0) { logmsg("eot kept: %s", name); return TRUE; }
    }
    return real_delfile ? real_delfile(name) : FALSE;
}
typedef HANDLE (WINAPI *pCreateFileA2)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static pCreateFileA2 real_cfilea = NULL;
static HANDLE WINAPI my_CreateFileA2(LPCSTR name, DWORD da, DWORD sm, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fa, HANDLE ht) {
    if (g_fontdiag && name) {
        if (g_flog == INVALID_HANDLE_VALUE) {
            char p[MAX_PATH]; join_path(p, "fontlog.txt");
            g_flog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        if (g_flog != INVALID_HANDLE_VALUE) {
            char b[300]; int n2 = wsprintfA(b, "FOPEN %s\r\n", name);
            DWORD w2; WriteFile(g_flog, b, n2, &w2, NULL);
        }
    }
    {
        HANDLE h = real_cfilea ? real_cfilea(name, da, sm, sa, cd, fa, ht) : INVALID_HANDLE_VALUE;
        if (g_fontdiag && name && g_flog != INVALID_HANDLE_VALUE) {
            char b[340]; int n2 = wsprintfA(b, "FOPEN2 da=%08X disp=%d share=%d -> %p err=%lu %s\r\n",
                (unsigned)da, (int)cd, (int)sm, h, (unsigned long)GetLastError(), name);
            DWORD w2; WriteFile(g_flog, b, n2, &w2, NULL);
        }
        /* if the engine opens an .eot, grab a private copy before it can disappear */
        if (name && _stricmp(name + (lstrlenA(name) > 4 ? lstrlenA(name) - 4 : 0), ".eot") == 0) {
            HANDLE hr = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hr != INVALID_HANDLE_VALUE) {
                CreateThread(NULL, 0, eot_dump_thread, (LPVOID)hr, 0, NULL);
            }
            if (0) {
                DWORD sz = GetFileSize(hr, NULL), got = 0;
                if (sz && sz < (8u << 20)) {
                    BYTE* buf = (BYTE*)malloc(sz);
                    if (buf && ReadFile(hr, buf, sz, &got, NULL) && got == sz) {
                        char p2[MAX_PATH]; join_path(p2, "eot_dump.eot");
                        HANDLE hw = CreateFileA(p2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hw != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(hw, buf, sz, &wr, NULL); CloseHandle(hw); }
                        logmsg("eot captured: %lu bytes", (unsigned long)sz);
                    }
                    free(buf);
                }
                CloseHandle(hr);
            }
        }
        return h;
    }
}
static int patch_iat_fonts(HMODULE m) {
    int n = 0;
    if (!m) return 0;
    if (g_fontdiag) {
        n += patch_iat(m, "KERNEL32.dll", "CreateFileA", (void*)my_CreateFileA2, (void**)&real_cfilea);
        n += patch_iat(m, "kernel32.dll", "CreateFileA", (void*)my_CreateFileA2, (void**)&real_cfilea);
        n += patch_iat(m, "KERNEL32.dll", "DeleteFileA", (void*)my_DeleteFileA, (void**)&real_delfile);
        n += patch_iat(m, "kernel32.dll", "DeleteFileA", (void*)my_DeleteFileA, (void**)&real_delfile);
    }
    n += patch_iat(m, "GDI32.dll", "CreateFontIndirectA", (void*)my_CreateFontIndirectA, (void**)&real_cfia);
    n += patch_iat(m, "gdi32.dll", "CreateFontIndirectA", (void*)my_CreateFontIndirectA, (void**)&real_cfia);
    n += patch_iat(m, "GDI32.dll", "CreateFontA", (void*)my_CreateFontA, (void**)&real_cfa);
    n += patch_iat(m, "gdi32.dll", "CreateFontA", (void*)my_CreateFontA, (void**)&real_cfa);
    return n;
}
static int patch_iat_textout(HMODULE m) {
    int n = 0;
    if (!m) return 0;
    n += patch_iat(m, "GDI32.dll", "TextOutA", (void*)my_TextOutA, (void**)&real_TextOutA);
    n += patch_iat(m, "gdi32.dll", "TextOutA", (void*)my_TextOutA, (void**)&real_TextOutA);
    n += patch_iat(m, "GDI32.dll", "TextOutW", (void*)my_TextOutW, (void**)&real_TextOutW);
    n += patch_iat(m, "gdi32.dll", "TextOutW", (void*)my_TextOutW, (void**)&real_TextOutW);
    n += patch_iat(m, "GDI32.dll", "ExtTextOutA", (void*)my_ExtTextOutA, (void**)&real_ExtTextOutA);
    n += patch_iat(m, "gdi32.dll", "ExtTextOutA", (void*)my_ExtTextOutA, (void**)&real_ExtTextOutA);
    n += patch_iat(m, "GDI32.dll", "ExtTextOutW", (void*)my_ExtTextOutW, (void**)&real_ExtTextOutW);
    n += patch_iat(m, "gdi32.dll", "ExtTextOutW", (void*)my_ExtTextOutW, (void**)&real_ExtTextOutW);
    n += patch_iat(m, "USER32.dll", "DrawTextA", (void*)my_DrawTextA, (void**)&real_DrawTextA);
    n += patch_iat(m, "user32.dll", "DrawTextA", (void*)my_DrawTextA, (void**)&real_DrawTextA);
    n += patch_iat(m, "USER32.dll", "DrawTextExA", (void*)my_DrawTextExA, (void**)&real_DrawTextExA);
    n += patch_iat(m, "user32.dll", "DrawTextExA", (void*)my_DrawTextExA, (void**)&real_DrawTextExA);
    return n;
}
static void install_textout_hooks(void) {
    if (g_trace) {
        char p[MAX_PATH]; join_path(p, "trace.log");
        g_tlog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    int total = patch_iat_textout(GetModuleHandleA(NULL));
    const char* mods[] = {"Vm60.dll", "UnivUI.dll", "rvmm.dll", "GMfc.dll", "MFC42.DLL", "MSVCP60.dll"};
    for (int i = 0; i < 6; i++) { total += patch_iat_textout(GetModuleHandleA(mods[i])); total += patch_iat_fonts(GetModuleHandleA(mods[i])); }
    total += patch_iat_fonts(GetModuleHandleA(NULL));
    logmsg("textout hooks: %d", total);
}

/* ---------------- text capture hook ---------------- */
volatile DWORD hook_ret = 0;
static char* g_origptr = NULL;
static char g_orig[65536];
static char g_zhbuf[65536];
static DWORD g_origlen = 0;
static int g_restore_active = 0;
extern void hook_entry(void);
extern void text_thunk(void);
void* g_method = NULL;
static void** g_vt = NULL;
static int g_vt_patched = 0;
static int g_ctx_n = 0;

void note_ctx(void* thisp, void* fn) {
    if (g_ctx_n < 12) { logmsg("ctx[%d] this=%p fn=%p", g_ctx_n, thisp, fn); g_ctx_n++; }
    if (g_vt_patched || !fn || !thisp) return;
    g_method = fn;
    g_vt = *(void***)thisp;
    DWORD old;
    VirtualProtect(&g_vt[0x1A8/4], sizeof(void*), PAGE_READWRITE, &old);
    g_vt[0x1A8/4] = (void*)text_thunk;
    VirtualProtect(&g_vt[0x1A8/4], sizeof(void*), old, &old);
    g_vt_patched = 1;
    logmsg("vtable patched: this=%p fn=%p vt=%p", thisp, fn, g_vt);
}

static HWND g_hwnd = NULL;
static WNDPROC g_oldproc = NULL;

static LRESULT CALLBACK cn_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && g_hotkey && (int)w == g_hotkey) {
        g_enabled = !g_enabled;
        logmsg("toggle (wndproc) enabled=%d", g_enabled);
        return 0;
    }
    if (g_oldproc) return CallWindowProcA(g_oldproc, h, m, w, l);
    return DefWindowProcA(h, m, w, l);
}

static int game_foreground(void) {
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

#define CN_HOTKEY_ID 0x434E
static DWORD WINAPI hotkey_thread(LPVOID p) {
    (void)p;
    logmsg("hotkey thread started (vk=0x%02X)", g_hotkey);
    int use_reg = 0;
    if (g_hotkey) {
        use_reg = RegisterHotKey(NULL, CN_HOTKEY_ID, MOD_NOREPEAT, (UINT)g_hotkey) ? 1 : 0;
        logmsg("RegisterHotKey vk=0x%02X -> %d", g_hotkey, use_reg);
    }
    if (g_trace == 2) { RegisterHotKey(NULL, 0x4350, MOD_NOREPEAT, VK_F9); logmsg("F9 dump hotkey registered"); }
    int prev = 0;
    for (;;) {
        if (!IsWindow(g_hwnd)) {
            HWND h = FindWindowA("relic UGP Frame Class", NULL);
            if (h) {
                g_oldproc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)cn_wndproc);
                if (g_oldproc) { g_hwnd = h; logmsg("wndproc hooked hwnd=%p", h); }
            }
        }
        if (use_reg) {
            MSG msg;
            while (PeekMessageA(&msg, NULL, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
                if ((int)msg.wParam == 0x4350) { dump_ring(); logmsg("ring dumped"); }
                else if ((int)msg.wParam == CN_HOTKEY_ID && game_foreground()) {
                    g_enabled = !g_enabled;
                    logmsg("toggle (hotkey) enabled=%d", g_enabled);
                }
            }
        } else if (g_hotkey) {
            int st = (GetAsyncKeyState((int)g_hotkey) & 0x8000) ? 1 : 0;
            if (st && !prev && game_foreground()) { g_enabled = !g_enabled; logmsg("toggle (async) enabled=%d", g_enabled); }
            prev = st;
        }
        Sleep(15);
    }
    return 0;
}

static void miss_log(const char* s, DWORD len) {
    if (g_miss == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(g_miss, &len, 4, &w, NULL);
    WriteFile(g_miss, s, len, &w, NULL);
}

static void script_dump(const char* s) {
    if (g_sd == INVALID_HANDLE_VALUE) return;
    DWORD base = ((DWORD)(ULONG_PTR)s) & 0xFFFF0000u;
    for (int i = 0; i < g_sd_n; i++) if (g_sd_pages[i] == base) return;
    if (g_sd_n >= 512) return;
    unsigned char* buf = (unsigned char*)VirtualAlloc(NULL, 0x8000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!buf) return;
    SIZE_T got = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (void*)base, buf, 0x8000, &got) && got >= 0x1000) {
        DWORD hdr[2] = { base, (DWORD)got };
        DWORD w;
        WriteFile(g_sd, hdr, 8, &w, NULL);
        WriteFile(g_sd, buf, (DWORD)got, &w, NULL);
        g_sd_pages[g_sd_n++] = base;
    }
    VirtualFree(buf, 0, MEM_RELEASE);
}

/* ---- store-fix: keep the engine's own copy of the line in Japanese ----
 * The 0x79B55 hook rewrites the engine's text buffer in place; the engine then
 * copies that (Chinese) text into its own buffer, which is what the save-file
 * comment is later taken from.  Right after the draw we put the Japanese back
 * into those copies, so a save made at this point stores readable Japanese
 * instead of tunnel-encoded Chinese that the UI font renders as garbage.
 */
static char g_pat[4096];
static DWORD g_patlen = 0;
static DWORD g_zhlen = 0;
static int g_sfix_count = 0;

static DWORD sjis_fit(DWORD n) {
    while (n > 0 && n < g_origlen) {
        unsigned char c = (unsigned char)g_orig[n - 1];
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) { n--; continue; }
        break;
    }
    return n;
}
static void patch_copy_hit(unsigned char* p) {
    if (!g_patlen) return;
    /* keepzh: the live display buffer must stay Chinese so the backlog and every
     * other reader of that buffer show the translation; only the engine's own
     * copies (the ones a save comment is built from) are reverted to Japanese. */
    if (g_keepzh && p == (unsigned char*)g_origptr) return;
    /* The engine keeps a per-line display node: the node stores a pointer to its
     * own text at node_text-0x10 (== node_text+0x18).  Those nodes are what the
     * backlog renders, so they must keep the Chinese text as well.  A save
     * comment is built from a counted string ([u16 len][text]) instead. */
    if (g_keepzh && *(unsigned int*)(p - 0x10) == (unsigned int)(ULONG_PTR)(p + 0x18)) return;
    DWORD* len32 = (DWORD*)(p - 8);
    DWORD cap = *(DWORD*)(p - 4);
    unsigned short len16 = *(unsigned short*)(p - 2);
    if (*len32 == g_zhlen && cap >= g_origlen + 1 && cap < 0x100000) {
        memcpy(p, g_orig, g_origlen);
        p[g_origlen] = 0;
        *len32 = g_origlen;
        g_sfix_count++;
    } else if (len16 == g_zhlen) {
        DWORD n = g_patlen;
        if (g_origlen < n) n = g_origlen;
        n = sjis_fit(n);
        memcpy(p, g_orig, n);
        for (DWORD i = n; i < g_patlen; i++) p[i] = ' ';
        g_sfix_count++;
    }
}
static void fix_stored_copy(void) {
    if (!g_storefix || !g_patlen || g_patlen > 4000) return;
    /* Short lines: the engine deep-copies them into the display node (SBO) instead of
     * sharing a heap buffer.  Reverting those copies puts Japanese back into the node
     * the backlog/first frame renders from, so the line shows Japanese again.
     * Keep them Chinese; the save comment of a short line is rare and cosmetic. */
    if (g_origlen <= 32) return;
    DWORD t0 = GetTickCount();
    unsigned char first = (unsigned char)g_pat[0];
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* addr = (unsigned char*)0x10000;
    int hits = 0;
    while (addr < (unsigned char*)0x7FFF0000) {
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
        SIZE_T sz = mbi.RegionSize;
        if (!sz) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            sz <= 16u * 1024 * 1024) {
            unsigned char* p = (unsigned char*)mbi.BaseAddress;
            unsigned char* end = p + sz;
            while (p + g_patlen <= end) {
                unsigned char* q = (unsigned char*)memchr(p, first, (size_t)(end - p - g_patlen + 1));
                if (!q) break;
                if (q >= (unsigned char*)0x10010 && !memcmp(q, g_pat, g_patlen)) { patch_copy_hit(q); hits++; }
                p = q + 1;
            }
        }
        addr = (unsigned char*)mbi.BaseAddress + sz;
    }
    if (g_log_enabled && (g_sfix_count < 5 || (g_sfix_count % 100) == 0))
        logmsg("storefix: %d copies in %lu ms", hits, (unsigned long)(GetTickCount() - t0));
}

/* ---- short-line post-fix ------------------------------------------------
 * For very short lines the engine deep-copies the text into its display node
 * *before* our replacement is visible, so the node keeps Japanese and the
 * backlog/first frame render Japanese.  Right after the original SetText call
 * we look for any remaining Japanese copy of this short line and rewrite it to
 * Chinese in place.  Long lines do not need this (their node points at the
 * buffer we already rewrote).
 */
static int g_pfix_count = 0;
static unsigned char g_orig2[64], g_pat2[64];
static DWORD g_origlen2 = 0, g_patlen2 = 0;
__attribute__((used)) void post_text_fix(void) {
    if (!g_enabled || !g_keepzh) return;
    if (!g_patlen2 || !g_origlen2) return;
    if (g_patlen2 > 32 || g_origlen2 > 32) return;    /* short lines only */
    if (g_patlen2 > g_origlen2) return;
    if (!g_orig2[0]) return;
    unsigned char first = (unsigned char)g_orig2[0];
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* addr = (unsigned char*)0x10000;
    int hits = 0;
    while (addr < (unsigned char*)0x7FFF0000) {
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
        SIZE_T sz = mbi.RegionSize;
        if (!sz) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE) &&
            sz <= 16u * 1024 * 1024) {
            unsigned char* p = (unsigned char*)mbi.BaseAddress;
            unsigned char* end = p + sz;
            while (p + g_origlen2 <= end) {
                unsigned char* q = (unsigned char*)memchr(p, first, (size_t)(end - p - g_origlen2 + 1));
                if (!q) break;
                if (q >= (unsigned char*)0x10010 && q != (unsigned char*)g_origptr &&
                    !memcmp(q, g_orig2, g_origlen2)) {
                    unsigned char* tail = q + g_origlen2;
                    if (*tail == 0x01 || *tail == 0) {
                        memcpy(q, g_pat2, g_patlen2);
                        unsigned char fill = (*tail == 0x01) ? 0x01 : 0x00;
                        for (DWORD i = g_patlen2; i < g_origlen2; i++) q[i] = fill;
                        hits++;
                        if (hits >= 32) break;
                    }
                }
                p = q + 1;
            }
        }
        addr = (unsigned char*)mbi.BaseAddress + sz;
    }
    if (g_log_enabled && g_pfix_count < 20) {
        g_pfix_count++;
        logmsg("postfix: %d copies of len %d orig %d", hits, (int)g_patlen2, (int)g_origlen2);
    }
}
void capture_text(char** ptext) {
    colour_restore_now();
    if (!ptext) return;
    char* s = *ptext;
    if (!s) return;
    g_hits++;
    msg_log(s);
    if (g_sd_on) script_dump(s);
    if (g_log_enabled && g_hits < 400) logmsg("hit %d: %s", g_hits, s);
    int replaced = 0;
    if (g_enabled && g_nrec) {
        const BYTE* rec = find_rec(s);
        if (rec) {
            unsigned klen = *(unsigned short*)rec;
            const BYTE* key = rec + 2;
            unsigned vlen = *(unsigned short*)(key + klen);
            const BYTE* val = key + klen + 2;
            DWORD ol = (DWORD)lstrlenA(s);
            if (vlen <= ol && ol < sizeof(g_orig) - 1) {
                memcpy(g_orig, s, ol + 1); g_origlen = ol; g_origptr = s; g_restore_active = 1;
                memcpy(s, val, vlen); s[vlen] = 0;
                g_zhlen = vlen;
                g_patlen = vlen;
                if (vlen && val[vlen - 1] == 0x01) g_patlen = vlen - 1;
                if (g_patlen > sizeof(g_pat)) g_patlen = sizeof(g_pat);
                memcpy(g_pat, val, g_patlen);
                /* remember a private copy for post_text_fix (restore_text2 clears g_pat) */
                if (g_origlen <= 32 && g_patlen <= 32) {
                    memcpy(g_orig2, g_orig, g_origlen); g_origlen2 = g_origlen;
                    memcpy(g_pat2, g_pat, g_patlen); g_patlen2 = g_patlen;
                } else { g_origlen2 = 0; g_patlen2 = 0; }
                replaced = 1; g_replaced++;
            }
        }
    }
    if ((g_hits & 255) == 0) logmsg("stats: hits=%d replaced=%d tunnel=%d enabled=%d", g_hits, g_replaced, g_tunnel, g_enabled);
    if (g_matchdiag && g_hits <= 400) logmsg("hitlen %d replaced %d: %s", (int)lstrlenA(s), replaced, s);
    /* palette is left untouched; the compare hook resolves Chinese names */
    if (!replaced) {
        if (g_restore_active == 0 && !IsBadStringPtrA(s, 65536)) {
            DWORD ol = (DWORD)lstrlenA(s);
            if (ol < sizeof(g_orig) - 1) { memcpy(g_orig, s, ol + 1); g_origlen = ol; g_origptr = s; g_restore_active = 1; }
        }
        if (g_dump_miss && s && s[0]) {
            DWORD len = (DWORD)lstrlenA(s);
            if (len > 0 && len < 60000) miss_log(s, len);
        }
    }
}

void restore_text2(void) {
    if (g_restore_active && g_origptr && !g_keepzh) memcpy(g_origptr, g_orig, g_origlen + 1);
    if (g_patlen) { fix_stored_copy(); g_patlen = 0; g_zhlen = 0; }
    g_origptr = NULL; g_restore_active = 0;
}

static void install_text_hook(void) {
    HMODULE vm = GetModuleHandleA("Vm60.dll");
    if (!vm) { logmsg("Vm60.dll not found"); return; }
    DWORD target = (DWORD)(ULONG_PTR)vm + 0x79B55;
    hook_ret = target + 6;
    DWORD old;
    VirtualProtect((LPVOID)target, 8, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE*)target = 0xE9;
    *(DWORD*)(target + 1) = (DWORD)(ULONG_PTR)&hook_entry - target - 5;
    VirtualProtect((LPVOID)target, 8, old, &old);
    logmsg("text hook at %08lX", (unsigned long)target);
}

static pGetGlyphOutlineW real_W_stub = NULL;
static DWORD WINAPI my_GetGlyphOutlineW_fwd(HDC hdc, UINT ch, UINT fmt, LPGLYPHMETRICS gm, DWORD cb, LPVOID buf, const MAT2* mat) {
    return my_GetGlyphOutlineW(hdc, ch, fmt, gm, cb, buf, mat);
}
/* ---- inline hook gdi32full!GetGlyphOutlineA ----
 * The UI text pipeline (rvmm CTextImage) asks UnivUI's CS5RFont::GetCachedFont for a
 * glyph per SJIS code; when the entry is created the engine appears to reach GDI
 * through a path that does not use the per-module IAT we patched (it keeps showing
 * the Japanese glyph for tunnel codes).  Patching the gdi32full export itself catches
 * every caller, whatever import mechanism it used.
 * gdi32full!GetGlyphOutlineA starts with  8B FF 55 8B EC  (mov edi,edi; push ebp;
 * mov ebp,esp) - exactly 5 bytes, so a plain 5-byte detour is safe here.
 */
typedef DWORD (WINAPI *pGOA)(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, const MAT2*);
static pGOA g_goa_tr = NULL;
static DWORD WINAPI my_gdi_glyphA(HDC hdc, UINT ch, UINT fmt, LPGLYPHMETRICS gm, DWORD cb, LPVOID buf, const MAT2* mat) {
    unsigned int uni;
    if (g_enabled && g_ntmap && find_uni((unsigned short)ch, &uni)) {
        g_tunnel++;
        gly_log("GA", (unsigned short)ch, __builtin_return_address(0));
        HFONT cn = GetCnFont(hdc);
        HFONT old = cn ? (HFONT)SelectObject(hdc, cn) : NULL;
        DWORD r = real_GetGlyphOutlineW ? real_GetGlyphOutlineW(hdc, uni, fmt, gm, cb, buf, mat) : 0;
        if (old) SelectObject(hdc, old);
        return r;
    }
    return g_goa_tr ? g_goa_tr(hdc, ch, fmt, gm, cb, buf, mat) : 0;
}
static pGOA g_gow_tr = NULL;
static DWORD WINAPI my_gdi_glyphW(HDC hdc, UINT ch, UINT fmt, LPGLYPHMETRICS gm, DWORD cb, LPVOID buf, const MAT2* mat) {
    if (g_fontdiag) gly_log("GW", (unsigned short)ch, __builtin_return_address(0));
    return g_gow_tr ? g_gow_tr(hdc, ch, fmt, gm, cb, buf, mat) : 0;
}
static void install_gdi_export_hook(void) {
    HMODULE gf = GetModuleHandleA("gdi32full.dll");
    if (!gf) gf = LoadLibraryA("gdi32full.dll");
    if (!gf) { logmsg("gdi export hook: gdi32full missing"); return; }
    BYTE* pa = (BYTE*)GetProcAddress(gf, "GetGlyphOutlineA");
    if (!pa) { logmsg("gdi export hook: proc missing"); return; }
    if (!(pa[0] == 0x8B && pa[1] == 0xFF && pa[2] == 0x55 && pa[3] == 0x8B && pa[4] == 0xEC)) {
        logmsg("gdi export hook: prologue %02X %02X %02X %02X %02X", pa[0], pa[1], pa[2], pa[3], pa[4]);
        return;
    }
    BYTE* tr = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) { logmsg("gdi export hook: valloc fail"); return; }
    memcpy(tr, pa, 5);
    tr[5] = 0xE9;
    *(DWORD*)(tr + 6) = (DWORD)(pa + 5) - (DWORD)(tr + 10);
    g_goa_tr = (pGOA)tr;
    DWORD old;
    VirtualProtect(pa, 5, PAGE_EXECUTE_READWRITE, &old);
    pa[0] = 0xE9;
    *(DWORD*)(pa + 1) = (DWORD)(ULONG_PTR)&my_gdi_glyphA - (DWORD)(ULONG_PTR)pa - 5;
    VirtualProtect(pa, 5, old, &old);
    logmsg("gdi export hook: GetGlyphOutlineA at %p tramp %p", pa, tr);
    if (g_fontdiag == 2) {
        BYTE* pw = (BYTE*)GetProcAddress(gf, "GetGlyphOutlineW");
        if (pw && pw[0] == 0x8B && pw[1] == 0xFF && pw[2] == 0x55 && pw[3] == 0x8B && pw[4] == 0xEC) {
            BYTE* tw = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (tw) {
                memcpy(tw, pw, 5);
                tw[5] = 0xE9;
                *(DWORD*)(tw + 6) = (DWORD)(pw + 5) - (DWORD)(tw + 10);
                g_gow_tr = (pGOA)tw;
                DWORD o2;
                VirtualProtect(pw, 5, PAGE_EXECUTE_READWRITE, &o2);
                pw[0] = 0xE9;
                *(DWORD*)(pw + 1) = (DWORD)(ULONG_PTR)&my_gdi_glyphW - (DWORD)(ULONG_PTR)pw - 5;
                VirtualProtect(pw, 5, o2, &o2);
                logmsg("gdi export hook: GetGlyphOutlineW at %p tramp %p", pw, tw);
            }
        }
    }
}
/* ---- diagnostic: inline hook UnivUI!CS5RFont::GetCachedFont ---- */
extern void font_entry(void);
extern void* font_tramp;
/* v1.21-diag: 记录最近 8 个字形码；当序列命中【まりか】这类特征时，抓一次调用栈。
 * 目的：回答「回味到底是实时排版还是预渲染」——栈里第 2~4 帧就是调用方
 * （UnivUI 的 GetCachedFont 只有一个直接调用点，RVA 0x307FB，见 docs/）。 */
static unsigned short g_seq[8];
static int g_seq_n = 0;
static int g_stack_dumped = 0;
static void maybe_dump_stack(void) {
    static const unsigned short pat_a[] = {0x8179,0x82DC,0x82E8,0x82A9}; /* 【まりか */
    static const unsigned short pat_b[] = {0x8179,0x969C,0x97A2,0x89D4}; /* 【万里花 */
    if (g_stack_dumped >= 40) return;
    if (g_seq_n < 8) return;
    int hitA = 1, hitB = 1;
    for (int i = 0; i < 4; i++) {
        if (g_seq[i] != pat_a[i]) hitA = 0;
        if (g_seq[i] != pat_b[i]) hitB = 0;
    }
    if (!hitA && !hitB) return;
    g_stack_dumped++;
    void* frames[10]; USHORT nf = CaptureStackBackTrace(1, 10, frames, NULL);
    char line[320]; int k = 0;
    k += wsprintfA(line + k, "STACK %s n=%u", hitA ? "marika" : "michiru", (unsigned)nf);
    for (int i = 0; i < nf && k < 260; i++) k += wsprintfA(line + k, " %08X", (unsigned)(ULONG_PTR)frames[i]);
    k += wsprintfA(line + k, "\r\n");
    DWORD w; WriteFile(g_flog, line, k, &w, NULL);
}
void font_probe2(unsigned ch, void* self) {
    if (g_fl_n >= 40000) return;
    g_last_ch = (unsigned short)ch;
    for (int i = 0; i < 7; i++) g_seq[i] = g_seq[i + 1];
    g_seq[7] = (unsigned short)ch; if (g_seq_n < 8) g_seq_n++;
    if (g_flog == INVALID_HANDLE_VALUE) {
        char p[MAX_PATH]; join_path(p, "fontlog.txt");
        g_flog = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (g_flog == INVALID_HANDLE_VALUE) return;
    unsigned int uni = 0;
    int tun = (g_enabled && g_ntmap) ? find_uni((unsigned short)ch, &uni) : 0;
    maybe_dump_stack();
    char buf[96];
    int n = wsprintfA(buf, "%04X %08X %s\r\n", ch & 0xFFFF, self, tun ? "TUNNEL" : "");
    DWORD w; WriteFile(g_flog, buf, n, &w, NULL);
    g_fl_n++;
}
/* CS5RFont::GetCachedFont is exported by ordinal 472 only.  Its real prologue is
 *   6A FF            push -1
 *   68 xx xx xx xx   push <SEH handler>
 *   64 A1 00 00 00 00  mov eax, fs:[0]
 * so the first *complete* instruction group is 7 bytes long; a 5-byte detour that
 * copies only 5 bytes splits the push and crashes the game.  Copy 7 bytes and jump
 * back to body+7. */
void font_entry_probe2(void* e) {
    if (!e || g_fl_n >= 40000) return;
    if (g_flog == INVALID_HANDLE_VALUE) return;
    char buf[64];
    int n = wsprintfA(buf, "ENTRY %04X %p\r\n", g_last_ch, e);
    DWORD w; WriteFile(g_flog, buf, n, &w, NULL);
    unsigned char dump[96];
    if (ReadProcessMemory(GetCurrentProcess(), e, dump, sizeof(dump), NULL)) {
        char line[256]; int k = 0;
        k += wsprintfA(line + k, "  ");
        for (int i = 0; i < 96; i++) k += wsprintfA(line + k, "%02X ", dump[i]);
        k += wsprintfA(line + k, "\r\n");
        WriteFile(g_flog, line, k, &w, NULL);
    }
}
static void install_font_hook(void) {
    HMODULE ui = GetModuleHandleA("UnivUI.dll");
    if (!ui) { logmsg("font hook: UnivUI missing"); return; }
    BYTE* p = (BYTE*)GetProcAddress(ui, (LPCSTR)472);
    if (!p) { logmsg("font hook: ordinal 472 not found"); return; }
    if (p[0] == 0xE9) {
        BYTE* body = p + 5 + *(int*)(p + 1);
        logmsg("font hook: thunk %p -> body %p", p, body);
        p = body;
    }
    if (!(p[0] == 0x6A && p[2] == 0x68)) {
        logmsg("font hook: unexpected prologue %02X %02X %02X", p[0], p[1], p[2]);
    }
    BYTE* tr = (BYTE*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) { logmsg("font hook: valloc fail"); return; }
    memcpy(tr, p, 7);
    tr[7] = 0xE9;
    *(DWORD*)(tr + 8) = (DWORD)(p + 7) - (DWORD)(tr + 12);
    font_tramp = (void*)tr;
    DWORD old;
    VirtualProtect(p, 7, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    *(DWORD*)(p + 1) = (DWORD)(ULONG_PTR)&font_entry - (DWORD)(ULONG_PTR)p - 5;
    VirtualProtect(p, 7, old, &old);
    logmsg("font hook at %p (tramp %p)", p, tr);
}
static DWORD WINAPI late_patch_thread(LPVOID p) {
    for (int i = 0; i < 900; i++) {
        HMODULE m = GetModuleHandleA("MFC42.DLL");
        if (m) {
            int n = 0;
            n += patch_iat_textout(m);
            n += patch_iat(m, "GDI32.dll", "GetGlyphOutlineA", (void*)my_GetGlyphOutlineA, (void**)&real_glyph);
            n += patch_iat(m, "gdi32.dll", "GetGlyphOutlineA", (void*)my_GetGlyphOutlineA, (void**)&real_glyph);
            logmsg("late patch MFC42: %d", n);
            return 0;
        }
        Sleep(100);
    }
    return 0;
}
static void install_glyph_hooks(void) {
    const char* mods[] = {"Vm60.dll", "UnivUI.dll", "rvmm.dll", "GMfc.dll", "MFC42.DLL", "MSVCP60.dll"};
    int total = 0;
    for (int i = 0; i < 6; i++) {
        HMODULE m = GetModuleHandleA(mods[i]);
        total += patch_iat(m, "GDI32.dll", "GetGlyphOutlineA", (void*)my_GetGlyphOutlineA, (void**)&real_glyph);
        total += patch_iat(m, "gdi32.dll", "GetGlyphOutlineA", (void*)my_GetGlyphOutlineA, (void**)&real_glyph);
        total += patch_iat(m, "GDI32.dll", "GetGlyphOutlineW", (void*)my_GetGlyphOutlineW_fwd, (void**)&real_W_stub);
        total += patch_iat(m, "gdi32.dll", "GetGlyphOutlineW", (void*)my_GetGlyphOutlineW_fwd, (void**)&real_W_stub);
    }
    {
        HMODULE ex = GetModuleHandleA(NULL);
        total += patch_iat(ex, "GDI32.dll", "GetGlyphOutlineA", (void*)my_GetGlyphOutlineA, (void**)&real_glyph);
        total += patch_iat(ex, "gdi32.dll", "GetGlyphOutlineA", (void*)my_GetGlyphOutlineA, (void**)&real_glyph);
        total += patch_iat(ex, "GDI32.dll", "GetGlyphOutlineW", (void*)my_GetGlyphOutlineW_fwd, (void**)&real_W_stub);
        total += patch_iat(ex, "gdi32.dll", "GetGlyphOutlineW", (void*)my_GetGlyphOutlineW_fwd, (void**)&real_W_stub);
    }
    logmsg("glyph IAT patched: %d", total);
}




/* ---------------- per-character text colour: just-in-time name swap ----------------
 * raySerifSetting is parsed by Vm60 into a table of 27 entries, 0x34 bytes each:
 * name (32 bytes, SJIS, zero padded) at +0x00, colour 0x00RRGGBB at +0x24.
 * When a spoken line is prepared the engine looks the speaker name up in that table
 * and paints the line with the entry's colour.  The Chinese patch replaces the name
 * with tunnel-encoded Chinese characters, so the lookup misses and the line falls back
 * to the default colour.
 *
 * Renaming the entries permanently is not acceptable: the engine serialises the table
 * back into the player's settings (Vmreg/gdb.uuc) when it exits, which corrupts the
 * original settings.  Instead the entry is renamed only for the short moment between
 * the message being handed over and the engine resolving the colour, then the original
 * name is written back (the window is ~150 ms, and the settings are only written on
 * exit, so a save can never observe the temporary name in practice).
 */
typedef struct { const unsigned char* jp; int jplen; const unsigned char* zh; int zhlen; } CMAP;
static const unsigned char c_j_masashi[] = {0x90,0xb3,0x8e,0xf7};
static const unsigned char c_j_yayoi[]   = {0x82,0xe2,0x82,0xe6,0x82,0xa2};
static const unsigned char c_j_michiru[] = {0x82,0xdd,0x82,0xbf,0x82,0xe9};
static const unsigned char c_j_marika[]  = {0x82,0xdc,0x82,0xe8,0x82,0xa9};
static const unsigned char c_j_akira[]   = {0x82,0xa0,0x82,0xab,0x82,0xe7};
static const unsigned char c_j_keisuke[] = {0x8c,0x5b,0x89,0xee};
static const unsigned char c_j_mutsuki[] = {0x96,0x72,0x8c,0x8e};
static const unsigned char c_j_mitsuko[] = {0x83,0x7e,0x83,0x63,0x83,0x52};
static const unsigned char c_j_miyu[]    = {0x94,0xfc,0x97,0x44};
static const unsigned char c_j_osuga[]   = {0x91,0xe5,0x90,0x7b,0x89,0xea};
static const unsigned char c_j_helmut[]  = {0x83,0x77,0x83,0x8b,0x83,0x80,0x81,0x5b,0x83,0x67};
static const unsigned char c_j_mishima[] = {0x8e,0x4f,0x93,0x87};
static const unsigned char c_j_kudo[]    = {0x8d,0x48,0x93,0xa1};
static const unsigned char c_j_onishi[]  = {0x91,0xe5,0x90,0xbc};
static const unsigned char c_j_sakagami[]= {0x8d,0xe2,0x8f,0xe3};
static const unsigned char c_j_tomita[]  = {0x95,0x78,0x93,0x63};
static const unsigned char c_j_shinji[]  = {0x90,0x54,0x93,0xf1};
static const unsigned char c_j_takada[]  = {0x8d,0x82,0x93,0x63};
static const unsigned char c_j_ono[]     = {0x8f,0xac,0x96,0xec};
static const unsigned char c_j_shiina[]  = {0x92,0xc5,0x96,0xbc};
static const unsigned char c_j_yuri[]    = {0x97,0x44,0xe4,0xbb};
static const unsigned char c_j_misa[]    = {0x94,0xfc,0x8d,0xb9};
static const unsigned char c_j_inoko[]   = {0x93,0x98,0x8e,0x71};
static const unsigned char c_j_misae[]   = {0x94,0xfc,0x8d,0xe1};
static const unsigned char c_j_yuko[]    = {0x97,0x5b,0x8c,0xc4};
static const unsigned char c_j_marimo[]  = {0x82,0xdc,0x82,0xe8,0x82,0xe0};
static const unsigned char c_z_masashi[] = {0x98,0xa7,0x98,0xa8};
static const unsigned char c_z_yayoi[]   = {0x99,0xb6,0x99,0xb7};
static const unsigned char c_z_michiru[] = {0x99,0xc9,0x99,0xca,0x99,0xcb};
static const unsigned char c_z_marika[]  = {0x98,0xa0,0x98,0xa1,0x98,0xa2};
static const unsigned char c_z_akira[]   = {0x99,0x5b};
static const unsigned char c_z_keisuke[] = {0x9a,0x57,0x9a,0x59};
static const unsigned char c_z_mutsuki[] = {0x9c,0x51,0x9b,0x62};
static const unsigned char c_z_mitsuko[] = {0x99,0xc9,0x9b,0x93,0x98,0xf4};
static const unsigned char c_z_miyu[]    = {0x99,0xc9,0x9e,0x87};
static const unsigned char c_z_osuga[]   = {0x99,0x49,0x9c,0x41,0xe0,0x7a};
static const unsigned char c_z_helmut[]  = {0x9c,0xef,0x9b,0x5e,0x9c,0xf1,0x9b,0xa3};
static const unsigned char c_z_mishima[] = {0x9a,0x5c,0x9a,0x5d};
static const unsigned char c_z_kudo[]    = {0x9b,0x6e,0x9b,0x75};
static const unsigned char c_z_onishi[]  = {0x99,0x49,0x99,0x8d};
static const unsigned char c_z_sakagami[]= {0x9b,0x72,0x98,0xb3};
static const unsigned char c_z_tomita[]  = {0x9b,0x74,0x9a,0x89};
static const unsigned char c_z_shinji[]  = {0xe1,0x82,0x9c,0xd9};
static const unsigned char c_z_takada[]  = {0x99,0x98,0x9a,0x89};
static const unsigned char c_z_ono[]     = {0x99,0xc4,0x9b,0x69};
static const unsigned char c_z_shiina[]  = {0x9c,0x52,0x9c,0x53};
static const unsigned char c_z_yuri[]    = {0x9e,0x87,0xe3,0x8a};
static const unsigned char c_z_misa[]    = {0x99,0xc9,0x9c,0x63};
static const unsigned char c_z_inoko[]   = {0xe2,0x66,0x98,0xf4};
static const unsigned char c_z_misae[]   = {0x99,0xc9,0xe2,0x68};
static const unsigned char c_z_yuko[]    = {0x98,0xae,0x99,0x4c};
static const unsigned char c_z_marimo[]  = {0x98,0xc6,0x99,0x65,0xe7,0x44};
static const CMAP g_cmap[] = {
    {c_j_masashi,4,c_z_masashi,4},{c_j_yayoi,6,c_z_yayoi,4},{c_j_michiru,6,c_z_michiru,6},
    {c_j_marika,6,c_z_marika,6},{c_j_akira,6,c_z_akira,2},{c_j_keisuke,4,c_z_keisuke,4},
    {c_j_mutsuki,4,c_z_mutsuki,4},{c_j_mitsuko,6,c_z_mitsuko,6},{c_j_miyu,4,c_z_miyu,4},
    {c_j_osuga,6,c_z_osuga,6},{c_j_helmut,10,c_z_helmut,8},{c_j_mishima,4,c_z_mishima,4},
    {c_j_kudo,4,c_z_kudo,4},{c_j_onishi,4,c_z_onishi,4},{c_j_sakagami,4,c_z_sakagami,4},
    {c_j_tomita,4,c_z_tomita,4},{c_j_shinji,4,c_z_shinji,4},{c_j_takada,4,c_z_takada,4},
    {c_j_ono,4,c_z_ono,4},{c_j_shiina,4,c_z_shiina,4},{c_j_yuri,4,c_z_yuri,4},
    {c_j_misa,4,c_z_misa,4},{c_j_inoko,4,c_z_inoko,4},{c_j_misae,4,c_z_misae,4},
    {c_j_yuko,4,c_z_yuko,4},{c_j_marimo,6,c_z_marimo,6}
};
static unsigned char* g_cpal = NULL;          /* located table base */
static unsigned char g_corig[27][0x20];       /* original name while patched */
static int g_cpatched = -1;                   /* index currently renamed */
static DWORD g_cpatch_time = 0;
static int memeq(const unsigned char* a, const unsigned char* b, int n){ for(int i=0;i<n;i++) if(a[i]!=b[i]) return 0; return 1; }
static int wmem(void* dst, const void* src, SIZE_T n){
    DWORD old;
    if(!VirtualProtect(dst,n,PAGE_READWRITE,&old)) return 0;
    memcpy(dst,src,n);
    VirtualProtect(dst,n,old,&old);
    return 1;
}
static int scan_palette_once(void){
    HANDLE hp=GetCurrentProcess(); MEMORY_BASIC_INFORMATION mbi; unsigned char* addr=0;
    const unsigned char* mar = c_j_marika;
    while(VirtualQueryEx(hp,addr,&mbi,sizeof mbi)){
        unsigned char* base=(unsigned char*)mbi.BaseAddress; SIZE_T size=mbi.RegionSize;
        if(mbi.State==MEM_COMMIT && !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && (mbi.Type==MEM_PRIVATE||mbi.Type==MEM_MAPPED) && size>=27*0x34 && size<0x2000000){
            unsigned char* buf=(unsigned char*)malloc(size);
            if(buf){
                SIZE_T got=0;
                if(ReadProcessMemory(hp,base,buf,size,&got) && got>=27*0x34){
                    for(SIZE_T i=0;i+27*0x34<=got;i++){
                        unsigned char* p=buf+i;
                        if(memeq(p,mar,6) && p[6]==0 && i>=4*0x34){
                            unsigned char* pb=base+i-4*0x34;
                            if(memeq(pb+1*0x34,c_j_masashi,4) && memeq(pb+2*0x34,c_j_yayoi,6) && memeq(pb+3*0x34,c_j_michiru,6)){
                                free(buf); return 1;
                            }
                        }
                    }
                }
                free(buf);
            }
        }
        addr+=size; if(!size) addr+=0x1000;
    }
    return 0;
}
static int find_palette(void){
    HANDLE hp=GetCurrentProcess(); MEMORY_BASIC_INFORMATION mbi; unsigned char* addr=0;
    while(VirtualQueryEx(hp,addr,&mbi,sizeof mbi)){
        unsigned char* base=(unsigned char*)mbi.BaseAddress; SIZE_T size=mbi.RegionSize;
        if(mbi.State==MEM_COMMIT && !(mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && (mbi.Type==MEM_PRIVATE||mbi.Type==MEM_MAPPED) && size>=27*0x34 && size<0x2000000){
            unsigned char* buf=(unsigned char*)malloc(size);
            if(buf){
                SIZE_T got=0;
                if(ReadProcessMemory(hp,base,buf,size,&got) && got>=27*0x34){
                    for(SIZE_T i=0;i+27*0x34<=got;i++){
                        unsigned char* p=buf+i;
                        if(memeq(p,c_j_marika,6) && p[6]==0 && i>=4*0x34){
                            unsigned char* pb=base+i-4*0x34;
                            if(memeq(pb+1*0x34,c_j_masashi,4) && memeq(pb+2*0x34,c_j_yayoi,6) && memeq(pb+3*0x34,c_j_michiru,6)){
                                g_cpal=pb; free(buf); return 1;
                            }
                        }
                    }
                }
                free(buf);
            }
        }
        addr+=size; if(!size) addr+=0x1000;
    }
    return 0;
}
/* Called from capture_text with the (already replaced) message string.  The text uses
 * native SJIS 0x8179/0x817A brackets (【】) around the speaker name, and the palette
 * table holds Japanese names.  Rename the matching palette entry to the Chinese name
 * (the exact bytes used in the text) so the engine's name lookup succeeds and the
 * per-character colour from the table is applied.  The rename is reverted on the next
 * message (and by a timeout thread as a safety net). */
static void colour_hint(const char* s){
    if(!g_cpal && !find_palette()) return;
    if(!s) return;
    const unsigned char* b=(const unsigned char*)s;
    if(!(b[0]==0x81 && b[1]==0x79)) return;
    const unsigned char* name=b+2;
    const unsigned char* end=name;
    while(*end && !(end[0]==0x81 && end[1]==0x7a)) end++;
    int nlen=(int)(end-name);
    if(nlen<=0 || nlen>30) return;
    for(int i=0;i<g_namemap_n;i++){
        if(g_namemap[i].zhlen!=nlen) continue;
        if(!memeq(name,g_namemap[i].zh,nlen)) continue;
        /* locate the palette entry carrying the Japanese name */
        int count=0; unsigned char* tab=NULL;
        HMODULE vm=GetModuleHandleA("Vm60.dll");
        if(vm && (ULONG_PTR)vm==(ULONG_PTR)0x10000000){ count=*(int*)(0x100bbf78); tab=*(unsigned char**)0x100bbf74; }
        if(!tab || count<=2 || count>256) return;
        for(int e=0;e<count;e++){
            unsigned char* ent=tab+e*0x34;
            if(memeq(ent,g_namemap[i].jp,g_namemap[i].jplen) && ent[g_namemap[i].jplen]==0){
                if(g_cpatched!=e){
                    memcpy(g_corig[e],ent,0x20);
                    unsigned char tmp[0x20]; memset(tmp,0,0x20);
                    memcpy(tmp,name,nlen);
                    wmem(ent,tmp,0x20);
                    g_cpatched=e;
                    logmsg("colour: entry %d <- name len %d", e, nlen);
                }
                g_cpatch_time=GetTickCount();
                return;
            }
        }
        return;
    }
}
static void colour_restore_now(void){
    if(g_cpatched>=0 && g_cpal){
        unsigned char* e=g_cpal+g_cpatched*0x34;
        wmem(e,g_corig[g_cpatched],0x20);
        g_cpatched=-1; g_cpatch_time=0;
    }
}
static DWORD WINAPI colour_restore_thread(LPVOID p){
    (void)p;
    for(;;){
        if(g_cpatched>=0 && g_cpatch_time && GetTickCount()-g_cpatch_time>2500){
            colour_restore_now();
        }
        Sleep(20);
    }
    return 0;
}

/* The engine compares a candidate speaker name against every palette entry with
 * 0x100164F0.  Palette entries carry the Japanese names, while the patched text uses
 * Chinese names, so the comparison fails and the line loses its per-character colour.
 * Detour that call: after the original comparison fails, treat a Chinese name as equal
 * to the corresponding Japanese entry.  No table is modified, so the game never writes
 * a renamed palette back to the user's settings. */
static int (__cdecl *g_real_cmp)(const char*, const char*) = NULL;
static int g_cmp_hits = 0;
static int __cdecl colour_compare(const char* name, const char* entry){
    if(!name || !entry) return 0;
    int r = g_real_cmp ? g_real_cmp(name, entry) : 0;
    if(r) return r;
    for(int i=0;i<g_namemap_n;i++){
        const NAMEPAIR* np=&g_namemap[i];
        if(memcmp(entry,np->jp,np->jplen)!=0 || entry[np->jplen]!=0) continue;
        if(memcmp(name,np->zh,np->zhlen)!=0) continue;
        if(name[np->zhlen]!=0 && name[np->zhlen]!=':') continue;
        if(g_cmp_hits<200){ g_cmp_hits++; logmsg("colour: cn name matches jp entry (len %d)", np->jplen); }
        return 1;
    }
    return 0;
}
static void install_compare_hook(void){
    HMODULE vm = GetModuleHandleA("Vm60.dll");
    if(!vm || (ULONG_PTR)vm != (ULONG_PTR)0x10000000) { logmsg("colour: Vm60 base unexpected"); return; }
    g_real_cmp = (int (__cdecl*)(const char*, const char*))((unsigned char*)vm + 0x164f0);
    BYTE* cs = (unsigned char*)vm + 0x164b7;
    if(cs[0] != 0xE8){ logmsg("colour: call site mismatch %02X", cs[0]); return; }
    DWORD old;
    VirtualProtect(cs, 8, PAGE_EXECUTE_READWRITE, &old);
    *(int*)(cs + 1) = (int)((BYTE*)&colour_compare - (cs + 5));
    VirtualProtect(cs, 8, old, &old);
    logmsg("colour: compare hook at %08lX", (unsigned long)(ULONG_PTR)cs);
}

__attribute__((used)) void log_fetch_hook(unsigned idx, unsigned ra){
    if(g_fetch_n < 3000){ g_fetch_n++; logmsg("fetch idx=%d ra=%08X", (int)idx, ra); }
}
__attribute__((naked)) static void fetch_hook(void){
    __asm__ __volatile__(
        "movl 4(%esp), %eax\n\t"
        "movl 0(%esp), %edx\n\t"
        "pushal\n\t"
        "pushfl\n\t"
        "pushl %eax\n\t"
        "pushl %edx\n\t"
        "call _log_fetch_hook\n\t"
        "addl $8, %esp\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *_g_fetch_tramp\n\t");
}
static void install_fetch_hook(void){
    HMODULE vm = GetModuleHandleA("Vm60.dll");
    if(!vm || (ULONG_PTR)vm != (ULONG_PTR)0x10000000){ logmsg("fetch: Vm60 base unexpected"); return; }
    unsigned char* tgt = (unsigned char*)vm + 0x16584;
    DWORD old;
    VirtualProtect(tgt, 8, PAGE_EXECUTE_READWRITE, &old);
    g_fetch_tramp = (unsigned char*)VirtualAlloc(NULL, 64, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_fetch_tramp, tgt, 5);
    g_fetch_tramp[5] = 0xE9;
    *(int*)(g_fetch_tramp + 6) = (int)(tgt + 5) - (int)(g_fetch_tramp + 5) - 5;
    *tgt = 0xE9;
    *(int*)(tgt + 1) = (int)((unsigned char*)&fetch_hook - tgt - 5);
    VirtualProtect(tgt, 8, old, &old);
    logmsg("fetch: hook installed at %08lX", (unsigned long)(ULONG_PTR)tgt);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_self = h;
        DWORD n = GetModuleFileNameA(h, g_dir, MAX_PATH);
        if (n) { char* p = strrchr(g_dir, '\\'); if (p) *p = 0; }
        if (!g_dir[0]) { GetCurrentDirectoryA(MAX_PATH, g_dir); }
        {
            char p[MAX_PATH]; join_path(p, "cnpatch.log");
            g_log = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            logmsg("DllMain attach: log=%s dir=%s", p, g_dir);
        }
        load_config();
        logmsg("config: enabled=%d log=%d hotkey=0x%02X", g_enabled, g_log_enabled, g_hotkey);
        if (g_sd_on) {
            char p[MAX_PATH]; join_path(p, "scriptdump.bin");
            g_sd = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            logmsg("scriptdump: %s", p);
        }
        {
            char p[MAX_PATH]; join_path(p, "msglog.bin");
            g_msg = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            logmsg("msglog: %s", p);
        }
        if (g_matchdiag) {
            char lp[MAX_PATH]; join_path(lp, "lookup.log");
            g_mlog = CreateFileA(lp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            logmsg("matchdiag: %s", lp);
        }
        if (g_fetch_log) install_fetch_hook();
        if (g_dump_miss) {
            char p[MAX_PATH]; join_path(p, "miss.bin");
            g_miss = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        logmsg("cnpatch loaded (enabled=%d hotkey=0x%02X)", g_enabled, g_hotkey);
        CreateThread(NULL, 0, hotkey_thread, NULL, 0, NULL);
        HMODULE gf = GetModuleHandleA("gdi32full.dll");
        if (!gf) gf = LoadLibraryA("gdi32full.dll");
        if (gf) real_GetGlyphOutlineW = (pGetGlyphOutlineW)GetProcAddress(gf, "GetGlyphOutlineW");
        load_txt();
        load_uni();
        logmsg("rev map: %d", build_rmap());
        install_text_hook();
        install_compare_hook();
        install_glyph_hooks();
        install_textout_hooks();
        install_gdi_export_hook();
        install_createfile_hooks();
        if (g_fontdiag == 2 && g_gow_tr) real_GetGlyphOutlineW = (pGetGlyphOutlineW)g_gow_tr;
        if (g_fontdiag) install_font_hook();
        CreateThread(NULL, 0, late_patch_thread, NULL, 0, NULL);
        CreateThread(NULL, 0, colour_restore_thread, NULL, 0, NULL);
        { extern void choice_init(const char*); choice_init(g_dir); }
        logmsg("ready: hits=0 replaced=0 tunnel=0");
    }
    return TRUE;
}
