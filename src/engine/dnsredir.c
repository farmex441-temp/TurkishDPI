/*
 * DNS UDP Connection Tracker for GoodbyeDPI
 *
 * This is a simple connection tracker for DNS UDP data.
 * It's not a proper one. The caveats as follows:
 *    * Uses only source IP address and port as a hash key;
 *    * One-shot only. Removes conntrack record as soon as gets the reply;
 *    * Does not properly parse DNS request and response, only checks some bytes;
 *
 * But anyway, it works fine for DNS.
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>
#include <stdio.h>
#include "goodbyedpi.h"
#include "dnsredir.h"
#include "utils/uthash.h"

// Key format: '4'/'6' (IPv4/IPv6) + srcip[16] + srcport[2]
#define UDP_CONNRECORD_KEY_LEN 19

// Clean up old DNS connection records every 30 seconds
#define DNS_CLEANUP_INTERVAL_SEC 30

/* HACK!
 * uthash uses strlen() for HASH_FIND_STR.
 * We have null bytes in our key, so we can't use strlen()
 * And since it's always UDP_CONNRECORD_KEY_LEN bytes long,
 * we don't need to use any string function to determine length.
 */
#undef uthash_strlen
#define uthash_strlen(s) UDP_CONNRECORD_KEY_LEN

// DNS connection tracking record structure
typedef struct udp_connrecord {
    char key[UDP_CONNRECORD_KEY_LEN]; // Lookup key
    time_t time;         // Time when record was added
    uint32_t dstip[4];   // Destination IP (can be IPv4 or IPv6)
    uint16_t dstport;    // Destination port
    uint16_t txid;       // DNS transaction ID of the query (hybrid mode)
    uint8_t answered;    // an answer was already delivered (hybrid mode)
    UT_hash_handle hh;   // Makes this structure hashable with uthash
} udp_connrecord_t;

static time_t last_cleanup = 0;
static udp_connrecord_t *conntrack = NULL;
static int hybrid = 0;
static int builtins_added = 0;
static uint32_t clean_v4[4], clean_v6[4];
static uint16_t clean_v4port, clean_v6port;

void dns_set_clean(uint32_t v4, uint16_t v4port, const uint32_t v6[4], uint16_t v6port) {
    clean_v4[0] = v4;
    clean_v4port = v4port;
    memcpy(clean_v6, v6, sizeof(clean_v6));
    clean_v6port = v6port;
}

/* Known block-page answers; more are learned by the app and passed in. */
#define MAX_BLOCK_IPS 32
static uint32_t block_v4[MAX_BLOCK_IPS];
static int n_block_v4 = 0;
static struct in6_addr block_v6[MAX_BLOCK_IPS];
static int n_block_v6 = 0;

void dns_set_hybrid(int on) {
    hybrid = on;
    if (on && !builtins_added) {   /* independent of argument order */
        builtins_added = 1;
        dns_add_block_ip("195.175.254.2");          /* BTK / Turk Telekom block page */
        dns_add_block_ip("2a01:358:4014:a00::3");   /* same, IPv6 */
    }
}

int dns_hybrid_enabled(void) {
    return hybrid;
}

int dns_add_block_ip(const char *ip) {
    if (n_block_v4 < MAX_BLOCK_IPS && inet_pton(AF_INET, ip, &block_v4[n_block_v4]) == 1) {
        n_block_v4++;
        return 1;
    }
    if (n_block_v6 < MAX_BLOCK_IPS && inet_pton(AF_INET6, ip, &block_v6[n_block_v6]) == 1) {
        n_block_v6++;
        return 1;
    }
    return 0;
}

static int skip_name(const unsigned char *m, unsigned int len, unsigned int off) {
    while (off < len) {
        unsigned char l = m[off];
        if (l == 0) return (int)off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? (int)off + 2 : -1;
        if (l > 63) return -1;
        off += l + 1u;
    }
    return -1;
}

static int forged_v4(const unsigned char *a) {
    uint32_t v;
    if (a[0] == 0 || a[0] == 127 || a[0] == 10 || a[0] >= 224) return 1;
    if (a[0] == 192 && a[1] == 168) return 1;
    if (a[0] == 172 && a[1] >= 16 && a[1] <= 31) return 1;
    if (a[0] == 169 && a[1] == 254) return 1;
    memcpy(&v, a, 4);
    for (int i = 0; i < n_block_v4; i++)
        if (block_v4[i] == v) return 1;
    return 0;
}

static int forged_v6(const unsigned char *a) {
    static const unsigned char zero[16] = { 0 };
    if (!memcmp(a, zero, 16)) return 1;                       /* :: */
    if (!memcmp(a, zero, 15) && a[15] == 1) return 1;         /* ::1 */
    for (int i = 0; i < n_block_v6; i++)
        if (!memcmp(a, &block_v6[i], 16)) return 1;
    return 0;
}

/*
 * 1 if the answer must not be trusted: an error code (NXDOMAIN, SERVFAIL,
 * REFUSED - common ways of censoring a name) or an address that is a block
 * page, a sinkhole or a private address. A clean resolver's answer will
 * follow within milliseconds, so dropping costs nothing for real answers.
 */
int dns_answer_is_forged(const char *data, unsigned int len) {
    const unsigned char *m = (const unsigned char *)data;
    unsigned int qd, an;
    int off = 12;
    if (len < 12) return 1;
    if ((m[3] & 0x0F) != 0) return 1;
    qd = (m[4] << 8) | m[5];
    an = (m[6] << 8) | m[7];
    for (unsigned int i = 0; i < qd; i++) {
        off = skip_name(m, len, (unsigned int)off);
        if (off < 0 || (unsigned int)off + 4 > len) return 1;   /* malformed: untrusted */
        off += 4;
    }
    for (unsigned int i = 0; i < an; i++) {
        unsigned int type, rdlen;
        off = skip_name(m, len, (unsigned int)off);
        if (off < 0 || (unsigned int)off + 10 > len) return 1;
        type = (m[off] << 8) | m[off + 1];
        rdlen = (m[off + 8] << 8) | m[off + 9];
        off += 10;
        if ((unsigned int)off + rdlen > len) return 1;
        if (type == 1 && rdlen == 4 && forged_v4(m + off)) return 1;
        if (type == 28 && rdlen == 16 && forged_v6(m + off)) return 1;
        off += (int)rdlen;
    }
    return 0;
}

/**
 * Flush DNS resolver cache in Windows
 * Ensures redirected DNS changes take effect immediately
 */
void flush_dns_cache() {
    typedef BOOL (WINAPI *flush_fn)(void);
    flush_fn flush;
    /* TurkishDPI: System32 only. dnsapi.dll is not a KnownDLL, so a plain
     * LoadLibrary would search the (possibly user-writable) exe folder first
     * from this elevated process. */
    HMODULE dnsapi = LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dnsapi == NULL)
    {
        printf("Can't load dnsapi.dll to flush DNS cache!\n");
        exit(EXIT_FAILURE);
    }

    flush = (flush_fn)(void *)GetProcAddress(dnsapi, "DnsFlushResolverCache");
    if (flush == NULL || !flush())
        printf("Can't flush DNS cache!");
    FreeLibrary(dnsapi);
}

/**
 * Fill key data structure with connection information
 */
inline static void fill_key_data(char *key, const uint8_t is_ipv6, const uint32_t srcip[4],
                          const uint16_t srcport)
{
    /* The key is a packed byte string: copy with memcpy, never through
     * misaligned uint32_t/uint16_t pointers (undefined behaviour). */
    uint32_t ip[4] = { 0, 0, 0, 0 };
    if (is_ipv6) {
        key[0] = '6';
        ipv6_copy_addr(ip, srcip);
    }
    else {
        key[0] = '4';
        ipv4_copy_addr(ip, srcip);
    }
    memcpy(key + 1, ip, sizeof(ip));
    memcpy(key + 1 + sizeof(ip), &srcport, sizeof(srcport));
}

/**
 * Extract connection information from key data
 */
inline static void fill_data_from_key(uint8_t *is_ipv6, uint32_t srcip[4], uint16_t *srcport,
                                      const char *key)
{
    uint32_t ip[4];
    memcpy(ip, key + 1, sizeof(ip));
    if (key[0] == '6') {
        *is_ipv6 = 1;
        ipv6_copy_addr(srcip, ip);
    }
    else {
        *is_ipv6 = 0;
        ipv4_copy_addr(srcip, ip);
    }
    memcpy(srcport, key + 1 + sizeof(ip), sizeof(*srcport));
}

/**
 * Construct a connection tracking key from connection parameters
 */
inline static void construct_key(const uint32_t srcip[4], const uint16_t srcport,
                                 char *key, const uint8_t is_ipv6)
{
    debug("Construct key enter\n");
    if (key) {
        debug("Constructing key\n");
        fill_key_data(key, is_ipv6, srcip, srcport);
    }
    debug("Construct key end\n");
}

/**
 * Extract connection info from key and record for use in the application
 */
inline static void deconstruct_key(const char *key, const udp_connrecord_t *connrecord,
                                   conntrack_info_t *conn_info)
{
    debug("Deconstruct key enter\n");
    if (key && conn_info) {
        debug("Deconstructing key\n");
        fill_data_from_key(&conn_info->is_ipv6, conn_info->srcip,
                           &conn_info->srcport, key);

        if (conn_info->is_ipv6)
            ipv6_copy_addr(conn_info->dstip, connrecord->dstip);
        else
            ipv4_copy_addr(conn_info->dstip, connrecord->dstip);

        conn_info->dstport = connrecord->dstport;
    }
    debug("Deconstruct key end\n");
}

/**
 * Check if a connection exists in the tracking table
 */
static int check_get_udp_conntrack_key(const char *key, udp_connrecord_t **connrecord) {
    udp_connrecord_t *tmp_connrecord = NULL;
    if (!conntrack) return FALSE;

    HASH_FIND_STR(conntrack, key, tmp_connrecord);
    if (tmp_connrecord) {
        if (connrecord)
            *connrecord = tmp_connrecord;
        debug("check_get_udp_conntrack_key found key\n");
        return TRUE;
    }
    debug("check_get_udp_conntrack_key key not found\n");
    return FALSE;
}

/**
 * Add a new DNS connection to the tracking table
 */
static int add_udp_conntrack(const uint32_t srcip[4], const uint16_t srcport,
                             const uint32_t dstip[4], const uint16_t dstport,
                             const uint8_t is_ipv6, const uint16_t txid
                            )
{
    udp_connrecord_t *existing = NULL;
    if (!(srcip && srcport && dstip && dstport))
        return FALSE;

    udp_connrecord_t *tmp_connrecord = malloc(sizeof(udp_connrecord_t));
    if (!tmp_connrecord)
        return FALSE;
    construct_key(srcip, srcport, tmp_connrecord->key, is_ipv6);

    if (check_get_udp_conntrack_key(tmp_connrecord->key, &existing) && existing) {
        /* Same local port, new query: refresh the record instead of losing it. */
        existing->time = time(NULL);
        if (is_ipv6) ipv6_copy_addr(existing->dstip, dstip);
        else ipv4_copy_addr(existing->dstip, dstip);
        existing->dstport = dstport;
        existing->txid = txid;
        existing->answered = 0;
        free(tmp_connrecord);
        return TRUE;
    }
    {
        tmp_connrecord->time = time(NULL);
        tmp_connrecord->txid = txid;
        tmp_connrecord->answered = 0;

        if (is_ipv6) {
            ipv6_copy_addr(tmp_connrecord->dstip, dstip);
        }
        else {
            ipv4_copy_addr(tmp_connrecord->dstip, dstip);
        }
        tmp_connrecord->dstport = dstport;
        HASH_ADD_STR(conntrack, key, tmp_connrecord);
        debug("Added UDP conntrack\n");
        return TRUE;
    }
}

/**
 * Periodically clean up old DNS connection records
 */
static void dns_cleanup() {
    udp_connrecord_t *tmp_connrecord, *tmp_connrecord2 = NULL;

    if (last_cleanup == 0) {
        last_cleanup = time(NULL);
        return;
    }

    if (difftime(time(NULL), last_cleanup) >= DNS_CLEANUP_INTERVAL_SEC) {
        last_cleanup = time(NULL);

        HASH_ITER(hh, conntrack, tmp_connrecord, tmp_connrecord2) {
            if (difftime(last_cleanup, tmp_connrecord->time) >= DNS_CLEANUP_INTERVAL_SEC) {
                HASH_DEL(conntrack, tmp_connrecord);
                free(tmp_connrecord);
            }
        }
    }
}

/**
 * Check if a packet looks like a DNS request/response
 */
int dns_is_dns_packet(const char *packet_data, const UINT packet_dataLen, const int outgoing) {
    if (packet_dataLen < 16) return FALSE;

    const unsigned char *m = (const unsigned char *)packet_data;
    unsigned int flags = ((unsigned int)m[2] << 8) | m[3];
    /* A query: QR=0, standard opcode, no answers and no authority records. */
    if (outgoing && (flags & 0xFA00) == 0 &&
        m[6] == 0 && m[7] == 0 && m[8] == 0 && m[9] == 0) {
        return TRUE;
    }
    /* A response: QR=1, standard opcode. */
    else if (!outgoing && (flags & 0xF800) == 0x8000) {
        return TRUE;
    }
    return FALSE;
}

/**
 * Handle outgoing DNS requests
 * Tracks them for later correlation with responses
 */
int dns_handle_outgoing(const uint32_t srcip[4], const uint16_t srcport,
                        const uint32_t dstip[4], const uint16_t dstport,
                        const char *packet_data, const UINT packet_dataLen,
                        const uint8_t is_ipv6)
{
    if (packet_dataLen < 16)
        return FALSE;

    dns_cleanup();

    if (dns_is_dns_packet(packet_data, packet_dataLen, 1)) {
        /* Looks like DNS request */
        debug("trying to add srcport = %hu, dstport = %hu\n", ntohs(srcport), ntohs(dstport));
        return add_udp_conntrack(srcip, srcport, dstip, dstport, is_ipv6,
                                 (uint16_t)(((unsigned char)packet_data[0] << 8) |
                                            (unsigned char)packet_data[1]));
    }
    debug("____dns_handle_outgoing FALSE: srcport = %hu, dstport = %hu\n", ntohs(srcport), ntohs(dstport));
    return FALSE;
}

/**
 * Handle incoming DNS responses
 * Correlates them with tracked requests and provides redirection info
 */
int dns_handle_incoming(const uint32_t srcip[4], const uint16_t srcport,
                        const char *packet_data, const UINT packet_dataLen,
                        conntrack_info_t *conn_info, const uint8_t is_ipv6)
{
    char key[UDP_CONNRECORD_KEY_LEN];
    udp_connrecord_t *tmp_connrecord = NULL;

    if (packet_dataLen < 16 || !conn_info)
        return FALSE;

    dns_cleanup();

    if (dns_is_dns_packet(packet_data, packet_dataLen, 0)) {
        /* Looks like DNS response */
        construct_key(srcip, srcport, key, is_ipv6);
        if (check_get_udp_conntrack_key(key, &tmp_connrecord) && tmp_connrecord) {
            /* Connection exists in conntrack, moving on */
            deconstruct_key(key, tmp_connrecord, conn_info);
            HASH_DEL(conntrack, tmp_connrecord);
            free(tmp_connrecord);
            return TRUE;
        }
    }
    debug("____dns_handle_incoming FALSE: srcport = %hu\n", ntohs(srcport));
    return FALSE;
}

static int same_addr(const uint32_t a[4], const uint32_t b[4], int is_ipv6) {
    return is_ipv6 ? !memcmp(a, b, 16) : a[0] == b[0];
}

int dns_handle_incoming_hybrid(const uint32_t local_ip[4], const uint16_t local_port,
                               const uint32_t remote_ip[4], const uint16_t remote_port,
                               const char *packet_data, const UINT packet_dataLen,
                               conntrack_info_t *conn_info, const uint8_t is_ipv6)
{
    char key[UDP_CONNRECORD_KEY_LEN];
    udp_connrecord_t *rec = NULL;
    uint16_t txid;

    if (packet_dataLen < 12 || !conn_info || !dns_is_dns_packet(packet_data, packet_dataLen, 0))
        return DNS_IN_NOT_OURS;
    dns_cleanup();
    construct_key(local_ip, local_port, key, is_ipv6);
    if (!check_get_udp_conntrack_key(key, &rec) || !rec)
        return DNS_IN_NOT_OURS;
    txid = (uint16_t)(((unsigned char)packet_data[0] << 8) | (unsigned char)packet_data[1]);
    if (txid != rec->txid)
        return DNS_IN_NOT_OURS;

    if (same_addr(remote_ip, is_ipv6 ? clean_v6 : clean_v4, is_ipv6) &&
        remote_port == (is_ipv6 ? clean_v6port : clean_v4port)) {
        /* From the clean resolver: make it look like the original resolver answered. */
        if (rec->answered)
            return DNS_IN_DROP;
        rec->answered = 1;
        deconstruct_key(key, rec, conn_info);
        return DNS_IN_REWRITE;
    }
    /* Forged answers are dropped whoever sends them. */
    if (dns_answer_is_forged(packet_data, packet_dataLen))
        return DNS_IN_DROP;
    if (same_addr(remote_ip, rec->dstip, is_ipv6) && remote_port == rec->dstport) {
        /* the resolver the query went to: the first trustworthy answer wins */
        if (rec->answered)
            return DNS_IN_DROP;
        rec->answered = 1;
        return DNS_IN_PASS;
    }
    /* Someone else (another resolver the system asked, or a spoofer): it
     * neither settles the race nor counts as trusted. */
    return DNS_IN_PASS_OTHER;
}

int dns_answer_matches(const uint32_t local_ip[4], const uint16_t local_port,
                       const uint32_t remote_ip[4], const uint16_t remote_port,
                       const char *packet_data, const UINT packet_dataLen, const uint8_t is_ipv6)
{
    char key[UDP_CONNRECORD_KEY_LEN];
    udp_connrecord_t *rec = NULL;
    uint16_t txid;
    if (packet_dataLen < 12 || !dns_is_dns_packet(packet_data, packet_dataLen, 0))
        return 0;
    construct_key(local_ip, local_port, key, is_ipv6);
    if (!check_get_udp_conntrack_key(key, &rec) || !rec)
        return 0;
    txid = (uint16_t)(((unsigned char)packet_data[0] << 8) | (unsigned char)packet_data[1]);
    return txid == rec->txid && same_addr(remote_ip, rec->dstip, is_ipv6) &&
           remote_port == rec->dstport;
}
