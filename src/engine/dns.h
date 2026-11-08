/*
 * TurkishDPI - DNS redirection with the hybrid race.
 *
 * Turkish ISPs block names by answering their own resolver's questions with a
 * block page (or NXDOMAIN), and many intercept port 53 to any server. The
 * engine therefore sends the system's questions to a clean resolver on a
 * non-standard port as well. In hybrid mode each question goes to both, and
 * the first trustworthy answer wins: the network's own fast, CDN-local answer
 * for ordinary names, the clean one where the network forged its answer.
 *
 * Pure C over byte buffers (no WinDivert), unit tested in tests/dns_test.c.
 * Addresses and ports are in network byte order; IPv4 uses element 0.
 */
#ifndef TURKISHDPI_DNS_H
#define TURKISHDPI_DNS_H
#include <stdint.h>

typedef struct conntrack_info {
    uint8_t  is_ipv6;
    uint32_t srcip[4];     /* the asking program's address */
    uint16_t srcport;      /* ... and port */
    uint32_t dstip[4];     /* the resolver it asked */
    uint16_t dstport;
} conntrack_info_t;

/* What to do with an inbound answer in hybrid mode. */
enum {
    DNS_IN_NOT_OURS = 0,   /* no question of ours: deliver unchanged */
    DNS_IN_REWRITE = 1,    /* the clean resolver's answer: rewrite to look like the asked one */
    DNS_IN_DROP = 2,       /* forged, or a late duplicate: drop */
    DNS_IN_PASS = 3,       /* the asked resolver's own clean answer: deliver, trusted */
    DNS_IN_PASS_OTHER = 4  /* a clean-looking answer from someone else: deliver, not trusted */
};

void dns_set_hybrid(int on);
int  dns_hybrid_enabled(void);
/* The clean resolver, so its answers can be told apart. */
void dns_set_clean(uint32_t v4, uint16_t v4port, const uint32_t v6[4], uint16_t v6port);
/* An extra block-page address ("a.b.c.d" or IPv6 text); 1 if it was parsed. */
int  dns_add_block_ip(const char *ip);
/* 1 if an answer must not be trusted: an error code, a block page, a private
 * or special address, or a message that does not parse. */
int  dns_answer_is_forged(const char *data, unsigned int len);
/* 1 for a well-formed DNS question (outgoing) or answer (incoming). */
int  dns_is_dns_packet(const char *data, unsigned int len, int outgoing);

/* A question leaving the machine: remembered (1) when it is one. */
int  dns_handle_outgoing(const uint32_t srcip[4], uint16_t srcport,
                         const uint32_t dstip[4], uint16_t dstport,
                         const char *data, unsigned int len, uint8_t is_ipv6);
/* Non-hybrid: an answer from the clean resolver to a remembered question;
 * fills ci with the original question's addresses (1), else 0. */
int  dns_handle_incoming(const uint32_t local_ip[4], uint16_t local_port,
                         const char *data, unsigned int len,
                         conntrack_info_t *ci, uint8_t is_ipv6);
/* Hybrid: the verdict for an answer, one of DNS_IN_*; ci filled for REWRITE. */
int  dns_handle_incoming_hybrid(const uint32_t local_ip[4], uint16_t local_port,
                                const uint32_t remote_ip[4], uint16_t remote_port,
                                const char *data, unsigned int len,
                                conntrack_info_t *ci, uint8_t is_ipv6);
/* 1 if an answer belongs to a remembered question: same local address and
 * port, transaction ID, and sent by the server the question went to. */
int  dns_answer_matches(const uint32_t local_ip[4], uint16_t local_port,
                        const uint32_t remote_ip[4], uint16_t remote_port,
                        const char *data, unsigned int len, uint8_t is_ipv6);

/* Empties Windows' DNS cache (answers forged before the engine started). */
void flush_dns_cache(void);

#endif
