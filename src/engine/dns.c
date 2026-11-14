/*
 * TurkishDPI - DNS redirection with the hybrid race (see dns.h).
 *
 * Questions are remembered in a small open-addressing table keyed by the
 * asking socket (address, port) and the transaction ID. An entry lives for a
 * few seconds - long enough for both answers of the race - and is replaced
 * when the same socket asks again with the same ID.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string.h>
#include "syslib.h"
#include "dns.h"

#define DNS_SLOTS     2048          /* power of two */
#define DNS_TTL_MS    6000          /* a question is answered within this */
#define DNS_PROBES    16            /* linear probing limit */
#define MAX_BLOCK     32

typedef struct {
    uint8_t  used, is_ipv6, answered;
    uint16_t txid;
    uint32_t local_ip[4];
    uint16_t local_port;
    uint32_t server_ip[4];          /* where the program sent it */
    uint16_t server_port;
    ULONGLONG at;
} dns_entry;

static dns_entry table[DNS_SLOTS];
static int hybrid;
static uint32_t clean_v4, clean_v6[4];
static uint16_t clean_v4_port, clean_v6_port;
static uint32_t block_v4[MAX_BLOCK];
static uint8_t  block_v6[MAX_BLOCK][16];
static int n_block_v4, n_block_v6, builtins_added;

/* ---------- settings ---------- */
void dns_set_hybrid(int on) {
    hybrid = on ? 1 : 0;
    if (on && !builtins_added) {
        /* the BTK block page all Turkish ISPs answer with, whatever the order
         * of the command-line options */
        builtins_added = 1;
        dns_add_block_ip("195.175.254.2");
        dns_add_block_ip("2a01:358:4014:a00::3");
    }
}

int dns_hybrid_enabled(void) { return hybrid; }

void dns_set_clean(uint32_t v4, uint16_t v4port, const uint32_t v6[4], uint16_t v6port) {
    clean_v4 = v4;
    clean_v4_port = v4port;
    memcpy(clean_v6, v6, sizeof(clean_v6));
    clean_v6_port = v6port;
}

int dns_add_block_ip(const char *ip) {
    struct in_addr a4;
    struct in6_addr a6;
    if (!ip) return 0;
    if (inet_pton(AF_INET, ip, &a4) == 1) {
        if (n_block_v4 < MAX_BLOCK) memcpy(&block_v4[n_block_v4++], &a4, 4);
        return 1;
    }
    if (inet_pton(AF_INET6, ip, &a6) == 1) {
        if (n_block_v6 < MAX_BLOCK) memcpy(block_v6[n_block_v6++], &a6, 16);
        return 1;
    }
    return 0;
}

/* ---------- message parsing ---------- */
static unsigned rd16(const unsigned char *p) { return (unsigned)(p[0] << 8 | p[1]); }

/* Offset just past a (possibly compressed) name, or -1. */
static int name_end(const unsigned char *m, unsigned len, unsigned off) {
    for (int labels = 0; labels < 128 && off < len; labels++) {
        unsigned l = m[off];
        if (l == 0) return (int)off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? (int)off + 2 : -1;
        if (l > 63) return -1;
        off += l + 1;
    }
    return -1;
}

int dns_is_dns_packet(const char *data, unsigned int len, int outgoing) {
    const unsigned char *m = (const unsigned char *)data;
    if (!data || len < 12) return 0;
    if (outgoing) return (m[2] & 0x80) == 0 && rd16(m + 4) >= 1;   /* question, QDCOUNT >= 1 */
    return (m[2] & 0x80) != 0;                                     /* answer */
}

/* RFC 1918 and link-local IPv4: what a LAN or VPN resolver may answer with. */
static int lan_v4(const unsigned char *a) {
    return a[0] == 10 || (a[0] == 192 && a[1] == 168) ||
           (a[0] == 172 && a[1] >= 16 && a[1] <= 31) || (a[0] == 169 && a[1] == 254);
}

/* An address no public answer should carry. lan_ok: the answer comes from a
 * resolver that is itself local (see resolver_is_local), so LAN addresses are
 * legitimate; 0/8, loopback, multicast and block-list addresses never are. */
static int bad_v4(const unsigned char *a, int lan_ok) {
    uint32_t v;
    if (a[0] == 0 || a[0] == 127 || a[0] >= 224) return 1;
    if (!lan_ok && lan_v4(a)) return 1;
    memcpy(&v, a, 4);
    for (int i = 0; i < n_block_v4; i++)
        if (block_v4[i] == v) return 1;
    return 0;
}

static int bad_v6(const unsigned char *a) {
    static const unsigned char zero[16];
    if (!memcmp(a, zero, 16)) return 1;                          /* :: */
    if (!memcmp(a, zero, 15) && a[15] == 1) return 1;            /* ::1 */
    for (int i = 0; i < n_block_v6; i++)
        if (!memcmp(a, block_v6[i], 16)) return 1;
    return 0;
}

/* A resolver on the LAN, a VPN or this machine: RFC 1918, 169.254/16, 127/8;
 * IPv6 ULA fc00::/7, link-local fe80::/10, ::1. Such a resolver answers local
 * names (router pages, NAS, printers, intranet) with LAN addresses. CGNAT
 * 100.64/10 is deliberately not local: there it is usually the carrier's own
 * (censoring) resolver.
 * Residual risk: a home router forwards its ISP's answers, so a censor that
 * answered with an RFC 1918 / 169.254 address would now be accepted unless
 * that address is on the block list (built-in, --dns-block-ip, learned per
 * network by the app). Turkish block pages are public addresses. */
static int resolver_is_local(const uint32_t ip[4], int v6) {
    unsigned char a[16];
    memcpy(a, ip, v6 ? 16 : 4);
    if (!v6)
        return lan_v4(a) || a[0] == 127;
    if ((a[0] & 0xFE) == 0xFC) return 1;                          /* fc00::/7 */
    if (a[0] == 0xFE && (a[1] & 0xC0) == 0x80) return 1;          /* fe80::/10 */
    {
        static const unsigned char zero[15];
        return !memcmp(a, zero, 15) && a[15] == 1;                /* ::1 */
    }
}

/* lan_ok: see bad_v4. IPv6 ULA/link-local answers were never forged, so
 * AAAA records need no such switch. */
static int answer_forged(const char *data, unsigned int len, int lan_ok) {
    const unsigned char *m = (const unsigned char *)data;
    unsigned qd, an;
    int off = 12;
    if (!data || len < 12) return 1;
    if ((m[3] & 0x0F) != 0) return 1;      /* NXDOMAIN, SERVFAIL, REFUSED...: untrusted */
    qd = rd16(m + 4);
    an = rd16(m + 6);
    for (unsigned i = 0; i < qd; i++) {
        off = name_end(m, len, (unsigned)off);
        if (off < 0 || (unsigned)off + 4 > len) return 1;
        off += 4;
    }
    for (unsigned i = 0; i < an; i++) {
        unsigned type, rdlen;
        off = name_end(m, len, (unsigned)off);
        if (off < 0 || (unsigned)off + 10 > len) return 1;
        type = rd16(m + off);
        rdlen = rd16(m + off + 8);
        off += 10;
        if ((unsigned)off + rdlen > len) return 1;
        if (type == 1 && rdlen == 4 && bad_v4(m + off, lan_ok)) return 1;
        if (type == 28 && rdlen == 16 && bad_v6(m + off)) return 1;
        off += (int)rdlen;
    }
    return 0;
}

int dns_answer_is_forged(const char *data, unsigned int len) {
    return answer_forged(data, len, 0);
}

/* ---------- the question table ---------- */
static int addr_eq(const uint32_t a[4], const uint32_t b[4], int v6) {
    return v6 ? !memcmp(a, b, 16) : a[0] == b[0];
}

static unsigned slot_of(const uint32_t ip[4], uint16_t port, uint16_t txid, int v6) {
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)ip;
    for (int i = 0; i < (v6 ? 16 : 4); i++) h = (h ^ p[i]) * 16777619u;
    h = (h ^ port) * 16777619u;
    h = (h ^ txid) * 16777619u;
    return h & (DNS_SLOTS - 1);
}

static dns_entry *find(const uint32_t ip[4], uint16_t port, uint16_t txid, int v6, ULONGLONG now) {
    unsigned s = slot_of(ip, port, txid, v6);
    for (int i = 0; i < DNS_PROBES; i++) {
        dns_entry *e = &table[(s + i) & (DNS_SLOTS - 1)];
        if (!e->used) return NULL;
        if (now - e->at <= DNS_TTL_MS && e->is_ipv6 == v6 && e->txid == txid &&
            e->local_port == port && addr_eq(e->local_ip, ip, v6))
            return e;
    }
    return NULL;
}

static dns_entry *insert(const uint32_t ip[4], uint16_t port, uint16_t txid, int v6, ULONGLONG now) {
    unsigned s = slot_of(ip, port, txid, v6);
    dns_entry *victim = NULL;
    for (int i = 0; i < DNS_PROBES; i++) {
        dns_entry *e = &table[(s + i) & (DNS_SLOTS - 1)];
        if (!e->used || now - e->at > DNS_TTL_MS ||
            (e->is_ipv6 == v6 && e->txid == txid && e->local_port == port && addr_eq(e->local_ip, ip, v6)))
            return e;                                  /* free, expired, or the same question again */
        if (!victim || e->at < victim->at) victim = e;
    }
    return victim;                                     /* full run: replace the oldest */
}

static ULONGLONG now_ms(void) { return GetTickCount64(); }

int dns_handle_outgoing(const uint32_t srcip[4], uint16_t srcport,
                        const uint32_t dstip[4], uint16_t dstport,
                        const char *data, unsigned int len, uint8_t is_ipv6) {
    const unsigned char *m = (const unsigned char *)data;
    ULONGLONG now = now_ms();
    dns_entry *e;
    if (!dns_is_dns_packet(data, len, 1)) return 0;
    e = insert(srcip, srcport, (uint16_t)rd16(m), is_ipv6, now);
    memset(e, 0, sizeof(*e));
    e->used = 1;
    e->is_ipv6 = is_ipv6;
    e->txid = (uint16_t)rd16(m);
    memcpy(e->local_ip, srcip, is_ipv6 ? 16 : 4);
    e->local_port = srcport;
    memcpy(e->server_ip, dstip, is_ipv6 ? 16 : 4);
    e->server_port = dstport;
    e->at = now;
    return 1;
}

static void fill(conntrack_info_t *ci, const dns_entry *e) {
    memset(ci, 0, sizeof(*ci));
    ci->is_ipv6 = e->is_ipv6;
    memcpy(ci->srcip, e->local_ip, e->is_ipv6 ? 16 : 4);
    ci->srcport = e->local_port;
    memcpy(ci->dstip, e->server_ip, e->is_ipv6 ? 16 : 4);
    ci->dstport = e->server_port;
}

int dns_handle_incoming(const uint32_t local_ip[4], uint16_t local_port,
                        const char *data, unsigned int len,
                        conntrack_info_t *ci, uint8_t is_ipv6) {
    dns_entry *e;
    if (!dns_is_dns_packet(data, len, 0)) return 0;
    e = find(local_ip, local_port, (uint16_t)rd16((const unsigned char *)data), is_ipv6, now_ms());
    if (!e) return 0;
    fill(ci, e);
    e->answered = 1;
    return 1;
}

static int from_clean(const uint32_t ip[4], uint16_t port, int v6) {
    return v6 ? (port == clean_v6_port && !memcmp(ip, clean_v6, 16))
              : (port == clean_v4_port && ip[0] == clean_v4);
}

int dns_handle_incoming_hybrid(const uint32_t local_ip[4], uint16_t local_port,
                               const uint32_t remote_ip[4], uint16_t remote_port,
                               const char *data, unsigned int len,
                               conntrack_info_t *ci, uint8_t is_ipv6) {
    dns_entry *e;
    if (!dns_is_dns_packet(data, len, 0)) return DNS_IN_NOT_OURS;
    e = find(local_ip, local_port, (uint16_t)rd16((const unsigned char *)data), is_ipv6, now_ms());
    if (!e) return DNS_IN_NOT_OURS;
    if (from_clean(remote_ip, remote_port, is_ipv6)) {
        /* The clean resolver's first answer always reaches the program, also
         * NXDOMAIN or a private address: a name that does not exist (or a LAN
         * name like plex.direct) must fail or resolve at once instead of
         * waiting for Windows' DNS timeout. The caller decides about trust
         * (payment learning skips answers that look forged). */
        if (e->answered) return DNS_IN_DROP;
        e->answered = 1;
        fill(ci, e);
        return DNS_IN_REWRITE;
    }
    if (remote_port == e->server_port && addr_eq(remote_ip, e->server_ip, is_ipv6)) {
        /* The resolver the question went to. When that address (taken from
         * the question, which this machine sent) is itself local - router,
         * VPN, local filter - LAN addresses in its answer are legitimate:
         * local names must keep resolving. Error codes, block-list addresses,
         * 0/8, loopback and multicast stay forged for every resolver. */
        if (e->answered || answer_forged(data, len, resolver_is_local(e->server_ip, e->is_ipv6)))
            return DNS_IN_DROP;                           /* the clean answer follows */
        e->answered = 1;
        return DNS_IN_PASS;
    }
    /* someone else: never trusted, never ends the race */
    return dns_answer_is_forged(data, len) ? DNS_IN_DROP : DNS_IN_PASS_OTHER;
}

int dns_answer_matches(const uint32_t local_ip[4], uint16_t local_port,
                       const uint32_t remote_ip[4], uint16_t remote_port,
                       const char *data, unsigned int len, uint8_t is_ipv6) {
    dns_entry *e;
    if (!dns_is_dns_packet(data, len, 0)) return 0;
    e = find(local_ip, local_port, (uint16_t)rd16((const unsigned char *)data), is_ipv6, now_ms());
    return e && remote_port == e->server_port && addr_eq(remote_ip, e->server_ip, is_ipv6);
}

void flush_dns_cache(void) {
    typedef BOOL (WINAPI *flush_fn)(void);
    HMODULE h = load_system_dll(L"dnsapi.dll");
    if (h) {
        flush_fn f = (flush_fn)(void *)GetProcAddress(h, "DnsFlushResolverCache");
        if (f) f();
    }
}
