/*
 * TurkishDPI - what the engine sends for one outbound first request (a TLS
 * ClientHello or an HTTP request): the decoy, the cut, fakedsplit /
 * fakeddisorder, the sequence overlap and the TLS record split, composed into
 * a list of finished packets. Pure (no WinDivert, no Win32) so the exact
 * composition is unit tested (tests/desync_test.c); engine.c only sends the
 * list and passes the original when it was not replaced.
 *
 * A captured packet that fits one wire packet (<= TCPDPI_WIRE_MAX bytes)
 * gives exactly the packets the engine sent before 2.0.2 (as measured on real
 * lines). A larger one - handed over before segmentation offload - is sent in
 * pieces of at most TCPDPI_PIECE_MAX payload bytes, with a decoy of the first
 * TCPDPI_PIECE_MAX bytes (see tcpdpi.h).
 */
#ifndef TURKISHDPI_DESYNC_H
#define TURKISHDPI_DESYNC_H
#include <stdint.h>
#include "core/tcpdpi.h"
#include "core/proto.h"

#define DESYNC_OUT_MAX (TCPDPI_PIECES_MAX + 2)       /* pieces + a decoy + an overlap */

typedef struct {
    int split;                 /* RAW_SPLIT_* */
    int desync_mode;           /* RAW_DESYNC_* */
    int split_pos;             /* > 0: cut after this byte, not at the name */
    int seqovl;                /* IPv4: overlap the first segment by N bytes */
    uint8_t seqovl_pattern, fake_pattern;
    int tlsrec;                /* IPv4: split the ClientHello's TLS record */
} desync_cfg;

typedef struct {
    uint8_t pkt[DESYNC_OUT_MAX][TCPDPI_SEG_MAX];   /* finished packets, checksums in them */
    int len[DESYNC_OUT_MAX];
    int fake[DESYNC_OUT_MAX];  /* 1: a decoy (never accepted by the server) */
    int n;                     /* packets to send, in this order */
    int replaced;              /* 1: they replace the original; 0: pass it after them */
    int grew;                  /* 1: the TLS record split grew the stream */
    int big;                   /* the captured packet was larger than one wire packet */
    tcpdpi_piece plan[TCPDPI_PIECES_MAX];          /* scratch */
    uint8_t grown[65535 + TCPDPI_TLSREC_GROWTH];   /* scratch: the split records */
} desync_out;

/* Called once the TLS record split is built: register the translation of the
 * connection's later sequence numbers; return 1 to go ahead, 0 to fall back
 * to the other techniques (nothing of the split is sent then). */
typedef int (*desync_grow_fn)(void *ctx);

/* IPv4: pkt/len the captured outbound segment (ihl, tcphl its header
 * lengths, payload after them, len - ihl - tcphl > 0), pi its proto_inspect,
 * decoy whether this connection may get decoys, decoy_ttl their TTL, *ipid
 * the next IP id (advanced by what was used). Returns o->replaced. */
int desync_compose4(desync_out *o, const desync_cfg *c, const uint8_t *pkt, int len, int ihl, int tcphl,
                    const proto_info *pi, int decoy, int decoy_ttl, uint16_t *ipid,
                    desync_grow_fn grow, void *grow_ctx);

/* IPv6 (fixed header only): the decoy and the cut (no overlap, no record
 * split). decoy_hop the decoy's hop limit. Returns o->replaced. */
int desync_compose6(desync_out *o, const desync_cfg *c, const uint8_t *pkt, int len, int tcphl,
                    const proto_info *pi, int decoy, int decoy_hop);

/* Real (non-decoy) packets in o. */
int desync_real_count(const desync_out *o);

#endif
