/*
 * TurkishDPI - pure decisions of the packet engine (see policy.h).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "policy.h"
#include "core/udpdpi.h"

/* A bounded text buffer: an append that does not fit marks it as overflowed
 * and leaves the text as it was (terminated); nothing is written past cap. */
typedef struct { char *buf; size_t cap, len; int over; } textbuf;

static void tb_init(textbuf *t, char *buf, size_t cap) {
    t->buf = buf; t->cap = cap; t->len = 0; t->over = cap == 0;
    if (cap) buf[0] = 0;
}

static void tb_add(textbuf *t, const char *fmt, ...) {
    va_list ap;
    size_t room;
    int n;
    if (t->over) return;
    room = t->cap - t->len;
    va_start(ap, fmt);
    n = vsnprintf(t->buf + t->len, room, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= room) {        /* would not fit: keep what was there */
        t->buf[t->len] = 0;
        t->over = 1;
        return;
    }
    t->len += (size_t)n;
}

int policy_build_filter(char *f, size_t cap, const policy_filter_opts *o) {
    char udp[1024] = "", subbuf[512];
    textbuf t, sub;
    if (!f || !o) return -1;
    tb_init(&t, f, cap);
    /* outbound web: the first request (TLS handshake records, HTTP), and the
     * client's own TLS alerts (a decoy that upset the server); with the TLS
     * record split, every IPv4 443 segment (their sequence numbers move) */
    tb_add(&t, "(outbound and !loopback and tcp and (%s"
               "(tcp.DstPort == 443 and tcp.PayloadLength >= 6 and tcp.Payload[1] == 3 and "
               "(tcp.Payload[0] == 22 or tcp.Payload[0] == 21)) or "
               "(tcp.DstPort == 80 and tcp.PayloadLength > 0)))",
           o->tlsrec ? "(ip and tcp.DstPort == 443) or " : "");
    /* QUIC Initials (both families) and Discord voice (IPv4) */
    if (o->quic_block && udp_windivert_clause(udp, sizeof(udp), 1, 0, UDP_PREFILTER_PAYLOAD) > 0)
        tb_add(&t, " or (outbound and !loopback and udp and %s)", udp);
    if (o->udp_fake && udp_windivert_clause(udp, sizeof(udp), 0, 1, UDP_PREFILTER_PAYLOAD) > 0)
        tb_add(&t, " or (outbound and !loopback and ip and udp and %s)", udp);
    /* DNS: the system's questions, and the answers to them */
    if (o->dns4 || o->dns6 || o->exclude) {
        int hybrid = o->dns_hybrid && (o->dns4 || o->dns6);
        tb_add(&t, " or (outbound and !loopback and udp and udp.DstPort == 53)");
        if (hybrid || (!o->dns4 && !o->dns6))
            tb_add(&t, " or (inbound and !loopback and udp and udp.SrcPort == 53)");
        if (o->dns4)
            tb_add(&t, " or (inbound and !loopback and udp and ip.SrcAddr == %s and udp.SrcPort == %u)",
                   o->dns4_addr ? o->dns4_addr : "", o->dns4_port);
        if (o->dns6)
            tb_add(&t, " or (inbound and !loopback and udp and ipv6.SrcAddr == %s and udp.SrcPort == %u)",
                   o->dns6_addr ? o->dns6_addr : "", o->dns6_port);
    }
    /* inbound: forged resets, SYN-ACKs (hop distance), TLS alerts, and the
     * acknowledgements of a connection whose record was split */
    tb_init(&sub, subbuf, sizeof(subbuf));
    if (o->tlsrec) tb_add(&sub, "(ip and tcp.SrcPort == 443)");
    if (o->drop_rst) tb_add(&sub, "%stcp.Rst", sub.len ? " or " : "");
    if (o->autottl) tb_add(&sub, "%s(tcp.Syn and tcp.Ack)", sub.len ? " or " : "");
    if (o->alert_watch)
        tb_add(&sub, "%s(tcp.SrcPort == 443 and tcp.PayloadLength >= 7 and tcp.Payload[0] == 21 and tcp.Payload[1] == 3)",
               sub.len ? " or " : "");
    if (sub.over) return -1;
    if (sub.len)
        tb_add(&t, " or (inbound and !loopback and tcp and (tcp.SrcPort == 443 or tcp.SrcPort == 80) and (%s))", subbuf);
    return t.over ? -1 : (int)t.len;
}

int policy_excluded(int enabled, const char *name, size_t name_len, int ech, const void *daddr, int v6,
                    policy_name_fn name_listed, policy_ip_fn ip_learned) {
    if (!enabled) return 0;
    if (name && name_len > 0) {
        if (name_listed && name_listed(name, name_len)) return 1;
        if (ech == 0) return 0;              /* the name is the site's own: it alone decides */
    }
    return ip_learned && daddr ? ip_learned(daddr, v6) != 0 : 0;
}
