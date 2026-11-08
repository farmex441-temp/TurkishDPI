/*
 * TurkishDPI - WinHTTP and IP Helper functions resolved at run time from
 * System32 only.
 *
 * winhttp.dll and iphlpapi.dll are not KnownDLLs. As static imports, Windows
 * looks for them in the exe's own folder first - and that folder is often
 * user-writable (a zip extracted to Downloads), while this process runs
 * elevated. So they are not linked: sysdll_init() loads them with
 * LOAD_LIBRARY_SEARCH_SYSTEM32 and the names below are redirected to the
 * resolved pointers. Include this header after the system headers.
 */
#ifndef TURKISHDPI_SYSDLL_H
#define TURKISHDPI_SYSDLL_H
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <netioapi.h>

/* Restricts the process DLL search path to System32, then loads the libraries.
 * Returns 1 when every function was found. Safe to call more than once. */
int sysdll_init(void);
/* Windows 7/8.0: WinHTTP offers only TLS 1.0 by default, which today's sites
 * refuse; turn on TLS 1.2 for this session. No-op on Windows 8.1 and later. */
void sysdll_winhttp_tls(void *session);

typedef HINTERNET (WINAPI *sd_WinHttpOpen_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL (WINAPI *sd_WinHttpCloseHandle_t)(HINTERNET);
typedef HINTERNET (WINAPI *sd_WinHttpConnect_t)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET (WINAPI *sd_WinHttpOpenRequest_t)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR,
                                                     LPCWSTR *, DWORD);
typedef BOOL (WINAPI *sd_WinHttpQueryHeaders_t)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *sd_WinHttpQueryOption_t)(HINTERNET, DWORD, LPVOID, LPDWORD);
typedef BOOL (WINAPI *sd_WinHttpReadData_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *sd_WinHttpReceiveResponse_t)(HINTERNET, LPVOID);
typedef BOOL (WINAPI *sd_WinHttpSendRequest_t)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *sd_WinHttpSetOption_t)(HINTERNET, DWORD, LPVOID, DWORD);
typedef WINHTTP_STATUS_CALLBACK (WINAPI *sd_WinHttpSetStatusCallback_t)(HINTERNET, WINHTTP_STATUS_CALLBACK,
                                                                        DWORD, DWORD_PTR);
typedef BOOL (WINAPI *sd_WinHttpSetTimeouts_t)(HINTERNET, int, int, int, int);
typedef DWORD (WINAPI *sd_NotifyAddrChange_t)(PHANDLE, LPOVERLAPPED);
typedef DWORD (WINAPI *sd_GetBestRoute2_t)(NET_LUID *, NET_IFINDEX, const SOCKADDR_INET *,
                                           const SOCKADDR_INET *, ULONG, PMIB_IPFORWARD_ROW2,
                                           SOCKADDR_INET *);
typedef DWORD (WINAPI *sd_ResolveIpNetEntry2_t)(PMIB_IPNET_ROW2, const SOCKADDR_INET *);
typedef DWORD (WINAPI *sd_GetIfEntry2_t)(PMIB_IF_ROW2);

extern sd_WinHttpOpen_t              sd_WinHttpOpen;
extern sd_WinHttpCloseHandle_t       sd_WinHttpCloseHandle;
extern sd_WinHttpConnect_t           sd_WinHttpConnect;
extern sd_WinHttpOpenRequest_t       sd_WinHttpOpenRequest;
extern sd_WinHttpQueryHeaders_t      sd_WinHttpQueryHeaders;
extern sd_WinHttpQueryOption_t       sd_WinHttpQueryOption;
extern sd_WinHttpReadData_t          sd_WinHttpReadData;
extern sd_WinHttpReceiveResponse_t   sd_WinHttpReceiveResponse;
extern sd_WinHttpSendRequest_t       sd_WinHttpSendRequest;
extern sd_WinHttpSetOption_t         sd_WinHttpSetOption;
extern sd_WinHttpSetStatusCallback_t sd_WinHttpSetStatusCallback;
extern sd_WinHttpSetTimeouts_t       sd_WinHttpSetTimeouts;
extern sd_NotifyAddrChange_t         sd_NotifyAddrChange;
extern sd_GetBestRoute2_t            sd_GetBestRoute2;
extern sd_ResolveIpNetEntry2_t       sd_ResolveIpNetEntry2;
extern sd_GetIfEntry2_t              sd_GetIfEntry2;

#ifndef SYSDLL_NO_REDIRECT
#define WinHttpOpen              sd_WinHttpOpen
#define WinHttpCloseHandle       sd_WinHttpCloseHandle
#define WinHttpConnect           sd_WinHttpConnect
#define WinHttpOpenRequest       sd_WinHttpOpenRequest
#define WinHttpQueryHeaders      sd_WinHttpQueryHeaders
#define WinHttpQueryOption       sd_WinHttpQueryOption
#define WinHttpReadData          sd_WinHttpReadData
#define WinHttpReceiveResponse   sd_WinHttpReceiveResponse
#define WinHttpSendRequest       sd_WinHttpSendRequest
#define WinHttpSetOption         sd_WinHttpSetOption
#define WinHttpSetStatusCallback sd_WinHttpSetStatusCallback
#define WinHttpSetTimeouts       sd_WinHttpSetTimeouts
#define NotifyAddrChange         sd_NotifyAddrChange
#define GetBestRoute2            sd_GetBestRoute2
#define ResolveIpNetEntry2       sd_ResolveIpNetEntry2
#define GetIfEntry2              sd_GetIfEntry2
#endif

#endif
