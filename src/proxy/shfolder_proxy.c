#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include <stdio.h>

static HMODULE g_real = NULL;
static void load_real(void)
{
    if (g_real) return;
    char sys[MAX_PATH];
    UINT n = GetSystemWow64DirectoryA(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) n = GetSystemDirectoryA(sys, MAX_PATH);
    if (n && n < MAX_PATH) {
        strcat(sys, "/SHFolder.dll");
        g_real = LoadLibraryA(sys);
    }
}

HRESULT WINAPI MySHGetFolderPathA(HWND hwnd, int nFolder, HANDLE hToken, DWORD dwFlags, LPSTR pszPath)
{
    typedef HRESULT (WINAPI *PFN)(HWND,int,HANDLE,DWORD,LPSTR);
    load_real();
    if (g_real) { PFN f = (PFN)GetProcAddress(g_real, "SHGetFolderPathA"); if (f) return f(hwnd,nFolder,hToken,dwFlags,pszPath); }
    return E_FAIL;
}

HRESULT WINAPI MySHGetFolderPathW(HWND hwnd, int nFolder, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath)
{
    typedef HRESULT (WINAPI *PFN)(HWND,int,HANDLE,DWORD,LPWSTR);
    load_real();
    if (g_real) { PFN f = (PFN)GetProcAddress(g_real, "SHGetFolderPathW"); if (f) return f(hwnd,nFolder,hToken,dwFlags,pszPath); }
    return E_FAIL;
}

static DWORD WINAPI starter(LPVOID p)
{
    (void)p;
    char sep = (char)92;
    char env[8];
    if (GetEnvironmentVariableA("CNPATCH_DISABLE", env, sizeof env) > 0) return 0;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    const char *base = strrchr(exe, sep); base = base ? base + 1 : exe;
    if (_stricmp(base, "rugp.exe") != 0) return 0;
    char dir[MAX_PATH];
    strcpy(dir, exe);
    char *sl = strrchr(dir, sep); if (sl) *sl = 0;
    char dll[MAX_PATH];
    _snprintf(dll, MAX_PATH, "%s/cnpatch/cnpatch.dll", dir);
    for (int i = 0; i < 240; i++) {
        if (GetModuleHandleA("Vm60.dll")) {
            HMODULE h = LoadLibraryA(dll);
            char log[MAX_PATH];
            _snprintf(log, MAX_PATH, "%s/cnpatch/proxy.log", dir);
            FILE *f = fopen(log, "a");
            if (f) { fprintf(f, "proxy: %s cnpatch.dll %s\n", h ? "loaded" : "FAILED", h ? dll : ""); fclose(f); }
            break;
        }
        Sleep(500);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(NULL, 0, starter, NULL, 0, NULL);
    }
    return TRUE;
}
