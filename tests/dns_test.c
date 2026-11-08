/* Tests forged-answer detection and the hybrid DNS race in src/engine/dnsredir.c. */
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
    }
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
