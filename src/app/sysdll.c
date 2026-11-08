/*
 * TurkishDPI - System32-only loading of winhttp.dll and iphlpapi.dll
 * (see sysdll.h for why they are not linked at build time).
 */
#define WIN32_LEAN_AND_MEAN
#define SYSDLL_NO_REDIRECT
#include "syslib.h"
#include "sysdll.h"

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

sd_WinHttpOpen_t              sd_WinHttpOpen;
sd_WinHttpCloseHandle_t       sd_WinHttpCloseHandle;
sd_WinHttpConnect_t           sd_WinHttpConnect;
sd_WinHttpOpenRequest_t       sd_WinHttpOpenRequest;
sd_WinHttpQueryHeaders_t      sd_WinHttpQueryHeaders;
sd_WinHttpReadData_t          sd_WinHttpReadData;
sd_WinHttpReceiveResponse_t   sd_WinHttpReceiveResponse;
sd_WinHttpSendRequest_t       sd_WinHttpSendRequest;
sd_WinHttpSetOption_t         sd_WinHttpSetOption;
sd_WinHttpSetStatusCallback_t sd_WinHttpSetStatusCallback;
sd_WinHttpSetTimeouts_t       sd_WinHttpSetTimeouts;
sd_NotifyAddrChange_t         sd_NotifyAddrChange;
sd_GetBestRoute2_t            sd_GetBestRoute2;
sd_ResolveIpNetEntry2_t       sd_ResolveIpNetEntry2;
sd_GetIfEntry2_t              sd_GetIfEntry2;

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static int g_ok;

static BOOL CALLBACK load_all(PINIT_ONCE o, PVOID p, PVOID *c) {
    typedef BOOL (WINAPI *set_dirs_fn)(DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll"), http, iph;
    set_dirs_fn set_dirs;
    (void)o; (void)p; (void)c;

    /* Every later LoadLibrary without explicit flags (ours, or one inside a
     * system component) then searches System32 only, never the exe folder or
     * the current directory. Present on Windows 8+ and Windows 7 with
     * KB2533623, which LOAD_LIBRARY_SEARCH_SYSTEM32 below needs as well. */
    set_dirs = k32 ? (set_dirs_fn)(void *)GetProcAddress(k32, "SetDefaultDllDirectories") : NULL;
    if (set_dirs)
        set_dirs(LOAD_LIBRARY_SEARCH_SYSTEM32);

    http = load_system_dll(L"winhttp.dll");
    iph = load_system_dll(L"iphlpapi.dll");
    if (!http || !iph)
        return TRUE;
#define SD(mod, name) sd_##name = (sd_##name##_t)(void *)GetProcAddress(mod, #name)
    SD(http, WinHttpOpen);
    SD(http, WinHttpCloseHandle);
    SD(http, WinHttpConnect);
    SD(http, WinHttpOpenRequest);
    SD(http, WinHttpQueryHeaders);
    SD(http, WinHttpReadData);
    SD(http, WinHttpReceiveResponse);
    SD(http, WinHttpSendRequest);
    SD(http, WinHttpSetOption);
    SD(http, WinHttpSetStatusCallback);
    SD(http, WinHttpSetTimeouts);
    SD(iph, NotifyAddrChange);
    SD(iph, GetBestRoute2);
    SD(iph, ResolveIpNetEntry2);
    SD(iph, GetIfEntry2);
#undef SD
    g_ok = sd_WinHttpOpen && sd_WinHttpCloseHandle && sd_WinHttpConnect && sd_WinHttpOpenRequest &&
           sd_WinHttpQueryHeaders && sd_WinHttpReadData && sd_WinHttpReceiveResponse &&
           sd_WinHttpSendRequest && sd_WinHttpSetOption && sd_WinHttpSetStatusCallback &&
           sd_WinHttpSetTimeouts && sd_NotifyAddrChange && sd_GetBestRoute2 &&
           sd_ResolveIpNetEntry2 && sd_GetIfEntry2;
    return TRUE;
}

void sysdll_winhttp_tls(void *session) {
    OSVERSIONINFOEXW v;
    DWORDLONG mask = 0;
    DWORD p;
    if (!session || !sd_WinHttpSetOption) return;
    ZeroMemory(&v, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    v.dwMajorVersion = 6;
    v.dwMinorVersion = 3;   /* 8.1 (the manifest lists it, so the answer is not shimmed) */
    mask = VerSetConditionMask(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    mask = VerSetConditionMask(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    if (VerifyVersionInfoW(&v, VER_MAJORVERSION | VER_MINORVERSION, mask)) return;
    /* WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | _TLS1_2; needs KB3140245 on Windows 7 */
    p = 0x00000200 | 0x00000800;
    sd_WinHttpSetOption((HINTERNET)session, 84 /* WINHTTP_OPTION_SECURE_PROTOCOLS */, &p, sizeof(p));
}

int sysdll_init(void) {
    InitOnceExecuteOnce(&g_once, load_all, NULL, NULL);
    return g_ok;
}
