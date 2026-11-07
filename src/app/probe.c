/*
 * TurkishDPI - connectivity probes used by the auto-detector.
 *
 *  - probe_https(): does a real TLS handshake + HTTP HEAD with WinHTTP, so the
 *    result reflects DNS poisoning, SNI blocking and RST injection exactly the
 *    way a browser would see them (idea from Shroudly's blockcheck/autopilot).
 *  - probe_dns(): asks a resolver on a non-standard port for an A record to
 *    find out which DNS server is reachable from this network.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include "probe.h"

typedef struct {
    const wchar_t *host;
    DWORD timeout_ms;
    volatile LONG ok;
    LONG ms;
} https_job_t;

/* Returns the time to first response in ms, or -1 on failure. */
static int https_once(const wchar_t *host, DWORD timeout_ms) {
    int ok = 0;
    ULONGLONG t0 = GetTickCount64();
    HINTERNET s = NULL, c = NULL, r = NULL;

    s = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) TurkishDPI-probe",
                    WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) goto out;
    WinHttpSetTimeouts(s, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms);
    c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) goto out;
    r = WinHttpOpenRequest(c, L"HEAD", L"/", NULL, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                           WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
    if (!r) goto out;
    {
        /* Never reuse a pooled connection: every probe must be a fresh handshake. */
        DWORD opt = WINHTTP_DISABLE_KEEP_ALIVE;
        WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &opt, sizeof(opt));
    }
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto out;
    if (!WinHttpReceiveResponse(r, NULL))
        goto out;
    {
        DWORD status = 0, len = sizeof(status);
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                                WINHTTP_NO_HEADER_INDEX))
            ok = status >= 100 && status < 600;
    }
out:
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok ? (int)(GetTickCount64() - t0) : -1;
}

static DWORD WINAPI https_thread(LPVOID p) {
    https_job_t *j = (https_job_t *)p;
    /* One retry: a single lost SYN must not condemn a working strategy. */
    int ms = https_once(j->host, j->timeout_ms);
    if (ms < 0) ms = https_once(j->host, j->timeout_ms);
    j->ms = ms;
    if (ms >= 0)
        InterlockedExchange(&j->ok, 1);
    return 0;
}

int probe_https_many(const wchar_t **hosts, int count, DWORD timeout_ms, int *results, int *lat_ms) {
    HANDLE th[PROBE_MAX_HOSTS];
    https_job_t jobs[PROBE_MAX_HOSTS];
    int i, n = count > PROBE_MAX_HOSTS ? PROBE_MAX_HOSTS : count, okc = 0;

    for (i = 0; i < n; i++) {
        jobs[i].host = hosts[i];
        jobs[i].timeout_ms = timeout_ms;
        jobs[i].ok = 0;
        jobs[i].ms = -1;
        th[i] = CreateThread(NULL, 64 * 1024, https_thread, &jobs[i],
                             STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (!th[i]) https_thread(&jobs[i]);
    }
    for (i = 0; i < n; i++) {
        if (th[i]) {
            WaitForSingleObject(th[i], INFINITE);
            CloseHandle(th[i]);
        }
        if (results) results[i] = (int)jobs[i].ok;
        if (lat_ms) lat_ms[i] = (int)jobs[i].ms;
        if (jobs[i].ok) okc++;
    }
    return okc;
}

int probe_https(const wchar_t *host, DWORD timeout_ms) {
    const wchar_t *h[1] = { host };
    return probe_https_many(h, 1, timeout_ms, NULL, NULL) == 1;
}

/* Build a minimal DNS query for an A record. Returns its length. */
static int dns_build_query(unsigned char *buf, size_t cap, const char *name, unsigned short id) {
    size_t pos = 12;
    const char *p = name;
    if (cap < 512) return 0;
    memset(buf, 0, 12);
    buf[0] = (unsigned char)(id >> 8);
    buf[1] = (unsigned char)(id & 0xFF);
    buf[2] = 0x01;              /* RD */
    buf[5] = 0x01;              /* QDCOUNT = 1 */
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        if (l == 0 || l > 63 || pos + l + 6 > cap) return 0;
        buf[pos++] = (unsigned char)l;
        memcpy(buf + pos, p, l);
        pos += l;
        p += l;
        if (*p == '.') p++;
    }
    buf[pos++] = 0;
    buf[pos++] = 0; buf[pos++] = 1;   /* QTYPE A */
    buf[pos++] = 0; buf[pos++] = 1;   /* QCLASS IN */
    return (int)pos;
}

int probe_dns(const char *server_v4, unsigned short port, const char *name, DWORD timeout_ms) {
    SOCKET s;
    struct sockaddr_in sa;
    unsigned char q[512], a[1500];
    int qlen, alen, ok = 0;
    unsigned short id = (unsigned short)(GetTickCount() & 0xFFFF);
    DWORD tmo = timeout_ms;

    qlen = dns_build_query(q, sizeof(q), name, id);
    if (!qlen) return 0;

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, server_v4, &sa.sin_addr) != 1) {
        closesocket(s);
        return 0;
    }
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        if (sendto(s, (const char *)q, qlen, 0, (struct sockaddr *)&sa, sizeof(sa)) != qlen)
            break;
        alen = recv(s, (char *)a, sizeof(a), 0);
        if (alen >= 12 &&
            a[0] == q[0] && a[1] == q[1] &&     /* same ID */
            (a[2] & 0x80) &&                    /* is a response */
            (a[3] & 0x0F) == 0 &&               /* RCODE NOERROR */
            ((a[6] << 8) | a[7]) > 0)           /* ANCOUNT > 0 */
            ok = 1;
    }
    closesocket(s);
    return ok;
}

void probe_flush_dns_cache(void) {
    typedef BOOL (WINAPI *flush_fn)(void);
    HMODULE h = LoadLibraryW(L"dnsapi.dll");
    if (h) {
        flush_fn f = (flush_fn)(void *)GetProcAddress(h, "DnsFlushResolverCache");
        if (f) f();
        FreeLibrary(h);
    }
}
