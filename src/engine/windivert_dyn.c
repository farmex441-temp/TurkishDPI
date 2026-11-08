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
    pOpen open; pRecv recv; pSend send; pShutdown shutdown; pClose close;
    pParse parse; pCalc calc;
} wd = { INIT_ONCE_STATIC_INIT, FALSE, 0, 0, 0, 0, 0, 0, 0 };

static BOOL CALLBACK wd_load(PINIT_ONCE o, PVOID p, PVOID *c) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    HMODULE h;
    (void)o; (void)p; (void)c;
    while (n > 0 && path[n - 1] != L'\\') n--;
    if (n == 0 || n + 14 >= MAX_PATH) return TRUE;
    lstrcpyW(path + n, L"WinDivert.dll");
    h = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
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
        SetLastError(ERROR_MOD_NOT_FOUND);
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
