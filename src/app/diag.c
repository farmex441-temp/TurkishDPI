/*
 * TurkishDPI - classification of a site that does not open (see diag.h).
 */
#include "diag.h"

diag_t diag_classify(const diag_facts_t *f) {
    if (f->https_ok)
        return DIAG_OK;
    if (f->sys_n == 0 && f->clean_n == 0)
        return DIAG_NO_DNS;
    /* Connections were tried and none got through: the address itself is
     * unreachable. No packet trick on the TLS handshake changes that. */
    if (f->tcp_tried > 0 && !f->tcp_ok)
        return DIAG_IP_BLOCK;
    /* The network's resolver gave only forged answers, or none while the
     * clean one knows the name. */
    if ((f->sys_n > 0 && f->sys_bogus == f->sys_n) || (f->sys_n == 0 && f->clean_n > 0))
        return DIAG_DNS_POISON;
    return DIAG_TLS_BLOCK;
}

int diag_bypassable(diag_t d) {
    return d == DIAG_DNS_POISON || d == DIAG_TLS_BLOCK;
}

const char *diag_code(diag_t d) {
    switch (d) {
    case DIAG_OK:         return "ok";
    case DIAG_NO_DNS:     return "no_dns";
    case DIAG_DNS_POISON: return "dns_poison";
    case DIAG_TLS_BLOCK:  return "tls_block";
    case DIAG_IP_BLOCK:   return "ip_block";
    }
    return "?";
}
