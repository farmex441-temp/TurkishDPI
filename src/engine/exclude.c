/*
 * TurkishDPI - "never touch" list.
 *
 * Connections to these domains (and their subdomains) are passed through
 * completely unmodified: no fake packets, no fragmentation, no Host header
 * tricks. Banks, card schemes, 3-D Secure and payment gateways run strict
 * fraud / bot detection, so we keep their traffic byte-for-byte original.
 *
 * The built-in list is enabled by default; extra domains can be loaded with
 * --exclude <file> (one domain per line, '#' comments allowed).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "exclude.h"

#define HOST_MAXLEN 253
/* The names, in an open-addressing set (a few hundred at most). */
#define EXCL_SLOTS 1024                  /* power of two, > 2x the names */
static char *excluded[EXCL_SLOTS];
static int excluded_count = 0;

static unsigned name_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static const char *BUILTIN_EXCLUDES[] = {
    /* Turkish banks */
    "garantibbva.com.tr", "garanti.com.tr", "isbank.com.tr", "akbank.com", "akbank.com.tr",
    "yapikredi.com.tr", "ziraatbank.com.tr", "vakifbank.com.tr", "halkbank.com.tr",
    "qnb.com.tr", "qnbfinansbank.com", "denizbank.com", "teb.com.tr", "ing.com.tr",
    "enpara.com", "kuveytturk.com.tr", "albaraka.com.tr", "turkiyefinans.com.tr",
    "vakifkatilim.com.tr", "ziraatkatilim.com.tr", "emlakkatilim.com.tr", "sekerbank.com.tr",
    "fibabanka.com.tr", "odeabank.com.tr", "anadolubank.com.tr", "hsbc.com.tr",
    "burgan.com.tr", "alternatifbank.com.tr", "icbc.com.tr", "tcmb.gov.tr",
    /* Turkish payment / e-money / card networks */
    "papara.com", "ininal.com", "param.com.tr", "iyzico.com", "iyzipay.com", "paytr.com",
    "craftgate.io", "sipay.com.tr", "paycell.com.tr", "troyodeme.com", "bkm.com.tr",
    "bkmexpress.com.tr", "masterpassturkiye.com", "masterpass.com", "tosla.com",
    "moka.com", "payu.com.tr", "hepsipay.com", "colendi.com", "midas.app", "ozan.com",
    "paynet.com.tr", "nestpay.com", "asseco-see.com.tr", "est.com.tr", "payten.com.tr",
    "turkiye.gov.tr", "gib.gov.tr", "sgk.gov.tr",
    /* Global payments, card schemes and 3-D Secure */
    "paypal.com", "paypalobjects.com", "braintreegateway.com", "braintree-api.com",
    "stripe.com", "stripe.network", "js.stripe.com", "adyen.com", "adyenpayments.com",
    "checkout.com", "klarna.com", "klarnacdn.net", "wise.com", "revolut.com", "payoneer.com",
    "skrill.com", "neteller.com", "paysafecard.com", "worldpay.com", "authorize.net",
    "squareup.com", "afterpay.com", "affirm.com", "2checkout.com", "paddle.com",
    "visa.com", "visa.com.tr", "mastercard.com", "mastercard.com.tr", "americanexpress.com",
    "aexp.com", "discover.com", "unionpayintl.com", "jcb.co.jp",
    "cardinalcommerce.com", "arcot.com", "3dsecure.io", "securecode.com",
    "pay.google.com", "payments.google.com", "apple-pay-gateway.apple.com",
    "pay.amazon.com", "payments.amazon.com", "payments-amazon.com",
    "coinbase.com", "binance.com", "binance.tr", "btcturk.com", "paribu.com",
};

static void normalize(char *s) {
    size_t n;
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    n = strlen(s);
    while (n && (s[n - 1] == '.' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static int lookup(const char *host) {
    for (unsigned i = name_hash(host) & (EXCL_SLOTS - 1);; i = (i + 1) & (EXCL_SLOTS - 1)) {
        if (!excluded[i]) return 0;
        if (!strcmp(excluded[i], host)) return 1;
    }
}

int exclude_add(const char *host) {
    char buf[HOST_MAXLEN + 1];
    const char *s = host;
    unsigned i;
    while (*s == ' ' || *s == '\t' || *s == '.') s++;
    if (strlen(s) < 2 || strlen(s) > HOST_MAXLEN || *s == '#') return 0;
    strcpy(buf, s);
    normalize(buf);
    if (!buf[0] || lookup(buf) || excluded_count >= EXCL_SLOTS / 2) return 0;
    for (i = name_hash(buf) & (EXCL_SLOTS - 1); excluded[i]; i = (i + 1) & (EXCL_SLOTS - 1)) {}
    excluded[i] = strdup(buf);
    if (!excluded[i]) return 0;
    excluded_count++;
    return 1;
}

int exclude_load_builtin(void) {
    int n = 0;
    for (size_t i = 0; i < sizeof(BUILTIN_EXCLUDES) / sizeof(BUILTIN_EXCLUDES[0]); i++)
        n += exclude_add(BUILTIN_EXCLUDES[i]);
    return n;
}

int exclude_load_list(const char *filename) {
    char line[1024];
    int cnt = 0, first = 1, skipping = 0;
    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        size_t n = strlen(line);
        int complete = n && line[n - 1] == '\n';
        if (skipping) {                 /* tail of an over-long line: ignore it */
            skipping = !complete;
            continue;
        }
        skipping = !complete && n == sizeof(line) - 1;
        /* UTF-8 byte order mark (Notepad writes one) */
        if (first && n >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, n - 3 + 1);
        first = 0;
        line[strcspn(line, "\r\n#")] = '\0';
        cnt += exclude_add(line);
    }
    fclose(fp);
    return cnt;
}

int exclude_enabled(void) {
    return excluded_count > 0;
}

/* ---- addresses of excluded names, learned from DNS answers ---- */
#define EXCL_V4 512
#define EXCL_V6 256
#define EXCL_TTL_MS (2ull * 3600ull * 1000ull)
static struct { unsigned long addr; ULONGLONG until; } excl_v4[EXCL_V4];
static struct { unsigned char addr[16]; ULONGLONG until; } excl_v6[EXCL_V6];
static int excl_v4_next, excl_v6_next;

static void ip_add(const unsigned char *a, int is_v6) {
    ULONGLONG until = GetTickCount64() + EXCL_TTL_MS;
    if (!is_v6) {
        unsigned long v;
        memcpy(&v, a, 4);
        for (int i = 0; i < EXCL_V4; i++)
            if (excl_v4[i].until && excl_v4[i].addr == v) { excl_v4[i].until = until; return; }
        excl_v4[excl_v4_next].addr = v;
        excl_v4[excl_v4_next].until = until;
        excl_v4_next = (excl_v4_next + 1) % EXCL_V4;
    } else {
        for (int i = 0; i < EXCL_V6; i++)
            if (excl_v6[i].until && !memcmp(excl_v6[i].addr, a, 16)) { excl_v6[i].until = until; return; }
        memcpy(excl_v6[excl_v6_next].addr, a, 16);
        excl_v6[excl_v6_next].until = until;
        excl_v6_next = (excl_v6_next + 1) % EXCL_V6;
    }
}

int exclude_check_ip(const void *addr, int is_ipv6) {
    ULONGLONG now;
    if (!excluded_count) return 0;
    now = GetTickCount64();
    if (!is_ipv6) {
        unsigned long v;
        memcpy(&v, addr, 4);
        for (int i = 0; i < EXCL_V4; i++)
            if (excl_v4[i].until > now && excl_v4[i].addr == v) return 1;
    } else {
        for (int i = 0; i < EXCL_V6; i++)
            if (excl_v6[i].until > now && !memcmp(excl_v6[i].addr, addr, 16)) return 1;
    }
    return 0;
}

static int skip_name(const unsigned char *m, unsigned int len, unsigned int off) {
    while (off < len) {
        unsigned char l = m[off];
        if (l == 0) return (int)off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? (int)off + 2 : -1;
        if (l > 63) return -1;
        off += l + 1u;
    }
    return -1;
}

void exclude_learn_dns(const char *data, unsigned int len) {
    const unsigned char *m = (const unsigned char *)data;
    char name[HOST_MAXLEN + 1];
    unsigned int off = 12, n = 0, qd, an;
    int o;
    if (!excluded_count || len < 12 || !(m[2] & 0x80) || (m[3] & 0x0F)) return;
    qd = (m[4] << 8) | m[5];
    an = (m[6] << 8) | m[7];
    if (qd != 1 || !an) return;
    /* question name (never compressed in a question) */
    while (off < len && m[off]) {
        unsigned int l = m[off];
        if (l > 63 || off + 1 + l > len || n + l + 1 > HOST_MAXLEN) return;
        if (n) name[n++] = '.';
        memcpy(name + n, m + off + 1, l);
        n += l;
        off += l + 1;
    }
    if (off >= len || !n) return;
    name[n] = 0;
    if (!exclude_check_hostname(name, n)) return;
    o = (int)off + 1 + 4;   /* root label, QTYPE, QCLASS */
    for (unsigned int i = 0; i < an; i++) {
        unsigned int type, rdlen;
        o = skip_name(m, len, (unsigned int)o);
        if (o < 0 || (unsigned int)o + 10 > len) return;
        type = (m[o] << 8) | m[o + 1];
        rdlen = (m[o + 8] << 8) | m[o + 9];
        o += 10;
        if ((unsigned int)o + rdlen > len) return;
        if (type == 1 && rdlen == 4) ip_add(m + o, 0);
        else if (type == 28 && rdlen == 16) ip_add(m + o, 1);
        o += (int)rdlen;
    }
}

int exclude_check_hostname(const char *host_addr, size_t host_len) {
    char host[HOST_MAXLEN + 1];
    char *dot;
    if (!excluded_count || !host_addr || !host_len || host_len > HOST_MAXLEN) return 0;
    memcpy(host, host_addr, host_len);
    host[host_len] = 0;
    /* strip :port from HTTP Host headers */
    if ((dot = strchr(host, ':')) != NULL) *dot = 0;
    normalize(host);
    if (lookup(host)) return 1;
    for (dot = strchr(host, '.'); dot; dot = strchr(dot + 1, '.'))
        if (lookup(dot + 1)) return 1;
    return 0;
}
