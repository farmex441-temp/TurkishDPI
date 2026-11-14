/* Tests forged-answer detection and the hybrid DNS race in src/engine/dns.c. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include "dns.h"

static int fails;
#define EXPECT(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); fails++; } } while (0)

/* Builds a response: id, rcode, one question (discord.com) and one answer. */
static int build(unsigned char *m, unsigned short id, int rcode, int type, const unsigned char *rdata, int rdlen) {
    static const unsigned char qname[] = { 7, 'd', 'i', 's', 'c', 'o', 'r', 'd', 3, 'c', 'o', 'm', 0 };
    int o = 12;
    memset(m, 0, 512);
    m[0] = (unsigned char)(id >> 8); m[1] = (unsigned char)id;
    m[2] = 0x81; m[3] = (unsigned char)(0x80 | rcode);
    m[5] = 1;                       /* QDCOUNT */
    m[7] = rdata ? 1 : 0;           /* ANCOUNT */
    memcpy(m + o, qname, sizeof(qname)); o += sizeof(qname);
    m[o++] = 0; m[o++] = (unsigned char)type; m[o++] = 0; m[o++] = 1;
    if (rdata) {
        m[o++] = 0xC0; m[o++] = 12;            /* compressed name -> question */
        m[o++] = 0; m[o++] = (unsigned char)type; m[o++] = 0; m[o++] = 1;
        m[o++] = 0; m[o++] = 0; m[o++] = 0; m[o++] = 60;
        m[o++] = 0; m[o++] = (unsigned char)rdlen;
        memcpy(m + o, rdata, (size_t)rdlen); o += rdlen;
    }
    return o;
}

/* Like build(), with a second A record appended to the answer section. */
static int build2(unsigned char *m, unsigned short id, const unsigned char a1[4], const unsigned char a2[4]) {
    int o = build(m, id, 0, 1, a1, 4);
    m[7] = 2;                       /* ANCOUNT */
    m[o++] = 0xC0; m[o++] = 12;
    m[o++] = 0; m[o++] = 1; m[o++] = 0; m[o++] = 1;
    m[o++] = 0; m[o++] = 0; m[o++] = 0; m[o++] = 60;
    m[o++] = 0; m[o++] = 4;
    memcpy(m + o, a2, 4); o += 4;
    return o;
}

/* Question from local:lport to resolver:53, which answers A 192.168.1.20.
 * accepted: expect DNS_IN_PASS (and the clean duplicate dropped); otherwise
 * expect DNS_IN_DROP and the clean answer rewritten. */
static void lan_answer_case(const uint32_t local[4], uint16_t lport, const uint32_t resolver[4],
                            const uint32_t clean[4], uint16_t cport, const unsigned char *q,
                            int v6, int accepted, const char *what) {
    unsigned char m[512], lan[4] = { 192, 168, 1, 20 }, real[4] = { 162, 159, 137, 232 };
    uint16_t p53 = htons(53);
    conntrack_info_t ci;
    int n;
    char msg[160];
    EXPECT(dns_handle_outgoing(local, lport, resolver, p53, (const char *)q, 29, (uint8_t)v6), "tracked for edge");
    n = build(m, (unsigned short)(q[0] << 8 | q[1]), 0, 1, lan, 4);
    snprintf(msg, sizeof(msg), "%s: %s", what, accepted ? "accepted" : "strict (dropped)");
    EXPECT(dns_handle_incoming_hybrid(local, lport, resolver, p53, (char *)m, (unsigned)n, &ci, (uint8_t)v6)
           == (accepted ? DNS_IN_PASS : DNS_IN_DROP), msg);
    n = build(m, (unsigned short)(q[0] << 8 | q[1]), 0, 1, real, 4);
    snprintf(msg, sizeof(msg), "%s: clean answer %s", what, accepted ? "dropped as duplicate" : "rewritten");
    EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, (uint8_t)v6)
           == (accepted ? DNS_IN_DROP : DNS_IN_REWRITE), msg);
}

int main(void) {
    unsigned char m[512];
    unsigned char blk[4] = { 195, 175, 254, 2 }, real[4] = { 162, 159, 137, 232 }, lo[4] = { 127, 0, 0, 1 };
    unsigned char v6blk[16], v6real[16];
    int n;
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    inet_pton(AF_INET6, "2a01:358:4014:a00::3", v6blk);
    inet_pton(AF_INET6, "2606:4700::6810:84e5", v6real);
    dns_set_hybrid(1);

    n = build(m, 1, 0, 1, blk, 4);    EXPECT(dns_answer_is_forged((char *)m, (unsigned)n), "BTK block page is forged");
    n = build(m, 1, 0, 1, real, 4);   EXPECT(!dns_answer_is_forged((char *)m, (unsigned)n), "real A is clean");
    n = build(m, 1, 0, 1, lo, 4);     EXPECT(dns_answer_is_forged((char *)m, (unsigned)n), "loopback is forged");
    n = build(m, 1, 3, 1, NULL, 0);   EXPECT(dns_answer_is_forged((char *)m, (unsigned)n), "NXDOMAIN untrusted");
    n = build(m, 1, 0, 28, v6blk, 16); EXPECT(dns_answer_is_forged((char *)m, (unsigned)n), "IPv6 block page forged");
    n = build(m, 1, 0, 28, v6real, 16); EXPECT(!dns_answer_is_forged((char *)m, (unsigned)n), "real AAAA clean");
    n = build(m, 1, 0, 28, NULL, 0);  EXPECT(!dns_answer_is_forged((char *)m, (unsigned)n), "empty NOERROR clean");
    EXPECT(dns_add_block_ip("10.1.2.3") == 1 && dns_add_block_ip("nonsense") == 0, "block ip parsing");
    m[12] = 0xC0; m[13] = 0xFF;       /* broken pointer must not crash */
    (void)dns_answer_is_forged((char *)m, 20);

    /* Hybrid race: query from 192.168.1.10:5000 to 192.168.1.1:53, clean = 94.140.14.140:5353 */
    {
        uint32_t local[4] = { 0 }, isp[4] = { 0 }, clean[4] = { 0 }, v6[4] = { 0 };
        uint16_t lport = htons(5000), p53 = htons(53), cport = htons(5353);
        conntrack_info_t ci;
        unsigned char q[64] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,
                                7, 'd', 'i', 's', 'c', 'o', 'r', 'd', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1 };
        inet_pton(AF_INET, "192.168.1.10", local);
        inet_pton(AF_INET, "192.168.1.1", isp);
        inet_pton(AF_INET, "94.140.14.140", clean);
        dns_set_clean(clean[0], cport, v6, 0);

        /* 1) ISP answers first with a clean answer -> passes, clean answer later dropped */
        EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "query tracked");
        n = build(m, 0x1234, 0, 1, real, 4);
        EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_PASS, "clean ISP answer passes");
        EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP, "late duplicate dropped");

        /* 2) ISP forges -> dropped, clean answer rewritten to look like the ISP */
        EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "query re-tracked on same port");
        n = build(m, 0x1234, 0, 1, blk, 4);
        EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP, "forged ISP answer dropped");
        n = build(m, 0x1234, 0, 1, real, 4);
        EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE, "clean answer rewritten");
        EXPECT(ci.dstip[0] == isp[0] && ci.dstport == p53 && ci.srcport == lport, "rewrite targets original resolver");

        /* 2b) a clean-looking answer from a third address neither wins the race
         *     nor counts as trusted; the real resolver's answer still passes */
        {
            uint32_t other[4] = { 0 };
            inet_pton(AF_INET, "10.9.9.9", other);
            EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 2b");
            n = build(m, 0x1234, 0, 1, real, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, other, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_PASS_OTHER,
                   "third-party answer is untrusted");
            EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_PASS,
                   "resolver answer still wins after a third-party one");
            /* query tracking used without redirection */
            EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for match");
            EXPECT(dns_answer_matches(local, lport, isp, p53, (char *)m, (unsigned)n, 0), "answer matches its query");
            EXPECT(!dns_answer_matches(local, lport, other, p53, (char *)m, (unsigned)n, 0), "other sender does not match");
            /* malformed answers are never trusted */
            n = build(m, 0x1234, 0, 1, real, 4);
            EXPECT(dns_answer_is_forged((char *)m, (unsigned)n - 2), "truncated answer is untrusted");
        }

        /* 2c) a name that does not exist: the network's NXDOMAIN is held back
         *     (it may be censorship), the clean resolver's NXDOMAIN gets through
         *     at once instead of the program waiting for a timeout */
        EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 2c");
        n = build(m, 0x1234, 3, 1, NULL, 0);
        EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
               "network NXDOMAIN held back");
        EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
               "clean NXDOMAIN delivered");
        EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
               "clean duplicate dropped");
        /* a LAN-style name whose real answer is a private address */
        EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for private");
        {
            unsigned char lan[4] = { 192, 168, 1, 50 };
            n = build(m, 0x1234, 0, 1, lan, 4);
        }
        EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
               "clean private answer delivered");

        /* 3) wrong transaction ID is not ours */
        EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked again");
        n = build(m, 0x9999, 0, 1, real, 4);
        EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_NOT_OURS, "foreign txid ignored");

        /* 4) local names: a resolver on the LAN/VPN answers with LAN addresses */
        {
            static const struct { const char *resolver, *answer, *what; } ok[] = {
                { "192.168.1.1", "192.168.1.20",  "router answers a LAN name" },
                { "10.8.0.1",    "10.0.0.20",     "VPN resolver answers an intranet name" },
                { "172.20.0.53", "172.20.4.4",    "172.16/12 resolver and answer" },
                { "192.168.1.1", "169.254.10.10", "link-local answer from a LAN resolver" },
            };
            for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
                uint32_t res[4] = { 0 };
                unsigned char a[4];
                inet_pton(AF_INET, ok[i].resolver, res);
                inet_pton(AF_INET, ok[i].answer, a);
                EXPECT(dns_handle_outgoing(local, lport, res, p53, (char *)q, 29, 0), "tracked for local name");
                n = build(m, 0x1234, 0, 1, a, 4);
                EXPECT(dns_handle_incoming_hybrid(local, lport, res, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_PASS, ok[i].what);
                n = build(m, 0x1234, 3, 1, NULL, 0);
                EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                       "clean NXDOMAIN after a local answer is a late duplicate");
            }
        }
        /* 4b) a LAN resolver still cannot pass a block page, a block-listed
         *     private address (--dns-block-ip 10.1.2.3 above), loopback,
         *     0.0.0.0 or multicast: dropped, the clean answer is rewritten */
        {
            static const char *bad[] = { "195.175.254.2", "10.1.2.3", "127.0.0.1", "0.0.0.0", "224.0.0.251" };
            for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
                unsigned char a[4];
                inet_pton(AF_INET, bad[i], a);
                EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 4b");
                n = build(m, 0x1234, 0, 1, a, 4);
                EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                       bad[i]);
                n = build(m, 0x1234, 0, 1, real, 4);
                EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
                       "clean answer rewritten after a LAN resolver's forged one");
                EXPECT(ci.dstip[0] == isp[0] && ci.dstport == p53, "rewrite targets the LAN resolver");
            }
        }
        /* 4c) a LAN resolver's NXDOMAIN is still held back (the router may
         *     forward the ISP's censorship); the clean NXDOMAIN gets through */
        EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 4c");
        n = build(m, 0x1234, 3, 1, NULL, 0);
        EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
               "LAN resolver NXDOMAIN held back");
        EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
               "clean NXDOMAIN delivered after a LAN NXDOMAIN");
        /* 4d) malformed answers from a LAN resolver are still dropped */
        {
            unsigned char lan[4] = { 192, 168, 1, 20 };
            EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 4d");
            n = build(m, 0x1234, 0, 1, lan, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n - 2, &ci, 0) == DNS_IN_DROP,
                   "truncated LAN answer dropped");
            /* the public check itself is unchanged: a private address is
             * forged when the sender is unknown (payment learning relies on it) */
            EXPECT(dns_answer_is_forged((char *)m, (unsigned)n), "private address forged without a resolver context");
            /* 4e) a third party on the LAN answering with a LAN address is
             *     neither trusted nor delivered (unchanged) */
            {
                uint32_t other[4] = { 0 };
                inet_pton(AF_INET, "192.168.1.2", other);
                EXPECT(dns_handle_incoming_hybrid(local, lport, other, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                       "third-party LAN answer still dropped");
            }
        }
        /* 4f) a public asked resolver answering with a private address: as
         *     before, the answer is treated as forged (dropped) and the clean
         *     resolver's answer is rewritten in its place */
        {
            uint32_t pub[4] = { 0 };
            unsigned char lan[4] = { 192, 168, 1, 20 };
            inet_pton(AF_INET, "195.175.39.39", pub);
            EXPECT(dns_handle_outgoing(local, lport, pub, p53, (char *)q, 29, 0), "tracked for 4f");
            n = build(m, 0x1234, 0, 1, lan, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, pub, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                   "public resolver's private answer dropped");
            n = build(m, 0x1234, 3, 1, NULL, 0);
            EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
                   "clean answer rewritten after a public resolver's private one");
            EXPECT(ci.dstip[0] == pub[0], "rewrite targets the public resolver");
        }
        /* 4g) the check uses the address the question went to, not the
         *     packet's claimed source: a public question cannot be answered
         *     with a LAN address by a LAN host claiming to be the resolver */
        {
            uint32_t pub[4] = { 0 };
            unsigned char lan[4] = { 192, 168, 1, 20 };
            inet_pton(AF_INET, "8.8.8.8", pub);
            EXPECT(dns_handle_outgoing(local, lport, pub, p53, (char *)q, 29, 0), "tracked for 4g");
            n = build(m, 0x1234, 0, 1, lan, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                   "LAN sender cannot answer a public question with a LAN address");
            EXPECT(dns_handle_incoming_hybrid(local, lport, pub, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                   "public resolver's LAN answer still dropped");
        }
        /* 4h) a CGNAT resolver (100.64/10) is usually the carrier's own one:
         *     strict - its private answer is dropped, the clean one rewritten */
        {
            uint32_t cg[4] = { 0 };
            inet_pton(AF_INET, "100.64.0.1", cg);
            lan_answer_case(local, lport, cg, clean, cport, q, 0, 0, "CGNAT resolver answers a LAN name");
            EXPECT(dns_handle_outgoing(local, lport, cg, p53, (char *)q, 29, 0), "tracked for 4h");
            n = build(m, 0x1234, 0, 1, real, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE &&
                   ci.dstip[0] == cg[0], "rewrite targets the CGNAT resolver");
        }
        /* 4i) port check: the question went to 192.168.1.1:53; an answer from
         *     192.168.1.1:5353 is not the asked resolver but a third party, so
         *     the strict check applies: its LAN address makes it DNS_IN_DROP,
         *     the race is not ended and the clean answer is rewritten */
        {
            unsigned char lan[4] = { 192, 168, 1, 20 };
            uint16_t p5353 = htons(5353);
            EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 4i");
            n = build(m, 0x1234, 0, 1, lan, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p5353, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                   "LAN answer from the resolver's address but another port dropped");
            n = build(m, 0x1234, 0, 1, real, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
                   "race not ended by the wrong-port answer: clean answer rewritten");
            EXPECT(ci.dstip[0] == isp[0] && ci.dstport == p53, "rewrite targets 192.168.1.1:53");
        }
        /* 4j) range edges of the local-resolver test, each answering 192.168.1.20 */
        {
            static const struct { const char *resolver; int accepted; } edge[] = {
                { "172.15.0.1", 0 }, { "172.32.0.1", 0 }, { "169.253.0.1", 0 },
                { "100.63.255.255", 0 }, { "100.64.0.1", 0 }, { "100.127.255.254", 0 }, { "100.128.0.1", 0 },
                { "172.16.0.1", 1 }, { "172.31.255.254", 1 }, { "169.254.1.1", 1 }, { "127.0.0.1", 1 },
            };
            for (unsigned i = 0; i < sizeof(edge) / sizeof(edge[0]); i++) {
                uint32_t res[4] = { 0 };
                inet_pton(AF_INET, edge[i].resolver, res);
                lan_answer_case(local, lport, res, clean, cport, q, 0, edge[i].accepted, edge[i].resolver);
            }
        }
        /* 4k) mixed answer from a LAN resolver: a LAN address and the block
         *     page in one message -> dropped, the clean answer rewritten */
        {
            unsigned char lan[4] = { 192, 168, 1, 20 };
            EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 4k");
            n = build2(m, 0x1234, lan, blk);
            EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_DROP,
                   "LAN address plus block page from a LAN resolver dropped");
            n = build(m, 0x1234, 0, 1, real, 4);
            EXPECT(dns_handle_incoming_hybrid(local, lport, clean, cport, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_REWRITE,
                   "clean answer rewritten after a mixed answer");
            /* the second record is really parsed: two LAN addresses pass */
            EXPECT(dns_handle_outgoing(local, lport, isp, p53, (char *)q, 29, 0), "tracked for 4k2");
            n = build2(m, 0x1234, lan, lan);
            EXPECT(dns_handle_incoming_hybrid(local, lport, isp, p53, (char *)m, (unsigned)n, &ci, 0) == DNS_IN_PASS,
                   "two LAN addresses from a LAN resolver pass");
        }
    }

    /* 5) IPv6: ULA / link-local resolvers and answers */
    {
        uint32_t local6[4], res6[4], ll6[4], pub6[4], clean6[4], clean4 = 0;
        uint16_t lport = htons(5001), p53 = htons(53), cport = htons(5353);
        conntrack_info_t ci;
        unsigned char ula[16], lla[16];
        unsigned char q[64] = { 0x43, 0x21, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,
                                7, 'd', 'i', 's', 'c', 'o', 'r', 'd', 3, 'c', 'o', 'm', 0, 0, 28, 0, 1 };
        inet_pton(AF_INET6, "fd00::10", local6);
        inet_pton(AF_INET6, "fd00::1", res6);
        inet_pton(AF_INET6, "fe80::1", ll6);
        inet_pton(AF_INET6, "2001:4860:4860::8888", pub6);
        inet_pton(AF_INET6, "2a10:50c0::ad1:ff", clean6);
        inet_pton(AF_INET6, "fd00::20", ula);
        inet_pton(AF_INET6, "fe80::20", lla);
        inet_pton(AF_INET, "94.140.14.140", &clean4);
        dns_set_clean(clean4, cport, clean6, cport);

        /* Regression guards: the next two cases (ULA and link-local AAAA from
         * a ULA / link-local resolver) also passed before the local-resolver
         * rule - bad_v6 never rejected those addresses. */
        EXPECT(dns_handle_outgoing(local6, lport, res6, p53, (char *)q, 29, 1), "v6 tracked");
        n = build(m, 0x4321, 0, 28, ula, 16);
        EXPECT(dns_handle_incoming_hybrid(local6, lport, res6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_PASS,
               "ULA resolver answers a ULA address");
        EXPECT(dns_handle_incoming_hybrid(local6, lport, clean6, cport, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_DROP,
               "v6 clean late duplicate dropped");

        EXPECT(dns_handle_outgoing(local6, lport, ll6, p53, (char *)q, 29, 1), "v6 tracked link-local");
        n = build(m, 0x4321, 0, 28, lla, 16);
        EXPECT(dns_handle_incoming_hybrid(local6, lport, ll6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_PASS,
               "link-local resolver answers a link-local address");

        /* an IPv4 private address in an A answer carried over IPv6 from a ULA resolver */
        EXPECT(dns_handle_outgoing(local6, lport, res6, p53, (char *)q, 29, 1), "v6 tracked for A");
        {
            unsigned char lan[4] = { 192, 168, 1, 20 };
            n = build(m, 0x4321, 0, 1, lan, 4);
        }
        EXPECT(dns_handle_incoming_hybrid(local6, lport, res6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_PASS,
               "ULA resolver answers a private A record");

        /* IPv6 range edges, each asked resolver answering A 192.168.1.20 */
        {
            static const struct { const char *resolver; int accepted; } edge[] = {
                { "fec0::1", 0 }, { "fe00::1", 0 }, { "::ffff:192.168.1.1", 0 },
                { "fd00::1", 1 }, { "fc00::1", 1 }, { "fe80::1", 1 }, { "febf::1", 1 }, { "::1", 1 },
            };
            for (unsigned i = 0; i < sizeof(edge) / sizeof(edge[0]); i++) {
                uint32_t res[4] = { 0 };
                inet_pton(AF_INET6, edge[i].resolver, res);
                lan_answer_case(local6, lport, res, clean6, cport, q, 1, edge[i].accepted, edge[i].resolver);
            }
        }

        /* block page from a ULA resolver: dropped, clean rewritten */
        EXPECT(dns_handle_outgoing(local6, lport, res6, p53, (char *)q, 29, 1), "v6 tracked for block");
        n = build(m, 0x4321, 0, 28, v6blk, 16);
        EXPECT(dns_handle_incoming_hybrid(local6, lport, res6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_DROP,
               "ULA resolver's IPv6 block page dropped");
        n = build(m, 0x4321, 0, 28, v6real, 16);
        EXPECT(dns_handle_incoming_hybrid(local6, lport, clean6, cport, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_REWRITE,
               "v6 clean answer rewritten");
        EXPECT(!memcmp(ci.dstip, res6, 16) && ci.dstport == p53, "v6 rewrite targets the ULA resolver");

        /* a ULA resolver's NXDOMAIN is still held back */
        EXPECT(dns_handle_outgoing(local6, lport, res6, p53, (char *)q, 29, 1), "v6 tracked for NX");
        n = build(m, 0x4321, 3, 28, NULL, 0);
        EXPECT(dns_handle_incoming_hybrid(local6, lport, res6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_DROP,
               "ULA resolver NXDOMAIN held back");

        /* public IPv6 resolver: unchanged - ULA AAAA was never forged and still
         * passes; a private A record is still forged */
        EXPECT(dns_handle_outgoing(local6, lport, pub6, p53, (char *)q, 29, 1), "v6 tracked public");
        n = build(m, 0x4321, 0, 28, ula, 16);
        EXPECT(dns_handle_incoming_hybrid(local6, lport, pub6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_PASS,
               "public v6 resolver's ULA answer passes (as before)");
        EXPECT(dns_handle_outgoing(local6, lport, pub6, p53, (char *)q, 29, 1), "v6 tracked public A");
        {
            unsigned char lan[4] = { 10, 0, 0, 20 };
            n = build(m, 0x4321, 0, 1, lan, 4);
        }
        EXPECT(dns_handle_incoming_hybrid(local6, lport, pub6, p53, (char *)m, (unsigned)n, &ci, 1) == DNS_IN_DROP,
               "public v6 resolver's private A answer dropped (as before)");
    }
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
