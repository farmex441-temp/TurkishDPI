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
 * Everything crafted is sent in pieces of at most TCPDPI_PIECE_MAX payload
 * bytes: WinDivert sees segments before NIC segmentation offload, so one
 * captured ClientHello can be larger than any packet on the wire.
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
#include "policy.h"
#include "engine.h"
#include "version.h"

int engine_report_stats = 0;

/* ---------- configuration ---------- */
static raw_config cfg;
static int no_decoy;                        /* only the cut, never a decoy */
static int split_pos;                       /* >0: cut the request at this byte, not at the name */
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
/* maxpay: the largest TCP payload of an outbound ClientHello segment seen
 * (larger than one MSS means the stack hands segments over before NIC
 * segmentation offload). It is appended at the END of the STATS line: the
 * app's sscanf reads the first five fields and ignores what follows. */
static struct { volatile LONG packets, https, http, excluded, dns, maxpay; } stats;

static DWORD WINAPI stats_reporter(LPVOID unused) {
    LONG last[6] = { -1, -1, -1, -1, -1, -1 };
    (void)unused;
    for (;;) {
        LONG now[6] = { stats.packets, stats.https, stats.http, stats.excluded, stats.dns, stats.maxpay };
        if (memcmp(now, last, sizeof(now))) {
            memcpy(last, now, sizeof(now));
            printf("STATS packets=%ld https=%ld http=%ld excluded=%ld dns=%ld maxpay=%ld\n",
                   now[0], now[1], now[2], now[3], now[4], now[5]);
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

/* The decoy's payload: the request with another (or a scrambled) name. A
 * decoy is one piece: of a request larger than TCPDPI_PIECE_MAX only the
 * first TCPDPI_PIECE_MAX bytes are copied (the name, when it is in this
 * segment at all, sits early in it). */
static int decoy_payload(uint8_t *out, int cap, const uint8_t *pay, int plen,
                         const proto_info *pi, int iphl, int tcphl) {
    int n;
    if (plen > TCPDPI_PIECE_MAX) plen = TCPDPI_PIECE_MAX;
    n = proto_decoy_rename(out, cap, pay, plen, pi->kind == PROTO_TLS, pi->host_off, pi->host_len,
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

/* Where the real request is cut: a fixed early byte (--split-pos: the
 * filter then sees a piece too short to classify, and a piece without a TLS
 * record header), or - by default - inside the site name (Shroudly); a
 * ClientHello whose name is not in this segment is cut after byte 2. */
static int cut_points(const uint8_t *pay, int plen, const proto_info *pi, int cuts[2]) {
    return tcpdpi_choose_cuts(pay, plen, pi->kind == PROTO_TLS, pi->host_off, pi->host_len, split_pos, cuts);
}

/* Builders' scratch: the packet loop is single-threaded. A plan is built in
 * full before anything is sent, so a failure leaves the original to pass. */
static tcpdpi_piece plan[TCPDPI_PIECES_MAX];
static uint8_t plan_seg[TCPDPI_PIECES_MAX][TCPDPI_SEG_MAX];
static int plan_len[TCPDPI_PIECES_MAX];
static uint8_t grown[65535 + TCPDPI_TLSREC_GROWTH];   /* a TLS record split of a whole packet */

/* ================= IPv4 (adapted from Shroudly raw_windows.c) ============= */
static int decoy_ttl4(uint32_t daddr) {
    if (cfg.autottl) {
        int hops = tcpdpi_hops_get(daddr, now_ms());
        if (hops > 0) return tcpdpi_decoy_ttl(hops, cfg.autottl_margin, cfg.autottl_min);
    }
    return cfg.fake_ttl;
}

/* Builds every piece of plan[0..n) from data (seq: the sequence number of
 * data[0]) into plan_seg, IP ids from ipid on, and takes those ids; 1 when
 * all were built, 0 when any could not be (nothing taken, nothing sent). */
static int build_plan4(const uint8_t *pkt, int ihl, int tcphl, const uint8_t *data, int n,
                       uint32_t seq, int ttl, int fake_ttl) {
    if (n <= 0 || n > TCPDPI_PIECES_MAX) return 0;
    for (int i = 0; i < n; i++) {
        plan_len[i] = tcpdpi_build_piece4(plan_seg[i], sizeof(plan_seg[i]), pkt, ihl, pkt + ihl, tcphl,
                                          ipid, seq, ttl, fake_ttl, cfg.fake_pattern, data, &plan[i]);
        if (plan_len[i] <= 0) return 0;
    }
    ipid = (uint16_t)(ipid + n);
    return 1;
}

static void send_plan(int n, const WINDIVERT_ADDRESS *addr, int v6) {
    for (int i = 0; i < n; i++) send_crafted(plan_seg[i], plan_len[i], addr, v6);
}

static int send_plan4(const uint8_t *pkt, int ihl, int tcphl, const uint8_t *data, int n,
                      uint32_t seq, int ttl, int fake_ttl, const WINDIVERT_ADDRESS *addr) {
    if (!build_plan4(pkt, ihl, tcphl, data, n, seq, ttl, fake_ttl)) return 0;
    send_plan(n, addr, 0);
    return 1;
}

static void send_fake4(const uint8_t *pkt, int len, int ihl, int tcphl, int poff,
                       const proto_info *pi, int ttl, const WINDIVERT_ADDRESS *addr) {
    uint8_t pay[TCPDPI_PIECE_MAX], out[TCPDPI_SEG_MAX];
    int plen = len - poff, dlen, n;
    if (plen <= 0) return;
    dlen = decoy_payload(pay, (int)sizeof(pay), pkt + poff, plen, pi, ihl, tcphl);
    if (dlen <= 0) return;
    n = tcpdpi_build_segment4(out, sizeof(out), pkt, ihl, pkt + ihl, tcphl, ipid++,
                              rd32be(pkt + ihl + 4), ttl > 0 ? ttl : cfg.fake_ttl, pay, dlen, 1);
    if (n > 0) send_crafted(out, n, addr, 0);
}

static int send_cut4(const uint8_t *pkt, int ihl, int tcphl, int poff, int plen,
                     const proto_info *pi, int disorder, const WINDIVERT_ADDRESS *addr) {
    int cuts[2], nc = cut_points(pkt + poff, plen, pi, cuts);
    int ttl = pkt[8] ? pkt[8] : 64;
    int n = nc > 0 ? tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, cuts, nc, disorder) : 0;
    return send_plan4(pkt, ihl, tcphl, pkt + poff, n, rd32be(pkt + ihl + 4), ttl, ttl, addr);
}

/* 1 when the original was replaced by what was sent. */
static int desync4(const uint8_t *pkt, int len, int ihl, int tcphl, int poff,
                   const proto_info *pi, int decoy, const WINDIVERT_ADDRESS *addr) {
    int plen = len - poff;
    uint32_t daddr = rd32be(pkt + 16), seq = rd32be(pkt + ihl + 4);
    int otl = pkt[8] ? pkt[8] : 64;
    int faked = cfg.desync_mode == RAW_DESYNC_FAKEDSPLIT || cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER;

    /* The TLS record split needs the name (the cut goes inside it); a
     * ClientHello whose name is not in this segment gets the plain cut below
     * (after byte 2) instead. The two records are sent in pieces: record 1,
     * then record 2 and the rest, each run at most TCPDPI_PIECE_MAX bytes. */
    if (cfg.tlsrec && pi->kind == PROTO_TLS && pi->host_off >= 1) {
        int cuts[2], nc = tcpdpi_split_points(pkt + poff, plen, pi->host_off, pi->host_len, cuts);
        int cut = nc >= 2 ? cuts[1] : (nc == 1 ? cuts[0] : 0);
        int rec1 = 0, glen = cut > 5 ? tcpdpi_tlsrec_form(grown, (int)sizeof(grown), pkt + poff, plen, cut, &rec1) : 0;
        int n = glen > 0 && rec1 > 0 && rec1 < glen ? tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, glen, &rec1, 1, 0) : 0;
        if (n > 0 && build_plan4(pkt, ihl, tcphl, grown, n, seq, otl, otl) &&
            tcpdpi_xlate_begin(rd32be(pkt + 12), daddr, rd16be(pkt + ihl), rd16be(pkt + ihl + 2),
                               seq + (uint32_t)plen, TCPDPI_TLSREC_GROWTH, now_ms())) {
            if (decoy) send_fake4(pkt, len, ihl, tcphl, poff, pi, decoy_ttl4(daddr), addr);
            send_plan(n, addr, 0);
            return 1;
        }
    }
    if (!decoy) {
        if (cfg.split == RAW_SPLIT_OFF && !faked) return 0;
        return send_cut4(pkt, ihl, tcphl, poff, plen, pi,
                         cfg.split == RAW_SPLIT_DISORDER || cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER, addr);
    }
    if (faked) {
        int split = split_pos > 0 ? (split_pos < plen ? split_pos : plen - 1)
                  : (pi->host_off > 0 && pi->host_off < plen) ? pi->host_off : (plen > 2 ? 2 : 1);
        int n = tcpdpi_plan_fakedsplit(plan, TCPDPI_PIECES_MAX, plen, split,
                                       cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER);
        return send_plan4(pkt, ihl, tcphl, pkt + poff, n, seq, otl, decoy_ttl4(daddr), addr);
    }
    send_fake4(pkt, len, ihl, tcphl, poff, pi, decoy_ttl4(daddr), addr);
    if (cfg.seqovl > 0 && plen > 0) {
        /* The overlapped first segment carries at most TCPDPI_PIECE_MAX
         * bytes (pattern included); the rest of a larger payload follows in
         * order, in pieces. */
        uint8_t out[TCPDPI_SEG_MAX];
        int first = plen + cfg.seqovl <= TCPDPI_PIECE_MAX ? plen : TCPDPI_PIECE_MAX - cfg.seqovl;
        int n = 0, rest = 0;
        if (first > 0 && first < plen) {
            rest = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen - first, NULL, 0, 0);
            if (rest <= 0 || !build_plan4(pkt, ihl, tcphl, pkt + poff + first, rest,
                                          seq + (uint32_t)first, otl, otl)) first = 0;
            /* build_plan4 took ids ipid-rest .. ipid-1; the overlap gets the next */
        }
        if (first > 0)
            n = tcpdpi_build_seqovl4(out, sizeof(out), pkt, ihl, pkt + ihl, tcphl, ipid, seq, otl,
                                     pkt + poff, first, cfg.seqovl, cfg.seqovl_pattern);
        if (n > 0) {
            ipid++;
            send_crafted(out, n, addr, 0);
            if (first < plen) send_plan(rest, addr, 0);
            return 1;
        }
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
/* IPv6 TCP segments like the captured one, with another sequence number,
 * hop limit and payload, are built by tcpdpi_build_segment6/_piece6 (the TCP
 * checksum computed there; a decoy's is spoiled on purpose). Only packets
 * without extension headers come here (next header 6 at the fixed header). */

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
    uint8_t out[TCPDPI_SEG_MAX];
    int cuts[2], nc, n;
    int disorder = cfg.split == RAW_SPLIT_DISORDER || cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER;
    int cut = cfg.split != RAW_SPLIT_OFF || cfg.desync_mode != RAW_DESYNC_FAKE;

    if (plen <= 0 || tcphl > 60) return 0;
    if (decoy) {
        uint8_t fake[TCPDPI_PIECE_MAX];      /* one piece: decoy_payload truncates */
        int dlen = decoy_payload(fake, (int)sizeof(fake), pay, plen, pi, 40, tcphl);
        if (dlen > 0 && (n = tcpdpi_build_segment6(out, sizeof(out), pkt, pkt + 40, tcphl, seq,
                                                   decoy_ttl6(pkt + 24), fake, dlen, 1)) > 0)
            send_crafted(out, n, addr, 1);
    }
    if (!cut) return 0;
    nc = cut_points(pay, plen, pi, cuts);
    n = nc > 0 ? tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, cuts, nc, disorder) : 0;
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) {              /* all built before any is sent */
        plan_len[i] = tcpdpi_build_piece6(plan_seg[i], sizeof(plan_seg[i]), pkt, pkt + 40, tcphl,
                                          seq, hop, hop, 0, pay, &plan[i]);
        if (plan_len[i] <= 0) return 0;      /* nothing sent: the original goes on */
    }
    send_plan(n, addr, 1);
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
/* The filter text (policy_build_filter, unit tested); -1 when it does not
 * fit the buffer - the engine then refuses to start. */
static int build_filter(char *f, size_t cap, int alert_watch) {
    policy_filter_opts o;
    memset(&o, 0, sizeof(o));
    o.tlsrec = cfg.tlsrec;
    o.quic_block = cfg.quic_block;
    o.udp_fake = cfg.udp_fake;
    o.drop_rst = cfg.drop_rst;
    o.autottl = cfg.autottl;
    o.alert_watch = alert_watch;
    o.dns4 = dns4;
    o.dns6 = dns6;
    o.dns_hybrid = dns_hybrid_enabled();
    o.exclude = exclude_enabled();
    o.dns4_addr = dns4_text;
    o.dns6_addr = dns6_text;
    o.dns4_port = ntohs(dns4_port);
    o.dns6_port = ntohs(dns6_port);
    return policy_build_filter(f, cap, &o);
}

/* ================= the loop ================= */
/* Payment protection: the name in the segment decides; the addresses learned
 * for payment domains only when the segment carries no name. */
static int excluded_conn(const uint8_t *pay, const proto_info *pi, const uint8_t *daddr, int v6) {
    int named = pi->host_off >= 0 && pi->host_len > 0;
    return policy_excluded(exclude_enabled(), named ? (const char *)pay + pi->host_off : NULL,
                           named ? (size_t)pi->host_len : 0, daddr, v6,
                           exclude_check_hostname, exclude_check_ip);
}

static void note_maxpay(const proto_info *pi, int plen) {
    if (pi->kind == PROTO_TLS && plen > stats.maxpay) InterlockedExchange(&stats.maxpay, plen);
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
                    note_maxpay(&pi, plen);
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
                    note_maxpay(&pi, plen);
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
           "  --split-pos N           cut it after byte N instead (e.g. 2), not inside the name\n"
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
    char filter[POLICY_FILTER_CAP];
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
        else if (!strcmp(a, "--split-pos")) { NEED(); split_pos = atoi(v); if (split_pos < 1 || split_pos > 1000) bad = a; }
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
    printf("decoy=%s ttl=%d autottl=%d split=%s split_pos=%d drop_rst=%d seqovl=%d tlsrec=%d quic_block=%d "
           "dns=%s payment_exclude=%d\n",
           no_decoy ? "none" : cfg.desync_mode == RAW_DESYNC_FAKEDSPLIT ? "fakedsplit"
                             : cfg.desync_mode == RAW_DESYNC_FAKEDDISORDER ? "fakeddisorder" : "fake",
           cfg.fake_ttl, cfg.autottl,
           cfg.split == RAW_SPLIT_OFF ? "off" : cfg.split == RAW_SPLIT_DISORDER ? "disorder" : "on",
           split_pos, cfg.drop_rst, cfg.seqovl, cfg.tlsrec, cfg.quic_block,
           dns4 || dns6 ? (dns_hybrid_enabled() ? "hybrid" : "redirect") : "system", payment);

    /* A filter the driver refuses (87) is retried without the TLS-alert
     * watch; any other error is the driver's or the files' and is final. */
    for (alert_watch = 1; alert_watch >= 0; alert_watch--) {
        if (build_filter(filter, sizeof(filter), alert_watch) < 0) {
            printf("Engine filter does not fit its %u-byte buffer: not started\n", (unsigned)sizeof(filter));
            printf("Error opening filter: %lu\n", (unsigned long)ERROR_BUFFER_OVERFLOW);
            return 1;
        }
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
