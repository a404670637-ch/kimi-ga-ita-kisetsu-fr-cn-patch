/* choice.c - runtime translation of choice/UI texts in the game's SJIS text pools.
 * v1.8 also records every text it sees in those pools that is not in the choice table
 * (cnpatch_choice.bin) into choicelog.bin, so the option corpus can be collected
 * by simply playing.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern int g_enabled;

#define MAXA 256
#define HSIZE (1<<16)
#define MAXLOG 30000
static char g_dir[MAX_PATH];
static unsigned char* g_tab = NULL;
static int g_cnt = 0;
static DWORD g_arr[MAXA];
static int g_na = 0;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static unsigned char g_buf[0x10000];
static DWORD g_last_scan = 0;

static HANDLE g_clog = INVALID_HANDLE_VALUE;
static unsigned long* g_hs = NULL;
static int g_nlog = 0;

static void jpath(char* out, const char* name) { wsprintfA(out, "%s\\%s", g_dir, name); }
static void lg(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char b[512];
    va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(b, fmt, ap);
    va_end(ap);
    DWORD w; WriteFile(g_log, b, n, &w, NULL);
    WriteFile(g_log, "\r\n", 2, &w, NULL);
}
static int srd(DWORD addr, void* out, DWORD n) {
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)addr, out, n, &got)) return 0;
    return got == n;
}
static unsigned long fnv(const unsigned char* p, int n) {
    unsigned long h = 2166136261UL;
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619UL; }
    if (!h) h = 1;
    return h;
}
static int hs_add(unsigned long h) {
    if (!g_hs) return 0;
    unsigned idx = h & (HSIZE - 1);
    for (int i = 0; i < 64; i++) {
        unsigned long v = g_hs[idx];
        if (v == 0) { g_hs[idx] = h; return 1; }
        if (v == h) return 0;
        idx = (idx + 1) & (HSIZE - 1);
    }
    return 1;
}
static int hs_has(unsigned long h) {
    if (!g_hs) return 1;
    unsigned idx = h & (HSIZE - 1);
    for (int i = 0; i < 64; i++) {
        unsigned long v = g_hs[idx];
        if (v == 0) return 0;
        if (v == h) return 1;
        idx = (idx + 1) & (HSIZE - 1);
    }
    return 0;
}
static void log_load(void) {
    char p[MAX_PATH]; jpath(p, "choicelog.bin");
    g_clog = CreateFileA(p, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_clog == INVALID_HANDLE_VALUE) { lg("choice: no choicelog"); return; }
    DWORD sz = GetFileSize(g_clog, NULL);
    if (sz) {
        unsigned char* d = (unsigned char*)VirtualAlloc(NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
        DWORD r = 0;
        SetFilePointer(g_clog, 0, NULL, FILE_BEGIN);
        ReadFile(g_clog, d, sz, &r, NULL);
        DWORD off = 0;
        while (off + 2 <= r) {
            int len = d[off] | (d[off+1] << 8);
            off += 2;
            if (len <= 0 || len > 0x40 || off + len > r) break;
            hs_add(fnv(d + off, len));
            g_nlog++;
            off += len;
        }
        VirtualFree(d, 0, MEM_RELEASE);
    }
    SetFilePointer(g_clog, 0, NULL, FILE_END);
    lg("choice: choicelog has %d entries", g_nlog);
}
static void log_text(const unsigned char* t, int len) {
    if (g_clog == INVALID_HANDLE_VALUE || g_nlog >= MAXLOG || len <= 0 || len > 0x40) return;
    unsigned long h = fnv(t, len);
    if (hs_has(h)) return;
    unsigned short l = (unsigned short)len;
    DWORD w;
    WriteFile(g_clog, &l, 2, &w, NULL);
    WriteFile(g_clog, t, len, &w, NULL);
    hs_add(h);
    g_nlog++;
}

static int load_choice(void) {
    char p[MAX_PATH]; jpath(p, "cnpatch_choice.bin");
    HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { lg("choice: no table"); return 0; }
    DWORD sz = GetFileSize(h, NULL);
    unsigned char* d = (unsigned char*)VirtualAlloc(NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    DWORD r = 0;
    ReadFile(h, d, sz, &r, NULL);
    CloseHandle(h);
    if (r < 8 || memcmp(d, "CNCH", 4)) { lg("choice: bad magic"); return 0; }
    g_cnt = *(DWORD*)(d + 4);
    g_tab = d;
    lg("choice: loaded %d entries", g_cnt);
    return 1;
}
static int entry_at(int i, const unsigned char** key, int* klen, const unsigned char** val, int* vlen) {
    unsigned char* p = g_tab + 8;
    for (int k = 0; k < i; k++) {
        int kl = *(unsigned short*)p; p += 2 + kl;
        int vl = *(unsigned short*)p; p += 2 + vl;
    }
    *klen = *(unsigned short*)p; p += 2;
    *key = p; p += *klen;
    *vlen = *(unsigned short*)p; p += 2;
    *val = p;
    return 1;
}
static void add_arr(DWORD a) {
    if (a < 0x10000) return;
    for (int i = 0; i < g_na; i++) if (g_arr[i] == a) return;
    if (g_na < MAXA) g_arr[g_na++] = a;
}
static void rescan(void) {
    g_na = 0;
    DWORD addr = 0x10000;
    int budget = 0;
    while (addr < 0x70000000 && budget < 3000) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) break;
        DWORD base = (DWORD)mbi.BaseAddress, size = (DWORD)mbi.RegionSize;
        DWORD pr = mbi.Protect;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && (pr & 0xFF) == PAGE_READWRITE &&
            !(pr & PAGE_GUARD) && size >= 0x1000 && size <= 0x200000) {
            budget++;
            for (DWORD off = 0; off + 0x200 < size; off += 0x10000 - 16) {
                DWORD chunk = 0x10000;
                if (off + chunk > size) chunk = size - off;
                if (!srd(base + off, g_buf, chunk)) continue;
                /* (a) placeholder signature: slot text starts with _$unrefix */
                for (DWORD i = 0; i + 16 < chunk; i += 4) {
                    if (g_buf[i] != '_' || memcmp(g_buf + i, "_$unrefix", 9)) continue;
                    DWORD a = base + off + i;
                    DWORD hdr[2] = {0, 0};
                    if (!srd(a - 8, hdr, 8)) continue;
                    if (hdr[1] != 0x40 || hdr[0] >= 0x40) continue;
                    DWORD t = a;
                    for (;;) {
                        if (t < 0x50 + 0x28 || t - 0x50 < base + 0x28) break;
                        DWORD h2[2] = {0, 0};
                        if (!srd(t - 0x50 - 8, h2, 8)) break;
                        if (h2[1] != 0x40 || h2[0] >= 0x40) break;
                        t -= 0x50;
                    }
                    add_arr(t - 0x28);
                }
                /* (b) structural signature: three consecutive cap==0x40 dwords at 0x50 stride
                   (a fully-used choice array has no _$unrefix placeholder at all). */
                for (DWORD i = 0; i + 0xB0 < chunk; i += 4) {
                    if (*(DWORD*)(g_buf + i) != 0x40) continue;
                    if (*(DWORD*)(g_buf + i + 0x50) != 0x40) continue;
                    if (*(DWORD*)(g_buf + i + 0xA0) != 0x40) continue;
                    DWORD c = base + off + i;
                    DWORD cap2 = 0;
                    while (c >= base + 0x24 + 0x50 && srd(c - 0x50, &cap2, 4) && cap2 == 0x40) c -= 0x50;
                    DWORD len0 = 0;
                    if (!srd(c - 4, &len0, 4) || len0 > 0x40) continue;
                    add_arr(c - 0x24);
                }
            }
        }
        addr = base + size;
        if (addr <= base) break;
    }
    g_last_scan = GetTickCount();
    lg("choice: scan found %d arrays", g_na);
}
static HWND game_child(void) {
    HWND h = FindWindowA("relic UGP Frame Class", NULL);
    if (!h) return NULL;
    HWND c = FindWindowExA(h, NULL, NULL, NULL);
    return c ? c : h;
}
static DWORD g_last_redraw = 0;
static void force_redraw(void) {
    HWND w = game_child();
    if (!w) return;
    DWORD now = GetTickCount();
    if (g_last_redraw && now - g_last_redraw < 1000) return;
    g_last_redraw = now;
    DWORD lp = (300 << 16) | 512;
    PostMessageA(w, 0x0204, 2, lp); Sleep(40); PostMessageA(w, 0x0205, 0, lp);
    Sleep(80);
    PostMessageA(w, 0x0204, 2, lp); Sleep(40); PostMessageA(w, 0x0205, 0, lp);
    lg("choice: redraw requested");
}
static unsigned char g_abuf[0x28 + 80 * 0x50];
static int g_nslot[80]; static int g_nlen[80]; static unsigned char g_ntext[80][0x40];
static int poll_one(DWORD pool) {
    int dirty = 0;
    int newlog = 0;
    int nrepl = 0;
    if (!srd(pool, g_abuf, sizeof(g_abuf))) return 0;
    for (int k = 0; k < 80; k++) {
        DWORD txt = pool + 0x28 + k * 0x50;
        unsigned char* hdr = g_abuf + 0x18 + k * 0x50;
        DWORD len = *(DWORD*)(hdr + 8), cap = *(DWORD*)(hdr + 12);
        if (cap != 0x40 || len > 0x40) continue;
        if (len == 0) continue;
        unsigned char text[0x40];
        memcpy(text, hdr + 0x10, 0x40);
        if (!memcmp(text, "_$unrefix", 9)) continue;
        int matched = 0;
        for (int i = 0; i < g_cnt; i++) {
            const unsigned char* vv; int vlen2;
            {
                const unsigned char* kk; int klen2;
                entry_at(i, &kk, &klen2, &vv, &vlen2);
            }
            if ((DWORD)vlen2 == len && !memcmp(text, vv, vlen2)) { matched = 2; break; }
        }
        if (matched == 2) continue;
        matched = 0;
        for (int i = 0; i < g_cnt; i++) {
            const unsigned char *key, *val; int kl, vl;
            entry_at(i, &key, &kl, &val, &vl);
            if ((DWORD)kl == len && !memcmp(text, key, kl)) {
                SIZE_T w = 0;
                if (!WriteProcessMemory(GetCurrentProcess(), (void*)txt, val, vl, &w)) break;
                unsigned char pad[0x40];
                memset(pad, 0, sizeof(pad));
                WriteProcessMemory(GetCurrentProcess(), (void*)(txt + vl), pad, len - vl, &w);
                WriteProcessMemory(GetCurrentProcess(), (void*)(txt - 0x8), &vl, 4, &w);
                lg("choice: replaced pool %08lX slot %d (%d -> %d bytes)", (unsigned long)pool, k, kl, vl);
                nrepl++;
                dirty = 1;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            unsigned long h = fnv(text, (int)len);
            if (!hs_has(h)) {
                char lb[96];
                int ln2 = wsprintfA(lb, "newtext pool=%08lX slot=%d len=%d\r\n", (unsigned long)pool, k, (int)len);
                DWORD w2; WriteFile(g_log, lb, ln2, &w2, NULL);
                if (newlog < 80) { g_nslot[newlog] = k; g_nlen[newlog] = (int)len; memcpy(g_ntext[newlog], text, 0x40); }
                newlog++;
            }
            log_text(text, (int)len);
        }
    }
    if (nrepl >= 2) {
        char b[64];
        int n = wsprintfA(b, "CHOICEK pool=%08lX n=%d\r\n", (unsigned long)pool, nrepl);
        DWORD w; WriteFile(g_log, b, n, &w, NULL);
    }
    if (newlog >= 2) {
        char b[256];
        int n = wsprintfA(b, "CHOICE pool=%08lX n=%d\r\n", (unsigned long)pool, newlog);
        DWORD w; WriteFile(g_log, b, n, &w, NULL);
        int lim = newlog < 80 ? newlog : 80;
        for (int q = 0; q < lim; q++) {
            n = wsprintfA(b, "  slot=%d len=%d hex=", g_nslot[q], g_nlen[q]);
            int ml = g_nlen[q] > 0x40 ? 0x40 : g_nlen[q];
            for (int j = 0; j < ml && n < 200; j++) n += wsprintfA(b + n, "%02x", g_ntext[q][j]);
            n += wsprintfA(b + n, "\r\n");
            WriteFile(g_log, b, n, &w, NULL);
        }
    }
    return dirty;
}
static DWORD WINAPI choice_thread(LPVOID p) {
    (void)p;
    Sleep(3000);
    for (;;) {
        Sleep(60);
        if (!g_enabled || !g_tab || !g_cnt) continue;
        if (g_na == 0 || GetTickCount() - g_last_scan > 5000) rescan();
        int dirty = 0;
        for (int i = 0; i < g_na; i++) dirty |= poll_one(g_arr[i]);
        (void)dirty; /* the choice box re-renders by itself when the pool text changes;
                    forcing a redraw made the engine rebuild the pool from the script
                    and overwrite the patch, so it is deliberately NOT done here. */
    }
    return 0;
}
void choice_init(const char* dir) {
    lstrcpynA(g_dir, dir, MAX_PATH);
    char p[MAX_PATH]; jpath(p, "choice.log");
    g_log = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    lg("choice: init");
    g_hs = (unsigned long*)VirtualAlloc(NULL, HSIZE * sizeof(unsigned long), MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!load_choice()) return;
    log_load();
    CreateThread(NULL, 0, choice_thread, NULL, 0, NULL);
}
void text_thunk(void) { }
