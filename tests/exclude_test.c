/* Checks the payment/bank "never touch" list: host name matcher, addresses
 * learned from DNS answers, the learned-address tables and the --exclude file
 * loader. Run under AddressSanitizer: every buffer handed to the code under
 * test is a heap copy of exactly the bytes meant, so a read past the end is
 * caught. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include "exclude.h"

static int fails;
static int grp;
static int cases[6];
#define G_HOST 0
#define G_LEARN 1
#define G_CAP 2
#define G_LIST 3
#define G_EXP 4

#define EXPECT(cond) do { cases[grp]++; if (!(cond)) { \
    printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ---------- helpers ---------- */

/* Calls the matcher with a heap buffer of exactly n bytes (no terminator). */
static int host_n(const char *s, size_t n) {
    char *p = (char *)malloc(n ? n : 1);
    int r;
    if (n) memcpy(p, s, n);
    r = exclude_check_hostname(p, n);
    free(p);
    return r;
}
static int host(const char *s) { return host_n(s, strlen(s)); }

typedef struct { unsigned char b[8192]; unsigned n; } msg_t;
static void p8(msg_t *m, unsigned v) { m->b[m->n++] = (unsigned char)v; }
static void p16(msg_t *m, unsigned v) { p8(m, v >> 8); p8(m, v); }
static void p32(msg_t *m, unsigned v) { p16(m, v >> 16); p16(m, v); }
static void hdr(msg_t *m, unsigned flags, unsigned qd, unsigned an) {
    m->n = 0;
    p16(m, 0x1234); p16(m, flags); p16(m, qd); p16(m, an); p16(m, 0); p16(m, 0);
}
static void name(msg_t *m, const char *s) {
    while (*s) {
        const char *e = strchr(s, '.');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        p8(m, (unsigned)l);
        memcpy(m->b + m->n, s, l);
        m->n += (unsigned)l;
        s += l + (e ? 1 : 0);
    }
    p8(m, 0);
}
static void question(msg_t *m, const char *qn) { name(m, qn); p16(m, 1); p16(m, 1); }
/* resource record whose owner name is the pointer to the question (C0 0C) */
static void rr(msg_t *m, unsigned type, const void *rd, unsigned rdlen) {
    p16(m, 0xC00C); p16(m, type); p16(m, 1); p32(m, 60); p16(m, rdlen);
    memcpy(m->b + m->n, rd, rdlen);
    m->n += rdlen;
}
static void rr_owner(msg_t *m, const unsigned char *owner, unsigned olen,
                     unsigned type, const void *rd, unsigned rdlen) {
    memcpy(m->b + m->n, owner, olen);
    m->n += olen;
    p16(m, type); p16(m, 1); p32(m, 60); p16(m, rdlen);
    memcpy(m->b + m->n, rd, rdlen);
    m->n += rdlen;
}
#define RESP 0x8180u

/* Feeds a heap copy of exactly n bytes to the learner. */
static void learn_n(const unsigned char *b, unsigned n) {
    unsigned char *p = (unsigned char *)malloc(n ? n : 1);
    if (n) memcpy(p, b, n);
    exclude_learn_dns((const char *)p, n);
    free(p);
}
static void learn(const msg_t *m) { learn_n(m->b, m->n); }
static int has4(int a, int b, int c, int d) {
    unsigned char x[4];
    x[0] = (unsigned char)a; x[1] = (unsigned char)b; x[2] = (unsigned char)c; x[3] = (unsigned char)d;
    return exclude_check_ip(x, 0);
}
static int has6(const unsigned char *a) { return exclude_check_ip(a, 1); }
/* One A answer for qn. */
static void learn_a(const char *qn, int a, int b, int c, int d) {
    msg_t m;
    unsigned char ip[4];
    ip[0] = (unsigned char)a; ip[1] = (unsigned char)b; ip[2] = (unsigned char)c; ip[3] = (unsigned char)d;
    hdr(&m, RESP, 1, 1); question(&m, qn); rr(&m, 1, ip, 4);
    learn(&m);
}

static int known;   /* names we know to be in the set (for the capacity test) */
static void track(int r) { if (r > 0) known += r; }

/* temp file in the current directory; the process id keeps two runs apart */
static char listfile_buf[64];
#define LISTFILE listfile_buf

/* fake clock (milliseconds), see exclude_set_clock_for_test */
static unsigned long long g_now = 1000000ull;
static unsigned long long fake_clock(void) { return g_now; }
#define TTL 7200000ull
static void write_file(const void *data, size_t n) {
    FILE *f = fopen(LISTFILE, "wb");
    if (!f) { printf("cannot write %s\n", LISTFILE); exit(2); }
    if (n) fwrite(data, 1, n, f);
    fclose(f);
}
static int load_str(const char *s) {
    int r;
    write_file(s, strlen(s));
    r = exclude_load_list(LISTFILE);
    track(r);
    return r;
}

/* ---------- 1. host name matcher ---------- */
static void test_hostname(void) {
    char big[400];
    grp = G_HOST;
    track(exclude_load_builtin());
    EXPECT(exclude_enabled());

    EXPECT(host("paypal.com"));                 /* exact */
    EXPECT(host("sanalpos.garanti.com.tr"));    /* subdomain */
    EXPECT(host("a.b.c.d.paypal.com"));         /* deep subdomain */
    EXPECT(host("js.stripe.com"));              /* listed itself and via stripe.com */
    EXPECT(host("x.pay.google.com"));           /* subdomain of a listed subdomain */
    EXPECT(host("pay.google.com"));
    EXPECT(!host("www.google.com"));            /* parent of a listed name is not listed */
    EXPECT(!host("google.com"));
    EXPECT(!host("evilpay.google.com"));
    EXPECT(!host("notpaypal.com"));             /* no dot boundary */
    EXPECT(!host("xpaypal.com"));
    EXPECT(!host("paypal.com.evil.net"));       /* listed name inside, not at the end */
    EXPECT(!host("paypal.co"));
    EXPECT(!host("paypal"));
    EXPECT(!host("com"));
    EXPECT(!host("discord.com"));
    EXPECT(!host("garanti.com.tr.evil.com"));
    EXPECT(!host("fakeisbank.com.tr"));

    /* case */
    EXPECT(host("PAYPAL.COM"));
    EXPECT(host("WwW.PayPal.Com"));
    EXPECT(host("ISBANK.com.tr"));
    /* trailing dot / blanks (current behaviour: stripped) */
    EXPECT(host("paypal.com."));
    EXPECT(host("WWW.PAYPAL.COM."));
    EXPECT(host("paypal.com "));
    EXPECT(host("paypal.com\t"));
    EXPECT(!host("notpaypal.com."));
    /* HTTP Host header with port */
    EXPECT(host("www.iyzico.com:443"));
    EXPECT(host("paypal.com:8080"));
    EXPECT(!host("notpaypal.com:443"));
    /* a leading dot is skipped by the subdomain walk (current behaviour) */
    EXPECT(host(".paypal.com"));

    /* empty and degenerate */
    EXPECT(!exclude_check_hostname("paypal.com", 0));
    EXPECT(!exclude_check_hostname(NULL, 10));
    EXPECT(!host(""));
    EXPECT(!host("."));
    EXPECT(!host(" "));
    EXPECT(!host(":"));
    EXPECT(!host(".."));

    /* explicit length, buffer not NUL terminated / longer than the length */
    EXPECT(host_n("paypal.comEVIL", 10) == 1);
    EXPECT(host_n("paypal.comEVIL", 14) == 0);
    EXPECT(host_n("evil.netpaypal.com", 8) == 0);
    EXPECT(host_n("evil.netpaypal.com", 18) == 0);
    EXPECT(host_n("www.paypal.com", 14) == 1);
    EXPECT(host_n("www.paypal.com", 13) == 0);   /* "www.paypal.co" */
    EXPECT(host_n("paypal.com", 9) == 0);

    /* length limit 253: a 254-byte name that ends exactly in ".paypal.com" is not matched
     * (the matcher refuses anything longer than a legal DNS name), the 253-byte one is */
    memset(big, 'a', sizeof big);
    EXPECT(!host_n(big, 254));
    EXPECT(!host_n(big, 399));
    memset(big, 'a', sizeof big);
    memcpy(big + 254 - 11, ".paypal.com", 11);
    EXPECT(host_n(big, 254) == 0);
    memset(big, 'a', sizeof big);
    memcpy(big + 253 - 11, ".paypal.com", 11);
    EXPECT(host_n(big, 253) == 1);
    EXPECT(host_n(big + 1, 253) == 0);            /* "aaa.paypa" cut: no suffix match */
    EXPECT(host_n(big + 1, 252) == 1);            /* 252-byte name ending in .paypal.com */
}

/* ---------- 2. learning from DNS answers ---------- */
static void test_learn(void) {
    msg_t m, t;
    unsigned char ip[16], ip6a[16], ip6b[16];
    unsigned len, full, end1;
    int i;
    grp = G_LEARN;

    /* a listed name: its A address is excluded afterwards */
    EXPECT(!has4(198, 51, 100, 1));
    learn_a("www.paypal.com", 198, 51, 100, 1);
    EXPECT(has4(198, 51, 100, 1));
    EXPECT(!has4(198, 51, 100, 2));
    /* mixed case question */
    learn_a("WWW.PayPal.COM", 198, 51, 100, 2);
    EXPECT(has4(198, 51, 100, 2));
    /* a name only ending like a listed one, and an unlisted one: nothing learned */
    learn_a("notpaypal.com", 198, 51, 100, 3);
    learn_a("example.org", 198, 51, 100, 4);
    learn_a("paypal.com.evil.net", 198, 51, 100, 5);
    EXPECT(!has4(198, 51, 100, 3));
    EXPECT(!has4(198, 51, 100, 4));
    EXPECT(!has4(198, 51, 100, 5));

    /* AAAA */
    memset(ip6a, 0, 16); ip6a[0] = 0x20; ip6a[1] = 0x01; ip6a[2] = 0x0d; ip6a[3] = 0xb8; ip6a[15] = 1;
    memcpy(ip6b, ip6a, 16); ip6b[15] = 2;
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com"); rr(&m, 28, ip6a, 16);
    learn(&m);
    EXPECT(has6(ip6a));
    EXPECT(!has6(ip6b));
    hdr(&m, RESP, 1, 1); question(&m, "example.org"); rr(&m, 28, ip6b, 16);
    learn(&m);
    EXPECT(!has6(ip6b));
    /* the v4 and the v6 table are separate: the first 4 bytes of an IPv6 address are no IPv4 hit */
    EXPECT(!has4(0x20, 0x01, 0x0d, 0xb8));

    /* several A records in one answer, plus an AAAA */
    hdr(&m, RESP, 1, 4); question(&m, "api.stripe.com");
    ip[0] = 198; ip[1] = 51; ip[2] = 100; ip[3] = 10; rr(&m, 1, ip, 4);
    ip[3] = 11; rr(&m, 1, ip, 4);
    ip[3] = 12; rr(&m, 1, ip, 4);
    memcpy(ip6b, ip6a, 16); ip6b[14] = 9; rr(&m, 28, ip6b, 16);
    learn(&m);
    EXPECT(has4(198, 51, 100, 10) && has4(198, 51, 100, 11) && has4(198, 51, 100, 12));
    EXPECT(!has4(198, 51, 100, 13));
    EXPECT(has6(ip6b));

    /* record types other than A/AAAA are ignored, wrong rdlength is ignored */
    hdr(&m, RESP, 1, 3); question(&m, "www.paypal.com");
    ip[0] = 198; ip[1] = 51; ip[2] = 100; ip[3] = 20; rr(&m, 16 /* TXT */, ip, 4);
    { unsigned char five[5] = {198, 51, 100, 21, 0}; rr(&m, 1, five, 5); }
    { unsigned char four[4] = {198, 51, 100, 22}; rr(&m, 28, four, 4); }
    learn(&m);
    EXPECT(!has4(198, 51, 100, 20) && !has4(198, 51, 100, 21) && !has4(198, 51, 100, 22));

    /* CNAME chain: listed name -> CNAME -> A. The code learns every A/AAAA record of
     * the answer section whatever its owner name is, so the CDN address is learned. */
    hdr(&m, RESP, 1, 2); question(&m, "www.paypal.com");
    t.n = 0; name(&t, "edge.cdn-example.net");
    rr(&m, 5, t.b, t.n);
    { unsigned char own[] = {4, 'e', 'd', 'g', 'e', 0xC0, 0x00}; /* owner: label + pointer to header */
      ip[0] = 198; ip[1] = 51; ip[2] = 100; ip[3] = 30;
      rr_owner(&m, own, sizeof own, 1, ip, 4); }
    learn(&m);
    EXPECT(has4(198, 51, 100, 30));
    /* an UNLISTED name whose CNAME points at a listed one is not learned (the question
     * name decides, CNAME targets are not followed) */
    hdr(&m, RESP, 1, 2); question(&m, "pay.shop-example.org");
    t.n = 0; name(&t, "www.paypal.com");
    rr(&m, 5, t.b, t.n);
    ip[3] = 31; rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(!has4(198, 51, 100, 31));

    /* compressed owner names that point somewhere odd. skip_name never follows a
     * pointer, so a loop or a pointer past the end cannot hang or read out of bounds;
     * the record is structurally fine and its address is learned (owner is not checked). */
    ip[3] = 40;
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com");
    { unsigned char own[2]; own[0] = 0xC0; own[1] = (unsigned char)m.n;       /* points at itself */
      rr_owner(&m, own, 2, 1, ip, 4); }
    learn(&m);
    EXPECT(has4(198, 51, 100, 40));
    ip[3] = 41;
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com");
    { unsigned char own[2] = {0xC0, 0xFF}; rr_owner(&m, own, 2, 1, ip, 4); }    /* past the end */
    learn(&m);
    EXPECT(has4(198, 51, 100, 41));
    ip[3] = 42;
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com");
    { unsigned char own[2] = {0xFF, 0xFF}; rr_owner(&m, own, 2, 1, ip, 4); }
    learn(&m);
    EXPECT(has4(198, 51, 100, 42));
    /* two pointers pointing at each other */
    ip[3] = 43;
    hdr(&m, RESP, 1, 2); question(&m, "www.paypal.com");
    { unsigned char own[2];
      own[0] = 0xC0; own[1] = (unsigned char)(m.n + 16); rr_owner(&m, own, 2, 1, ip, 4);
      own[0] = 0xC0; own[1] = (unsigned char)(m.n - 16); ip[3] = 44; rr_owner(&m, own, 2, 1, ip, 4); }
    learn(&m);
    EXPECT(has4(198, 51, 100, 43) && has4(198, 51, 100, 44));
    /* pointer cut in half by the end of the message */
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com"); p8(&m, 0xC0);
    learn(&m);
    /* label that runs past the end */
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com"); p8(&m, 50); p8(&m, 'a'); p8(&m, 'b');
    learn(&m);
    /* reserved label types (0x40, 0x80) are rejected: record not learned */
    ip[3] = 45;
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com");
    { unsigned char own[] = {0x80, 0x00}; rr_owner(&m, own, 2, 1, ip, 4); }
    learn(&m);
    EXPECT(!has4(198, 51, 100, 45));
    ip[3] = 46;
    hdr(&m, RESP, 1, 1); question(&m, "www.paypal.com");
    { unsigned char own[] = {0x40, 0x00}; rr_owner(&m, own, 2, 1, ip, 4); }
    learn(&m);
    EXPECT(!has4(198, 51, 100, 46));
    /* compression in the question name is not allowed: nothing learned */
    ip[3] = 47;
    hdr(&m, RESP, 1, 1); p8(&m, 0xC0); p8(&m, 0x00); p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(!has4(198, 51, 100, 47));

    /* header fields: QDCOUNT != 1, ANCOUNT 0, rcode, response bit */
    ip[3] = 50; hdr(&m, RESP, 0, 1); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
    EXPECT(!has4(198, 51, 100, 50));
    ip[3] = 51; hdr(&m, RESP, 2, 1); question(&m, "www.paypal.com"); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
    EXPECT(!has4(198, 51, 100, 51));
    ip[3] = 52; hdr(&m, RESP, 1, 0); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
    EXPECT(!has4(198, 51, 100, 52));
    for (i = 1; i <= 15; i++) {
        ip[3] = (unsigned char)(60 + i);
        hdr(&m, RESP | (unsigned)i, 1, 1); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
        EXPECT(!has4(198, 51, 100, 60 + i));
    }
    ip[3] = 80; hdr(&m, RESP & ~0x8000u, 1, 1); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
    EXPECT(!has4(198, 51, 100, 80));                  /* a query, not a response */
    ip[3] = 81; hdr(&m, 0x8580, 1, 1); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
    EXPECT(has4(198, 51, 100, 81));                   /* AA/RD flags do not matter */
    /* ANCOUNT larger than the records present: the complete first record counts, no overread */
    ip[3] = 82; hdr(&m, RESP, 1, 7); question(&m, "www.paypal.com"); rr(&m, 1, ip, 4); learn(&m);
    EXPECT(has4(198, 51, 100, 82));
    /* too short / empty */
    learn_n(m.b, 0);
    learn_n(m.b, 11);
    exclude_learn_dns(NULL, 0);

    /* question name longer than 253 bytes: ignored, no overflow */
    ip[3] = 83;
    hdr(&m, RESP, 1, 1);
    for (i = 0; i < 5; i++) { p8(&m, 60); memset(m.b + m.n, 'a', 60); m.n += 60; }
    name(&m, "paypal.com"); p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(!has4(198, 51, 100, 83));
    /* a 64-byte label followed by paypal.com is not a legal label: nothing learned;
     * a 63-byte label (the maximum) is a subdomain of paypal.com: learned */
    ip[3] = 84;
    hdr(&m, RESP, 1, 1); p8(&m, 64); memset(m.b + m.n, 'a', 64); m.n += 64; name(&m, "paypal.com");
    p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(!has4(198, 51, 100, 84));
    ip[3] = 85;
    hdr(&m, RESP, 1, 1); p8(&m, 63); memset(m.b + m.n, 'a', 63); m.n += 63; name(&m, "paypal.com");
    p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(has4(198, 51, 100, 85));
    /* labels that contain '.', ':' or NUL are not rejected: the name handed to the matcher
     * is the raw bytes. known: accepted, see review note */
    ip[3] = 90;   /* label "x.paypal" + "com" -> "x.paypal.com" */
    hdr(&m, RESP, 1, 1); p8(&m, 8); memcpy(m.b + m.n, "x.paypal", 8); m.n += 8; p8(&m, 3);
    memcpy(m.b + m.n, "com", 3); m.n += 3; p8(&m, 0); p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(has4(198, 51, 100, 90));               /* known: accepted, see review note */
    ip[3] = 91;   /* one label "paypal.com:x" -> the port cut leaves "paypal.com" */
    hdr(&m, RESP, 1, 1); p8(&m, 12); memcpy(m.b + m.n, "paypal.com:x", 12); m.n += 12; p8(&m, 0);
    p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(has4(198, 51, 100, 91));               /* known: accepted, see review note */
    ip[3] = 92;   /* one label "paypal.com\0z": the string ends at the NUL */
    hdr(&m, RESP, 1, 1); p8(&m, 12); memcpy(m.b + m.n, "paypal.com\0z", 12); m.n += 12; p8(&m, 0);
    p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(has4(198, 51, 100, 92));               /* known: accepted, see review note */

    /* every truncation of a two-record answer: an address is learned only when its whole
     * record is present. The copy handed to the code is exactly len bytes (ASan). */
    for (len = 0;; len++) {
        unsigned char a1[4], a2[4];
        a1[0] = 10; a1[1] = 1; a1[2] = (unsigned char)len; a1[3] = 1;
        a2[0] = 10; a2[1] = 2; a2[2] = (unsigned char)len; a2[3] = 2;
        hdr(&m, RESP, 1, 2); question(&m, "www.paypal.com");
        rr(&m, 1, a1, 4);
        end1 = m.n;
        rr(&m, 1, a2, 4);
        full = m.n;
        if (len > full) break;
        learn_n(m.b, len);
        EXPECT(has4(10, 1, (int)len, 1) == (len >= end1));
        EXPECT(has4(10, 2, (int)len, 2) == (len >= full));
    }
    /* same with AAAA records */
    for (len = 0;; len++) {
        unsigned char a1[16], a2[16];
        memset(a1, 0, 16); memset(a2, 0, 16);
        a1[0] = 0x20; a1[1] = 0x01; a1[2] = 0x0d; a1[3] = 0xb8; a1[4] = 1; a1[5] = (unsigned char)len; a1[15] = 1;
        memcpy(a2, a1, 16); a2[4] = 2; a2[15] = 2;
        hdr(&m, RESP, 1, 2); question(&m, "checkout.com");
        rr(&m, 28, a1, 16);
        end1 = m.n;
        rr(&m, 28, a2, 16);
        full = m.n;
        if (len > full) break;
        learn_n(m.b, len);
        EXPECT(has6(a1) == (len >= end1));
        EXPECT(has6(a2) == (len >= full));
    }
    /* the same bytes with every single byte flipped to 0xFF / 0x00 / 0xC0: only needs to not crash */
    hdr(&m, RESP, 1, 2); question(&m, "www.paypal.com");
    ip[0] = 10; ip[1] = 9; ip[2] = 9; ip[3] = 9; rr(&m, 1, ip, 4); rr(&m, 28, ip6a, 16);
    for (len = 0; len < m.n; len++) {
        static const unsigned char vals[3] = {0xFF, 0x00, 0xC0};
        int v;
        for (v = 0; v < 3; v++) {
            msg_t c = m;
            c.b[len] = vals[v];
            learn(&c);
            cases[G_LEARN]++;   /* counts as a case: survived */
        }
    }
}


/* ---------- 3. --exclude file ---------- */

static int load_bytes(const void *data, size_t n) {
    int r;
    write_file(data, n);
    r = exclude_load_list(LISTFILE);
    track(r);
    return r;
}

/* Fills line[0..total) with blanks and puts name at the very end. */
static void padded(char *line, size_t total, const char *nm) {
    size_t l = strlen(nm);
    memset(line, ' ', total);
    memcpy(line + total - l, nm, l);
}

static void test_list(void) {
    static char big[6000];
    int r, i, added, before;
    size_t n;
    grp = G_LIST;

    EXPECT(exclude_load_list("exclude_test_no_such_file.tmp") == -1);
    write_file("", 0);
    EXPECT(exclude_load_list(LISTFILE) == 0);

    EXPECT(load_str("a-one.test\nb-two.test\n") == 2);                       /* LF */
    EXPECT(host("a-one.test") && host("www.b-two.test"));
    EXPECT(load_str("c-three.test\r\nd-four.test\r\n") == 2);                /* CRLF */
    EXPECT(host("c-three.test") && host("d-four.test"));
    EXPECT(!host("c-three.test\r"));                                         /* nothing weird stored */
    EXPECT(load_str("e-five.test\r\nf-six.test") == 2);                      /* no final newline */
    EXPECT(host("f-six.test"));
    EXPECT(load_str("crlf-a.test\r\n\r\n\r\ncrlf-b.test\r\n") == 2);         /* CRLF blank lines: no extra names */
    EXPECT(host("crlf-a.test") && host("crlf-b.test"));

    /* lone CR line ends (old Mac / some editors), and mixes */
    EXPECT(load_str("lc-one.test\rlc-two.test\rlc-three.test") == 3);
    EXPECT(host("lc-one.test") && host("lc-two.test") && host("lc-three.test"));
    EXPECT(load_str("lc-four.test\r\rlc-five.test\n\r\nlc-six.test\r\n") == 3);
    EXPECT(host("lc-four.test") && host("lc-five.test") && host("lc-six.test"));

    /* a 0x1A (Ctrl-Z) byte must not end the file (binary mode) */
    r = load_bytes("ctl-one.test\n" "\x1a" "\n" "ctl-two.test\n" "\x1a" "\n" "ctl-three.test\n" "\x1a",
                   13 + 2 + 13 + 2 + 15 + 1);
    EXPECT(r == 3);
    EXPECT(host("ctl-one.test") && host("ctl-two.test") && host("ctl-three.test"));

    /* comments, blank lines, blanks around names, leading/trailing dots */
    r = load_str("# real-comment.test\n\n   \n\t \n  # indented-name.test\n"
                 "   g-seven.test   \t\n"
                 " .h-eight.test.  \n"
                 "i-nine.test # trailing comment j-ten.test\n"
                 "#k-eleven.test\n");
    EXPECT(r == 3);
    EXPECT(host("g-seven.test") && host("h-eight.test") && host("i-nine.test"));
    EXPECT(!host("j-ten.test") && !host("k-eleven.test"));
    EXPECT(!host("indented-name.test") && !host("real-comment.test"));
    EXPECT(!host("comment.test") && !host("trailing"));
    /* duplicates (also of built-in names) and one-character lines are not added */
    EXPECT(load_str("paypal.com\nA-ONE.test\nx\n\n") == 0);
    EXPECT(load_str("paypal.com\nl-twelve.test\n") == 1);
    EXPECT(load_str("\r\n# c\r\nm-thirteen.test\r\n\r\n") == 1);
    EXPECT(host("m-thirteen.test"));

    /* trailing blanks and dots do not count towards the 253-character limit */
    memset(big, ' ', 600);
    big[600] = '\n';
    big[601] = 0;
    memcpy(big, "pay-example.test", 16);
    EXPECT(load_str(big) == 1);
    EXPECT(host("pay-example.test"));
    memset(big, '\t', 300); big[300] = 0;
    memcpy(big, "pay-example2.test", 17);
    r = exclude_add(big); track(r);
    EXPECT(r == 1);
    EXPECT(host("pay-example2.test"));
    memset(big, 'q', 253); memset(big + 253, ' ', 200); big[453] = 0;
    r = exclude_add(big); track(r);
    EXPECT(r == 1);                                                          /* 253 + blanks: fine */
    EXPECT(host_n(big, 253));
    memset(big, 'r', 254); memset(big + 254, ' ', 200); big[454] = 0;
    EXPECT(exclude_add(big) == 0);                                           /* 254 + blanks: too long */
    memset(big, 's', 253); big[253] = '.'; big[254] = '.'; big[255] = 0;
    r = exclude_add(big); track(r);
    EXPECT(r == 1);                                                          /* 253 + trailing dots: fine */

    /* UTF-8 byte order mark (Notepad) must not swallow the first name */
    r = load_bytes("\xEF\xBB\xBF" "bom-first.test\r\nbom-second.test\r\n", 3 + 14 + 2 + 15 + 2);
    EXPECT(r == 2);
    EXPECT(host("bom-first.test") && host("bom-second.test"));
    EXPECT(load_bytes("\xEF\xBB\xBF", 3) == 0);                              /* BOM only */
    r = load_bytes("\xEF\xBB\xBF" "bom-cr.test\rbom-cr2.test", 3 + 11 + 1 + 12);
    EXPECT(r == 2);
    EXPECT(host("bom-cr.test") && host("bom-cr2.test"));

    /* line length: up to 1023 bytes are a valid line; longer lines are ignored as a whole
     * (head and tail), and the next line is read normally */
    padded(big, 1022, "e1022.test"); big[1022] = '\n';
    r = load_bytes(big, 1023);
    EXPECT(r == 1 && host("e1022.test"));
    padded(big, 1023, "e1023.test"); big[1023] = '\n';
    r = load_bytes(big, 1024);
    EXPECT(r == 1 && host("e1023.test"));
    padded(big, 1023, "e1023eof.test");                                      /* at EOF, no newline */
    r = load_bytes(big, 1023);
    EXPECT(r == 1 && host("e1023eof.test"));
    padded(big, 1024, "e1024.test"); big[1024] = '\n';
    memcpy(big + 1025, "after-1024.test\n", 16);
    r = load_bytes(big, 1025 + 16);
    EXPECT(r == 1);
    EXPECT(!host("e1024.test") && host("after-1024.test"));
    padded(big, 1024, "e1024eof.test");                                      /* at EOF */
    EXPECT(load_bytes(big, 1024) == 0 && !host("e1024eof.test"));
    memcpy(big, "long-head.test", 14);                                       /* name, blanks to 1023, then a tail */
    memset(big + 14, ' ', 1009);
    memcpy(big + 1023, "long-tail.test\r\nafter-long.test\r\n", 33);
    r = load_bytes(big, 1023 + 33);
    EXPECT(r == 1);
    EXPECT(!host("long-head.test") && !host("long-tail.test") && host("after-long.test"));
    n = 0;
    big[n++] = '#'; big[n++] = ' ';
    while (n < 1023) big[n++] = 'x';
    memcpy(big + n, "tail-of-comment.test\nnext-line.test\n", 36); n += 36;
    r = load_bytes(big, n);
    EXPECT(r == 1);
    EXPECT(!host("tail-of-comment.test") && host("next-line.test"));
    for (n = 0; n < 4999; n++) big[n] = (char)('a' + n % 20);
    big[4999] = '\n';
    EXPECT(load_bytes(big, 5000) == 0);
    memset(big, 'c', 3000);                                                  /* long, no newline at all */
    EXPECT(load_bytes(big, 3000) == 0);
    memset(big, 'z', 1100);                                                  /* lone CR after a long line */
    memcpy(big + 1100, "\rcr-after-long.test\r", 20);
    EXPECT(load_bytes(big, 1120) == 1 && host("cr-after-long.test"));

    /* NUL bytes: such a line is invalid as a whole (also when long, so the tail of a long
     * line is never parsed as a line of its own) */
    {
        static const char d[] = "nul-a.test\0junk\nnul-b.test\n";
        r = load_bytes(d, sizeof d - 1);
        EXPECT(r == 1);
        EXPECT(!host("nul-a.test") && host("nul-b.test"));
    }
    memset(big, 'a', 600);
    big[600] = 0;
    memcpy(big + 601, "tail-nul.test\nafter-nul.test\n", 29);
    r = load_bytes(big, 601 + 29);
    EXPECT(r == 1);
    EXPECT(!host("tail-nul.test") && host("after-nul.test"));
    memset(big, 'a', 1200);
    big[300] = 0;
    memcpy(big + 1200, "\ntail-nul2.test\nafter-nul2.test\n", 32);
    r = load_bytes(big, 1200 + 32);
    EXPECT(r == 2);                                      /* the long NUL line is dropped, the two lines after it load */
    EXPECT(host("tail-nul2.test") && host("after-nul2.test"));

    /* name set capacity: 512 names; further names are refused (silently, the call still
     * succeeds); nothing already stored is lost */
    before = known;
    EXPECT(before > 0 && before < 512);
    {
        char *names = (char *)malloc(700 * 16 + 1);
        n = 0;
        for (i = 0; i < 700; i++) n += (size_t)sprintf(names + n, "cap%03d.test\n", i);
        write_file(names, n);
        free(names);
    }
    added = exclude_load_list(LISTFILE);
    EXPECT(added == 512 - before);
    track(added);
    EXPECT(known == 512);
    for (i = 0; i < 700; i++) {
        char nm[32];
        sprintf(nm, "cap%03d.test", i);
        EXPECT(host(nm) == (i < added));              /* first come, first served */
    }
    EXPECT(exclude_add("one-more.test") == 0);
    EXPECT(!host("one-more.test"));
    EXPECT(exclude_add("paypal.com") == 0);
    EXPECT(host("paypal.com") && host("a-one.test") && host("bom-first.test"));   /* still all there */
    EXPECT(!host("absent.example"));                  /* lookup of a missing name still ends */
    EXPECT(!host("www.absent.example"));
    EXPECT(exclude_load_list(LISTFILE) == 0);         /* loading again adds nothing */
    EXPECT(exclude_enabled());
    /* learning still works with a full name set */
    learn_a("cap000.test", 192, 0, 2, 77);
    EXPECT(has4(192, 0, 2, 77));
}

/* ---------- 4. learned-address tables ---------- */
static void ip6_of(unsigned char *a6, int i) {
    memset(a6, 0, 16);
    a6[0] = 0x20; a6[1] = 0x01; a6[2] = 0x0d; a6[3] = 0xb8; a6[4] = 0x77;
    a6[14] = (unsigned char)(i >> 8); a6[15] = (unsigned char)i;
}
static void learn6(const unsigned char *a6) {
    msg_t m;
    hdr(&m, RESP, 1, 1); question(&m, "stripe.com"); rr(&m, 28, a6, 16);
    learn(&m);
}
/* one tick of the fake clock per learned address: strict order of expiry */
static void learn_a_t(int a, int b, int c, int d) { g_now++; learn_a("www.paypal.com", a, b, c, d); }
static void learn6_t(int i) { unsigned char a6[16]; g_now++; ip6_of(a6, i); learn6(a6); }
static int has6_i(int i) { unsigned char a6[16]; ip6_of(a6, i); return has6(a6); }

static void fresh(void) {
    exclude_reset_for_test();
    exclude_load_builtin();
    g_now = 10000000ull;
}

static void test_capacity(void) {
    int i, k, live;
    msg_t m;
    unsigned char ip[4], a6[16];
    grp = G_CAP;

    /* IPv4: 512 entries. A full table replaces the entry that expires soonest (the one
     * learned or refreshed longest ago), so a refreshed entry outlives older ones. */
    fresh();
    for (i = 0; i < 512; i++) learn_a_t(172, 16, i >> 8, i & 255);
    for (i = 0; i < 512; i++) EXPECT(has4(172, 16, i >> 8, i & 255));
    learn_a_t(172, 16, 0, 0);                                  /* refresh the oldest */
    learn_a_t(172, 17, 0, 0);                                  /* new: pushes out entry 1, not 0 */
    EXPECT(has4(172, 16, 0, 0));
    EXPECT(!has4(172, 16, 0, 1));
    EXPECT(has4(172, 16, 0, 2));
    EXPECT(has4(172, 17, 0, 0));
    for (i = 1; i < 100; i++) learn_a_t(172, 18, 0, i);        /* 99 more: entries 2..100 go */
    EXPECT(has4(172, 16, 0, 0));
    for (i = 1; i <= 100; i++) EXPECT(!has4(172, 16, i >> 8, i & 255));
    for (i = 101; i < 512; i++) EXPECT(has4(172, 16, i >> 8, i & 255));
    /* refreshing never uses a slot */
    for (i = 0; i < 1000; i++) learn_a_t(172, 18, 0, 99);
    for (i = 101; i < 512; i++) EXPECT(has4(172, 16, i >> 8, i & 255));
    /* 600 distinct addresses in a row: exactly the last 512 are left */
    fresh();
    for (i = 0; i < 600; i++) learn_a_t(172, 16, i >> 8, i & 255);
    for (i = 0; i < 600; i++) EXPECT(has4(172, 16, i >> 8, i & 255) == (i >= 88));

    /* IPv6: 256 entries, same rule */
    fresh();
    for (i = 0; i < 256; i++) learn6_t(i);
    learn6_t(0);                                               /* refresh */
    learn6_t(1000);                                            /* new: entry 1 is replaced */
    EXPECT(has6_i(0) && !has6_i(1) && has6_i(2) && has6_i(1000));
    for (i = 0; i < 300; i++) learn6_t(2000 + i);
    for (i = 0; i < 300; i++) EXPECT(has6_i(2000 + i) == (i >= 44));
    EXPECT(!has6_i(0) && !has6_i(2));

    /* at most 16 addresses are learned from one answer, the rest is ignored */
    fresh();
    hdr(&m, RESP, 1, 100); question(&m, "www.paypal.com");
    for (i = 0; i < 100; i++) { ip[0] = 10; ip[1] = 20; ip[2] = 0; ip[3] = (unsigned char)i; rr(&m, 1, ip, 4); }
    learn(&m);
    for (i = 0; i < 100; i++) EXPECT(has4(10, 20, 0, i) == (i < 16));
    hdr(&m, RESP, 1, 100); question(&m, "www.paypal.com");
    for (i = 0; i < 100; i++) { ip6_of(a6, 3000 + i); rr(&m, 28, a6, 16); }
    learn(&m);
    for (i = 0; i < 100; i++) EXPECT(has6_i(3000 + i) == (i < 16));
    /* the limit counts A and AAAA together; other record types do not count */
    hdr(&m, RESP, 1, 33); question(&m, "www.paypal.com");
    for (i = 0; i < 5; i++) { ip[0] = 10; ip[1] = 21; ip[2] = 0; ip[3] = (unsigned char)i; rr(&m, 16, ip, 4); }
    for (i = 0; i < 8; i++) { ip[0] = 10; ip[1] = 22; ip[2] = 0; ip[3] = (unsigned char)i; rr(&m, 1, ip, 4); }
    for (i = 0; i < 20; i++) { ip6_of(a6, 4000 + i); rr(&m, 28, a6, 16); }
    learn(&m);
    for (i = 0; i < 5; i++) EXPECT(!has4(10, 21, 0, i));
    for (i = 0; i < 8; i++) EXPECT(has4(10, 22, 0, i));
    for (i = 0; i < 20; i++) EXPECT(has6_i(4000 + i) == (i < 8));
    /* a few oversized answers cannot flush the genuine addresses */
    fresh();
    for (i = 0; i < 512; i++) learn_a_t(172, 16, i >> 8, i & 255);
    for (k = 0; k < 3; k++) {
        g_now++;
        hdr(&m, RESP, 1, 100); question(&m, "www.paypal.com");
        for (i = 0; i < 100; i++) { ip[0] = 10; ip[1] = (unsigned char)(30 + k); ip[2] = 0; ip[3] = (unsigned char)i; rr(&m, 1, ip, 4); }
        learn(&m);
    }
    live = 0;
    for (i = 0; i < 512; i++) live += has4(172, 16, i >> 8, i & 255);
    EXPECT(live == 512 - 48);
}

static void test_expiry(void) {
    unsigned long long t0 = 20000000ull;
    unsigned char a6[16];
    int i, live;
    grp = G_EXP;

    fresh();
    g_now = t0;
    learn_a("www.paypal.com", 198, 18, 0, 1);
    g_now = t0 + 1;
    EXPECT(has4(198, 18, 0, 1));
    g_now = t0 + TTL - 1;
    EXPECT(has4(198, 18, 0, 1));                               /* just before the TTL */
    g_now = t0 + TTL;
    EXPECT(!has4(198, 18, 0, 1));                              /* at the TTL: gone */
    g_now = t0 + TTL + 5000;
    EXPECT(!has4(198, 18, 0, 1));

    /* re-learned: fresh TTL from the moment of the new answer */
    g_now = t0;
    learn_a("www.paypal.com", 198, 18, 0, 2);
    g_now = t0 + TTL - 10;
    learn_a("www.paypal.com", 198, 18, 0, 2);
    g_now = t0 + TTL + 5;
    EXPECT(has4(198, 18, 0, 2));                               /* would have expired without the refresh */
    g_now = t0 + 2 * TTL - 11;
    EXPECT(has4(198, 18, 0, 2));
    g_now = t0 + 2 * TTL - 10;
    EXPECT(!has4(198, 18, 0, 2));
    /* an already expired address is learned again */
    g_now = t0 + 3 * TTL;
    learn_a("www.paypal.com", 198, 18, 0, 2);
    EXPECT(has4(198, 18, 0, 2));

    /* IPv6 */
    g_now = t0;
    ip6_of(a6, 7); learn6(a6);
    g_now = t0 + TTL - 1;
    EXPECT(has6(a6));
    g_now = t0 + TTL;
    EXPECT(!has6(a6));

    /* an expired entry is reused before any live one, wherever it sits in the table:
     * entry E (learned at an early time) is stored after 100 later-expiring ones */
    fresh();
    g_now = t0 + TTL - 600;
    for (i = 0; i < 100; i++) { g_now++; learn_a("www.paypal.com", 172, 16, 0, i); }
    g_now = t0;
    learn_a("www.paypal.com", 172, 31, 0, 1);                  /* E, expires at t0 + TTL */
    g_now = t0 + TTL - 500;
    for (i = 100; i < 511; i++) { g_now++; learn_a("www.paypal.com", 172, 16, i >> 8, i & 255); }
    g_now = t0 + TTL - 1;
    EXPECT(has4(172, 31, 0, 1));                               /* table is full, E still live */
    g_now = t0 + TTL + 1;                                      /* E expired, the others are not */
    EXPECT(!has4(172, 31, 0, 1));
    learn_a("www.paypal.com", 172, 32, 0, 1);                  /* new address takes E's slot */
    EXPECT(has4(172, 32, 0, 1));
    live = 0;
    for (i = 0; i < 511; i++) live += has4(172, 16, i >> 8, i & 255);
    EXPECT(live == 511);
    /* no expired entry left: the one that expires soonest (the oldest) goes */
    g_now = t0 + TTL + 2;
    learn_a("www.paypal.com", 172, 32, 0, 2);
    EXPECT(has4(172, 32, 0, 2) && !has4(172, 16, 0, 0));
    EXPECT(has4(172, 16, 0, 1) && has4(172, 32, 0, 1));
}

/* ---------- 5. built-in names win whatever the load order ---------- */
static void test_builtin_first(void) {
    char *names;
    int i, user_added, builtin_n, builtin_n2;
    size_t n;
    grp = G_LIST;

    exclude_reset_for_test();
    builtin_n = exclude_load_builtin();
    exclude_reset_for_test();
    EXPECT(!exclude_enabled());                                 /* reset really empties */

    /* a user list that is larger than the whole set, loaded BEFORE the built-in names */
    names = (char *)malloc(700 * 16 + 1);
    n = 0;
    for (i = 0; i < 700; i++) n += (size_t)sprintf(names + n, "usr%03d.test\n", i);
    write_file(names, n);
    free(names);
    user_added = exclude_load_list(LISTFILE);
    EXPECT(user_added > 0 && user_added <= 512 - builtin_n);   /* room is kept for the built-in list */
    builtin_n2 = exclude_load_builtin();
    EXPECT(builtin_n2 == builtin_n);                           /* none of them displaced */
    EXPECT(host("paypal.com") && host("www.garanti.com.tr") && host("isbank.com.tr"));
    EXPECT(host("btcturk.com") && host("paribu.com") && host("sanalpos.iyzico.com"));
    EXPECT(host("usr000.test") && !host("usr699.test"));
    /* after the built-in names are in, user names may use the rest of the 512 */
    EXPECT(exclude_add("late-user.test") == (user_added + builtin_n < 512));
}

int main(void) {
    sprintf(listfile_buf, "exclude_test_list_%d.tmp", (int)_getpid());
    remove(LISTFILE);
    exclude_set_clock_for_test(fake_clock);
    test_hostname();
    test_learn();
    test_list();
    test_capacity();
    test_expiry();
    test_builtin_first();
    remove(LISTFILE);
    printf("cases: hostname=%d learn=%d capacity=%d list=%d expiry=%d\n",
           cases[G_HOST], cases[G_LEARN], cases[G_CAP], cases[G_LIST], cases[G_EXP]);
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
