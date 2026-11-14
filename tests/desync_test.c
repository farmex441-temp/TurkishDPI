/* MSVC: /Isrc/engine /Isrc/engine/core tests/desync_test.c src/engine/desync.c src/engine/core/tcpdpi.c src/engine/core/proto.c */
/*
 * TurkishDPI: what the engine sends for one first request (src/engine/desync.c).
 *
 * 1. Every captured packet that fits one wire packet (<= 1500 bytes) gives
 *    byte for byte what the engine sent before 2.0.2: the old engine
 *    composition (decoy, cut, fakedsplit/fakeddisorder, seqovl, tlsrec pair,
 *    their order and IP ids) is copied below as the reference (from engine.c
 *    and Shroudly's builders, MIT, see THIRD_PARTY_LICENSES.txt), for every
 *    mode the app's presets use, with and without decoys, with scrambled and
 *    renamed decoys, IPv4 and IPv6, TCP headers of 20 and 32 bytes.
 * 2. A larger one (segmentation offload) is sent in pieces of at most 1200
 *    bytes, every byte once, in the same technique order; its decoy is the
 *    first 1200 bytes.
 *
 *   gcc -O2 -Wall -Isrc/engine -Isrc/engine/core tests/desync_test.c src/engine/desync.c src/engine/core/tcpdpi.c src/engine/core/proto.c -o build/desync_test
 */
#include "desync.h"
#include "raw_desync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void wr16(uint8_t *p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* ======================= the old composition (reference) ======================= */
#define FAKE_TTL 4
static struct { uint8_t pkt[8][2048]; int len[8]; int n; } ref;
static uint16_t old_ipid;
static void ref_send(const uint8_t *seg, int len) {
  if (ref.n < 8 && len > 0) { memcpy(ref.pkt[ref.n], seg, (size_t)len); ref.len[ref.n++] = len; }
}

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

static int old_decoy_payload(uint8_t *out, int cap, const uint8_t *pay, int plen,
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

static int old_cut_points(const desync_cfg *c, const uint8_t *pay, int plen, const proto_info *pi, int cuts[2]) {
  if (c->split_pos > 0) {
    if (plen < 2) return 0;
    cuts[0] = c->split_pos < plen ? c->split_pos : plen - 1;
    return 1;
  }
  return tcpdpi_split_points(pay, plen, pi->host_off, pi->host_len, cuts);
}

static void old_send_fake4(const uint8_t *pkt, int len, int ihl, int tcphl, int poff,
                           const proto_info *pi, int ttl) {
  uint8_t pay[1600], out[1700];
  int plen = len - poff, dlen, n;
  if (plen <= 0 || plen > 1500) return;
  dlen = old_decoy_payload(pay, (int)sizeof(pay), pkt + poff, plen, pi, ihl, tcphl);
  if (dlen <= 0) return;
  n = tcpdpi_build_segment4(out, sizeof(out), pkt, ihl, pkt + ihl, tcphl, old_ipid++,
                            rd32(pkt + ihl + 4), ttl > 0 ? ttl : FAKE_TTL, pay, dlen, 1);
  if (n > 0) ref_send(out, n);
}

static int old_send_cut4(const desync_cfg *c, const uint8_t *pkt, int ihl, int tcphl, int poff, int plen,
                         const proto_info *pi, int disorder) {
  int cuts[2], nc = old_cut_points(c, pkt + poff, plen, pi, cuts);
  tcpdpi_seg segs[3];
  int ns = 0;
  if (nc > 0 && old_multisplit4(segs, &ns, pkt, ihl, pkt + ihl, tcphl, old_ipid,
                                rd32(pkt + ihl + 4), pkt[8] ? pkt[8] : 64,
                                pkt + poff, plen, cuts, nc, disorder)) {
    old_ipid = (uint16_t)(old_ipid + ns);
    for (int i = 0; i < ns; i++) ref_send(segs[i].data, segs[i].len);
    return 1;
  }
  return 0;
}

static int old_desync4(const desync_cfg *c, const uint8_t *pkt, int len, int ihl, int tcphl,
                       const proto_info *pi, int decoy, int decoy_ttl, int grow_ok) {
  int poff = ihl + tcphl, plen = len - poff;
  uint32_t seq = rd32(pkt + ihl + 4);
  int otl = pkt[8] ? pkt[8] : 64;
  int faked = c->desync_mode == RAW_DESYNC_FAKEDSPLIT || c->desync_mode == RAW_DESYNC_FAKEDDISORDER;
  if (c->tlsrec && pi->kind == PROTO_TLS && pi->host_off >= 1) {
    int cuts[2], nc = tcpdpi_split_points(pkt + poff, plen, pi->host_off, pi->host_len, cuts);
    int cut = nc >= 2 ? cuts[1] : (nc == 1 ? cuts[0] : 0);
    tcpdpi_seg segs[2];
    int ns = 0;
    int ok = cut > 5 && tcpdpi_build_tlsrec_pair(segs, &ns, pkt, ihl, pkt + ihl, tcphl,
                                                 old_ipid, seq, otl, pkt + poff, plen, cut);
    if (ok && grow_ok) {
      old_ipid = (uint16_t)(old_ipid + 2);
      if (decoy) old_send_fake4(pkt, len, ihl, tcphl, poff, pi, decoy_ttl);
      for (int i = 0; i < ns; i++) ref_send(segs[i].data, segs[i].len);
      return 1;
    }
  }
  if (!decoy) {
    if (c->split == RAW_SPLIT_OFF && !faked) return 0;
    return old_send_cut4(c, pkt, ihl, tcphl, poff, plen, pi,
                         c->split == RAW_SPLIT_DISORDER || c->desync_mode == RAW_DESYNC_FAKEDDISORDER);
  }
  if (faked) {
    int split = c->split_pos > 0 ? (c->split_pos < plen ? c->split_pos : plen - 1)
              : (pi->host_off > 0 && pi->host_off < plen) ? pi->host_off : (plen > 2 ? 2 : 1);
    tcpdpi_seg segs[3];
    int ns = 0;
    if (old_fakedsplit4(segs, &ns, pkt, ihl, pkt + ihl, tcphl, old_ipid, seq, otl,
                        pkt + poff, plen, split, decoy_ttl, c->fake_pattern,
                        c->desync_mode == RAW_DESYNC_FAKEDDISORDER)) {
      old_ipid = (uint16_t)(old_ipid + 3);
      for (int i = 0; i < ns; i++) ref_send(segs[i].data, segs[i].len);
      return 1;
    }
    return 0;
  }
  old_send_fake4(pkt, len, ihl, tcphl, poff, pi, decoy_ttl);
  if (c->seqovl > 0 && plen > 0) {
    uint8_t out[1700];
    int n = tcpdpi_build_seqovl4(out, sizeof(out), pkt, ihl, pkt + ihl, tcphl, old_ipid, seq, otl,
                                 pkt + poff, plen, c->seqovl, c->seqovl_pattern);
    if (n > 0) { old_ipid++; ref_send(out, n); return 1; }
  }
  if (c->split != RAW_SPLIT_OFF)
    return old_send_cut4(c, pkt, ihl, tcphl, poff, plen, pi, c->split == RAW_SPLIT_DISORDER);
  return 0;
}

/* The old IPv6 path; its packets are built with tcpdpi_build_segment6 (the old
 * code had WinDivert compute the same checksums). The decoy's spoiled checksum
 * is the one deliberate difference (old: one byte XOR 0x5a; now as IPv4), so
 * the reference spoils it the new way. */
static int old_desync6(const desync_cfg *c, const uint8_t *pkt, int len, int tcphl, const proto_info *pi,
                       int decoy, int decoy_hop) {
  const int poff = 40 + tcphl;
  const uint8_t *pay = pkt + poff;
  int plen = len - poff, hop = pkt[7] ? pkt[7] : 64;
  uint32_t seq = rd32(pkt + 40 + 4);
  uint8_t out[1800];
  int cuts[2], nc, n;
  int disorder = c->split == RAW_SPLIT_DISORDER || c->desync_mode == RAW_DESYNC_FAKEDDISORDER;
  int cut = c->split != RAW_SPLIT_OFF || c->desync_mode != RAW_DESYNC_FAKE;
  if (plen <= 0 || plen > 1600) return 0;
  if (decoy) {
    uint8_t fake[1700];
    int dlen = old_decoy_payload(fake, (int)sizeof(fake), pay, plen, pi, 40, tcphl);
    if (dlen > 0 && (n = tcpdpi_build_segment6(out, sizeof(out), pkt, pkt + 40, tcphl, seq, decoy_hop, fake, dlen, 1)) > 0)
      ref_send(out, n);
  }
  if (!cut) return 0;
  nc = old_cut_points(c, pay, plen, pi, cuts);
  if (nc <= 0) return 0;
  {
    int from[3], to[3], ns = 0, start = 0;
    for (int i = 0; i < nc; i++) { from[ns] = start; to[ns] = cuts[i]; start = cuts[i]; ns++; }
    from[ns] = start; to[ns] = plen; ns++;
    for (int k = 0; k < ns; k++) {
      int i = disorder ? ns - 1 - k : k;
      n = tcpdpi_build_segment6(out, sizeof(out), pkt, pkt + 40, tcphl, seq + (uint32_t)from[i], hop,
                                pay + from[i], to[i] - from[i], 0);
      if (n <= 0) return k > 0;
      ref_send(out, n);
    }
  }
  return 1;
}

/* ======================= inputs ======================= */
static uint8_t pkt[70000];

/* A ClientHello of exactly plen bytes: the server name early, then padding. */
static void make_hello(uint8_t *b, int plen, const char *host) {
  int hl = (int)strlen(host), p = 0;
  b[p++] = 0x16; b[p++] = 3; b[p++] = 1; wr16(b + p, plen - 5); p += 2;
  b[p++] = 1; b[p++] = (uint8_t)((plen - 9) >> 16); b[p++] = (uint8_t)((plen - 9) >> 8); b[p++] = (uint8_t)(plen - 9);
  b[p++] = 3; b[p++] = 3; for (int i = 0; i < 32; i++) b[p++] = (uint8_t)(i * 7);
  b[p++] = 32; for (int i = 0; i < 32; i++) b[p++] = (uint8_t)(0x80 + i);
  b[p++] = 0; b[p++] = 4; b[p++] = 0x13; b[p++] = 0x01; b[p++] = 0x13; b[p++] = 0x02;
  b[p++] = 1; b[p++] = 0;
  wr16(b + p, plen - p - 2); p += 2;
  b[p++] = 0; b[p++] = 0; wr16(b + p, hl + 5); p += 2; wr16(b + p, hl + 3); p += 2; b[p++] = 0;
  wr16(b + p, hl); p += 2; memcpy(b + p, host, (size_t)hl); p += hl;
  b[p++] = 0x00; b[p++] = 0x15; wr16(b + p, plen - p - 2); p += 2;     /* padding to plen */
  for (int i = p; i < plen; i++) b[i] = (uint8_t)(i * 31 + 7);
}

/* IPv4 (v6 == 0) or IPv6 packet with a TCP header of tcphl bytes. Returns its length. */
static int make_packet(int v6, int tcphl, int plen, const char *host) {
  int ipl = v6 ? 40 : 20, len = ipl + tcphl + plen;
  uint8_t *t = pkt + ipl;
  memset(pkt, 0, (size_t)(ipl + tcphl));
  if (v6) {
    pkt[0] = 0x60; pkt[1] = 0x0a; pkt[2] = 0xbc; pkt[3] = 0xde; wr16(pkt + 4, tcphl + plen); pkt[6] = 6; pkt[7] = 64;
    pkt[8] = 0x20; pkt[9] = 0x01; pkt[10] = 0x0d; pkt[11] = 0xb8; pkt[23] = 5;
    pkt[24] = 0x20; pkt[25] = 0x01; pkt[26] = 0x0d; pkt[27] = 0xb8; pkt[39] = 9;
  } else {
    pkt[0] = 0x45; wr16(pkt + 2, len); wr16(pkt + 4, 0x4242); pkt[6] = 0x40; pkt[8] = 128; pkt[9] = 6;
    pkt[12] = 10; pkt[15] = 2; pkt[16] = 93; pkt[17] = 184; pkt[18] = 216; pkt[19] = 34;
  }
  wr16(t, 50123); wr16(t + 2, 443); wr32(t + 4, 0xfffffe00u); wr32(t + 8, 0x01020304u);
  t[12] = (uint8_t)((tcphl / 4) << 4); t[13] = 0x18; wr16(t + 14, 512);
  if (tcphl == 32) { t[20] = 1; t[21] = 1; t[22] = 8; t[23] = 10; wr32(t + 24, 123456); wr32(t + 28, 654321); }
  make_hello(pkt + ipl + tcphl, plen, host);
  return len;
}

/* ======================= the modes ======================= */
typedef struct { const char *name; desync_cfg c; int decoy_ttl; } mode_t_;
static mode_t_ modes[] = {
  /* split, desync_mode, split_pos, seqovl, seqovl_pattern, fake_pattern, tlsrec */
  { "none+disorder pos2 (preset 0)",  { RAW_SPLIT_DISORDER, RAW_DESYNC_FAKE, 2, 0, 0, 0, 0 }, 5 },
  { "fake+disorder pos2 ttl5",        { RAW_SPLIT_DISORDER, RAW_DESYNC_FAKE, 2, 0, 0, 0, 0 }, 5 },
  { "fakeddisorder pos2",             { RAW_SPLIT_ON, RAW_DESYNC_FAKEDDISORDER, 2, 0, 0, 0, 0 }, 3 },
  { "fake+split pos2 ttl5",           { RAW_SPLIT_ON, RAW_DESYNC_FAKE, 2, 0, 0, 0, 0 }, 5 },
  { "fake+split tlsrec",              { RAW_SPLIT_ON, RAW_DESYNC_FAKE, 0, 0, 0, 0, 1 }, 3 },
  { "fake+split name cut",            { RAW_SPLIT_ON, RAW_DESYNC_FAKE, 0, 0, 0, 0, 0 }, 3 },
  { "fakeddisorder name cut",         { RAW_SPLIT_ON, RAW_DESYNC_FAKEDDISORDER, 0, 0, 0, 0, 0 }, 3 },
  /* not in the presets, but engine modes all the same */
  { "fakedsplit name cut",            { RAW_SPLIT_ON, RAW_DESYNC_FAKEDSPLIT, 0, 0, 0, 0xCC, 0 }, 3 },
  { "fake+seqovl 10",                 { RAW_SPLIT_ON, RAW_DESYNC_FAKE, 0, 10, 0x61, 0, 0 }, 3 },
  { "fake+seqovl 300 split off",      { RAW_SPLIT_OFF, RAW_DESYNC_FAKE, 0, 300, 0x61, 0, 0 }, 3 },
  { "fake only (split off)",          { RAW_SPLIT_OFF, RAW_DESYNC_FAKE, 0, 0, 0, 0, 0 }, 3 },
};
#define NMODES ((int)(sizeof modes / sizeof modes[0]))
static int mode_decoy(int m) { return m != 0; }     /* preset 0 is --desync none */

static desync_out out;
static int grow_answer;
static int grow_cb(void *ctx) { (void)ctx; return grow_answer; }

/* ======================= 1. identical where it fits ======================= */
static int compare_ref(const char *what, int plen, int tcphl, int v6) {
  int same = ref.n == out.n;
  for (int i = 0; same && i < out.n; i++)
    same = ref.len[i] == out.len[i] && memcmp(ref.pkt[i], out.pkt[i], (size_t)out.len[i]) == 0;
  if (!same) printf("  differs: %s plen %d tcphl %d IPv%d (old %d packets, new %d)\n", what, plen, tcphl, v6 ? 6 : 4, ref.n, out.n);
  return same;
}

/* the table for the report */
static const int fit_sizes[] = { 517, 1200, 1201, 1400, 1448, 1452, 1460 };
#define NFIT ((int)(sizeof fit_sizes / sizeof fit_sizes[0]))
static char table[NMODES][NFIT][2];   /* [mode][size][tcphl 20/32]: 'I' identical, 'X' differs, 'P' pieces */

static void test_identical(void) {
  for (int names = 0; names <= 1; names++) {
    proto_decoy_names_reset();
    if (names) proto_decoy_name_add("www.example.org");
    for (int v6 = 0; v6 <= 1; v6++)
      for (int th = 0; th <= 1; th++) {
        int tcphl = th ? 32 : 20, ipl = v6 ? 40 : 20;
        for (int s = 0; s < NFIT; s++) {
          int plen = fit_sizes[s];
          int len = make_packet(v6, tcphl, plen, "discord.com");
          proto_info pi;
          proto_inspect(pkt + ipl + tcphl, (size_t)plen, &pi);
          for (int m = 0; m < NMODES; m++)
            for (int decoy = 0; decoy <= mode_decoy(m); decoy++)
              for (int g = 1; g >= (modes[m].c.tlsrec && !v6 ? 0 : 1); g--) {
                int ok;
                ref.n = 0;
                if (len > TCPDPI_WIRE_MAX) {     /* not one wire packet: pieces, checked in 2. */
                  if (!names && !v6 && decoy == mode_decoy(m) && g == 1) table[m][s][th] = 'P';
                  continue;
                }
                if (v6) {
                  int ro = old_desync6(&modes[m].c, pkt, len, tcphl, &pi, decoy, modes[m].decoy_ttl);
                  int rn = desync_compose6(&out, &modes[m].c, pkt, len, tcphl, &pi, decoy, modes[m].decoy_ttl);
                  ok = compare_ref(modes[m].name, plen, tcphl, 1) && ro == rn;
                } else {
                  uint16_t idn = 0x7ff0;
                  old_ipid = 0x7ff0;
                  grow_answer = g;
                  int ro = old_desync4(&modes[m].c, pkt, len, 20, tcphl, &pi, decoy, modes[m].decoy_ttl, g);
                  int rn = desync_compose4(&out, &modes[m].c, pkt, len, 20, tcphl, &pi, decoy, modes[m].decoy_ttl,
                                           &idn, grow_cb, NULL);
                  ok = compare_ref(modes[m].name, plen, tcphl, 0) && ro == rn && idn == old_ipid;
                  CHECK(!out.big && (out.grew == (modes[m].c.tlsrec && g && rn)));
                }
                CHECK(ok);
                if (!names && !v6 && decoy == mode_decoy(m) && g == 1) table[m][s][th] = ok ? 'I' : 'X';
              }
        }
      }
  }
  proto_decoy_names_reset();
  printf("plen x mode (IPv4, presets' decoy setting; I = identical to the old engine, P = larger than one wire packet: pieces)\n");
  printf("  %-32s", "mode \\ plen (tcp 20 / 32)");
  for (int s = 0; s < NFIT; s++) printf(" %5d", fit_sizes[s]);
  printf("\n");
  for (int m = 0; m < NMODES; m++) {
    printf("  %-32s", modes[m].name);
    for (int s = 0; s < NFIT; s++) printf("   %c/%c", table[m][s][0], table[m][s][1]);
    printf("\n");
  }
}

/* ======================= 2. larger than one wire packet ======================= */
static unsigned char seen[70000];
static uint8_t expect[70000];

/* Checks out against payload `data` (len dlen) at stream offset 0 = seq. */
static void check_pieces(const char *what, int v6, int tcphl, uint32_t seq, const uint8_t *data, int dlen, int ovl) {
  int ipl = v6 ? 40 : 20, hl = ipl + tcphl, ok = 1;
  memset(seen, 0, (size_t)dlen);
  for (int i = 0; i < out.n; i++) {
    const uint8_t *p = out.pkt[i];
    int pl = out.len[i] - hl;
    uint32_t s = rd32(p + ipl + 4);
    ok &= pl >= 1 && pl <= TCPDPI_PIECE_MAX;
    ok &= (v6 ? rd16(p + 4) == tcphl + pl : rd16(p + 2) == out.len[i]);
    if (out.fake[i]) continue;
    int off = (int)(s - seq), skip = 0;
    if (off < 0) { skip = -off; ok &= skip == ovl; off = 0; }       /* the overlap's pattern */
    for (int b = skip; b < pl; b++) {
      if (off + b - skip >= dlen) { ok = 0; break; }
      seen[off + b - skip]++;
      ok &= p[hl + b] == data[off + b - skip];
    }
  }
  for (int b = 0; b < dlen; b++) ok &= seen[b] == 1;
  if (!ok) printf("  pieces wrong: %s IPv%d dlen %d\n", what, v6 ? 6 : 4, dlen);
  CHECK(ok);
}

static void test_large(void) {
  const int sizes[] = { 1461, 1800, 2400, 4000, 64000 };
  for (int names = 0; names <= 1; names++) {
    proto_decoy_names_reset();
    if (names) proto_decoy_name_add("www.example.org");
    for (int v6 = 0; v6 <= 1; v6++)
      for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        int tcphl = 20, ipl = v6 ? 40 : 20, plen = sizes[s];
        int len = make_packet(v6, tcphl, plen, "discord.com");
        const uint8_t *pay = pkt + ipl + tcphl;
        uint32_t seq = rd32(pkt + ipl + 4);
        proto_info pi;
        proto_inspect(pay, (size_t)plen, &pi);
        CHECK(len > TCPDPI_WIRE_MAX && pi.host_off > 0);
        for (int m = 0; m < NMODES; m++)
          for (int decoy = 0; decoy <= mode_decoy(m); decoy++) {
            const desync_cfg *c = &modes[m].c;
            int rn, fakes = 0;
            uint16_t id = 0x100, id0 = id;
            grow_answer = 1;
            if (v6) rn = desync_compose6(&out, c, pkt, len, tcphl, &pi, decoy, 3);
            else rn = desync_compose4(&out, c, pkt, len, 20, tcphl, &pi, decoy, 3, &id, grow_cb, NULL);
            CHECK(out.big);
            for (int i = 0; i < out.n; i++) fakes += out.fake[i];
            int grew = out.grew, ovl = !v6 && decoy && c->seqovl && c->desync_mode == RAW_DESYNC_FAKE ? c->seqovl : 0;
            int faked = c->desync_mode != RAW_DESYNC_FAKE;
            /* a decoy is never skipped: a plain decoy first (fake modes), or
             * the fakedsplit decoy inside the plan */
            if (decoy) CHECK(fakes == 1);
            if (decoy && (!faked || v6)) CHECK(out.fake[0] == 1);
            /* the plain decoy is the old one of the first 1200 bytes */
            if (decoy && out.fake[0] && (!faked || v6)) {
              uint8_t d[TCPDPI_SEG_MAX];
              int dl = old_decoy_payload(d, (int)sizeof d, pay, TCPDPI_PIECE_MAX, &pi, ipl, tcphl);
              CHECK(out.len[0] - ipl - tcphl == dl && memcmp(out.pkt[0] + ipl + tcphl, d, (size_t)dl) == 0);
              CHECK(rd32(out.pkt[0] + ipl + 4) == seq);
              CHECK(v6 ? out.pkt[0][7] == 3 : out.pkt[0][8] == 3);
            }
            if (!rn) continue;                       /* only the decoy (split off, no overlap) */
            if (grew) {
              int rec1 = 0, g = tcpdpi_tlsrec_form(expect, (int)sizeof expect, pay, plen,
                                                   pi.host_off + 3, &rec1);
              CHECK(g == plen + TCPDPI_TLSREC_GROWTH);
              check_pieces(modes[m].name, v6, tcphl, seq, expect, g, 0);
              /* order: decoy, record 1, record 2 and the rest in sequence */
              for (int i = decoy ? 2 : 1; i < out.n; i++)
                CHECK(rd32(out.pkt[i] + 24) > rd32(out.pkt[i - 1] + 24) || rd32(out.pkt[i - 1] + 24) > 0xffff0000u);
              CHECK(rd32(out.pkt[decoy ? 1 : 0] + 24) == seq && out.len[decoy ? 1 : 0] - 40 == rec1);
            } else {
              check_pieces(modes[m].name, v6, tcphl, seq, pay, plen, ovl);
            }
            if (ovl) {                               /* seqovl: decoy, overlap at seq-ovl, the rest in order */
              CHECK(rd32(out.pkt[1] + 24) == seq - (uint32_t)ovl && out.len[1] - 40 == TCPDPI_PIECE_MAX);
              CHECK(memcmp(out.pkt[1] + 40, "aaaaaaaaaa", 10) == 0);
              CHECK(rd32(out.pkt[2] + 24) == seq + (uint32_t)(TCPDPI_PIECE_MAX - ovl));
              CHECK(rd16(out.pkt[1] + 4) == id0 + 1 && rd16(out.pkt[2] + 4) == id0 + 2);   /* decoy id0 */
            }
            int disorder = c->split == RAW_SPLIT_DISORDER || c->desync_mode == RAW_DESYNC_FAKEDDISORDER;
            if (disorder && !grew && !ovl) {         /* byte 0 last */
              int last = out.n - 1;
              CHECK(!out.fake[last] && rd32(out.pkt[last] + ipl + 4) == seq);
            }
            if (!v6) {                               /* IP ids: distinct, all taken */
              CHECK((uint16_t)(id - id0) == out.n);
              for (int i = 0; i < out.n; i++)
                for (int j = 0; j < i; j++) CHECK(rd16(out.pkt[i] + 4) != rd16(out.pkt[j] + 4));
            }
          }
        /* tlsrec refused by the translation table: nothing grows, the plain
         * techniques run instead */
        if (!v6) {
          uint16_t id = 1;
          grow_answer = 0;
          desync_compose4(&out, &modes[4].c, pkt, len, 20, tcphl, &pi, 1, 3, &id, grow_cb, NULL);
          CHECK(!out.grew && out.fake[0]);
          check_pieces("tlsrec refused", 0, tcphl, seq, pay, plen, 0);
          grow_answer = 1;
        }
      }
  }
  proto_decoy_names_reset();

  /* the first size that does not fit: total 1501 -> pieces of at most 1200 */
  int len = make_packet(0, 20, 1461, "discord.com");
  proto_info pi;
  uint16_t id = 1;
  proto_inspect(pkt + 40, 1461, &pi);
  CHECK(len == 1501);
  desync_compose4(&out, &modes[0].c, pkt, len, 20, 20, &pi, 0, 3, &id, grow_cb, NULL);
  CHECK(out.n == 3 && out.len[0] - 40 == 259 && out.len[1] - 40 == 1200 && out.len[2] - 40 == 2);
  len = make_packet(0, 20, 1460, "discord.com");
  proto_inspect(pkt + 40, 1460, &pi);
  CHECK(len == 1500);
  desync_compose4(&out, &modes[0].c, pkt, len, 20, 20, &pi, 0, 3, &id, grow_cb, NULL);
  CHECK(out.n == 2 && out.len[0] - 40 == 1458 && out.len[1] - 40 == 2);   /* as measured: [2,1460) [0,2) */
}

/* A ClientHello whose name is beyond the first segment: cut after byte 2. */
static void test_nameless(void) {
  int len = make_packet(0, 20, 1460, "discord.com");
  proto_info pi;
  uint16_t id = 1;
  /* move the name out of reach: proto_inspect sees the server name as
   * running past the segment */
  pi.kind = PROTO_TLS; pi.host_off = -1; pi.host_len = 0; pi.ech = PROTO_ECH_UNKNOWN; pi.host[0] = 0;
  CHECK(desync_compose4(&out, &modes[5].c, pkt, len, 20, 20, &pi, 1, 3, &id, grow_cb, NULL) == 1);
  CHECK(out.n == 3 && out.fake[0] && out.len[1] - 40 == 2 && out.len[2] - 40 == 1458);   /* decoy, [0,2), [2,1460) */
  CHECK(desync_compose4(&out, &modes[4].c, pkt, len, 20, 20, &pi, 1, 3, &id, grow_cb, NULL) == 1);
  CHECK(!out.grew && out.n == 3 && out.len[1] - 40 == 2);                                /* tlsrec: the plain cut */
}

int main(void) {
  test_identical();
  test_large();
  test_nameless();
  if (fails) { printf("%d of %d checks FAILED\n", fails, checks); return 1; }
  printf("desync_test: all %d checks passed\n", checks);
  return 0;
}
