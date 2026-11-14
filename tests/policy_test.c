/* MSVC: /Isrc/engine /Isrc/engine/core tests/policy_test.c src/engine/policy.c src/engine/core/udpdpi.c */
/*
 * TurkishDPI: the engine's pure decisions (src/engine/policy.c):
 *  - the WinDivert filter text: identical to the pre-2.0.2 builder for every
 *    option combination, the longest one fits POLICY_FILTER_CAP, and a buffer
 *    that is too small gives -1 without writing past it;
 *  - payment exclusion precedence: the name in the segment decides, a learned
 *    address only when there is no name.
 *
 *   gcc -O2 -Wall -Isrc/engine -Isrc/engine/core tests/policy_test.c src/engine/policy.c src/engine/core/udpdpi.c -o build/policy_test
 */
#include "policy.h"
#include "udpdpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* The pre-2.0.2 build_filter (engine.c), with its globals as parameters and
 * an ample buffer: the reference text. */
static void old_build_filter(char *f, size_t cap, const policy_filter_opts *c) {
    char udp[1024] = "";
    int o;
    int hybrid = c->dns_hybrid && (c->dns4 || c->dns6);
    if (c->tlsrec)
        o = snprintf(f, cap, "(outbound and !loopback and tcp and ("
                     "(ip and tcp.DstPort == 443) or "
                     "(tcp.DstPort == 443 and tcp.PayloadLength >= 6 and tcp.Payload[1] == 3 and "
                     "(tcp.Payload[0] == 22 or tcp.Payload[0] == 21)) or "
                     "(tcp.DstPort == 80 and tcp.PayloadLength > 0)))");
    else
        o = snprintf(f, cap, "(outbound and !loopback and tcp and ("
                     "(tcp.DstPort == 443 and tcp.PayloadLength >= 6 and tcp.Payload[1] == 3 and "
                     "(tcp.Payload[0] == 22 or tcp.Payload[0] == 21)) or "
                     "(tcp.DstPort == 80 and tcp.PayloadLength > 0)))");
    if (c->quic_block && udp_windivert_clause(udp, sizeof(udp), 1, 0, UDP_PREFILTER_PAYLOAD) > 0)
        o += snprintf(f + o, cap - o, " or (outbound and !loopback and udp and %s)", udp);
    if (c->udp_fake && udp_windivert_clause(udp, sizeof(udp), 0, 1, UDP_PREFILTER_PAYLOAD) > 0)
        o += snprintf(f + o, cap - o, " or (outbound and !loopback and ip and udp and %s)", udp);
    if (c->dns4 || c->dns6 || c->exclude) {
        o += snprintf(f + o, cap - o, " or (outbound and !loopback and udp and udp.DstPort == 53)");
        if (hybrid || (!c->dns4 && !c->dns6))
            o += snprintf(f + o, cap - o, " or (inbound and !loopback and udp and udp.SrcPort == 53)");
        if (c->dns4)
            o += snprintf(f + o, cap - o, " or (inbound and !loopback and udp and ip.SrcAddr == %s and udp.SrcPort == %u)",
                          c->dns4_addr, c->dns4_port);
        if (c->dns6)
            o += snprintf(f + o, cap - o, " or (inbound and !loopback and udp and ipv6.SrcAddr == %s and udp.SrcPort == %u)",
                          c->dns6_addr, c->dns6_port);
    }
    {
        char sub[256] = "";
        int so = 0;
        if (c->tlsrec) so += snprintf(sub + so, sizeof(sub) - so, "(ip and tcp.SrcPort == 443)");
        if (c->drop_rst) so += snprintf(sub + so, sizeof(sub) - so, "%stcp.Rst", so ? " or " : "");
        if (c->autottl) so += snprintf(sub + so, sizeof(sub) - so, "%s(tcp.Syn and tcp.Ack)", so ? " or " : "");
        if (c->alert_watch)
            so += snprintf(sub + so, sizeof(sub) - so,
                           "%s(tcp.SrcPort == 443 and tcp.PayloadLength >= 7 and tcp.Payload[0] == 21 and tcp.Payload[1] == 3)",
                           so ? " or " : "");
        if (sub[0])
            snprintf(f + o, cap - o, " or (inbound and !loopback and tcp and (tcp.SrcPort == 443 or tcp.SrcPort == 80) and (%s))", sub);
    }
}

static void set_opts(policy_filter_opts *o, unsigned bits) {
    memset(o, 0, sizeof(*o));
    o->tlsrec = (bits >> 0) & 1;
    o->quic_block = (bits >> 1) & 1;
    o->udp_fake = (bits >> 2) & 1;
    o->drop_rst = (bits >> 3) & 1;
    o->autottl = (bits >> 4) & 1;
    o->alert_watch = (bits >> 5) & 1;
    o->dns4 = (bits >> 6) & 1;
    o->dns6 = (bits >> 7) & 1;
    o->dns_hybrid = (bits >> 8) & 1;
    o->exclude = (bits >> 9) & 1;
    o->dns4_addr = "77.88.8.8";
    o->dns6_addr = "2a02:6b8::feed:0ff";
    o->dns4_port = 1253;
    o->dns6_port = 53;
}

static void test_filter(void) {
    static char want[16384], got[POLICY_FILTER_CAP];
    policy_filter_opts o;
    /* every combination: the same text as before */
    for (unsigned bits = 0; bits < 1024; bits++) {
        set_opts(&o, bits);
        old_build_filter(want, sizeof(want), &o);
        int n = policy_build_filter(got, sizeof(got), &o);
        CHECK(n == (int)strlen(want) && strcmp(got, want) == 0);
    }
    /* the longest filter today: everything on, the longest address texts */
    set_opts(&o, 1023);
    o.dns4_addr = "255.255.255.255";
    o.dns6_addr = "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255";
    o.dns4_port = 65535;
    o.dns6_port = 65535;
    int longest = policy_build_filter(got, sizeof(got), &o);
    CHECK(longest > 0 && longest < POLICY_FILTER_CAP);
    printf("longest filter: %d of %d bytes\n", longest, POLICY_FILTER_CAP);
    /* too small a buffer: -1, terminated, nothing written past it (each
     * buffer is exactly `cap` bytes from the heap, so ASan sees an overrun) */
    for (int cap = 1; cap <= longest + 1; cap++) {
        char *b = (char *)malloc((size_t)cap);
        if (!b) { CHECK(b != NULL); return; }
        int n = policy_build_filter(b, (size_t)cap, &o);
        CHECK(cap <= longest ? (n == -1 && strlen(b) < (size_t)cap) : n == longest);
        free(b);
    }
    CHECK(policy_build_filter(got, 0, &o) == -1);
    CHECK(policy_build_filter(NULL, 10, &o) == -1);
}

/* ---------- payment exclusion precedence ---------- */
static const unsigned char LEARNED[4] = { 104, 16, 1, 1 };     /* a CDN address learned for a bank */
static int name_listed(const char *s, size_t n) {
    return n == 14 && !memcmp(s, "secure.bank.tr", 14);
}
static int ip_learned(const void *a, int v6) {
    return !v6 && !memcmp(a, LEARNED, 4);
}
/* the pre-2.0.2 order (excluded_conn): the address first */
static int old_excluded(const char *name, size_t len, const void *ip, int v6) {
    if (ip_learned(ip, v6)) return 1;
    return name && len > 0 && name_listed(name, len);
}

static void test_exclusion(void) {
    const unsigned char other[4] = { 93, 184, 216, 34 };
    /* learned address + another site's name: the name decides - not excluded */
    CHECK(old_excluded("blocked.example", 15, LEARNED, 0) == 1);            /* the defect */
    CHECK(policy_excluded(1, "blocked.example", 15, LEARNED, 0, name_listed, ip_learned) == 0);
    /* learned address, no name in the segment: excluded */
    CHECK(policy_excluded(1, NULL, 0, LEARNED, 0, name_listed, ip_learned) == 1);
    CHECK(policy_excluded(1, "x", 0, LEARNED, 0, name_listed, ip_learned) == 1);
    /* a payment name: excluded, whatever the address */
    CHECK(policy_excluded(1, "secure.bank.tr", 14, other, 0, name_listed, ip_learned) == 1);
    CHECK(policy_excluded(1, "secure.bank.tr", 14, LEARNED, 0, name_listed, ip_learned) == 1);
    /* neither */
    CHECK(policy_excluded(1, NULL, 0, other, 0, name_listed, ip_learned) == 0);
    CHECK(policy_excluded(1, "blocked.example", 15, other, 0, name_listed, ip_learned) == 0);
    /* protection off: never */
    CHECK(policy_excluded(0, "secure.bank.tr", 14, LEARNED, 0, name_listed, ip_learned) == 0);
    CHECK(policy_excluded(0, NULL, 0, LEARNED, 0, name_listed, ip_learned) == 0);
}

int main(void) {
    test_filter();
    test_exclusion();
    if (fails) { printf("%d of %d checks FAILED\n", fails, checks); return 1; }
    printf("policy_test: all %d checks passed\n", checks);
    return 0;
}
