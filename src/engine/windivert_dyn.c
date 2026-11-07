/*
 * TurkishDPI - runtime loader for WinDivert.dll.
 *
 * GoodbyeDPI links WinDivert.dll at load time, so a missing DLL (typically
 * quarantined by an antivirus) makes Windows refuse to start the program with
 * a cryptic system dialog. TurkishDPI loads it on first use instead, so the
 * tray app always starts and can explain the problem in plain words.
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

/*
 * Single-file builds embed WinDivert.dll and the driver(s) as RCDATA
 * resources 101..103. They are unpacked to %ProgramData%\TurkishDPI\bin,
 * a directory writable only by Administrators/SYSTEM (we run elevated, so a
 * user-writable location would allow DLL planting), and re-written whenever
 * their content differs from the embedded copy.
 */
#define RES_WINDIVERT_DLL   101
#define RES_WINDIVERT_SYS64 102
#define RES_WINDIVERT_SYS32 103

static BOOL file_matches(const wchar_t *path, const void *data, DWORD size) {
    BOOL same = FALSE;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    if (GetFileSize(f, NULL) == size) {
        BYTE *buf = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
        DWORD rd = 0;
        if (buf && ReadFile(f, buf, size, &rd, NULL) && rd == size)
            same = memcmp(buf, data, size) == 0;
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
    }
    CloseHandle(f);
    return same;
}

static BOOL extract_res(int id, const wchar_t *dir, const wchar_t *name) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    HGLOBAL g;
    const void *data;
    DWORD size, wr;
    wchar_t path[MAX_PATH];
    HANDLE f;
    if (!r || !(g = LoadResource(NULL, r)) || !(data = LockResource(g))) return FALSE;
    size = SizeofResource(NULL, r);
    wsprintfW(path, L"%s\\%s", dir, name);
    if (file_matches(path, data, size)) return TRUE;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;   /* e.g. driver in use: keep old copy */
    WriteFile(f, data, size, &wr, NULL);
    CloseHandle(f);
    return wr == size;
}

static BOOL secure_dir(const wchar_t *dir) {
    typedef BOOL (WINAPI *conv_fn)(LPCWSTR, DWORD, PSECURITY_DESCRIPTOR *, PULONG);
    /* Protected DACL: SYSTEM + Administrators full, Users read/execute. */
    static const wchar_t sddl[] = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)";
    HMODULE adv = GetModuleHandleW(L"advapi32.dll");
    conv_fn conv = adv ? (conv_fn)(void *)GetProcAddress(adv,
                       "ConvertStringSecurityDescriptorToSecurityDescriptorW") : NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
    BOOL ok;
    if (!conv || !conv(sddl, 1, &sd, NULL)) return FALSE;
    sa.lpSecurityDescriptor = sd;
    ok = CreateDirectoryW(dir, &sa) ||
         (GetLastError() == ERROR_ALREADY_EXISTS &&
          SetFileSecurityW(dir, DACL_SECURITY_INFORMATION, sd));
    LocalFree(sd);
    return ok;
}

static HMODULE load_embedded(void) {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"ProgramData", dir, MAX_PATH);
    if (!FindResourceW(NULL, MAKEINTRESOURCEW(RES_WINDIVERT_DLL), (LPCWSTR)RT_RCDATA))
        return NULL;
    if (n == 0 || n > MAX_PATH - 40) lstrcpyW(dir, L"C:\\ProgramData");
    lstrcatW(dir, L"\\TurkishDPI");
    CreateDirectoryW(dir, NULL);
    lstrcatW(dir, L"\\bin");
    if (!secure_dir(dir)) return NULL;
    extract_res(RES_WINDIVERT_SYS64, dir, L"WinDivert64.sys");
    extract_res(RES_WINDIVERT_SYS32, dir, L"WinDivert32.sys");
    if (!extract_res(RES_WINDIVERT_DLL, dir, L"WinDivert.dll")) return NULL;
    wsprintfW(path, L"%s\\WinDivert.dll", dir);
    return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

static BOOL CALLBACK wd_load(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    HMODULE h = NULL;
    /* 1) WinDivert.dll next to the program (zip release), never CWD/PATH. */
    while (n > 0 && path[n - 1] != L'\\') n--;
    if (n > 0 && n + 14 < MAX_PATH) {
        lstrcpyW(path + n, L"WinDivert.dll");
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            h = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    /* 2) Copy embedded in the single-file build. */
    if (!h) h = load_embedded();
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
