/*
 * TurkishDPI - pure decisions of the packet engine: the WinDivert filter text
 * and the payment-exclusion precedence. No WinDivert, no Win32: unit tested
 * in tests/policy_test.c.
 */
#ifndef TURKISHDPI_POLICY_H
#define TURKISHDPI_POLICY_H
#include <stddef.h>

/* The engine's filter buffer. The longest filter today (tlsrec, QUIC, voice,
 * hybrid DNS on both families with the longest address texts, every inbound
 * clause) is checked to fit by tests/policy_test.c. */
#define POLICY_FILTER_CAP 4096

typedef struct {
    int tlsrec;        /* every IPv4 443 segment out, every IPv4 443 segment in */
    int quic_block;    /* QUIC Initials (both families) */
    int udp_fake;      /* Discord voice (IPv4) */
    int drop_rst;      /* inbound resets */
    int autottl;       /* inbound SYN-ACKs */
    int alert_watch;   /* inbound TLS alerts */
    int dns4, dns6;    /* a clean resolver for that family */
    int dns_hybrid;    /* hybrid race on (only meaningful with dns4/dns6) */
    int exclude;       /* payment exclusion: learn from DNS answers */
    const char *dns4_addr, *dns6_addr;   /* text, as given on the command line */
    unsigned dns4_port, dns6_port;       /* host order */
} policy_filter_opts;

/* Writes the filter for these options to f (capacity cap) and returns its
 * length, or -1 when it does not fit (f then holds a truncated, terminated
 * string that must not be used). Never writes past cap. */
int policy_build_filter(char *f, size_t cap, const policy_filter_opts *o);

/* Whether a connection is left alone by payment protection. When the
 * segment carries a host name (name != NULL, name_len > 0) the NAME alone
 * decides; the addresses learned from DNS answers for payment domains decide
 * only when there is no name (a shared CDN/anycast address must not exclude
 * an unrelated blocked site that names itself). */
typedef int (*policy_name_fn)(const char *name, size_t len);
typedef int (*policy_ip_fn)(const void *addr, int is_ipv6);
int policy_excluded(int enabled, const char *name, size_t name_len, const void *daddr, int v6,
                    policy_name_fn name_listed, policy_ip_fn ip_learned);

#endif
