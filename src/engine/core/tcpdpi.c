/* From Shroudly (https://github.com/umutxyp/Shroudly), MIT License,
 * Copyright (c) 2026 Codeshare Technology Ltd - see THIRD_PARTY_LICENSES.txt. */
/*
 * tcpdpi.c — see tcpdpi.h. Pure, platform-independent packet-layer helpers for
 * the Advanced-mode raw backends. Byte-offset based so the same code compiles
 * and is tested on Windows, Linux and macOS without any OS packet headers.
 */
#include "tcpdpi.h"
#include <string.h>

/* ── byte helpers (explicit big-endian, so the result is host-independent) ── */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void     wr32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }

/* TCP flag bits. */
#define TCPF_FIN 0x01
#define TCPF_SYN 0x02
#define TCPF_RST 0x04
#define TCPF_ACK 0x10

/* IPv4 flags byte (offset 6) — DF = "don't fragment". */
#define TCPDPI_IP_DF 0x40

/* ── Internet checksum, computed over explicit big-endian 16-bit words so the
 * 16-bit result is the same on any host and is stored big-endian on the wire. */
static uint32_t csum_add(const uint8_t *p, int len, uint32_t sum) {
  int i = 0;
  for (; i + 1 < len; i += 2) sum += (uint32_t)((p[i] << 8) | p[i + 1]);
  if (i < len) sum += (uint32_t)(p[i] << 8); /* final odd byte is the high half */
  return sum;
}
static uint16_t csum_fold(uint32_t sum) {
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return (uint16_t)(~sum & 0xffff);
}

/* Write IPv4 header checksum and TCP checksum into a complete IPv4+TCP segment
 * buffer `pkt` of length `len`. ihl = IPv4 header length in bytes. If bad != 0
 * the TCP checksum is inverted so the destination drops the segment. */
static void fill_checksums4(uint8_t *pkt, int len, int ihl, int bad) {
  /* IPv4 header checksum. */
  pkt[10] = 0; pkt[11] = 0;
  wr16(pkt + 10, csum_fold(csum_add(pkt, ihl, 0)));

  /* TCP checksum over the pseudo-header + TCP header + payload. */
  int tcp_off = ihl;
  int tcp_len = len - ihl;
  uint32_t sum = 0;
  sum = csum_add(pkt + 12, 4, sum);         /* src addr */
  sum = csum_add(pkt + 16, 4, sum);         /* dst addr */
  sum += 6;                                 /* zero byte + protocol (TCP=6) */
  sum += (uint32_t)tcp_len;                 /* TCP length */
  pkt[tcp_off + 16] = 0; pkt[tcp_off + 17] = 0;
  uint16_t c = csum_fold(csum_add(pkt + tcp_off, tcp_len, sum));
  if (bad) c = (uint16_t)(c ^ 0xffff);      /* deliberately wrong: server drops it */
  wr16(pkt + tcp_off + 16, c);
}

/* ───────────────────────── injected-RST classifier ───────────────────────── */
int tcpdpi_is_injected_rst4(const uint8_t *pkt, int len) {
  if (!pkt || len < 20) return 0;
  if ((pkt[0] >> 4) != 4) return 0;               /* IPv4 */
  int ihl = (pkt[0] & 0x0f) * 4;
  if (ihl < 20 || len < ihl + 20) return 0;
  if (pkt[9] != 6) return 0;                       /* TCP */
  const uint8_t *tcp = pkt + ihl;
  int tcphl = ((tcp[12] >> 4) & 0x0f) * 4;
  if (tcphl < 20 || ihl + tcphl > len) return 0;
  if (!(tcp[13] & TCPF_RST)) return 0;             /* RST (or RST+ACK) */
  if (len - ihl - tcphl > 0) return 0;             /* no payload */
  int sport = rd16(tcp);
  if (sport != 80 && sport != 443) return 0;       /* monitored flow only */
  uint16_t ipid = rd16(pkt + 4);
  if (ipid > TCPDPI_INJ_RST_IPID_MAX) return 0;    /* real: per-socket ID above the window */
  /* One genuine reset also lands in the small-ID window and must be kept: Linux
   * emits its socket-less resets — connection-refused to a closed port
   * (tcp_v4_send_reset) and resets of a flow whose socket is already gone — with
   * DF set, and ip_select_ident_segs() then writes IP ID 0. That exact
   * fingerprint, DF set with IP ID 0, is a real server reset, so it is kept and
   * a connect() to a closed port still fails fast instead of hanging. (The cost
   * is one false negative: a forged RST that also sets DF with IP ID 0 is let
   * through — failing safe, a connection that drops rather than one swallowed.) */
  if (ipid == 0 && (pkt[6] & TCPDPI_IP_DF)) return 0;
  return 1;                                        /* forged: tiny IP ID */
}

int tcpdpi_is_injected_rst6(const uint8_t *pkt, int len) {
  if (!pkt || len < 40) return 0;
  if ((pkt[0] >> 4) != 6) return 0;                /* IPv6 */
  if (pkt[6] != 6) return 0;                        /* next header == TCP, no ext hdrs */
  const uint8_t *tcp = pkt + 40;
  if (len < 40 + 20) return 0;
  int tcphl = ((tcp[12] >> 4) & 0x0f) * 4;
  if (tcphl < 20 || 40 + tcphl > len) return 0;
  if (!(tcp[13] & TCPF_RST)) return 0;
  if (len - 40 - tcphl > 0) return 0;              /* no payload */
  int sport = rd16(tcp);
  if (sport != 80 && sport != 443) return 0;
  uint32_t flow = ((uint32_t)(pkt[1] & 0x0f) << 16) | ((uint32_t)pkt[2] << 8) | pkt[3];
  return flow == 0;                                 /* forged: zero flow label */
}

/* ───────────────────────── measured decoy TTL ───────────────────────── */
int tcpdpi_hops_from_ttl(int ttl) {
  if (ttl >= 35  && ttl <= 63)  return 64  - ttl;   /* initial 64  (Linux, macOS) */
  if (ttl >= 99  && ttl <= 127) return 128 - ttl;   /* initial 128 (Windows) */
  if (ttl >= 230 && ttl <= 255) return 255 - ttl;   /* initial 255 (some routers) */
  return 0;                                          /* not in any band */
}

int tcpdpi_decoy_ttl(int nhops, int margin, int min_ttl) {
  int t = nhops - margin;
  if (min_ttl < 1) min_ttl = 1;
  if (t < min_ttl) t = min_ttl;
  if (t < 1) t = 1;
  return t;
}

/* ───────────────────────── hop-count table ───────────────────────── */
typedef struct { uint32_t ip; int hops; uint64_t t; int used; } hop_ent;
static hop_ent g_hops[TCPDPI_HOPS_CAP];

void tcpdpi_hops_reset(void) { memset(g_hops, 0, sizeof(g_hops)); }

void tcpdpi_hops_put(uint32_t ip_be, int hops, uint64_t now_ms) {
  if (hops <= 0) return;
  int free_slot = -1, oldest = 0;
  for (int i = 0; i < TCPDPI_HOPS_CAP; i++) {
    if (g_hops[i].used && g_hops[i].ip == ip_be) {       /* refresh existing */
      g_hops[i].hops = hops; g_hops[i].t = now_ms; return;
    }
    if (!g_hops[i].used && free_slot < 0) free_slot = i;
    if (g_hops[i].t < g_hops[oldest].t) oldest = i;
  }
  int slot = (free_slot >= 0) ? free_slot : oldest;      /* evict oldest if full */
  g_hops[slot].ip = ip_be; g_hops[slot].hops = hops; g_hops[slot].t = now_ms; g_hops[slot].used = 1;
}

int tcpdpi_hops_get(uint32_t ip_be, uint64_t now_ms) {
  for (int i = 0; i < TCPDPI_HOPS_CAP; i++) {
    if (g_hops[i].used && g_hops[i].ip == ip_be) {
      if (now_ms - g_hops[i].t > TCPDPI_HOPS_TTL_MS) { g_hops[i].used = 0; return 0; }
      return g_hops[i].hops;
    }
  }
  return 0;
}

/* ───────────────────────── segment builders ───────────────────────── */
/* TurkishDPI: TCPDPI_SEG_MAX (1600) moved to tcpdpi.h */

int tcpdpi_build_segment4(uint8_t *dst, size_t dstcap,
                          const uint8_t *ip_tmpl, int ihl,
                          const uint8_t *tcp_tmpl, int tcphl,
                          uint16_t ip_id, uint32_t seq, int ttl,
                          const uint8_t *payload, int seg_len,
                          int bad_tcp_csum) {
  if (ihl < 20 || tcphl < 20 || seg_len < 0) return 0;
  int total = ihl + tcphl + seg_len;
  if (total > TCPDPI_SEG_MAX || (size_t)total > dstcap) return 0;
  if ((ip_tmpl[0] >> 4) != 4) return 0;

  memcpy(dst, ip_tmpl, (size_t)ihl);
  memcpy(dst + ihl, tcp_tmpl, (size_t)tcphl);
  if (seg_len > 0) {
    if (!payload) return 0;
    memcpy(dst + ihl + tcphl, payload, (size_t)seg_len);
  }
  wr16(dst + 2, (uint16_t)total);          /* IPv4 total length */
  wr16(dst + 4, ip_id);                     /* IPv4 identification */
  dst[8] = (uint8_t)(ttl < 1 ? 1 : (ttl > 255 ? 255 : ttl)); /* TTL */
  wr32(dst + ihl + 4, seq);                 /* TCP sequence number */
  fill_checksums4(dst, total, ihl, bad_tcp_csum);
  return total;
}

int tcpdpi_build_seqovl4(uint8_t *dst, size_t dstcap,
                         const uint8_t *ip_tmpl, int ihl,
                         const uint8_t *tcp_tmpl, int tcphl,
                         uint16_t ip_id, uint32_t seq, int ttl,
                         const uint8_t *real, int real_len,
                         int overlap, uint8_t pattern) {
  if (overlap < 1 || real_len < 1 || !real) return 0;
  int paylen = overlap + real_len;
  if (ihl < 20 || tcphl < 20) return 0;
  if (ihl + tcphl + paylen > TCPDPI_SEG_MAX) return 0;
  uint8_t pay[TCPDPI_SEG_MAX];
  memset(pay, pattern, (size_t)overlap);
  memcpy(pay + overlap, real, (size_t)real_len);
  /* Sequence moved back by `overlap`: the first `overlap` bytes fall before the
   * receiver's window and are discarded; real[0,real_len) lands in-window. */
  return tcpdpi_build_segment4(dst, dstcap, ip_tmpl, ihl, tcp_tmpl, tcphl,
                               ip_id, seq - (uint32_t)overlap, ttl,
                               pay, paylen, 0 /* good: the server keeps it */);
}

/* TurkishDPI: now built from tcpdpi_plan_fakedsplit (one piece per part as
 * before while the parts fit TCPDPI_PIECE_MAX; a larger payload needs more
 * pieces than segs[3] holds and is refused here - the engine sends whole
 * plans itself). Output for payloads that fit is unchanged. */
int tcpdpi_build_fakedsplit4(tcpdpi_seg segs[3], int *nsegs,
                             const uint8_t *ip_tmpl, int ihl,
                             const uint8_t *tcp_tmpl, int tcphl,
                             uint16_t ip_id, uint32_t seq, int ttl,
                             const uint8_t *real, int real_len,
                             int split_pos, int fake_ttl, uint8_t pattern,
                             int disorder) {
  if (!segs || !nsegs || !real) return 0;
  tcpdpi_piece plan[3];
  int n = tcpdpi_plan_fakedsplit(plan, 3, real_len, split_pos, disorder);
  if (n != 3) return 0;
  /* The three segments in the order they are sent. The fake shadows the real
   * part that follows it, carrying junk so the DPI mis-reads that region; its
   * low TTL and bad checksum keep it from ever being accepted by the server. */
  for (int i = 0; i < n; i++) {
    int len = tcpdpi_build_piece4(segs[i].data, sizeof segs[i].data, ip_tmpl, ihl, tcp_tmpl, tcphl,
                                  ip_id, seq, ttl, fake_ttl, pattern, real, &plan[i]);
    if (len <= 0) return 0;
    segs[i].len = len;
    segs[i].is_fake = plan[i].fake;
  }
  *nsegs = n;
  return 1;
}

int tcpdpi_split_points(const uint8_t *payload, int plen, int host_off, int host_len, int cuts[2]) {
  if (!payload || !cuts || host_off < 1 || host_len < 2 || host_off + host_len > plen) return 0;
  const uint8_t *h = payload + host_off;
  int end = host_len;
  if (h[end - 1] == '.') end--;              /* a fully qualified "name." */
  if (end < 2) return 0;
  int last = -1, prev = -1;
  for (int i = 0; i < end; i++) if (h[i] == '.') { prev = last; last = i; }
  int s = 0, e = end;                        /* the second-level label ... */
  if (last > 0) { s = prev + 1; e = last; }
  if (e - s < 2) { s = 0; e = end; }         /* ... or the whole name */
  int a = host_off + 1, b = host_off + s + (e - s) / 2;
  int n = 0;
  if (a > 0 && a < plen) cuts[n++] = a;
  if (b > 0 && b < plen && (n == 0 || b > cuts[0])) cuts[n++] = b;
  return n;
}

int tcpdpi_build_multisplit4(tcpdpi_seg segs[3], int *nsegs,
                             const uint8_t *ip_tmpl, int ihl,
                             const uint8_t *tcp_tmpl, int tcphl,
                             uint16_t ip_id, uint32_t seq, int ttl,
                             const uint8_t *real, int real_len,
                             const int *cuts, int ncuts, int reverse) {
  if (!segs || !nsegs || !real || !cuts || ncuts < 1 || ncuts > 2 || real_len < 2) return 0;
  /* TurkishDPI: built from tcpdpi_plan_split; a payload whose parts do not
   * fit TCPDPI_PIECE_MAX needs more than three pieces and is refused here. */
  tcpdpi_piece plan[3];
  int k = tcpdpi_plan_split(plan, 3, real_len, cuts, ncuts, reverse);
  if (k != ncuts + 1) return 0;
  for (int i = 0; i < k; i++) {
    int len = tcpdpi_build_piece4(segs[i].data, sizeof segs[i].data, ip_tmpl, ihl, tcp_tmpl, tcphl,
                                  ip_id, seq, ttl, ttl, 0, real, &plan[i]);
    if (len <= 0) return 0;
    segs[i].len = len;
    segs[i].is_fake = 0;
  }
  *nsegs = k;
  return 1;
}

/* ───────────── TurkishDPI: bounded pieces for payloads of any size ───────────── */
/* Appends the runs of [from, to) to out[*n..], ids from *id; ascending. */
static int plan_runs(tcpdpi_piece *out, int cap, int *n, int from, int to, int fake, int *id) {
  for (int off = from; off < to; off += TCPDPI_PIECE_MAX) {
    if (*n >= cap) return 0;
    int len = to - off < TCPDPI_PIECE_MAX ? to - off : TCPDPI_PIECE_MAX;
    out[*n].off = off; out[*n].len = len; out[*n].fake = fake; out[*n].id = (*id)++;
    (*n)++;
  }
  return 1;
}

static void plan_reverse(tcpdpi_piece *p, int n) {
  for (int i = 0, j = n - 1; i < j; i++, j--) { tcpdpi_piece t = p[i]; p[i] = p[j]; p[j] = t; }
}

int tcpdpi_plan_split(tcpdpi_piece *out, int cap, int plen, const int *cuts, int ncuts, int reverse) {
  if (!out || cap < 1 || plen < 1 || ncuts < 0 || ncuts > 4 || (ncuts > 0 && !cuts)) return 0;
  int n = 0, id = 0, start = 0;
  for (int i = 0; i <= ncuts; i++) {
    int end = i < ncuts ? cuts[i] : plen;
    if (end <= start || end > plen || (i < ncuts && end >= plen)) return 0;  /* ascending, inside */
    if (!plan_runs(out, cap, &n, start, end, 0, &id)) return 0;
    start = end;
  }
  if (reverse) plan_reverse(out, n);
  return n;
}

int tcpdpi_plan_fakedsplit(tcpdpi_piece *out, int cap, int plen, int split, int disorder) {
  if (!out || cap < 1 || plen < 2 || split < 1 || split >= plen) return 0;
  int n = 0, id = 0, m;
  if (!disorder) {                                  /* A, decoy@split, B */
    if (!plan_runs(out, cap, &n, 0, split, 0, &id)) return 0;
    if (!plan_runs(out, cap, &n, split, split + (plen - split < TCPDPI_PIECE_MAX ? plen - split : TCPDPI_PIECE_MAX), 1, &id)) return 0;
    if (!plan_runs(out, cap, &n, split, plen, 0, &id)) return 0;
  } else {                                          /* B desc, decoy@0, A desc */
    if (!plan_runs(out, cap, &n, split, plen, 0, &id)) return 0;
    plan_reverse(out, n);
    if (!plan_runs(out, cap, &n, 0, split < TCPDPI_PIECE_MAX ? split : TCPDPI_PIECE_MAX, 1, &id)) return 0;
    m = n;
    if (!plan_runs(out, cap, &n, 0, split, 0, &id)) return 0;
    plan_reverse(out + m, n - m);
  }
  for (int i = 0; i < n; i++) out[i].id = i;        /* ids in send order */
  return n;
}

int tcpdpi_build_piece4(uint8_t *dst, size_t dstcap,
                        const uint8_t *ip_tmpl, int ihl,
                        const uint8_t *tcp_tmpl, int tcphl,
                        uint16_t ip_id, uint32_t seq, int ttl, int fake_ttl, uint8_t pattern,
                        const uint8_t *data, const tcpdpi_piece *p) {
  uint8_t junk[TCPDPI_PIECE_MAX];
  if (!p || !data || p->off < 0 || p->len < 1 || p->len > TCPDPI_PIECE_MAX) return 0;
  if (p->fake) memset(junk, pattern, (size_t)p->len);
  return tcpdpi_build_segment4(dst, dstcap, ip_tmpl, ihl, tcp_tmpl, tcphl,
                               (uint16_t)(ip_id + p->id), seq + (uint32_t)p->off,
                               p->fake ? fake_ttl : ttl, p->fake ? junk : data + p->off, p->len, p->fake);
}

int tcpdpi_build_segment6(uint8_t *dst, size_t dstcap,
                          const uint8_t *ip6_tmpl, const uint8_t *tcp_tmpl, int tcphl,
                          uint32_t seq, int hop, const uint8_t *payload, int seg_len,
                          int bad_tcp_csum) {
  if (!dst || !ip6_tmpl || !tcp_tmpl || tcphl < 20 || tcphl > 60 || seg_len < 0) return 0;
  int total = 40 + tcphl + seg_len;
  if (total > TCPDPI_SEG_MAX || (size_t)total > dstcap) return 0;
  if ((ip6_tmpl[0] >> 4) != 6) return 0;
  memcpy(dst, ip6_tmpl, 40);
  memcpy(dst + 40, tcp_tmpl, (size_t)tcphl);
  if (seg_len > 0) {
    if (!payload) return 0;
    memcpy(dst + 40 + tcphl, payload, (size_t)seg_len);
  }
  dst[6] = 6;                                       /* next header: TCP, no extensions */
  wr16(dst + 4, (uint16_t)(tcphl + seg_len));       /* payload length */
  dst[7] = (uint8_t)(hop < 1 ? 1 : (hop > 255 ? 255 : hop));
  wr32(dst + 40 + 4, seq);
  uint32_t sum = csum_add(dst + 8, 32, 0);          /* source + destination */
  sum += (uint32_t)(tcphl + seg_len);               /* upper-layer length */
  sum += 6;                                         /* next header */
  dst[40 + 16] = 0; dst[40 + 17] = 0;
  uint16_t c = csum_fold(csum_add(dst + 40, tcphl + seg_len, sum));
  if (bad_tcp_csum) c = (uint16_t)(c ^ 0xffff);
  wr16(dst + 40 + 16, c);
  return total;
}

int tcpdpi_build_piece6(uint8_t *dst, size_t dstcap,
                        const uint8_t *ip6_tmpl, const uint8_t *tcp_tmpl, int tcphl,
                        uint32_t seq, int hop, int fake_hop, uint8_t pattern,
                        const uint8_t *data, const tcpdpi_piece *p) {
  uint8_t junk[TCPDPI_PIECE_MAX];
  if (!p || !data || p->off < 0 || p->len < 1 || p->len > TCPDPI_PIECE_MAX) return 0;
  if (p->fake) memset(junk, pattern, (size_t)p->len);
  return tcpdpi_build_segment6(dst, dstcap, ip6_tmpl, tcp_tmpl, tcphl, seq + (uint32_t)p->off,
                               p->fake ? fake_hop : hop, p->fake ? junk : data + p->off, p->len, p->fake);
}

int tcpdpi_choose_cuts(const uint8_t *payload, int plen, int is_tls, int host_off, int host_len,
                       int split_pos, int cuts[2]) {
  if (!payload || !cuts || plen < 2) return 0;
  if (split_pos > 0) {
    cuts[0] = split_pos < plen ? split_pos : plen - 1;
    return 1;
  }
  int n = tcpdpi_split_points(payload, plen, host_off, host_len, cuts);
  if (n > 0) return n;
  if (is_tls && plen > TCPDPI_FALLBACK_CUT) {       /* the name is in a later segment */
    cuts[0] = TCPDPI_FALLBACK_CUT;
    return 1;
  }
  return 0;
}

void tcpdpi_refresh_csum4(uint8_t *pkt, int len, int ihl) {
  if (!pkt || ihl < 20 || len < ihl + 20) return;
  fill_checksums4(pkt, len, ihl, 0);
}

/* ───────────────────────── TLS record split (tlsrec) ───────────────────────── */
/* Form the two-record payload from a ClientHello segment. `pay` (capacity
 * TCPDPI_SEG_MAX) gets record 1 (header + k body bytes), record 2 (its header
 * declaring the whole remainder, then whatever of its body is present here), and
 * any bytes that followed the record in the segment. Returns the total length,
 * and *rec1_len the length of record 1 (5 + k) where record 2 begins; 0 on any
 * error. A ClientHello that spans several TCP segments is handled: record 2's
 * body simply continues in the later segments (shifted by the translation
 * table); the split only needs the name to straddle it, and the name's SNI sits
 * early, in this first segment. */
static int tlsrec_form(uint8_t *pay, const uint8_t *real, int real_len, int cut, int *rec1_len) {
  return tcpdpi_tlsrec_form(pay, TCPDPI_SEG_MAX, real, real_len, cut, rec1_len);
}

/* TurkishDPI: the same with the capacity given (the engine forms a large
 * ClientHello's records in a 64 KB buffer and sends them in pieces). */
int tcpdpi_tlsrec_form(uint8_t *pay, int cap, const uint8_t *real, int real_len, int cut, int *rec1_len) {
  if (!pay || !real || real_len < 6) return 0;
  if (real[0] != 0x16 || real[1] != 0x03) return 0;        /* a TLS record */
  int rl = rd16(real + 3);                                  /* this record's body */
  if (rl < 2) return 0;
  int present = real_len - 5;                               /* body bytes in this segment */
  if (present < 2) return 0;
  int k = cut - 5;                                          /* split offset into the body */
  if (k < 1 || k >= rl || k > present) return 0;            /* strictly inside, and present here */
  int this_rec = rl < present ? rl : present;               /* body of THIS record present here */
  int rec2_here = this_rec - k;                             /* record 2 body bytes in this segment */
  int tail = present - this_rec;                            /* bytes after this record (another record) */
  int total = real_len + TCPDPI_TLSREC_GROWTH;
  if (total > cap) return 0;

  int o = 0;
  pay[o++] = 0x16; pay[o++] = real[1]; pay[o++] = real[2];   /* record 1 header, length k */
  wr16(pay + o, (uint16_t)k); o += 2;
  memcpy(pay + o, real + 5, (size_t)k); o += k;
  if (rec1_len) *rec1_len = o;                               /* record 2 begins here */
  pay[o++] = 0x16; pay[o++] = real[1]; pay[o++] = real[2];   /* record 2 header, length rl-k */
  wr16(pay + o, (uint16_t)(rl - k)); o += 2;
  memcpy(pay + o, real + 5 + k, (size_t)rec2_here); o += rec2_here;
  if (tail > 0) { memcpy(pay + o, real + 5 + this_rec, (size_t)tail); o += tail; }
  return o;
}

int tcpdpi_build_tlsrec4(uint8_t *dst, size_t dstcap,
                         const uint8_t *ip_tmpl, int ihl,
                         const uint8_t *tcp_tmpl, int tcphl,
                         uint16_t ip_id, uint32_t seq, int ttl,
                         const uint8_t *real, int real_len, int cut) {
  if (ihl < 20 || tcphl < 20) return 0;
  uint8_t pay[TCPDPI_SEG_MAX];
  int o = tlsrec_form(pay, real, real_len, cut, NULL);
  if (o <= 0) return 0;
  return tcpdpi_build_segment4(dst, dstcap, ip_tmpl, ihl, tcp_tmpl, tcphl,
                               ip_id, seq, ttl, pay, o, 0 /* good: the server keeps it */);
}

int tcpdpi_build_tlsrec_pair(tcpdpi_seg segs[2], int *nsegs,
                             const uint8_t *ip_tmpl, int ihl,
                             const uint8_t *tcp_tmpl, int tcphl,
                             uint16_t ip_id, uint32_t seq, int ttl,
                             const uint8_t *real, int real_len, int cut) {
  if (!segs || !nsegs || ihl < 20 || tcphl < 20) return 0;
  uint8_t pay[TCPDPI_SEG_MAX];
  int rec1 = 0;
  int total = tlsrec_form(pay, real, real_len, cut, &rec1);
  if (total <= 0 || rec1 <= 0 || rec1 >= total) return 0;
  /* Two TCP segments, split at the record boundary: record 1, then record 2 and
   * any tail. Each is no larger than the original segment, so neither can exceed
   * the path MTU the way one five-bytes-bigger segment might; and the two
   * records land in separate segments, which a filter must also reassemble. */
  int a = tcpdpi_build_segment4(segs[0].data, sizeof segs[0].data, ip_tmpl, ihl, tcp_tmpl, tcphl,
                                ip_id, seq, ttl, pay, rec1, 0);
  int b = tcpdpi_build_segment4(segs[1].data, sizeof segs[1].data, ip_tmpl, ihl, tcp_tmpl, tcphl,
                                (uint16_t)(ip_id + 1), seq + (uint32_t)rec1, ttl, pay + rec1, total - rec1, 0);
  if (a <= 0 || b <= 0) return 0;
  segs[0].len = a; segs[0].is_fake = 0;
  segs[1].len = b; segs[1].is_fake = 0;
  *nsegs = 2;
  return 1;
}

/* ───────────────────────── sequence-number translation ───────────────────────── */
typedef struct {
  uint32_t sa, da; uint16_t sp, dp;   /* the outbound four-tuple (network order) */
  uint32_t from;                      /* outbound sequence where the growth began */
  int      growth;                    /* bytes added at/after `from` */
  uint64_t t;                         /* last touched */
  int      used;
  int      fins;                      /* TurkishDPI: FIN seen, bit 0 out, bit 1 in */
  uint64_t dead_at;                   /* TurkishDPI: both FINs seen: retire after this */
} xl_ent;
static xl_ent g_xl[TCPDPI_XLATE_CAP];
/* TurkishDPI: slots [0, g_xl_hi) may be used and g_xl_n are, so a lookup in
 * the per-packet path scans only what is there (nothing at all, usually). */
static int g_xl_hi, g_xl_n;

static void xl_free(xl_ent *e) {
  if (!e->used) return;
  e->used = 0;
  g_xl_n--;
  while (g_xl_hi > 0 && !g_xl[g_xl_hi - 1].used) g_xl_hi--;
}

/* Serial-number comparison (RFC 1982): a is at or after b. */
static int seq_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

void tcpdpi_xlate_reset(void) { memset(g_xl, 0, sizeof(g_xl)); g_xl_hi = g_xl_n = 0; }

static xl_ent *xl_find(uint32_t sa, uint32_t da, uint16_t sp, uint16_t dp) {
  if (!g_xl_n) return NULL;
  for (int i = 0; i < g_xl_hi; i++)
    if (g_xl[i].used && g_xl[i].sa == sa && g_xl[i].da == da && g_xl[i].sp == sp && g_xl[i].dp == dp)
      return &g_xl[i];
  return NULL;
}

int tcpdpi_xlate_begin(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport,
                       uint32_t at_seq, int growth, uint64_t now_ms) {
  if (growth <= 0) return 0;
  xl_ent *e = xl_find(saddr, daddr, sport, dport);
  if (e) { e->from = at_seq; e->growth = growth; e->t = now_ms; e->fins = 0; e->dead_at = 0; return 1; } /* refresh */
  int free_slot = -1;
  for (int i = 0; i < TCPDPI_XLATE_CAP; i++) {
    if (!g_xl[i].used) { if (free_slot < 0) free_slot = i; continue; }
    if (now_ms - g_xl[i].t > TCPDPI_XLATE_TTL_MS ||
        (g_xl[i].dead_at && now_ms > g_xl[i].dead_at)) {
      xl_free(&g_xl[i]);
      if (free_slot < 0 || i < free_slot) free_slot = i;
    }
  }
  if (free_slot < 0) return 0;              /* full of live connections: do not grow */
  xl_ent *s = &g_xl[free_slot];
  memset(s, 0, sizeof(*s));
  s->sa = saddr; s->da = daddr; s->sp = sport; s->dp = dport;
  s->from = at_seq; s->growth = growth; s->t = now_ms; s->used = 1;
  g_xl_n++;
  if (free_slot >= g_xl_hi) g_xl_hi = free_slot + 1;
  return 1;
}

int tcpdpi_xlate_out(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport,
                     uint32_t *seq, uint64_t now_ms) {
  xl_ent *e = xl_find(saddr, daddr, sport, dport);
  if (!e) return 0;
  e->t = now_ms;
  if (!seq_ge(*seq, e->from)) return 0;     /* before the growth: unchanged */
  *seq += (uint32_t)e->growth;
  return 1;
}

int tcpdpi_xlate_in(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport,
                    uint32_t *ack, uint64_t now_ms) {
  /* inbound server->client: match the stored outbound tuple reversed */
  xl_ent *e = xl_find(daddr, saddr, dport, sport);
  if (!e) return 0;
  e->t = now_ms;
  /* The server acknowledges grown bytes once it has taken anything past `from`. */
  if (!seq_ge(*ack, e->from + (uint32_t)e->growth)) return 0;
  *ack -= (uint32_t)e->growth;
  return 1;
}

void tcpdpi_xlate_end(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport) {
  xl_ent *e = xl_find(saddr, daddr, sport, dport);
  if (e) xl_free(e);
}

void tcpdpi_xlate_fin(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport,
                      int inbound, uint64_t now_ms) {
  xl_ent *e = xl_find(saddr, daddr, sport, dport);
  if (!e) return;
  e->fins |= inbound ? 2 : 1;
  if (e->fins == 3 && !e->dead_at) e->dead_at = now_ms + TCPDPI_XLATE_CLOSE_MS;
}

int tcpdpi_xlate_live(uint64_t now_ms) {
  int n = 0;
  for (int i = 0; i < TCPDPI_XLATE_CAP; i++)
    if (g_xl[i].used && now_ms - g_xl[i].t <= TCPDPI_XLATE_TTL_MS) n++;
  return n;
}
