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

/*
 * Phase timestamps (QueryPerformanceCounter) collected by the WinHTTP status
 * callback. What we report as a site's latency is the connection set-up time
 * (TCP + TLS handshake): that is what a DPI tool can add or remove. DNS time
 * and the server's own time to build its page are left out, otherwise a slow
 * web server would look like a slow bypass.
 */
typedef struct {
    LONGLONG connecting, sending;
} phase_t;

static LONGLONG qpc_now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static void CALLBACK status_cb(HINTERNET h, DWORD_PTR ctx, DWORD status, LPVOID info, DWORD len) {
    phase_t *p = (phase_t *)ctx;
    (void)h; (void)info; (void)len;
    if (!p) return;
    if (status == WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER && !p->connecting)
        p->connecting = qpc_now();
    else if (status == WINHTTP_CALLBACK_STATUS_SENDING_REQUEST && !p->sending)
        p->sending = qpc_now();
}

/* Returns the connection set-up time in ms (>= 1), or -1 on failure. */
static int https_once(const wchar_t *host, DWORD timeout_ms) {
    int ok = 0;
    LONGLONG t0 = qpc_now();
    LARGE_INTEGER freq;
    phase_t ph = { 0, 0 };
    HINTERNET s = NULL, c = NULL, r = NULL;

    QueryPerformanceFrequency(&freq);
    s = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) TurkishDPI-probe",
                    WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) goto out;
    WinHttpSetTimeouts(s, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms, (int)timeout_ms);
    WinHttpSetStatusCallback(s, status_cb,
                             WINHTTP_CALLBACK_FLAG_CONNECT_TO_SERVER | WINHTTP_CALLBACK_FLAG_SEND_REQUEST,
                             0);
    c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) goto out;
    r = WinHttpOpenRequest(c, L"HEAD", L"/", NULL, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                           WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH);
    if (!r) goto out;
    {
        /* Fresh handshake every time, and the first answer counts: a redirect
         * already proves the site is reachable, following it only adds time. */
        DWORD opt = WINHTTP_DISABLE_KEEP_ALIVE | WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &opt, sizeof(opt));
    }
    if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, (DWORD_PTR)&ph))
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
    if (s) {
        WinHttpSetStatusCallback(s, NULL, 0, 0);
        WinHttpCloseHandle(s);
    }
    if (!ok) return -1;
    {
        LONGLONG a = ph.connecting ? ph.connecting : t0;
        LONGLONG b = ph.sending ? ph.sending : qpc_now();
        int ms = (int)((b - a) * 1000 / freq.QuadPart);
        return ms < 1 ? 1 : ms;
    }
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

/* Addresses that a real site never resolves to: forged/sinkhole answers. */
static int bogus_ipv4(const unsigned char *a) {
    static const unsigned char block_pages[][4] = {
        { 195, 175, 254, 2 },     /* BTK / Turk Telekom block page */
        { 193, 192, 98, 43 },
    };
    if (a[0] == 0 || a[0] == 127 || a[0] == 10 || a[0] >= 224) return 1;
    if (a[0] == 192 && a[1] == 168) return 1;
    if (a[0] == 172 && a[1] >= 16 && a[1] <= 31) return 1;
    if (a[0] == 169 && a[1] == 254) return 1;
    for (size_t i = 0; i < sizeof(block_pages) / sizeof(block_pages[0]); i++)
        if (!memcmp(a, block_pages[i], 4)) return 1;
    return 0;
}

/* Skips a (possibly compressed) DNS name. Returns new offset or -1. */
static int dns_skip_name(const unsigned char *m, int len, int off) {
    while (off < len) {
        unsigned char l = m[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : -1;
        off += l + 1;
    }
    return -1;
}

/* 1 if the answer holds at least one A record and none of them is bogus. */
int dns_answer_ok(const unsigned char *m, int len) {
    int qd, an, off = 12, good = 0;
    if (len < 12) return 0;
    qd = (m[4] << 8) | m[5];
    an = (m[6] << 8) | m[7];
    for (int i = 0; i < qd; i++) {
        off = dns_skip_name(m, len, off);
        if (off < 0 || off + 4 > len) return 0;
        off += 4;
    }
    for (int i = 0; i < an; i++) {
        int type, rdlen;
        off = dns_skip_name(m, len, off);
        if (off < 0 || off + 10 > len) return 0;
        type = (m[off] << 8) | m[off + 1];
        rdlen = (m[off + 8] << 8) | m[off + 9];
        off += 10;
        if (off + rdlen > len) return 0;
        if (type == 1 && rdlen == 4) {
            if (bogus_ipv4(m + off)) return 0;
            good = 1;
        }
        off += rdlen;
    }
    return good;
}

/* Collects the A records of an answer (up to max). Returns the count. */
static int dns_collect_a(const unsigned char *m, int len, unsigned long *out, int max) {
    int qd, an, off = 12, n = 0;
    if (len < 12) return 0;
    qd = (m[4] << 8) | m[5];
    an = (m[6] << 8) | m[7];
    for (int i = 0; i < qd; i++) {
        off = dns_skip_name(m, len, off);
        if (off < 0 || off + 4 > len) return n;
        off += 4;
    }
    for (int i = 0; i < an && n < max; i++) {
        int type, rdlen;
        off = dns_skip_name(m, len, off);
        if (off < 0 || off + 10 > len) return n;
        type = (m[off] << 8) | m[off + 1];
        rdlen = (m[off + 8] << 8) | m[off + 9];
        off += 10;
        if (off + rdlen > len) return n;
        if (type == 1 && rdlen == 4)
            memcpy(&out[n++], m + off, 4);
        off += rdlen;
    }
    return n;
}

int probe_dns_ms(const char *server_v4, unsigned short port, const char *name, DWORD timeout_ms,
                 unsigned long *addrs, int *naddrs) {
    SOCKET s;
    struct sockaddr_in sa;
    unsigned char q[512], a[1500];
    int qlen, alen, ms = -1;
    unsigned short id = (unsigned short)((GetTickCount() ^ GetCurrentThreadId()) & 0xFFFF);
    DWORD tmo = timeout_ms;
    LARGE_INTEGER f, t0, t1;

    if (naddrs) *naddrs = 0;
    qlen = dns_build_query(q, sizeof(q), name, id);
    if (!qlen) return -1;

    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return -1;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof(tmo));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, server_v4, &sa.sin_addr) != 1) {
        closesocket(s);
        return -1;
    }
    QueryPerformanceFrequency(&f);
    for (int attempt = 0; attempt < 2 && ms < 0; attempt++) {
        QueryPerformanceCounter(&t0);
        if (sendto(s, (const char *)q, qlen, 0, (struct sockaddr *)&sa, sizeof(sa)) != qlen)
            break;
        alen = recv(s, (char *)a, sizeof(a), 0);
        QueryPerformanceCounter(&t1);
        if (alen >= 12 &&
            a[0] == q[0] && a[1] == q[1] &&     /* same ID */
            (a[2] & 0x80) &&                    /* is a response */
            (a[3] & 0x0F) == 0 &&               /* RCODE NOERROR */
            dns_answer_ok(a, alen)) {           /* real, non-forged A record */
            ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / f.QuadPart);
            if (ms < 1) ms = 1;
            if (addrs && naddrs) *naddrs = dns_collect_a(a, alen, addrs, 16);
        }
    }
    closesocket(s);
    return ms;
}

int probe_dns(const char *server_v4, unsigned short port, const char *name, DWORD timeout_ms) {
    return probe_dns_ms(server_v4, port, name, timeout_ms, NULL, NULL) >= 0;
}

/*
 * Learns the block-page addresses of the network's own resolver: asks it
 * (through the system resolver, cache bypassed) for sites that are commonly
 * blocked, and compares with a clean resolver. An address the network gives
 * for two or more different sites, which the clean resolver never gives for
 * them, is a block page. Writes up to max IPv4 addresses (dotted strings).
 */
int probe_learn_block_ips(const char *clean_v4, unsigned short clean_port,
                          char out[][16], int max) {
    typedef LONG (WINAPI *dnsq_fn)(PCSTR, WORD, DWORD, PVOID, PVOID *, PVOID *);
    typedef VOID (WINAPI *dnsfree_fn)(PVOID, int);
    static const char *canary[] = { "discord.com", "www.wattpad.com", "www.roblox.com",
                                    "pastebin.com", "imgur.com" };
    unsigned long isp[5][16], cand[16];
    int nisp[5] = { 0 }, ncand = 0, nout = 0;
    HMODULE h = LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    dnsq_fn q;
    dnsfree_fn fr;
    if (!h) return 0;
    q = (dnsq_fn)(void *)GetProcAddress(h, "DnsQuery_A");
    fr = (dnsfree_fn)(void *)GetProcAddress(h, "DnsRecordListFree");
    if (!q || !fr) { FreeLibrary(h); return 0; }

    for (int i = 0; i < 5; i++) {
        /* DNS_RECORDA layout: pNext, pName, wType, wDataLength, Flags, dwTtl, dwReserved, Data */
        struct rec { struct rec *next; char *name; WORD type, len; DWORD flags, ttl, res; DWORD ip; };
        PVOID list = NULL;
        if (q(canary[i], 1 /* A */, 0x8 /* BYPASS_CACHE */, NULL, &list, NULL) == 0 && list) {
            for (struct rec *r = (struct rec *)list; r && nisp[i] < 16; r = r->next)
                if (r->type == 1) isp[i][nisp[i]++] = r->ip;
            fr(list, 1);
        }
    }
    FreeLibrary(h);

    for (int i = 0; i < 5; i++)
        for (int k = 0; k < nisp[i]; k++) {
            int dup = 0;
            for (int c = 0; c < ncand; c++) dup |= cand[c] == isp[i][k];
            if (!dup && ncand < 16) cand[ncand++] = isp[i][k];
        }

    for (int c = 0; c < ncand && nout < max; c++) {
        int sites = 0, genuine = 0;
        for (int i = 0; i < 5; i++) {
            int has = 0;
            for (int k = 0; k < nisp[i]; k++) has |= isp[i][k] == cand[c];
            if (!has) continue;
            sites++;
            {
                unsigned long clean[16];
                int nclean = 0;
                probe_dns_ms(clean_v4, clean_port, canary[i], 1500, clean, &nclean);
                for (int k = 0; k < nclean; k++) genuine |= clean[k] == cand[c];
            }
        }
        if (sites >= 2 && !genuine) {
            unsigned char *b = (unsigned char *)&cand[c];
            snprintf(out[nout++], 16, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        }
    }
    return nout;
}

void probe_flush_dns_cache(void) {
    typedef BOOL (WINAPI *flush_fn)(void);
    HMODULE h = LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (h) {
        flush_fn f = (flush_fn)(void *)GetProcAddress(h, "DnsFlushResolverCache");
        if (f) f();
        FreeLibrary(h);
    }
}
