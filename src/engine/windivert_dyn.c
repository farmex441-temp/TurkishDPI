/*
 * TurkishDPI - runtime loader for WinDivert.dll.
 *
 * GoodbyeDPI links WinDivert.dll at load time, so a missing DLL (typically
 * quarantined by an antivirus, or an exe copied without its folder) makes
 * Windows refuse to start the program with a cryptic system dialog.
 * TurkishDPI loads it on first use instead, so the app always starts and can
 * explain the problem in plain words.
 *
 * The DLL is only ever loaded from the program's own folder (never the
 * current directory or PATH). TurkishDPI does not carry or unpack any driver
 * itself: WinDivert's signed files ship next to the exe in the release zip.
 *
 * Compile the engine with -DWINDIVERTEXPORT=extern so windivert.h declares
 * plain functions, which are defined here as thin forwarding stubs.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "windivert.h"

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

typedef HANDLE (*pOpen)(const char *, WINDIVERT_LAYER, INT16, UINT64);
typedef BOOL   (*pRecv)(HANDLE, VOID *, UINT, UINT *, WINDIVERT_ADDRESS *);
typedef BOOL   (*pSend)(HANDLE, const VOID *, UINT, UINT *, const WINDIVERT_ADDRESS *);
typedef BOOL   (*pShutdown)(HANDLE, WINDIVERT_SHUTDOWN);
typedef BOOL   (*pClose)(HANDLE);
typedef BOOL   (*pParse)(const VOID *, UINT, PWINDIVERT_IPHDR *, PWINDIVERT_IPV6HDR *,
                         UINT8 *, PWINDIVERT_ICMPHDR *, PWINDIVERT_ICMPV6HDR *,
                         PWINDIVERT_TCPHDR *, PWINDIVERT_UDPHDR *, PVOID *, UINT *,
                         PVOID *, UINT *);
typedef BOOL   (*pCalc)(VOID *, UINT, WINDIVERT_ADDRESS *, UINT64);

static struct {
    INIT_ONCE once;
    BOOL ok;
    DWORD err;   /* why loading failed, reported by WinDivertOpen */
    pOpen open; pRecv recv; pSend send; pShutdown shutdown; pClose close;
    pParse parse; pCalc calc;
} wd = { INIT_ONCE_STATIC_INIT, FALSE, ERROR_MOD_NOT_FOUND, 0, 0, 0, 0, 0, 0, 0 };

#ifdef WINDIVERT_DLL_SHA256
/*
 * The DLL next to the exe is loaded into an elevated process, and that folder
 * may be writable by ordinary users (a zip extracted to Downloads). Its
 * SHA-256 is fixed at build time (Makefile: taken from the checksum-pinned
 * WinDivert archive) and checked here on an open handle that denies writes
 * and deletes, so the file cannot be swapped between the check and the load.
 */
typedef LONG (WINAPI *bc_open_t)(void **, LPCWSTR, LPCWSTR, ULONG);
typedef LONG (WINAPI *bc_create_t)(void *, void **, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *bc_data_t)(void *, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *bc_finish_t)(void *, PUCHAR, ULONG, ULONG);
typedef LONG (WINAPI *bc_destroy_t)(void *);
typedef LONG (WINAPI *bc_close_t)(void *, ULONG);

static BOOL sha256_of_handle(HANDLE f, unsigned char out[32]) {
    HMODULE bc = LoadLibraryExW(L"bcrypt.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    bc_open_t bopen; bc_create_t bcreate; bc_data_t bdata; bc_finish_t bfinish;
    bc_destroy_t bdestroy; bc_close_t bclose;
    void *alg = NULL, *hash = NULL;
    unsigned char buf[16384];
    DWORD got;
    BOOL ok = FALSE;
    if (!bc) return FALSE;
    bopen = (bc_open_t)(void *)GetProcAddress(bc, "BCryptOpenAlgorithmProvider");
    bcreate = (bc_create_t)(void *)GetProcAddress(bc, "BCryptCreateHash");
    bdata = (bc_data_t)(void *)GetProcAddress(bc, "BCryptHashData");
    bfinish = (bc_finish_t)(void *)GetProcAddress(bc, "BCryptFinishHash");
    bdestroy = (bc_destroy_t)(void *)GetProcAddress(bc, "BCryptDestroyHash");
    bclose = (bc_close_t)(void *)GetProcAddress(bc, "BCryptCloseAlgorithmProvider");
    if (bopen && bcreate && bdata && bfinish && bdestroy && bclose &&
        bopen(&alg, L"SHA256", NULL, 0) == 0) {
        if (bcreate(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
            ok = TRUE;
            while (ok && ReadFile(f, buf, sizeof(buf), &got, NULL) && got)
                ok = bdata(hash, buf, got, 0) == 0;
            ok = ok && bfinish(hash, out, 32, 0) == 0;
            bdestroy(hash);
        }
        bclose(alg, 0);
    }
    FreeLibrary(bc);
    return ok;
}

static BOOL hash_matches(const unsigned char h[32], const char *hex) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        char hi = hex[2 * i], lo;
        if (!hi) return FALSE;                 /* never read past a short string */
        lo = hex[2 * i + 1];
        if (!lo) return FALSE;
        if (hi >= 'A' && hi <= 'F') hi += 32;
        if (lo >= 'A' && lo <= 'F') lo += 32;
        if (hi != digits[h[i] >> 4] || lo != digits[h[i] & 15]) return FALSE;
    }
    return hex[64] == 0;
}
#endif

/* 1 if the file at path is the pinned WinDivert.dll (always 1 in a build
 * without a pin). Used before the app copies the DLL into Program Files. */
int windivert_dll_ok(const wchar_t *path) {
#ifdef WINDIVERT_DLL_SHA256
    unsigned char digest[32];
    BOOL ok;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = sha256_of_handle(f, digest) && hash_matches(digest, WINDIVERT_DLL_SHA256);
    CloseHandle(f);
    return ok;
#else
    (void)path;
    return 1;
#endif
}

static BOOL CALLBACK wd_load(PINIT_ONCE o, PVOID p, PVOID *c) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    HMODULE h;
    HANDLE pin = INVALID_HANDLE_VALUE;
    (void)o; (void)p; (void)c;
    while (n > 0 && path[n - 1] != L'\\') n--;
    if (n == 0 || n + 14 >= MAX_PATH) return TRUE;
    lstrcpyW(path + n, L"WinDivert.dll");
#ifdef WINDIVERT_DLL_SHA256
    {
        unsigned char digest[32];
        pin = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (pin == INVALID_HANDLE_VALUE) {
            wd.err = GetLastError() == ERROR_FILE_NOT_FOUND ? ERROR_MOD_NOT_FOUND : GetLastError();
            return TRUE;
        }
        if (!sha256_of_handle(pin, digest) || !hash_matches(digest, WINDIVERT_DLL_SHA256)) {
            wd.err = ERROR_FILE_INVALID;   /* app: "WinDivert.dll is not the expected file" */
            CloseHandle(pin);
            return TRUE;
        }
    }
#endif
    /* Absolute path; its own imports (kernel32, advapi32) from System32 only. */
    h = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) wd.err = GetLastError();   /* e.g. 193: DLL of the wrong architecture */
    if (pin != INVALID_HANDLE_VALUE) CloseHandle(pin);   /* mapped now: cannot change */
    if (!h) return TRUE;
#define WD_SYM(field, type, name) wd.field = (type)(void *)GetProcAddress(h, name)
    WD_SYM(open, pOpen, "WinDivertOpen");
    WD_SYM(recv, pRecv, "WinDivertRecv");
    WD_SYM(send, pSend, "WinDivertSend");
    WD_SYM(shutdown, pShutdown, "WinDivertShutdown");
    WD_SYM(close, pClose, "WinDivertClose");
    WD_SYM(parse, pParse, "WinDivertHelperParsePacket");
    WD_SYM(calc, pCalc, "WinDivertHelperCalcChecksums");
#undef WD_SYM
    wd.ok = wd.open && wd.recv && wd.send && wd.shutdown && wd.close && wd.parse && wd.calc;
    return TRUE;
}

static BOOL wd_ready(void) {
    InitOnceExecuteOnce(&wd.once, wd_load, NULL, NULL);
    return wd.ok;
}

HANDLE WinDivertOpen(const char *filter, WINDIVERT_LAYER layer, INT16 priority, UINT64 flags) {
    if (!wd_ready()) {
        SetLastError(wd.err);
        return INVALID_HANDLE_VALUE;
    }
    return wd.open(filter, layer, priority, flags);
}

BOOL WinDivertRecv(HANDLE h, VOID *pkt, UINT len, UINT *recvLen, WINDIVERT_ADDRESS *addr) {
    return wd_ready() && wd.recv(h, pkt, len, recvLen, addr);
}

BOOL WinDivertSend(HANDLE h, const VOID *pkt, UINT len, UINT *sendLen, const WINDIVERT_ADDRESS *addr) {
    return wd_ready() && wd.send(h, pkt, len, sendLen, addr);
}

BOOL WinDivertShutdown(HANDLE h, WINDIVERT_SHUTDOWN how) {
    return wd_ready() && wd.shutdown(h, how);
}

BOOL WinDivertClose(HANDLE h) {
    return wd_ready() && wd.close(h);
}

BOOL WinDivertHelperParsePacket(const VOID *pPacket, UINT packetLen,
    PWINDIVERT_IPHDR *ppIpHdr, PWINDIVERT_IPV6HDR *ppIpv6Hdr, UINT8 *pProtocol,
    PWINDIVERT_ICMPHDR *ppIcmpHdr, PWINDIVERT_ICMPV6HDR *ppIcmpv6Hdr,
    PWINDIVERT_TCPHDR *ppTcpHdr, PWINDIVERT_UDPHDR *ppUdpHdr, PVOID *ppData,
    UINT *pDataLen, PVOID *ppNext, UINT *pNextLen) {
    return wd_ready() && wd.parse(pPacket, packetLen, ppIpHdr, ppIpv6Hdr, pProtocol,
                                  ppIcmpHdr, ppIcmpv6Hdr, ppTcpHdr, ppUdpHdr, ppData,
                                  pDataLen, ppNext, pNextLen);
}

BOOL WinDivertHelperCalcChecksums(VOID *pPacket, UINT packetLen, WINDIVERT_ADDRESS *pAddr, UINT64 flags) {
    return wd_ready() && wd.calc(pPacket, packetLen, pAddr, flags);
}
