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
#include "sysdll.h"
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
    /* A plain browser-like agent: the probed sites need not learn that
     * TurkishDPI is in use (the ISP cannot see it anyway, it is inside TLS). */
    s = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
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

/* RtlGenRandom (advapi32!SystemFunction036), resolved by hand: its header
 * declaration differs between the MinGW and Microsoft SDKs. */
static int random_bytes(void *buf, ULONG len) {
    typedef BOOLEAN (WINAPI *rtlgen_fn)(PVOID, ULONG);
    HMODULE adv = GetModuleHandleW(L"advapi32.dll");   /* linked, always loaded */
    rtlgen_fn f = adv ? (rtlgen_fn)(void *)GetProcAddress(adv, "SystemFunction036") : NULL;
    return f && f(buf, len);
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
    unsigned short id;
    DWORD tmo = timeout_ms;
    LARGE_INTEGER f, t0, t1;

    if (naddrs) *naddrs = 0;
    /* Unpredictable transaction ID (an off-path sender must not be able to
     * forge the answer that decides which resolver is used). */
    if (!random_bytes(&id, sizeof(id)))
        id = (unsigned short)((GetTickCount() ^ GetCurrentThreadId()) & 0xFFFF);
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
    /* Connected socket: answers from any other address/port are discarded
     * by the stack. */
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        closesocket(s);
        return -1;
    }
    QueryPerformanceFrequency(&f);
    for (int attempt = 0; attempt < 2 && ms < 0; attempt++) {
        QueryPerformanceCounter(&t0);
        if (send(s, (const char *)q, qlen, 0) != qlen)
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
#define SYSDNS_BYPASS_CACHE 0x00000008u   /* DNS_QUERY_BYPASS_CACHE */
#define SYSDNS_TCP_ONLY     0x00000002u   /* DNS_QUERY_USE_TCP_ONLY */

/*
 * A records of name from the system's (= the network's) resolver, cache
 * bypassed. Returns the number of addresses (0..max), or -1 if the query
 * itself failed. The one place that loads dnsapi.dll and walks DNS_RECORDA,
 * whose layout is declared by hand (verified for x86 and x64):
 * pNext, pName, wType, wDataLength, Flags, dwTtl, dwReserved, Data.
 */
static int system_dns_a(const char *name, DWORD flags, unsigned long *ips, int max) {
    typedef LONG (WINAPI *dnsq_fn)(PCSTR, WORD, DWORD, PVOID, PVOID *, PVOID *);
    typedef VOID (WINAPI *dnsfree_fn)(PVOID, int);
    struct rec { struct rec *next; char *name; WORD type, len; DWORD flags, ttl, res; DWORD ip; };
    HMODULE h = LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    dnsq_fn q;
    dnsfree_fn fr;
    PVOID list = NULL;
    int n = -1;
    if (!h) return -1;
    q = (dnsq_fn)(void *)GetProcAddress(h, "DnsQuery_A");
    fr = (dnsfree_fn)(void *)GetProcAddress(h, "DnsRecordListFree");
    if (q && fr && q(name, 1 /* A */, flags | SYSDNS_BYPASS_CACHE, NULL, &list, NULL) == 0) {
        n = 0;
        for (struct rec *r = (struct rec *)list; r && n < max; r = r->next)
            if (r->type == 1) ips[n++] = r->ip;
        if (list) fr(list, 1);
    }
    FreeLibrary(h);
    return n;
}

int probe_learn_block_ips(const char *clean_v4, unsigned short clean_port,
                          char out[][16], int max) {
    static const char *canary[] = { "discord.com", "www.wattpad.com", "www.roblox.com",
                                    "pastebin.com", "imgur.com" };
    unsigned long isp[5][16], cand[16];
    int nisp[5] = { 0 }, ncand = 0, nout = 0;

    for (int i = 0; i < 5; i++) {
        int n = system_dns_a(canary[i], 0, isp[i], 16);
        nisp[i] = n > 0 ? n : 0;
    }

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

/* ---------- facts for diagnosing one site (classified in diag.c) ---------- */

/* TCP connection set-up to ip:port (ip in network byte order). ms, or -1;
 * -2 if no socket could be created (cannot tell). */
int probe_tcp_connect_ms(unsigned long ip, unsigned short port, DWORD timeout_ms) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa;
    u_long nb = 1;
    fd_set w, e;
    struct timeval tv;
    LARGE_INTEGER f, t0, t1;
    int ms = -1;
    if (s == INVALID_SOCKET) return -2;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = ip;
    ioctlsocket(s, FIONBIO, &nb);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    connect(s, (struct sockaddr *)&sa, sizeof(sa));
    FD_ZERO(&w); FD_SET(s, &w);
    FD_ZERO(&e); FD_SET(s, &e);
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    if (select(0, NULL, &w, &e, &tv) == 1 && FD_ISSET(s, &w)) {
        int err = 0, l = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &l);
        if (err == 0) {
            QueryPerformanceCounter(&t1);
            ms = (int)((t1.QuadPart - t0.QuadPart) * 1000 / f.QuadPart);
            if (ms < 1) ms = 1;
        }
    }
    closesocket(s);
    return ms;
}

static int in_list(unsigned long ip, const char (*list)[16], int n) {
    for (int i = 0; i < n; i++) {
        struct in_addr a;
        if (inet_pton(AF_INET, list[i], &a) == 1 && a.s_addr == ip) return 1;
    }
    return 0;
}

typedef struct { const wchar_t *host; DWORD timeout_ms; int ok; } https_bg_t;

static DWORD WINAPI https_bg(LPVOID p) {
    https_bg_t *j = (https_bg_t *)p;
    const wchar_t *hs[1] = { j->host };
    j->ok = probe_https_many(hs, 1, j->timeout_ms, NULL, NULL) == 1;
    return 0;
}

void probe_collect_facts(const wchar_t *host, const char *clean_v4, unsigned short clean_port,
                         const char (*block_ips)[16], int nblock, DWORD timeout_ms,
                         diag_facts_t *out) {
    char name[256];
    unsigned long sys_ip[16], clean_ip[16], try_ip[2];
    int ntry = 0, n;
    https_bg_t job = { host, timeout_ms, 0 };
    HANDLE th;

    memset(out, 0, sizeof(*out));
    if (WideCharToMultiByte(CP_ACP, 0, host, -1, name, sizeof(name), NULL, NULL) <= 0) return;

    /* The full HTTPS request takes longest: it runs while the facts below
     * are collected instead of after them. */
    th = CreateThread(NULL, 64 * 1024, https_bg, &job, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);

    /* The network's own resolver. Over TCP first: the engine diverts only UDP
     * port 53, so this sees the network's real (possibly forged) answers even
     * while protection runs (in hybrid mode the engine drops forged UDP
     * answers before the system sees them). Resolvers that refuse TCP fall
     * back to the normal query. */
    n = system_dns_a(name, SYSDNS_TCP_ONLY, sys_ip, 16);
    if (n < 0) n = system_dns_a(name, 0, sys_ip, 16);
    out->sys_n = n > 0 ? n : 0;
    for (int i = 0; i < out->sys_n; i++)
        if (bogus_ipv4((const unsigned char *)&sys_ip[i]) || in_list(sys_ip[i], block_ips, nblock))
            out->sys_bogus++;
    /* the clean resolver */
    probe_dns_ms(clean_v4, clean_port, name, timeout_ms, clean_ip, &out->clean_n);

    /* TCP to the real addresses: the clean resolver's, else the network's
     * non-forged ones. Two are enough to tell "this address is dead". */
    for (int i = 0; i < out->clean_n && ntry < 2; i++) try_ip[ntry++] = clean_ip[i];
    for (int i = 0; i < out->sys_n && ntry < 2; i++)
        if (!bogus_ipv4((const unsigned char *)&sys_ip[i]) && !in_list(sys_ip[i], block_ips, nblock))
            try_ip[ntry++] = sys_ip[i];
    for (int i = 0; i < ntry; i++) {
        out->tcp_tried++;
        if (probe_tcp_connect_ms(try_ip[i], 443, timeout_ms) >= 0) { out->tcp_ok = 1; break; }
    }
    if (th) {
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    } else {
        https_bg(&job);
    }
    out->https_ok = job.ok;
}
