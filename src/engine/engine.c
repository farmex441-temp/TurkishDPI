/*
 * TurkishDPI - the packet engine.
 *
 * One WinDivert handle sees only what the engine acts on (see build_filter):
 * the first data segment of web connections, QUIC Initials, DNS, and the
 * inbound resets / SYN-ACKs / TLS alerts the desync logic needs. For a TLS
 * ClientHello or an HTTP request it sends a decoy copy that reaches the ISP's
 * filter but dies before the server (low TTL, bad TCP checksum, the name
 * scrambled), then the real request cut so that no segment carries the whole
 * name. Resets the filter forges against such a connection are dropped.
 * Payment and bank connections are never touched (exclude.c); DNS goes to a
 * clean resolver as well (dns.c, the hybrid race).
 *
 * The IPv4 packet path - the decoy, the cut, the sequence overlap, the TLS
 * record split, the reset drop, the hop-distance TTL and the QUIC / Discord
 * voice handling - is adapted from Shroudly's packet backend
 * (core/src/raw_windows.c, https://github.com/umutxyp/Shroudly), together
 * with the helpers in src/engine/core/:
 *
 *   MIT License, Copyright (c) 2026 Codeshare Technology Ltd
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions: The above copyright notice and this permission
 *   notice shall be included in all copies or substantial portions of the
 *   Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *   (Full text: THIRD_PARTY_LICENSES.txt.)
 *
 * TurkishDPI's own parts: WinDivert loaded only from pinned files
 * (windivert_dyn.c), the IPv6 path, DNS redirection, payment exclusion, the
 * command line and the status lines for the app.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "windivert.h"
#include "core/proto.h"
#include "core/tcpdpi.h"
#include "core/udpdpi.h"
#include "core/rawscope.h"
#include "core/raw_desync.h"
#include "dns.h"
#include "exclude.h"
#include "engine.h"
#include "version.h"

int engine_report_stats = 0;

/* ---------- configuration ---------- */
static raw_config cfg;
static int no_decoy;                        /* only the cut, never a decoy */
static int dns4, dns6;                       /* redirect DNS to the clean resolver */
static uint32_t dns4_addr;                   /* network order */
static uint16_t dns4_port;
static uint32_t dns6_addr[4];
static uint16_t dns6_port;
static char dns4_text[64], dns6_text[64];

static HANDLE wd = INVALID_HANDLE_VALUE;
static uint16_t ipid = 0x1000;              /* IP id of our crafted IPv4 segments */
static udp_flow_table flows;

/* ---------- counters for the app ---------- */
static struct { volatile LONG packets, https, http, excluded, dns; } stats;

static DWORD WINAPI stats_reporter(LPVOID unused) {
    LONG last[5] = { -1, -1, -1, -1, -1 };
    (void)unused;
    for (;;) {
        LONG now[5] = { stats.packets, stats.https, stats.http, stats.excluded, stats.dns };
        if (memcmp(now, last, sizeof(now))) {
            memcpy(last, now, sizeof(now));
            printf("STATS packets=%ld https=%ld http=%ld excluded=%ld dns=%ld\n",
                   now[0], now[1], now[2], now[3], now[4]);
            fflush(stdout);
        }
        Sleep(2000);
    }
    return 0;
}

/* ---------- small helpers ---------- */
static uint64_t now_ms(void) { return GetTickCount64(); }
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wr32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void pass(const uint8_t *pkt, UINT len, const WINDIVERT_ADDRESS *a) {
    WinDivertSend(wd, pkt, len, NULL, a);
}

/* A crafted segment whose checksums are already in it (a deliberately bad
 * one included): marked so the stack does not recompute them. */
static void send_crafted(const uint8_t *seg, int len, const WINDIVERT_ADDRESS *base, int v6) {
    WINDIVERT_ADDRESS a = *base;
    if (len <= 0) return;
    a.Outbound = 1;
    a.IPv6 = v6 ? 1 : 0;
    a.IPChecksum = 1;
    a.TCPChecksum = 1;
    WinDivertSend(wd, seg, (UINT)len, NULL, &a);
}

/* The decoy's payload: the request with another (or a scrambled) name. */
static int decoy_payload(uint8_t *out, int cap, const uint8_t *pay, int plen,
                         const proto_info *pi, int iphl, int tcphl) {
    int n = proto_decoy_rename(out, cap, pay, plen, pi->kind == PROTO_TLS, pi->host_off, pi->host_len,
                               decoy_room(plen, iphl, tcphl));
    if (n > 0) return n;
    if (plen > cap) return 0;
    memcpy(out, pay, (size_t)plen);
    if (pi->host_off >= 0 && pi->host_off + pi->host_len <= plen) {
        for (int i = pi->host_off; i < pi->host_off + pi->host_len; i++) out[i] ^= 0x5a;
    } else {
        for (int i = plen / 3; i < plen; i++) out[i] ^= 0x5a;
    }
    return plen;
}

/* ================= IPv4 (adapted from Shroudly raw_windows.c) ============= */
static int decoy_ttl4(uint32_t daddr) {
    if (cfg.autottl) {
        int hops = tcpdpi_hops_get(daddr, now_ms());
        if (hops > 0) return tcpdpi_decoy_ttl(hops, cfg.autottl_margin, cfg.autottl_min);
    }
    return cfg.fake_ttl;
}

static void send_fake4(const uint8_t *pkt, int len, int ihl, int tcphl, int poff,
                       const proto_info *pi, int ttl, const WINDIVERT_ADDRESS *addr) {
    uint8_t pay[1600], out[1700];
    int plen = len - poff, dlen, n;
    if (plen <= 0 || plen > 1500) return;
    dlen = decoy_payload(pay, (int)sizeof(pay), pkt + poff, plen, pi, ihl, tcphl);
    if (dlen <= 0) return;
    n = tcpdpi_build_segment4(out, sizeof(out), pkt, ihl, pkt + ihl, tcphl, ipid++,
                              rd32be(pkt + ihl + 4), ttl > 0 ? ttl : cfg.fake_ttl, pay, dlen, 1);
    if (n > 0) send_crafted(out, n, addr, 0);
}

static int send_cut4(const uint8_t *pkt, int ihl, int tcphl, int poff, int plen,
                     const proto_info *pi, int disorder, const WINDIVERT_ADDRESS *addr) {
    int cuts[2], nc = tcpdpi_split_points(pkt + poff, plen, pi->host_off, pi->host_len, cuts);
    tcpdpi_seg segs[3];
    int ns = 0;
    if (nc > 0 && tcpdpi_build_multisplit4(segs, &ns, pkt, ihl, pkt + ihl, tcphl, ipid,
                                           rd32be(pkt + ihl + 4), pkt[8] ? pkt[8] : 64,
                                           pkt + poff, plen, cuts, nc, disorder)) {
        ipid = (uint16_t)(ipid + ns);
        for (int i = 0; i < ns; i++) send_crafted(segs[i].data, segs[i].len, addr, 0);
        return 1;
    }
    return 0;
}

/* 1 when the original was replaced by what was sent. */
static int desync4(const uint8_t *pkt, int len, int ihl, int tcphl, int poff,
                   const proto_info *pi, int decoy, const WINDIVERT_ADDRESS *addr) {
    int plen = len - poff;
    uint32_t daddr = rd32be(pkt + 16), seq = rd32be(pkt + ihl + 4);
    int otl = pkt[8] ? pkt[8] : 64;
    int faked = cfg.desync_mode == RAW_DESYNC_FAKEDSPLIT || cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER;

    if (cfg.tlsrec && pi->kind == PROTO_TLS && pi->host_off >= 1) {
        int cuts[2], nc = tcpdpi_split_points(pkt + poff, plen, pi->host_off, pi->host_len, cuts);
        int cut = nc >= 2 ? cuts[1] : (nc == 1 ? cuts[0] : 0);
        tcpdpi_seg segs[2];
        int ns = 0;
        int ok = cut > 5 && tcpdpi_build_tlsrec_pair(segs, &ns, pkt, ihl, pkt + ihl, tcphl,
                                                     ipid, seq, otl, pkt + poff, plen, cut);
        if (ok && tcpdpi_xlate_begin(rd32be(pkt + 12), daddr, rd16be(pkt + ihl), rd16be(pkt + ihl + 2),
                                     seq + (uint32_t)plen, TCPDPI_TLSREC_GROWTH, now_ms())) {
            ipid = (uint16_t)(ipid + 2);
            if (decoy) send_fake4(pkt, len, ihl, tcphl, poff, pi, decoy_ttl4(daddr), addr);
            for (int i = 0; i < ns; i++) send_crafted(segs[i].data, segs[i].len, addr, 0);
            return 1;
        }
    }
    if (!decoy) {
        if (cfg.split == RAW_SPLIT_OFF && !faked) return 0;
        return send_cut4(pkt, ihl, tcphl, poff, plen, pi,
                         cfg.split == RAW_SPLIT_DISORDER || cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER, addr);
    }
    if (faked) {
        int split = (pi->host_off > 0 && pi->host_off < plen) ? pi->host_off : (plen > 2 ? 2 : 1);
        tcpdpi_seg segs[3];
        int ns = 0;
        if (tcpdpi_build_fakedsplit4(segs, &ns, pkt, ihl, pkt + ihl, tcphl, ipid, seq, otl,
                                     pkt + poff, plen, split, decoy_ttl4(daddr), cfg.fake_pattern,
                                     cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER)) {
            ipid = (uint16_t)(ipid + 3);
            for (int i = 0; i < ns; i++) send_crafted(segs[i].data, segs[i].len, addr, 0);
            return 1;
        }
        return 0;
    }
    send_fake4(pkt, len, ihl, tcphl, poff, pi, decoy_ttl4(daddr), addr);
    if (cfg.seqovl > 0 && plen > 0) {
        uint8_t out[1700];
        int n = tcpdpi_build_seqovl4(out, sizeof(out), pkt, ihl, pkt + ihl, tcphl, ipid, seq, otl,
                                     pkt + poff, plen, cfg.seqovl, cfg.seqovl_pattern);
        if (n > 0) { ipid++; send_crafted(out, n, addr, 0); return 1; }
    }
    if (cfg.split != RAW_SPLIT_OFF)
        return send_cut4(pkt, ihl, tcphl, poff, plen, pi, cfg.split == RAW_SPLIT_DISORDER, addr);
    return 0;
}

static void send_udp_decoys4(const uint8_t *pkt, int ihl, int plen, const WINDIVERT_ADDRESS *addr) {
    uint8_t buf[1600];
    uint32_t saddr, daddr, tick = (uint32_t)GetTickCount();
    uint16_t sport, dport;
    int ttl;
    if (plen <= 0 || plen > 1400) return;
    memcpy(&saddr, pkt + 12, 4);
    memcpy(&daddr, pkt + 16, 4);
    memcpy(&sport, pkt + ihl, 2);
    memcpy(&dport, pkt + ihl + 2, 2);
    if (!udp_flow_note(&flows, saddr, sport, daddr, dport, tick)) return;
    ttl = decoy_ttl4(ntohl(daddr));
    for (int i = 0; i < cfg.fake_repeats; i++) {
        uint32_t seed = saddr ^ (daddr << 1) ^ ((uint32_t)sport << 3) ^ ((uint32_t)dport << 5) ^
                        (uint32_t)i * 2654435761u ^ tick;
        int n = udp_build_decoy(buf, (int)sizeof(buf), saddr, daddr, sport, dport, plen, ttl,
                                (uint16_t)(tick + (uint32_t)i * 7u + 1u), seed);
        if (n <= 0) break;
        WinDivertSend(wd, buf, (UINT)n, NULL, addr);
    }
}

/* ================= IPv6 (TurkishDPI) ================= */
/* An IPv6 TCP segment like the captured one, with another sequence number,
 * hop limit and payload. Checksums computed by WinDivert; a decoy's TCP
 * checksum is then spoiled on purpose. Only packets without extension
 * headers come here (next header 6 at the fixed header). */
static int build6(uint8_t *out, int cap, const uint8_t *pkt, int tcphl, uint32_t seq, int hop,
                  const uint8_t *pay, int plen, int bad, const WINDIVERT_ADDRESS *addr) {
    WINDIVERT_ADDRESS a = *addr;
    int n = 40 + tcphl + plen;
    if (plen < 0 || n > cap) return 0;
    memcpy(out, pkt, (size_t)(40 + tcphl));
    if (plen) memcpy(out + 40 + tcphl, pay, (size_t)plen);
    out[4] = (uint8_t)((tcphl + plen) >> 8);
    out[5] = (uint8_t)(tcphl + plen);
    out[7] = (uint8_t)hop;
    wr32be(out + 40 + 4, seq);
    WinDivertHelperCalcChecksums(out, (UINT)n, &a, 0);
    if (bad) out[40 + 16] ^= 0x5a;
    return n;
}

/* The hop table is keyed by 32 bits: an IPv6 address goes in hashed. A
 * collision with another address only gives a decoy that one's distance. */
static uint32_t key6(const uint8_t a[16]) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) h = (h ^ a[i]) * 16777619u;
    return h | 1u;                     /* never 0 */
}

static int decoy_ttl6(const uint8_t daddr[16]) {
    if (cfg.autottl) {
        int hops = tcpdpi_hops_get(key6(daddr), now_ms());
        if (hops > 0) return tcpdpi_decoy_ttl(hops, cfg.autottl_margin, cfg.autottl_min);
    }
    return cfg.fake_ttl;
}

static int desync6(const uint8_t *pkt, int len, int tcphl, const proto_info *pi, int decoy,
                   const WINDIVERT_ADDRESS *addr) {
    const int poff = 40 + tcphl;
    const uint8_t *pay = pkt + poff;
    int plen = len - poff, hop = pkt[7] ? pkt[7] : 64;
    uint32_t seq = rd32be(pkt + 40 + 4);
    uint8_t out[1800];
    int cuts[2], nc, n;
    int disorder = cfg.split == RAW_SPLIT_DISORDER || cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER;
    int cut = cfg.split != RAW_SPLIT_OFF || cfg.desync_mode != RAW_DESYNC_FAKE;

    if (plen <= 0 || plen > 1600) return 0;
    if (decoy) {
        uint8_t fake[1700];
        int dlen = decoy_payload(fake, (int)sizeof(fake), pay, plen, pi, 40, tcphl);
        if (dlen > 0 && (n = build6(out, (int)sizeof(out), pkt, tcphl, seq, decoy_ttl6(pkt + 24), fake, dlen, 1, addr)) > 0)
            send_crafted(out, n, addr, 1);
    }
    if (!cut) return 0;
    nc = tcpdpi_split_points(pay, plen, pi->host_off, pi->host_len, cuts);
    if (nc <= 0) return 0;
    {
        int from[3], to[3], ns = 0, start = 0;
        for (int i = 0; i < nc; i++) { from[ns] = start; to[ns] = cuts[i]; start = cuts[i]; ns++; }
        from[ns] = start; to[ns] = plen; ns++;
        for (int k = 0; k < ns; k++) {
            int i = disorder ? ns - 1 - k : k;
            n = build6(out, (int)sizeof(out), pkt, tcphl, seq + (uint32_t)from[i], hop,
                       pay + from[i], to[i] - from[i], 0, addr);
            if (n <= 0) return k > 0;        /* nothing sent yet: the original goes on */
            send_crafted(out, n, addr, 1);
        }
    }
    return 1;
}

/* ================= DNS ================= */
/* 1 when the packet was consumed (sent changed, or dropped). */
static int dns_outbound(uint8_t *pkt, UINT len, int v6, int l4, WINDIVERT_ADDRESS *addr) {
    uint32_t src[4] = { 0 }, dst[4] = { 0 };
    uint16_t sport, dport;
    const char *data = (const char *)pkt + l4 + 8;
    unsigned dlen = len - (UINT)l4 - 8;
    int redirect = v6 ? dns6 : dns4;
    memcpy(src, pkt + (v6 ? 8 : 12), v6 ? 16 : 4);
    memcpy(dst, pkt + (v6 ? 24 : 16), v6 ? 16 : 4);
    memcpy(&sport, pkt + l4, 2);
    memcpy(&dport, pkt + l4 + 2, 2);
    if (!dns_handle_outgoing(src, sport, dst, dport, data, dlen, (uint8_t)v6)) return 0;
    if (!redirect) return 0;                      /* only remembered (payment learning) */
    if (dns_hybrid_enabled()) pass(pkt, len, addr); /* the network's resolver asked too */
    if (v6) memcpy(pkt + 24, dns6_addr, 16);
    else memcpy(pkt + 16, &dns4_addr, 4);
    memcpy(pkt + l4 + 2, v6 ? &dns6_port : &dns4_port, 2);
    WinDivertHelperCalcChecksums(pkt, len, addr, 0);
    pass(pkt, len, addr);
    if (!dns_hybrid_enabled()) InterlockedIncrement(&stats.dns);
    return 1;
}

static int dns_inbound(uint8_t *pkt, UINT len, int v6, int l4, WINDIVERT_ADDRESS *addr) {
    uint32_t local[4] = { 0 }, remote[4] = { 0 };
    uint16_t lport, rport;
    const char *data = (const char *)pkt + l4 + 8;
    unsigned dlen = len - (UINT)l4 - 8;
    conntrack_info_t ci;
    int trusted = 0, rewrite = 0, redirect = v6 ? dns6 : dns4;
    memcpy(remote, pkt + (v6 ? 8 : 12), v6 ? 16 : 4);
    memcpy(local, pkt + (v6 ? 24 : 16), v6 ? 16 : 4);
    memcpy(&rport, pkt + l4, 2);
    memcpy(&lport, pkt + l4 + 2, 2);
    if (redirect && dns_hybrid_enabled()) {
        int v = dns_handle_incoming_hybrid(local, lport, remote, rport, data, dlen, &ci, (uint8_t)v6);
        if (v == DNS_IN_DROP) return 1;
        rewrite = v == DNS_IN_REWRITE;
        trusted = rewrite || v == DNS_IN_PASS;
    } else if (redirect) {
        int from_clean = v6 ? (rport == dns6_port && !memcmp(remote, dns6_addr, 16))
                            : (rport == dns4_port && remote[0] == dns4_addr);
        if (from_clean && dns_handle_incoming(local, lport, data, dlen, &ci, (uint8_t)v6)) {
            rewrite = 1;
            trusted = 1;
        }
    } else {
        trusted = dns_answer_matches(local, lport, remote, rport, data, dlen, (uint8_t)v6);
    }
    if (rewrite) {
        /* looks like the answer of the resolver the program asked */
        memcpy(pkt + (v6 ? 8 : 12), ci.dstip, v6 ? 16 : 4);
        memcpy(pkt + l4, &ci.dstport, 2);
        memcpy(pkt + l4 + 2, &ci.srcport, 2);
        WinDivertHelperCalcChecksums(pkt, len, addr, 0);
        if (dns_hybrid_enabled()) InterlockedIncrement(&stats.dns);
    }
    /* payment learning: only answers to our questions from the server asked
     * (or the clean one), and never one that looks forged */
    if (trusted && exclude_enabled() && !dns_answer_is_forged(data, dlen)) exclude_learn_dns(data, dlen);
    pass(pkt, len, addr);
    return 1;
}

/* ================= the filter ================= */
static void build_filter(char *f, size_t cap, int alert_watch) {
    char udp[1024] = "";
    int o;
    /* outbound web: the first request (TLS handshake records, HTTP), and the
     * client's own TLS alerts (a decoy that upset the server); with the TLS
     * record split, every IPv4 443 segment (their sequence numbers move) */
    if (cfg.tlsrec)
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
    /* QUIC Initials (both families) and Discord voice (IPv4) */
    if (cfg.quic_block && udp_windivert_clause(udp, sizeof(udp), 1, 0, UDP_PREFILTER_PAYLOAD) > 0)
        o += snprintf(f + o, cap - o, " or (outbound and !loopback and udp and %s)", udp);
    if (cfg.udp_fake && udp_windivert_clause(udp, sizeof(udp), 0, 1, UDP_PREFILTER_PAYLOAD) > 0)
        o += snprintf(f + o, cap - o, " or (outbound and !loopback and ip and udp and %s)", udp);
    /* DNS: the system's questions, and the answers to them */
    if (dns4 || dns6 || exclude_enabled()) {
        o += snprintf(f + o, cap - o, " or (outbound and !loopback and udp and udp.DstPort == 53)");
        if (dns_hybrid_enabled() || (!dns4 && !dns6))
            o += snprintf(f + o, cap - o, " or (inbound and !loopback and udp and udp.SrcPort == 53)");
        if (dns4)
            o += snprintf(f + o, cap - o, " or (inbound and !loopback and udp and ip.SrcAddr == %s and udp.SrcPort == %u)",
                          dns4_text, (unsigned)ntohs(dns4_port));
        if (dns6)
            o += snprintf(f + o, cap - o, " or (inbound and !loopback and udp and ipv6.SrcAddr == %s and udp.SrcPort == %u)",
                          dns6_text, (unsigned)ntohs(dns6_port));
    }
    /* inbound: forged resets, SYN-ACKs (hop distance), TLS alerts, and the
     * acknowledgements of a connection whose record was split */
    {
        char sub[256] = "";
        int so = 0;
        if (cfg.tlsrec) so += snprintf(sub + so, sizeof(sub) - so, "(ip and tcp.SrcPort == 443)");
        if (cfg.drop_rst) so += snprintf(sub + so, sizeof(sub) - so, "%stcp.Rst", so ? " or " : "");
        if (cfg.autottl) so += snprintf(sub + so, sizeof(sub) - so, "%s(tcp.Syn and tcp.Ack)", so ? " or " : "");
        if (alert_watch)
            so += snprintf(sub + so, sizeof(sub) - so,
                           "%s(tcp.SrcPort == 443 and tcp.PayloadLength >= 7 and tcp.Payload[0] == 21 and tcp.Payload[1] == 3)",
                           so ? " or " : "");
        if (sub[0])
            snprintf(f + o, cap - o, " or (inbound and !loopback and tcp and (tcp.SrcPort == 443 or tcp.SrcPort == 80) and (%s))", sub);
    }
}

/* ================= the loop ================= */
static int excluded_conn(const uint8_t *pay, const proto_info *pi, const uint8_t *daddr, int v6) {
    if (!exclude_enabled()) return 0;
    if (exclude_check_ip(daddr, v6)) return 1;
    return pi->host_off >= 0 && pi->host_len > 0 &&
           exclude_check_hostname((const char *)pay + pi->host_off, (size_t)pi->host_len);
}

static void count_kind(const proto_info *pi) {
    InterlockedIncrement(pi->kind == PROTO_TLS ? &stats.https : &stats.http);
}

/* SYN, FIN or RST on a segment with data (TCP Fast Open, data + FIN): never
 * desynced - every crafted part would carry the flag. */
#define TCP_CTL(flags) ((flags) & 0x07)

static int run_loop(void) {
    static uint8_t pkt[65535];
    WINDIVERT_ADDRESS addr;
    UINT len;
    int errors = 0;
    for (;;) {
        if (!WinDivertRecv(wd, pkt, sizeof(pkt), &len, &addr)) {
            DWORD e = GetLastError();
            if (e == ERROR_NO_DATA) return 0;               /* shut down */
            if (e == ERROR_INSUFFICIENT_BUFFER) continue;   /* a packet too big for us: dropped by WinDivert */
            if (++errors >= 100) {                          /* the handle is gone: let the app restart us */
                printf("Engine stopped: receive error %lu\n", (unsigned long)e);
                return 1;
            }
            Sleep(10);
            continue;
        }
        errors = 0;
        InterlockedIncrement(&stats.packets);
        if (len < 28) { pass(pkt, len, &addr); continue; }

        if (addr.IPv6) {
            /* ---------- IPv6 (no extension headers) ---------- */
            int nh = pkt[6];
            if (len < 40 + 8 || (nh != 6 && nh != 17)) { pass(pkt, len, &addr); continue; }
            if (nh == 17) {
                uint16_t sp = rd16be(pkt + 40), dp = rd16be(pkt + 42);
                if (addr.Outbound && dp == 53) { if (!dns_outbound(pkt, len, 1, 40, &addr)) pass(pkt, len, &addr); continue; }
                if (!addr.Outbound && (sp == 53 || (dns6 && sp == ntohs(dns6_port)))) { dns_inbound(pkt, len, 1, 40, &addr); continue; }
                if (addr.Outbound && dp == 443 && cfg.quic_block && quic_is_initial(pkt + 48, (int)len - 48) &&
                    !rawscope_local6(pkt + 24)) continue;          /* QUIC Initial: dropped */
                pass(pkt, len, &addr);
                continue;
            }
            if ((int)len < 40 + 20) { pass(pkt, len, &addr); continue; }
            {
                int tcphl = ((pkt[40 + 12] >> 4) & 0x0f) * 4, poff = 40 + tcphl, handled = 0;
                if (tcphl < 20 || poff > (int)len) { pass(pkt, len, &addr); continue; }
                int flags = pkt[40 + 13], plen = (int)len - poff;
                if (!addr.Outbound) {
                    if (cfg.drop_rst && (flags & 0x04) && tcpdpi_is_injected_rst6(pkt, (int)len) &&
                        rawscope_touched(pkt + 8, 16, now_ms()))
                        continue;                                   /* forged reset: dropped */
                    if (cfg.autottl && (flags & 0x02) && (flags & 0x10)) {   /* SYN-ACK: hop distance */
                        int hops = tcpdpi_hops_from_ttl(pkt[7]);
                        if (hops > 0) tcpdpi_hops_put(key6(pkt + 8), hops, now_ms());
                    }
                    if (rd16be(pkt + 40) == 443 && plen > 0 &&
                        rawscope_tls_kind(pkt + poff, plen) != RAWSCOPE_TLS_OTHER)
                        rawscope_decoy_hurt_in(pkt + 8, 16, 443, rd16be(pkt + 42), pkt + poff, plen, now_ms());
                    pass(pkt, len, &addr);
                    continue;
                }
                if (plen > 0 && !TCP_CTL(flags)) {
                    uint64_t now = now_ms();
                    uint16_t sport = rd16be(pkt + 40), dport = rd16be(pkt + 42);
                    uint32_t seq = rd32be(pkt + 44);
                    proto_info pi;
                    if (dport == 443) rawscope_decoy_hurt_out(pkt + 24, 16, dport, sport, seq, pkt + poff, plen, now);
                    proto_inspect(pkt + poff, (size_t)plen, &pi);
                    if (pi.kind != PROTO_UNKNOWN && !rawscope_local6(pkt + 24)) {
                        if (excluded_conn(pkt + poff, &pi, pkt + 24, 1)) {
                            InterlockedIncrement(&stats.excluded);
                        } else {
                            int decoy = !no_decoy && !rawscope_nodecoy(pkt + 24, 16, now);
                            rawscope_touch(pkt + 24, 16, now);
                            if (decoy && pi.kind == PROTO_TLS)
                                rawscope_decoy_sent(pkt + 24, 16, dport, sport,
                                                    rawscope_tls_next_seq(seq, pkt + poff, plen), now);
                            count_kind(&pi);
                            handled = desync6(pkt, (int)len, tcphl, &pi, decoy, &addr);
                        }
                    }
                }
                if (!handled) pass(pkt, len, &addr);
            }
            continue;
        }

        /* ---------- IPv4 ---------- */
        {
            int ihl = (pkt[0] & 0x0f) * 4, proto = pkt[9];
            if (ihl < 20 || (int)len < ihl + 8) { pass(pkt, len, &addr); continue; }

            if (proto == 17) {
                const uint8_t *up = pkt + ihl + 8;
                int plen = (int)len - ihl - 8;
                uint16_t sp = rd16be(pkt + ihl), dp = rd16be(pkt + ihl + 2);
                if (addr.Outbound && dp == 53) { if (!dns_outbound(pkt, len, 0, ihl, &addr)) pass(pkt, len, &addr); continue; }
                if (!addr.Outbound && (sp == 53 || (dns4 && sp == ntohs(dns4_port)))) { dns_inbound(pkt, len, 0, ihl, &addr); continue; }
                if (addr.Outbound && cfg.quic_block && dp == 443 && plen > 0 && quic_is_initial(up, plen) &&
                    !rawscope_local4(pkt + 16))
                    continue;                                       /* QUIC Initial: dropped */
                if (addr.Outbound && cfg.udp_fake && discord_voice_port(dp) && plen > 0 &&
                    udp_is_discord_voice(up, plen) && !rawscope_local4(pkt + 16))
                    send_udp_decoys4(pkt, ihl, plen, &addr);
                pass(pkt, len, &addr);
                continue;
            }
            if (proto != 6 || (int)len < ihl + 20) { pass(pkt, len, &addr); continue; }
            {
                const uint8_t *tcp = pkt + ihl;
                int tcphl = ((tcp[12] >> 4) & 0x0f) * 4, poff = ihl + tcphl, plen, handled = 0;
                if (tcphl < 20 || poff > (int)len) { pass(pkt, len, &addr); continue; }
                plen = (int)len - poff;

                if (!addr.Outbound) {
                    if (cfg.drop_rst && (tcp[13] & 0x04) && tcpdpi_is_injected_rst4(pkt, (int)len) &&
                        rawscope_touched(pkt + 12, 4, now_ms()))
                        continue;                                   /* forged reset: dropped */
                    if (cfg.autottl && (tcp[13] & 0x02) && (tcp[13] & 0x10)) {
                        int hops = tcpdpi_hops_from_ttl(pkt[8]);
                        if (hops > 0) tcpdpi_hops_put(rd32be(pkt + 12), hops, now_ms());
                    }
                    /* a TLS alert from as far away as the server's own SYN-ACK:
                     * a decoy that reached it (one injected on the way has
                     * another hop count and must not switch decoys off) */
                    if (rd16be(tcp) == 443 && plen > 0 &&
                        rawscope_tls_kind(pkt + poff, plen) != RAWSCOPE_TLS_OTHER) {
                        int known = tcpdpi_hops_get(rd32be(pkt + 12), now_ms()), h = tcpdpi_hops_from_ttl(pkt[8]);
                        if (known <= 0 || h <= 0 || (h - known <= 1 && known - h <= 1))
                            rawscope_decoy_hurt_in(pkt + 12, 4, 443, rd16be(tcp + 2), pkt + poff, plen, now_ms());
                    }
                    if (cfg.tlsrec) {
                        /* the server acknowledges grown bytes: take the growth
                         * back off. The translation goes on through the close
                         * (FIN and its ACK carry shifted numbers too); only a
                         * reset ends it at once. */
                        uint32_t sa = rd32be(pkt + 12), da = rd32be(pkt + 16), ack = rd32be(pkt + ihl + 8);
                        uint16_t sp = rd16be(tcp), dp = rd16be(tcp + 2);
                        int changed = (tcp[13] & 0x10) && tcpdpi_xlate_in(sa, da, sp, dp, &ack, now_ms());
                        if (tcp[13] & 0x04) tcpdpi_xlate_end(da, sa, dp, sp);
                        else if (tcp[13] & 0x01) tcpdpi_xlate_fin(da, sa, dp, sp, 1, now_ms());
                        if (changed) {
                            wr32be(pkt + ihl + 8, ack);
                            WinDivertHelperCalcChecksums(pkt, len, &addr, 0);
                        }
                    }
                    pass(pkt, len, &addr);
                    continue;
                }

                if (cfg.tlsrec) {
                    uint32_t sa = rd32be(pkt + 12), da = rd32be(pkt + 16), s = rd32be(pkt + ihl + 4);
                    uint16_t sp = rd16be(tcp), dp = rd16be(tcp + 2);
                    int changed;
                    /* a new connection on a four-tuple seen before: the old
                     * translation must not touch it */
                    if ((tcp[13] & 0x02) && !(tcp[13] & 0x10)) tcpdpi_xlate_end(sa, da, sp, dp);
                    changed = tcpdpi_xlate_out(sa, da, sp, dp, &s, now_ms());
                    if (tcp[13] & 0x04) tcpdpi_xlate_end(sa, da, sp, dp);
                    else if (tcp[13] & 0x01) tcpdpi_xlate_fin(sa, da, sp, dp, 0, now_ms());
                    if (changed) {
                        wr32be(pkt + ihl + 4, s);
                        WinDivertHelperCalcChecksums(pkt, len, &addr, 0);
                        pass(pkt, len, &addr);
                        continue;
                    }
                }
                if (plen > 0 && !TCP_CTL(tcp[13])) {
                    uint64_t now = now_ms();
                    uint16_t sport = rd16be(tcp), dport = rd16be(tcp + 2);
                    uint32_t seq = rd32be(pkt + ihl + 4);
                    proto_info pi;
                    if (dport == 443) rawscope_decoy_hurt_out(pkt + 16, 4, dport, sport, seq, pkt + poff, plen, now);
                    proto_inspect(pkt + poff, (size_t)plen, &pi);
                    if (pi.kind != PROTO_UNKNOWN && !rawscope_local4(pkt + 16)) {
                        if (excluded_conn(pkt + poff, &pi, pkt + 16, 0)) {
                            InterlockedIncrement(&stats.excluded);
                        } else {
                            int decoy = !no_decoy && !rawscope_nodecoy(pkt + 16, 4, now);
                            rawscope_touch(pkt + 16, 4, now);
                            if (decoy && pi.kind == PROTO_TLS)
                                rawscope_decoy_sent(pkt + 16, 4, dport, sport,
                                                    rawscope_tls_next_seq(seq, pkt + poff, plen), now);
                            count_kind(&pi);
                            handled = desync4(pkt, (int)len, ihl, tcphl, poff, &pi, decoy, &addr);
                        }
                    }
                }
                if (!handled) pass(pkt, len, &addr);
            }
        }
    }
}

/* ================= command line ================= */
static void usage(void) {
    printf("TurkishDPI engine %s\n"
           "  --fake-ttl N            decoy TTL when the hop distance is unknown (default 4)\n"
           "  --autottl 0|1           decoy TTL from the measured hop distance (default 1)\n"
           "  --drop-rst 0|1          drop the resets the filter forges (default 1)\n"
           "  --split off|on|disorder cut the real request so no segment holds the whole name\n"
           "  --desync fake|fakedsplit|fakeddisorder|none   decoy technique (none: only the cut)\n"
           "  --seqovl N              overlap the first segment by N bytes (IPv4)\n"
           "  --tlsrec 0|1            split the ClientHello's TLS record in two (IPv4)\n"
           "  --decoy-name HOST       a name the network allows, carried by decoys\n"
           "  --udp-fake 0|1          decoys ahead of Discord voice datagrams (default 0)\n"
           "  --no-quic-block         keep QUIC (by default its Initial is dropped)\n"
           "  --dns-addr IP --dns-port N        clean IPv4 resolver for the system's DNS\n"
           "  --dnsv6-addr IP --dnsv6-port N    clean IPv6 resolver\n"
           "  --dns-hybrid            ask both; the first trustworthy answer wins\n"
           "  --dns-block-ip IP       a block-page address answers are checked against\n"
           "  --exclude FILE          never touch these domains (one per line)\n"
           "  --no-payment-exclude    also touch banks and payment systems (not recommended)\n",
           TURKISHDPI_VERSION_A);
}

static int parse_port(const char *s, uint16_t *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || end == s || *end || v <= 0 || v > 65535) return 0;
    *out = htons((uint16_t)v);
    return 1;
}

int engine_main(int argc, char *argv[]) {
    char filter[4096];
    int payment = 1, dns_hybrid = 0, alert_watch;
    const char *bad = NULL;
    WSADATA w;

    WSAStartup(MAKEWORD(2, 2), &w);
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&cfg, 0, sizeof(cfg));
    raw_config_defaults(&cfg, 4);
    cfg.udp_fake = 0;
    dns4_port = dns6_port = htons(53);

    for (int i = 1; i < argc && !bad; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        /* a missing value ends the parse (break leaves the for loop) */
#define NEED() if (!v) { bad = a; break; } i++
        if (!strcmp(a, "--fake-ttl")) { NEED(); cfg.fake_ttl = atoi(v); if (cfg.fake_ttl < 1 || cfg.fake_ttl > 255) bad = a; }
        else if (!strcmp(a, "--autottl")) { NEED(); cfg.autottl = atoi(v) ? 1 : 0; }
        else if (!strcmp(a, "--drop-rst")) { NEED(); cfg.drop_rst = atoi(v) ? 1 : 0; }
        else if (!strcmp(a, "--split")) {
            NEED();
            if (!strcmp(v, "off")) cfg.split = RAW_SPLIT_OFF;
            else if (!strcmp(v, "on")) cfg.split = RAW_SPLIT_ON;
            else if (!strcmp(v, "disorder")) cfg.split = RAW_SPLIT_DISORDER;
            else bad = a;
        }
        else if (!strcmp(a, "--desync")) {
            NEED();
            no_decoy = 0;
            if (!strcmp(v, "fake")) cfg.desync_mode = RAW_DESYNC_FAKE;
            else if (!strcmp(v, "fakedsplit")) cfg.desync_mode = RAW_DESYNC_FAKEDSPLIT;
            else if (!strcmp(v, "fakeddisorder")) cfg.desync_mode = RAW_DESYNC_FAKEDDISORDER;
            else if (!strcmp(v, "none")) { cfg.desync_mode = RAW_DESYNC_FAKE; no_decoy = 1; }
            else bad = a;
        }
        else if (!strcmp(a, "--seqovl")) { NEED(); cfg.seqovl = atoi(v); if (cfg.seqovl < 0 || cfg.seqovl > 1000) bad = a; }
        else if (!strcmp(a, "--tlsrec")) { NEED(); cfg.tlsrec = atoi(v) ? 1 : 0; }
        else if (!strcmp(a, "--decoy-name")) { NEED(); if (proto_decoy_name_add(v) != 0) bad = a; }
        else if (!strcmp(a, "--udp-fake")) { NEED(); cfg.udp_fake = atoi(v) ? 1 : 0; }
        else if (!strcmp(a, "--no-quic-block")) cfg.quic_block = 0;
        else if (!strcmp(a, "--dns-addr")) {
            NEED();
            if (inet_pton(AF_INET, v, &dns4_addr) != 1) bad = a;
            else { dns4 = 1; snprintf(dns4_text, sizeof(dns4_text), "%s", v); }
        }
        else if (!strcmp(a, "--dns-port")) { NEED(); if (!parse_port(v, &dns4_port)) bad = a; }
        else if (!strcmp(a, "--dnsv6-addr")) {
            NEED();
            if (inet_pton(AF_INET6, v, dns6_addr) != 1) bad = a;
            else { dns6 = 1; snprintf(dns6_text, sizeof(dns6_text), "%s", v); }
        }
        else if (!strcmp(a, "--dnsv6-port")) { NEED(); if (!parse_port(v, &dns6_port)) bad = a; }
        else if (!strcmp(a, "--dns-hybrid")) dns_hybrid = 1;
        else if (!strcmp(a, "--dns-block-ip")) { NEED(); if (!dns_add_block_ip(v)) bad = a; }
        else if (!strcmp(a, "--exclude")) { NEED(); if (exclude_load_list(v) < 0) bad = a; }
        else if (!strcmp(a, "--no-payment-exclude")) payment = 0;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else bad = a;
#undef NEED
    }
    if (bad) {
        printf("Unknown or invalid option: %s\n", bad);
        usage();
        return 2;
    }
    if (cfg.fake_repeats <= 0 || cfg.fake_repeats > 64) cfg.fake_repeats = 6;
    if (payment) exclude_load_builtin();
    if (dns_hybrid && (dns4 || dns6)) dns_set_hybrid(1);
    dns_set_clean(dns4_addr, dns4_port, dns6_addr, dns6_port);
    udp_flow_init(&flows);
    tcpdpi_hops_reset();
    tcpdpi_xlate_reset();
    rawscope_touch_reset();
    rawscope_nodecoy_reset();

    printf("TurkishDPI engine %s\n", TURKISHDPI_VERSION_A);
    printf("decoy=%s ttl=%d autottl=%d split=%s drop_rst=%d seqovl=%d tlsrec=%d quic_block=%d "
           "dns=%s payment_exclude=%d\n",
           no_decoy ? "none" : cfg.desync_mode == RAW_DESYNC_FAKEDSPLIT ? "fakedsplit"
                             : cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER ? "fakeddisorder" : "fake",
           cfg.fake_ttl, cfg.autottl,
           cfg.split == RAW_SPLIT_OFF ? "off" : cfg.split == RAW_SPLIT_DISORDER ? "disorder" : "on",
           cfg.drop_rst, cfg.seqovl, cfg.tlsrec, cfg.quic_block,
           dns4 || dns6 ? (dns_hybrid_enabled() ? "hybrid" : "redirect") : "system", payment);

    /* A filter the driver refuses (87) is retried without the TLS-alert
     * watch; any other error is the driver's or the files' and is final. */
    for (alert_watch = 1; alert_watch >= 0; alert_watch--) {
        build_filter(filter, sizeof(filter), alert_watch);
        wd = WinDivertOpen(filter, WINDIVERT_LAYER_NETWORK, 0, 0);
        if (wd != INVALID_HANDLE_VALUE || GetLastError() != ERROR_INVALID_PARAMETER) break;
    }
    if (wd == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        printf("Error opening filter: %lu\n", (unsigned long)e);
        return 1;
    }
    if (dns4 || dns6) flush_dns_cache();
    printf("Filter activated\n");
    if (engine_report_stats) {
        HANDLE t = CreateThread(NULL, 64 * 1024, stats_reporter, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (t) CloseHandle(t);
    }
    {
        int rc = run_loop();
        WinDivertClose(wd);
        return rc;
    }
}
