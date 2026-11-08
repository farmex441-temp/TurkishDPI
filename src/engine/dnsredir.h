#ifndef _DNSREDIR_H
#define _DNSREDIR_H
#include <stdint.h>

// Connection tracking information for DNS requests
typedef struct conntrack_info {
    uint8_t  is_ipv6;      // Flag indicating if IPv6 (1) or IPv4 (0)
    uint32_t srcip[4];     // Source IP address (supports both IPv4 and IPv6)
    uint16_t srcport;      // Source port
    uint32_t dstip[4];     // Destination IP address (supports both IPv4 and IPv6)
    uint16_t dstport;      // Destination port
} conntrack_info_t;

// Helper function to copy IPv4 address (uses only the first element in the array)
inline static void ipv4_copy_addr(uint32_t dst[4], const uint32_t src[4]) {
    dst[0] = src[0];
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
}

// Helper function to copy IPv6 address (uses all 4 elements in the array)
inline static void ipv6_copy_addr(uint32_t dst[4], const uint32_t src[4]) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

// Handle incoming DNS packet and fill connection info
int dns_handle_incoming(const uint32_t srcip[4], const uint16_t srcport,
                        const char *packet_data, const UINT packet_dataLen,
                        conntrack_info_t *conn_info, const uint8_t is_ipv6);

// Handle outgoing DNS packet
int dns_handle_outgoing(const uint32_t srcip[4], const uint16_t srcport,
                        const uint32_t dstip[4], const uint16_t dstport,
                        const char *packet_data, const UINT packet_dataLen,
                        const uint8_t is_ipv6
                       );

/*
 * Hybrid DNS (TurkishDPI): every query goes both to the original resolver and
 * to the clean one. The first trustworthy answer wins; forged answers (block
 * pages, sinkholes) and error answers from the original resolver are dropped,
 * so a clean answer is used for blocked names and the fast, CDN-local answer
 * of the network's own resolver for everything else.
 */
enum { DNS_IN_NOT_OURS = 0, DNS_IN_REWRITE = 1, DNS_IN_DROP = 2, DNS_IN_PASS = 3 };
void dns_set_hybrid(int on);
/* The clean resolver (network byte order), so its answers can be told apart. */
void dns_set_clean(uint32_t v4, uint16_t v4port, const uint32_t v6[4], uint16_t v6port);
int  dns_hybrid_enabled(void);
int  dns_add_block_ip(const char *ip);           /* returns 1 if parsed */
int  dns_answer_is_forged(const char *data, unsigned int len);
/* Inbound answer in hybrid mode: one of DNS_IN_*. conn_info filled for REWRITE. */
int  dns_handle_incoming_hybrid(const uint32_t local_ip[4], const uint16_t local_port,
                                const uint32_t remote_ip[4], const uint16_t remote_port,
                                const char *packet_data, const UINT packet_dataLen,
                                conntrack_info_t *conn_info, const uint8_t is_ipv6);

// Clear the DNS cache
void flush_dns_cache();

// Check if packet is a DNS packet
int dns_is_dns_packet(const char *packet_data, const UINT packet_dataLen, const int outgoing);
#endif
