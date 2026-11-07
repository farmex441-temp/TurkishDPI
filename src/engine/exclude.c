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
#include "goodbyedpi.h"
#include "exclude.h"
#include "utils/uthash.h"
#include "utils/getline.h"

typedef struct exclude_record {
    char *host;
    UT_hash_handle hh;
} exclude_record_t;

static exclude_record_t *excluded = NULL;
static int excluded_count = 0;

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
    exclude_record_t *r = NULL;
    HASH_FIND_STR(excluded, host, r);
    return r != NULL;
}

int exclude_add(const char *host) {
    char buf[HOST_MAXLEN + 1];
    const char *s = host;
    exclude_record_t *r;
    while (*s == ' ' || *s == '\t' || *s == '.') s++;
    if (strlen(s) < 2 || strlen(s) > HOST_MAXLEN || *s == '#') return 0;
    strcpy(buf, s);
    normalize(buf);
    if (!buf[0] || lookup(buf)) return 0;
    r = malloc(sizeof(*r));
    if (!r) return 0;
    r->host = strdup(buf);
    HASH_ADD_KEYPTR(hh, excluded, r->host, strlen(r->host), r);
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
    char *line = malloc(HOST_MAXLEN + 2);
    size_t linelen = HOST_MAXLEN + 2;
    int cnt = 0;
    FILE *fp = fopen(filename, "r");
    if (!fp) { free(line); return -1; }
    while (getline(&line, &linelen, fp) != -1) {
        line[strcspn(line, "\r\n#")] = '\0';
        cnt += exclude_add(line);
    }
    free(line);
    fclose(fp);
    return cnt;
}

int exclude_enabled(void) {
    return excluded_count > 0;
}

int exclude_check_hostname(const char *host_addr, size_t host_len) {
    char host[HOST_MAXLEN + 1];
    char *dot;
    if (!excluded || !host_addr || !host_len || host_len > HOST_MAXLEN) return 0;
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
