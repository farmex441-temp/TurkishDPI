/*
 * TurkishDPI - the composition of what is sent for one first request (see
 * desync.h). The technique order and the IPv4 path follow Shroudly's packet
 * backend (core/src/raw_windows.c, MIT, see THIRD_PARTY_LICENSES.txt), as
 * engine.c did before this was moved here.
 */
#include <string.h>
#include "desync.h"
#include "core/raw_desync.h"

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int desync_real_count(const desync_out *o) {
    int r = 0;
    for (int i = 0; i < o->n; i++) r += !o->fake[i];
    return r;
}

/* The decoy's payload: the request with another (or a scrambled) name. Of a
 * captured packet larger than one wire packet only the first
 * TCPDPI_PIECE_MAX bytes are used (one piece; the name, when it is in this
 * segment at all, sits early in it). */
static int decoy_payload(uint8_t *out, int cap, const uint8_t *pay, int plen,
                         const proto_info *pi, int iphl, int tcphl, int big) {
    int n;
    if (big && plen > TCPDPI_PIECE_MAX) plen = TCPDPI_PIECE_MAX;
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

static void out_drop(desync_out *o, int k) {             /* remove packet k (rare) */
    for (int i = k; i + 1 < o->n; i++) {
        memcpy(o->pkt[i], o->pkt[i + 1], (size_t)o->len[i + 1]);
        o->len[i] = o->len[i + 1];
        o->fake[i] = o->fake[i + 1];
    }
    o->n--;
}

/* ---------------- IPv4 ---------------- */
/* The decoy (send_fake4) into slot k; takes an IP id only when it has a
 * payload, as before. 1 when built. */
static int decoy4(desync_out *o, int k, const uint8_t *pkt, int len, int ihl, int tcphl,
                  const proto_info *pi, int ttl, uint16_t *ipid) {
    uint8_t pay[TCPDPI_SEG_MAX];
    int poff = ihl + tcphl, plen = len - poff, dlen, n;
    if (plen <= 0 || (!o->big && plen > TCPDPI_WIRE_MAX)) return 0;
    dlen = decoy_payload(pay, (int)sizeof(pay), pkt + poff, plen, pi, ihl, tcphl, o->big);
    if (dlen <= 0) return 0;
    n = tcpdpi_build_segment4(o->pkt[k], sizeof(o->pkt[k]), pkt, ihl, pkt + ihl, tcphl, (*ipid)++,
                              rd32(pkt + ihl + 4), ttl, pay, dlen, 1);
    if (n <= 0) return 0;
    o->len[k] = n;
    o->fake[k] = 1;
    return 1;
}

/* Every piece of o->plan[0..n) built into slots base.., IP ids from id0. */
static int pieces4(desync_out *o, int base, int n, const uint8_t *pkt, int ihl, int tcphl,
                   const uint8_t *data, uint32_t seq, int ttl, int fake_ttl, uint8_t pattern, uint16_t id0) {
    if (n <= 0 || base + n > DESYNC_OUT_MAX) return 0;
    for (int i = 0; i < n; i++) {
        int len = tcpdpi_build_piece4(o->pkt[base + i], sizeof(o->pkt[0]), pkt, ihl, pkt + ihl, tcphl,
                                      id0, seq, ttl, fake_ttl, pattern, data, &o->plan[i]);
        if (len <= 0) return 0;
        o->len[base + i] = len;
        o->fake[base + i] = o->plan[i].fake;
    }
    return 1;
}

/* The real request cut (send_cut4), appended after what o holds. */
static int cut4(desync_out *o, const desync_cfg *c, const uint8_t *pkt, int len, int ihl, int tcphl,
                const proto_info *pi, int disorder, uint16_t *ipid) {
    int poff = ihl + tcphl, plen = len - poff, cuts[2];
    int ttl = pkt[8] ? pkt[8] : 64;
    int nc = tcpdpi_choose_cuts(pkt + poff, plen, pi->kind == PROTO_TLS, pi->host_off, pi->host_len,
                                c->split_pos, cuts);
    int n = nc > 0 ? tcpdpi_plan_split(o->plan, TCPDPI_PIECES_MAX, plen, cuts, nc, disorder,
                                       tcpdpi_piece_limit(len, poff)) : 0;
    if (!pieces4(o, o->n, n, pkt, ihl, tcphl, pkt + poff, rd32(pkt + ihl + 4), ttl, ttl, 0, *ipid)) return 0;
    *ipid = (uint16_t)(*ipid + n);
    o->n += n;
    o->replaced = 1;
    return 1;
}

int desync_compose4(desync_out *o, const desync_cfg *c, const uint8_t *pkt, int len, int ihl, int tcphl,
                    const proto_info *pi, int decoy, int decoy_ttl, uint16_t *ipid,
                    desync_grow_fn grow, void *grow_ctx) {
    int poff = ihl + tcphl, plen = len - poff;
    uint32_t seq;
    int otl, lim;
    int faked = c->desync_mode == RAW_DESYNC_FAKEDSPLIT || c->desync_mode == RAW_DESYNC_FAKEDDISORDER;
    o->n = 0; o->replaced = 0; o->grew = 0; o->big = len > TCPDPI_WIRE_MAX;
    if (ihl < 20 || tcphl < 20 || plen <= 0) return 0;
    seq = rd32(pkt + ihl + 4);
    otl = pkt[8] ? pkt[8] : 64;
    lim = tcpdpi_piece_limit(len, poff);

    /* The TLS record split needs the name (the cut goes inside it); a
     * ClientHello whose name is not in this segment gets the plain cut below
     * instead. Record 1, then record 2 and the rest; the decoy first. */
    if (c->tlsrec && pi->kind == PROTO_TLS && pi->host_off >= 1) {
        int cuts[2], nc = tcpdpi_split_points(pkt + poff, plen, pi->host_off, pi->host_len, cuts);
        int cut = nc >= 2 ? cuts[1] : (nc == 1 ? cuts[0] : 0);
        int rec1 = 0, glen = cut > 5 ? tcpdpi_tlsrec_form(o->grown, (int)sizeof(o->grown), pkt + poff, plen, cut, &rec1) : 0;
        int n = glen > 0 && rec1 > 0 && rec1 < glen
              ? tcpdpi_plan_split(o->plan, TCPDPI_PIECES_MAX, glen, &rec1, 1, 0, lim) : 0;
        int base = decoy ? 1 : 0;
        if (n > 0 && (o->big || n == 2) &&
            pieces4(o, base, n, pkt, ihl, tcphl, o->grown, seq, otl, otl, 0, *ipid) &&
            (!grow || grow(grow_ctx))) {
            *ipid = (uint16_t)(*ipid + n);
            o->n = base + n;
            if (decoy && !decoy4(o, 0, pkt, len, ihl, tcphl, pi, decoy_ttl, ipid)) out_drop(o, 0);
            o->replaced = 1;
            o->grew = 1;
            return 1;
        }
        o->n = 0;
    }
    if (!decoy) {
        if (c->split == RAW_SPLIT_OFF && !faked) return 0;
        return cut4(o, c, pkt, len, ihl, tcphl, pi,
                    c->split == RAW_SPLIT_DISORDER || c->desync_mode == RAW_DESYNC_FAKEDDISORDER, ipid);
    }
    if (faked) {
        int split = c->split_pos > 0 ? (c->split_pos < plen ? c->split_pos : plen - 1)
                  : (pi->host_off > 0 && pi->host_off < plen) ? pi->host_off : (plen > 2 ? 2 : 1);
        int n = tcpdpi_plan_fakedsplit(o->plan, TCPDPI_PIECES_MAX, plen, split,
                                       c->desync_mode == RAW_DESYNC_FAKEDDISORDER, lim);
        if (!o->big && n != 3) return 0;                 /* as before: three segments or nothing */
        if (!pieces4(o, 0, n, pkt, ihl, tcphl, pkt + poff, seq, otl, decoy_ttl, c->fake_pattern, *ipid)) return 0;
        *ipid = (uint16_t)(*ipid + n);
        o->n = n;
        o->replaced = 1;
        return 1;
    }
    if (decoy4(o, 0, pkt, len, ihl, tcphl, pi, decoy_ttl, ipid)) o->n = 1;
    if (c->seqovl > 0) {
        /* one overlapped segment; of a larger captured packet the overlap
         * carries the first TCPDPI_PIECE_MAX bytes (pattern included) and the
         * rest follows in order, in pieces */
        int first = !o->big || plen + c->seqovl <= TCPDPI_PIECE_MAX ? plen : TCPDPI_PIECE_MAX - c->seqovl;
        int rest = 0, k = o->n;
        if (first > 0 && first < plen)
            rest = tcpdpi_plan_split(o->plan, TCPDPI_PIECES_MAX, plen - first, NULL, 0, 0, TCPDPI_PIECE_MAX);
        if (first > 0 && (first == plen || rest > 0)) {
            int n = tcpdpi_build_seqovl4(o->pkt[k], sizeof(o->pkt[k]), pkt, ihl, pkt + ihl, tcphl, *ipid, seq, otl,
                                         pkt + poff, first, c->seqovl, c->seqovl_pattern);
            if (n > 0 && (first == plen ||
                          pieces4(o, k + 1, rest, pkt, ihl, tcphl, pkt + poff + first, seq + (uint32_t)first,
                                  otl, otl, 0, (uint16_t)(*ipid + 1)))) {
                o->len[k] = n;
                o->fake[k] = 0;
                o->n = k + 1 + (first < plen ? rest : 0);
                *ipid = (uint16_t)(*ipid + 1 + (first < plen ? rest : 0));
                o->replaced = 1;
                return 1;
            }
        }
    }
    if (c->split != RAW_SPLIT_OFF)
        return cut4(o, c, pkt, len, ihl, tcphl, pi, c->split == RAW_SPLIT_DISORDER, ipid);
    return 0;
}

/* ---------------- IPv6 ---------------- */
int desync_compose6(desync_out *o, const desync_cfg *c, const uint8_t *pkt, int len, int tcphl,
                    const proto_info *pi, int decoy, int decoy_hop) {
    const int poff = 40 + tcphl;
    const uint8_t *pay = pkt + poff;
    int plen = len - poff, hop = pkt[7] ? pkt[7] : 64;
    int cuts[2], nc, n;
    int disorder = c->split == RAW_SPLIT_DISORDER || c->desync_mode == RAW_DESYNC_FAKEDDISORDER;
    int cut = c->split != RAW_SPLIT_OFF || c->desync_mode != RAW_DESYNC_FAKE;
    uint32_t seq;
    o->n = 0; o->replaced = 0; o->grew = 0; o->big = len > TCPDPI_WIRE_MAX;
    if (tcphl < 20 || tcphl > 60 || plen <= 0) return 0;
    seq = rd32(pkt + 40 + 4);
    if (decoy) {
        uint8_t fake[TCPDPI_SEG_MAX];
        int dlen = decoy_payload(fake, (int)sizeof(fake), pay, plen, pi, 40, tcphl, o->big);
        if (dlen > 0 && (n = tcpdpi_build_segment6(o->pkt[0], sizeof(o->pkt[0]), pkt, pkt + 40, tcphl, seq,
                                                   decoy_hop, fake, dlen, 1)) > 0) {
            o->len[0] = n;
            o->fake[0] = 1;
            o->n = 1;
        }
    }
    if (!cut) return 0;
    nc = tcpdpi_choose_cuts(pay, plen, pi->kind == PROTO_TLS, pi->host_off, pi->host_len, c->split_pos, cuts);
    n = nc > 0 ? tcpdpi_plan_split(o->plan, TCPDPI_PIECES_MAX, plen, cuts, nc, disorder,
                                   tcpdpi_piece_limit(len, poff)) : 0;
    if (n <= 0 || o->n + n > DESYNC_OUT_MAX) return 0;
    for (int i = 0; i < n; i++) {                  /* all built before any is sent */
        int k = o->n + i;
        o->len[k] = tcpdpi_build_piece6(o->pkt[k], sizeof(o->pkt[k]), pkt, pkt + 40, tcphl,
                                        seq, hop, hop, 0, pay, &o->plan[i]);
        if (o->len[k] <= 0) return 0;          /* the decoy goes, the original passes */
        o->fake[k] = 0;
    }
    o->n += n;
    o->replaced = 1;
    return 1;
}
