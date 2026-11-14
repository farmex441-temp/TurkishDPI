/* MSVC: /Isrc/engine/core tests/pieces_test.c src/engine/core/tcpdpi.c src/engine/core/proto.c */
/*
 * TurkishDPI: payloads of any size cut into bounded pieces (tcpdpi_plan_split,
 * tcpdpi_plan_fakedsplit, tcpdpi_build_piece4/6, tcpdpi_tlsrec_form) and the
 * cut choice for a ClientHello whose name is not in the first segment
 * (tcpdpi_choose_cuts). Pure byte buffers, no WinDivert.
 *
 * The reference builders old_multisplit4 / old_fakedsplit4 below are the
 * pre-2.0.2 code (from Shroudly, MIT, see THIRD_PARTY_LICENSES.txt), kept
 * here to prove that payloads that fit give byte-identical segments.
 *
 *   gcc -O2 -Wall -Isrc/engine/core tests/pieces_test.c src/engine/core/tcpdpi.c src/engine/core/proto.c -o build/pieces_test
 */
#include "tcpdpi.h"
#include "proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void wr16(uint8_t *p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* ---------- headers and checksums ---------- */
static void mk_hdr4(uint8_t *b) {             /* 1.2.3.4:50000 -> 5.6.7.8:443, PSH+ACK */
  memset(b, 0, 40);
  b[0] = 0x45; b[6] = 0x40; b[8] = 128; b[9] = 6;
  b[12] = 1; b[13] = 2; b[14] = 3; b[15] = 4; b[16] = 5; b[17] = 6; b[18] = 7; b[19] = 8;
  wr16(b + 20, 50000); wr16(b + 22, 443); b[32] = 0x50; b[33] = 0x18; wr16(b + 34, 1024);
}
static void mk_hdr6(uint8_t *b) {             /* 2001:db8::1 -> 2001:db8::2 */
  memset(b, 0, 60);
  b[0] = 0x60; b[1] = 0x01; b[2] = 0x23; b[3] = 0x45; b[6] = 6; b[7] = 64;
  b[8] = 0x20; b[9] = 0x01; b[10] = 0x0d; b[11] = 0xb8; b[23] = 1;
  b[24] = 0x20; b[25] = 0x01; b[26] = 0x0d; b[27] = 0xb8; b[39] = 2;
  wr16(b + 40, 50000); wr16(b + 42, 443); b[52] = 0x50; b[53] = 0x18; wr16(b + 54, 1024);
}
static uint32_t sum16(const uint8_t *p, int n, uint32_t s) {
  int i = 0;
  for (; i + 1 < n; i += 2) s += (uint32_t)((p[i] << 8) | p[i + 1]);
  if (i < n) s += (uint32_t)(p[i] << 8);
  return s;
}
static int folds_ok(uint32_t s) { while (s >> 16) s = (s & 0xffff) + (s >> 16); return s == 0xffff; }
static int ip4_ok(const uint8_t *p) { return folds_ok(sum16(p, 20, 0)); }
static int tcp4_ok(const uint8_t *p, int len) {
  return folds_ok(sum16(p + 20, len - 20, sum16(p + 12, 8, 0) + 6 + (uint32_t)(len - 20)));
}
static int tcp6_ok(const uint8_t *p, int len) {
  return folds_ok(sum16(p + 40, len - 40, sum16(p + 8, 32, 0) + 6 + (uint32_t)(len - 40)));
}

/* ---------- the old builders (reference) ---------- */
static int old_multisplit4(tcpdpi_seg segs[3], int *nsegs, const uint8_t *ip_tmpl, int ihl,
                           const uint8_t *tcp_tmpl, int tcphl, uint16_t ip_id, uint32_t seq, int ttl,
                           const uint8_t *real, int real_len, const int *cuts, int ncuts, int reverse) {
  if (!segs || !nsegs || !real || !cuts || ncuts < 1 || ncuts > 2 || real_len < 2) return 0;
  int at[4], k = 0;
  at[k++] = 0;
  for (int i = 0; i < ncuts; i++) {
    if (cuts[i] <= at[k - 1] || cuts[i] >= real_len) return 0;
    at[k++] = cuts[i];
  }
  at[k] = real_len;
  for (int i = 0; i < k; i++) {
    int slot = reverse ? k - 1 - i : i;
    int len = tcpdpi_build_segment4(segs[slot].data, sizeof segs[slot].data, ip_tmpl, ihl,
                                    tcp_tmpl, tcphl, (uint16_t)(ip_id + i), seq + (uint32_t)at[i],
                                    ttl, real + at[i], at[i + 1] - at[i], 0);
    if (len <= 0) return 0;
    segs[slot].len = len;
    segs[slot].is_fake = 0;
  }
  *nsegs = k;
  return 1;
}
static int old_fakedsplit4(tcpdpi_seg segs[3], int *nsegs, const uint8_t *ip_tmpl, int ihl,
                           const uint8_t *tcp_tmpl, int tcphl, uint16_t ip_id, uint32_t seq, int ttl,
                           const uint8_t *real, int real_len, int split_pos, int fake_ttl, uint8_t pattern,
                           int disorder) {
  if (real_len < 2 || real_len > 1600 || split_pos < 1 || split_pos >= real_len) return 0;
  int lenA = split_pos, lenB = real_len - split_pos, a, f, b;
  uint32_t seqB = seq + (uint32_t)split_pos;
  uint8_t junk[1600];
  if (!disorder) {
    memset(junk, pattern, (size_t)lenB);
    a = tcpdpi_build_segment4(segs[0].data, sizeof segs[0].data, ip_tmpl, ihl, tcp_tmpl, tcphl, ip_id, seq, ttl, real, lenA, 0);
    f = tcpdpi_build_segment4(segs[1].data, sizeof segs[1].data, ip_tmpl, ihl, tcp_tmpl, tcphl, (uint16_t)(ip_id + 1), seqB, fake_ttl, junk, lenB, 1);
    b = tcpdpi_build_segment4(segs[2].data, sizeof segs[2].data, ip_tmpl, ihl, tcp_tmpl, tcphl, (uint16_t)(ip_id + 2), seqB, ttl, real + split_pos, lenB, 0);
  } else {
    memset(junk, pattern, (size_t)lenA);
    b = tcpdpi_build_segment4(segs[0].data, sizeof segs[0].data, ip_tmpl, ihl, tcp_tmpl, tcphl, ip_id, seqB, ttl, real + split_pos, lenB, 0);
    f = tcpdpi_build_segment4(segs[1].data, sizeof segs[1].data, ip_tmpl, ihl, tcp_tmpl, tcphl, (uint16_t)(ip_id + 1), seq, fake_ttl, junk, lenA, 1);
    a = tcpdpi_build_segment4(segs[2].data, sizeof segs[2].data, ip_tmpl, ihl, tcp_tmpl, tcphl, (uint16_t)(ip_id + 2), seq, ttl, real, lenA, 0);
  }
  if (a <= 0 || f <= 0 || b <= 0) return 0;
  segs[0].len = disorder ? b : a; segs[1].len = f; segs[2].len = disorder ? a : b;
  *nsegs = 3;
  return 1;
}

static tcpdpi_piece plan[TCPDPI_PIECES_MAX];
static uint8_t seg[TCPDPI_SEG_MAX];
static uint8_t big[65536 + 16], back[65536 + 16];
static unsigned char seen[65536 + 16];

static void fill(uint8_t *b, int n, unsigned seed) {
  for (int i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; b[i] = (uint8_t)(seed >> 16); }
}

static int ceil_div(int a, int b) { return (a + b - 1) / b; }

/* Builds every piece (IPv4 and IPv6) and checks: sizes, checksums, contiguous
 * sequence numbers covering every byte once, distinct IP ids, the cut
 * points kept as boundaries, the order. */
static void check_plan(const uint8_t *data, int plen, const int *cuts, int nc, int rev, int n) {
  uint8_t h4[40], h6[60];
  const uint32_t seq0 = 4294966000u;          /* wraps inside large payloads */
  int ids[TCPDPI_PIECES_MAX], expect = 0, start = 0;
  mk_hdr4(h4); mk_hdr6(h6);
  for (int i = 0; i <= nc; i++) { int e = i < nc ? cuts[i] : plen; expect += ceil_div(e - start, TCPDPI_PIECE_MAX); start = e; }
  CHECK(n == expect);
  memset(seen, 0, (size_t)plen);
  memset(back, 0, (size_t)plen);
  for (int i = 0; i < n; i++) {
    const tcpdpi_piece *p = &plan[i];
    CHECK(!p->fake && p->len >= 1 && p->len <= TCPDPI_PIECE_MAX && p->off >= 0 && p->off + p->len <= plen);
    if (i > 0) CHECK(rev ? plan[i].off + plan[i].len == plan[i - 1].off : plan[i - 1].off + plan[i - 1].len == plan[i].off);
    /* IPv4 */
    int len = tcpdpi_build_piece4(seg, sizeof seg, h4, 20, h4 + 20, 20, 0xfff0, seq0, 128, 3, 0, data, p);
    CHECK(len == 40 + p->len && rd16(seg + 2) == len && ip4_ok(seg) && tcp4_ok(seg, len) && seg[8] == 128);
    uint32_t off = rd32(seg + 24) - seq0;
    CHECK(off == (uint32_t)p->off && memcmp(seg + 40, data + p->off, (size_t)p->len) == 0);
    ids[i] = rd16(seg + 4);
    for (int j = 0; j < i; j++) CHECK(ids[j] != ids[i]);
    for (int b = 0; b < p->len; b++) seen[off + (uint32_t)b]++;
    memcpy(back + off, seg + 40, (size_t)p->len);
    /* IPv6 */
    len = tcpdpi_build_piece6(seg, sizeof seg, h6, h6 + 40, 20, seq0, 64, 3, 0, data, p);
    CHECK(len == 60 + p->len && rd16(seg + 4) == 20 + p->len && seg[6] == 6 && seg[7] == 64 && tcp6_ok(seg, len));
    CHECK(rd32(seg + 44) - seq0 == (uint32_t)p->off && memcmp(seg + 60, data + p->off, (size_t)p->len) == 0);
    CHECK(memcmp(seg, h6, 4) == 0 && memcmp(seg + 8, h6 + 8, 32) == 0);     /* flow label, addresses kept */
  }
  for (int b = 0; b < plen; b++) if (seen[b] != 1) { CHECK(seen[b] == 1); break; }
  CHECK(memcmp(back, data, (size_t)plen) == 0);
  /* every cut is a piece boundary */
  for (int c = 0; c < nc; c++) {
    int found = 0;
    for (int i = 0; i < n; i++) if (plan[i].off == cuts[c]) found = 1;
    CHECK(found);
  }
  /* order: in sequence, or strictly descending with byte 0 last */
  CHECK(rev ? plan[n - 1].off == 0 && (n == 1 || plan[0].off > plan[1].off) : plan[0].off == 0);
}

static void test_sizes(void) {
  const int sizes[] = { 3, 1200, 1201, 1800, 4000, 64000 };
  for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
    int plen = sizes[s];
    fill(big, plen, (unsigned)plen);
    int one[1] = { 2 };                                      /* preset 0: after byte 2 */
    int two[2] = { plen == 3 ? 1 : 2, plen == 3 ? 2 : plen * 2 / 3 };
    int far[1] = { plen > 2500 ? 2500 : plen - 1 };          /* a cut past the first piece */
    for (int rev = 0; rev <= 1; rev++) {
      int n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, one, 1, rev);
      check_plan(big, plen, one, 1, rev, n);
      n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, two, 2, rev);
      check_plan(big, plen, two, 2, rev, n);
      n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, far, 1, rev);
      check_plan(big, plen, far, 1, rev, n);
      n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, NULL, 0, rev);
      check_plan(big, plen, NULL, 0, rev, n);
    }
  }
  /* preset 0 on an 1800-byte ClientHello: [1200,1800) [2,1200) [0,2) */
  int one[1] = { 2 };
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 1800, one, 1, 1) == 3);
  CHECK(plan[0].off == 1202 && plan[0].len == 598 && plan[1].off == 2 && plan[1].len == 1200 && plan[2].off == 0 && plan[2].len == 2);
  CHECK(plan[2].id == 0 && plan[1].id == 1 && plan[0].id == 2);     /* ids in sequence order */
  /* the largest packet the loop takes still fits the plan table */
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 65535 + TCPDPI_TLSREC_GROWTH, one, 1, 1) > 0);
  int two[2] = { 2, 40000 };
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 65535, two, 2, 0) > 0);
  /* refusals: too many pieces for cap, cuts not ascending / outside */
  CHECK(tcpdpi_plan_split(plan, 10, 64000, one, 1, 0) == 0);
  int bad1[2] = { 9, 5 }, bad2[1] = { 0 }, bad3[1] = { 100 };
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 100, bad1, 2, 0) == 0);
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 100, bad2, 1, 0) == 0);
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 100, bad3, 1, 0) == 0);
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 0, NULL, 0, 0) == 0);
}

/* Payloads that fit: byte-identical to the old builders. */
static void test_identical(void) {
  uint8_t h4[40];
  tcpdpi_seg olds[3], news[3];
  int on, nn;
  mk_hdr4(h4);
  const int lens[] = { 2, 3, 40, 517, 1199, 1200, 1201, 1450 };
  for (unsigned li = 0; li < sizeof lens / sizeof lens[0]; li++) {
    int plen = lens[li];
    fill(big, plen, 7u + (unsigned)plen);
    for (int rev = 0; rev <= 1; rev++) {
      for (int c1 = 1; c1 < plen; c1 += (plen > 50 ? 97 : 1)) {
        int cuts1[1] = { c1 };
        int ok_old = old_multisplit4(olds, &on, h4, 20, h4 + 20, 20, 0x1234, 99, 128, big, plen, cuts1, 1, rev);
        int fits = c1 <= TCPDPI_PIECE_MAX && plen - c1 <= TCPDPI_PIECE_MAX;
        int n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, cuts1, 1, rev);
        CHECK(ok_old && (n == 2) == fits);
        if (fits) {
          for (int i = 0; i < n; i++) {
            int len = tcpdpi_build_piece4(seg, sizeof seg, h4, 20, h4 + 20, 20, 0x1234, 99, 128, 128, 0, big, &plan[i]);
            CHECK(len == olds[i].len && memcmp(seg, olds[i].data, (size_t)len) == 0);
          }
          CHECK(tcpdpi_build_multisplit4(news, &nn, h4, 20, h4 + 20, 20, 0x1234, 99, 128, big, plen, cuts1, 1, rev) == 1 && nn == 2);
          for (int i = 0; i < nn; i++) CHECK(news[i].len == olds[i].len && memcmp(news[i].data, olds[i].data, (size_t)news[i].len) == 0);
        }
        for (int c2 = c1 + 1; c2 < plen; c2 += (plen > 50 ? 331 : 1)) {
          int cuts2[2] = { c1, c2 };
          ok_old = old_multisplit4(olds, &on, h4, 20, h4 + 20, 20, 0x4321, 7, 64, big, plen, cuts2, 2, rev);
          fits = c1 <= TCPDPI_PIECE_MAX && c2 - c1 <= TCPDPI_PIECE_MAX && plen - c2 <= TCPDPI_PIECE_MAX;
          n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, plen, cuts2, 2, rev);
          CHECK(ok_old && (n == 3) == fits);
          if (fits)
            for (int i = 0; i < n; i++) {
              int len = tcpdpi_build_piece4(seg, sizeof seg, h4, 20, h4 + 20, 20, 0x4321, 7, 64, 64, 0, big, &plan[i]);
              CHECK(len == olds[i].len && memcmp(seg, olds[i].data, (size_t)len) == 0);
            }
        }
      }
      /* fakedsplit / fakeddisorder */
      for (int sp = 1; sp < plen; sp += (plen > 50 ? 113 : 1)) {
        int ok_old = old_fakedsplit4(olds, &on, h4, 20, h4 + 20, 20, 0x0100, 5000, 128, big, plen, sp, 4, 0xCC, rev);
        int fits = sp <= TCPDPI_PIECE_MAX && plen - sp <= TCPDPI_PIECE_MAX;
        int n = tcpdpi_plan_fakedsplit(plan, TCPDPI_PIECES_MAX, plen, sp, rev);
        CHECK(ok_old && (n == 3) == fits);
        if (fits) {
          for (int i = 0; i < n; i++) {
            int len = tcpdpi_build_piece4(seg, sizeof seg, h4, 20, h4 + 20, 20, 0x0100, 5000, 128, 4, 0xCC, big, &plan[i]);
            CHECK(len == olds[i].len && memcmp(seg, olds[i].data, (size_t)len) == 0);
          }
          CHECK(tcpdpi_build_fakedsplit4(news, &nn, h4, 20, h4 + 20, 20, 0x0100, 5000, 128, big, plen, sp, 4, 0xCC, rev) == 1);
          for (int i = 0; i < nn; i++) CHECK(news[i].len == olds[i].len && memcmp(news[i].data, olds[i].data, (size_t)news[i].len) == 0);
        }
      }
    }
  }
  /* IPv6: the old loop's order (from/to ranges, reversed for disorder) */
  for (int rev = 0; rev <= 1; rev++) {
    int cuts[2] = { 2, 300 }, from[3] = { 0, 2, 300 }, to[3] = { 2, 300, 900 };
    int n = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 900, cuts, 2, rev);
    CHECK(n == 3);
    for (int k = 0; k < 3 && n == 3; k++) {
      int i = rev ? 2 - k : k;
      CHECK(plan[k].off == from[i] && plan[k].len == to[i] - from[i]);
    }
  }
}

/* fakedsplit / fakeddisorder of a large payload: still a decoy, one piece. */
static void test_faked_large(void) {
  uint8_t h4[40];
  mk_hdr4(h4);
  const int lens[] = { 1800, 4000, 64000 };
  for (unsigned li = 0; li < 3; li++) {
    int plen = lens[li];
    fill(big, plen, 99u);
    for (int dis = 0; dis <= 1; dis++) {
      int n = tcpdpi_plan_fakedsplit(plan, TCPDPI_PIECES_MAX, plen, 2, dis), fakes = 0, fake_at = -1;
      CHECK(n == 1 + 1 + ceil_div(plen - 2, TCPDPI_PIECE_MAX));
      memset(seen, 0, (size_t)plen);
      for (int i = 0; i < n; i++) {
        int len = tcpdpi_build_piece4(seg, sizeof seg, h4, 20, h4 + 20, 20, 0x10, 1000, 128, 4, 0, big, &plan[i]);
        CHECK(len > 40 && len - 40 <= TCPDPI_PIECE_MAX && ip4_ok(seg) && rd16(seg + 4) == 0x10 + i);
        if (plan[i].fake) {
          fakes++; fake_at = i;
          CHECK(!tcp4_ok(seg, len) && seg[8] == 4 && rd32(seg + 24) - 1000 == (uint32_t)plan[i].off);
          CHECK(plan[i].off == (dis ? 0 : 2) && plan[i].len == (dis ? 2 : TCPDPI_PIECE_MAX));
        } else {
          CHECK(tcp4_ok(seg, len) && seg[8] == 128);
          for (int b = 0; b < plan[i].len; b++) seen[plan[i].off + b]++;
        }
      }
      CHECK(fakes == 1);
      for (int b = 0; b < plen; b++) if (seen[b] != 1) { CHECK(seen[b] == 1); break; }
      if (!dis) CHECK(fake_at == 1 && plan[0].off == 0 && plan[2].off == 2);                 /* A, decoy, B... */
      else CHECK(plan[0].off > plan[1].off && plan[n - 1].off == 0 && plan[n - 2].fake);     /* B desc, decoy@0, A */
    }
  }
}

/* A ClientHello: record + handshake headers, a 32-byte session id, a
 * key_share of `ks` bytes, then the server_name extension LAST. */
static int make_ch(uint8_t *b, const char *host, int ks) {
  int hl = (int)strlen(host), p = 0;
  b[p++] = 0x16; b[p++] = 3; b[p++] = 1; p += 2;
  int hs = p; b[p++] = 1; p += 3;
  b[p++] = 3; b[p++] = 3; for (int i = 0; i < 32; i++) b[p++] = (uint8_t)i;
  b[p++] = 32; for (int i = 0; i < 32; i++) b[p++] = (uint8_t)(0x80 + i);
  b[p++] = 0; b[p++] = 2; b[p++] = 0x13; b[p++] = 0x01;
  b[p++] = 1; b[p++] = 0;
  int ext = p; p += 2;
  b[p++] = 0x00; b[p++] = 0x33; wr16(b + p, ks); p += 2; memset(b + p, 0x5c, (size_t)ks); p += ks;   /* key_share */
  b[p++] = 0; b[p++] = 0; wr16(b + p, hl + 5); p += 2; wr16(b + p, hl + 3); p += 2; b[p++] = 0;
  wr16(b + p, hl); p += 2; memcpy(b + p, host, (size_t)hl); p += hl;                                  /* server_name */
  wr16(b + ext, p - ext - 2);
  int hlen = p - hs - 4; b[hs + 1] = (uint8_t)(hlen >> 16); b[hs + 2] = (uint8_t)(hlen >> 8); b[hs + 3] = (uint8_t)hlen;
  wr16(b + 3, p - 5);
  return p;
}

static void test_truncated_hello(void) {
  int n = make_ch(big, "www.blocked.example", 1700), cuts[2] = { -1, -1 };
  proto_info pi;
  CHECK(n > 1800);
  /* the whole hello: the name found, cut inside it */
  proto_inspect(big, (size_t)n, &pi);
  CHECK(pi.kind == PROTO_TLS && pi.host_off > 1460 && strcmp(pi.host, "www.blocked.example") == 0);
  CHECK(tcpdpi_choose_cuts(big, n, 1, pi.host_off, pi.host_len, 0, cuts) == 2 && cuts[0] == pi.host_off + 1);
  /* the first 1460 bytes only (one MSS): still TLS, no name, cut after byte 2 */
  proto_inspect(big, 1460, &pi);
  CHECK(pi.kind == PROTO_TLS && pi.host_off == -1);
  CHECK(tcpdpi_split_points(big, 1460, pi.host_off, pi.host_len, cuts) == 0);   /* the old choice: no cut */
  CHECK(tcpdpi_choose_cuts(big, 1460, pi.kind == PROTO_TLS, pi.host_off, pi.host_len, 0, cuts) == 1 && cuts[0] == TCPDPI_FALLBACK_CUT);
  /* ... and that cut, in disorder, is two pieces */
  CHECK(tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, 1460, cuts, 1, 1) == 3);   /* 1458 bytes after the cut: 2 runs */
  /* --split-pos still wins; HTTP without a Host in the segment is not cut */
  CHECK(tcpdpi_choose_cuts(big, 1460, 1, -1, 0, 5, cuts) == 1 && cuts[0] == 5);
  CHECK(tcpdpi_choose_cuts(big, 4, 1, -1, 0, 9, cuts) == 1 && cuts[0] == 3);
  const char *get = "GET / HTTP/1.1\r\nAccept: */*\r\n";
  proto_inspect((const uint8_t *)get, strlen(get), &pi);
  CHECK(pi.kind == PROTO_HTTP && pi.host_off == -1);
  CHECK(tcpdpi_choose_cuts((const uint8_t *)get, (int)strlen(get), 0, pi.host_off, pi.host_len, 0, cuts) == 0);
  CHECK(tcpdpi_choose_cuts(big, 2, 1, -1, 0, 0, cuts) == 0);                   /* nothing to cut */
}

/* tlsrec of a large ClientHello: the records formed in a big buffer and sent
 * in pieces; small ones exactly as the old two-segment pair. */
static void test_tlsrec_large(void) {
  uint8_t h4[40];
  int rec1 = 0;
  mk_hdr4(h4);
  /* small: identical to tcpdpi_build_tlsrec_pair */
  int n = make_ch(big, "discord.com", 300);
  proto_info pi;
  proto_inspect(big, (size_t)n, &pi);
  int cuts[2], nc = tcpdpi_split_points(big, n, pi.host_off, pi.host_len, cuts);
  CHECK(nc == 2);
  tcpdpi_seg pair[2]; int np = 0;
  CHECK(tcpdpi_build_tlsrec_pair(pair, &np, h4, 20, h4 + 20, 20, 0x55, 1000, 128, big, n, cuts[1]) == 1);
  int g = tcpdpi_tlsrec_form(back, (int)sizeof(back), big, n, cuts[1], &rec1);
  CHECK(g == n + TCPDPI_TLSREC_GROWTH);
  int k = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, g, &rec1, 1, 0);
  CHECK(k == 2);
  for (int i = 0; i < k; i++) {
    int len = tcpdpi_build_piece4(seg, sizeof seg, h4, 20, h4 + 20, 20, 0x55, 1000, 128, 128, 0, back, &plan[i]);
    CHECK(len == pair[i].len && memcmp(seg, pair[i].data, (size_t)len) == 0);
  }
  /* large (64000 bytes in one segment): pieces of at most 1200, records intact */
  int plen = 64000;
  fill(big, plen, 5u);
  big[0] = 0x16; big[1] = 3; big[2] = 1; wr16(big + 3, plen - 5); big[5] = 1;
  g = tcpdpi_tlsrec_form(back, (int)sizeof(back), big, plen, 300, &rec1);
  CHECK(g == plen + 5 && rec1 == 300);
  CHECK(back[0] == 0x16 && rd16(back + 3) == 295 && memcmp(back + 5, big + 5, 295) == 0);
  CHECK(back[300] == 0x16 && rd16(back + 303) == plen - 5 - 295 && memcmp(back + 305, big + 300, (size_t)(plen - 300)) == 0);
  k = tcpdpi_plan_split(plan, TCPDPI_PIECES_MAX, g, &rec1, 1, 0);
  check_plan(back, g, &rec1, 1, 0, k);
  /* the old one-packet limit is gone only where the caller gives room */
  CHECK(tcpdpi_tlsrec_form(back, 1600, big, plen, 300, &rec1) == 0);
}

int main(void) {
  test_sizes();
  test_identical();
  test_faked_large();
  test_truncated_hello();
  test_tlsrec_large();
  if (fails) { printf("%d of %d checks FAILED\n", fails, checks); return 1; }
  printf("pieces_test: all %d checks passed\n", checks);
  return 0;
}
