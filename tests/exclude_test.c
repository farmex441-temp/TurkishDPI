/* Checks the payment/bank "never touch" list: host name matcher, addresses
 * learned from DNS answers, the learned-address tables and the --exclude file
 * loader. Run under AddressSanitizer: every buffer handed to the code under
 * test is a heap copy of exactly the bytes meant, so a read past the end is
 * caught. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exclude.h"

static int fails;
static int grp;
static int cases[5];
#define G_HOST 0
#define G_LEARN 1
#define G_CAP 2
#define G_LIST 3

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

typedef struct { unsigned char b[2048]; unsigned n; } msg_t;
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

static const char *LISTFILE = "exclude_test_list.tmp";
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
    size_t i;
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

    /* length limit 253 */
    memset(big, 'a', sizeof big);
    EXPECT(!host_n(big, 254));
    EXPECT(!host_n(big, 255));
    EXPECT(!host_n(big, 399));
    for (i = 0; i < 253 - 11; i++) big[i] = 'a';
    memcpy(big + 253 - 11, ".paypal.com", 11);
    EXPECT(host_n(big, 253) == 1);               /* longest legal name is still matched */
    big[0] = 'b';
    EXPECT(host_n(big, 254) == 0);               /* current behaviour: rejected */
    EXPECT(host_n(big, 400) == 0);
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
    /* a question label length of 64+ */
    hdr(&m, RESP, 1, 1); p8(&m, 64); memset(m.b + m.n, 'a', 64); m.n += 64; p8(&m, 0); p16(&m, 1); p16(&m, 1); rr(&m, 1, ip, 4);
    learn(&m);
    EXPECT(!has4(198, 51, 100, 83));

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

/* ---------- 3. capacity of the learned-address tables ---------- */
static void test_capacity(void) {
    int i;
    unsigned char a6[16];
    grp = G_CAP;

    /* IPv4: the table holds 512 entries and is a ring: when it is full the next new
     * address replaces the entry that was written longest ago (insertion order). */
    for (i = 0; i < 600; i++) learn_a("www.paypal.com", 172, 16, i >> 8, i & 255);
    for (i = 0; i < 88; i++) EXPECT(!has4(172, 16, i >> 8, i & 255));       /* evicted */
    for (i = 88; i < 600; i++) EXPECT(has4(172, 16, i >> 8, i & 255));      /* kept */
    /* an address that is already known only refreshes its entry, it does not use a slot */
    for (i = 0; i < 1000; i++) learn_a("www.paypal.com", 172, 16, 2, 87);   /* i = 599 */
    EXPECT(has4(172, 16, 0, 88));
    EXPECT(has4(172, 16, 2, 87));
    /* ... but one more new address pushes out the oldest */
    learn_a("www.paypal.com", 172, 17, 0, 1);
    EXPECT(!has4(172, 16, 0, 88));
    EXPECT(has4(172, 17, 0, 1));
    EXPECT(has4(172, 16, 0, 89));

    /* IPv6: 256 entries */
    for (i = 0; i < 300; i++) {
        msg_t m;
        memset(a6, 0, 16); a6[0] = 0x20; a6[1] = 0x01; a6[2] = 0x0d; a6[3] = 0xb8; a6[4] = 0x77;
        a6[14] = (unsigned char)(i >> 8); a6[15] = (unsigned char)i;
        hdr(&m, RESP, 1, 1); question(&m, "stripe.com"); rr(&m, 28, a6, 16);
        learn(&m);
    }
    for (i = 0; i < 300; i++) {
        memset(a6, 0, 16); a6[0] = 0x20; a6[1] = 0x01; a6[2] = 0x0d; a6[3] = 0xb8; a6[4] = 0x77;
        a6[14] = (unsigned char)(i >> 8); a6[15] = (unsigned char)i;
        EXPECT(has6(a6) == (i >= 44));
    }
    /* expiry (2 hours, GetTickCount64) cannot be tested here: the clock is read
     * directly inside exclude.c. */
}

/* ---------- 4. --exclude file ---------- */
static void test_list(void) {
    char *big;
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

    /* comments, blank lines, blanks around names, leading/trailing dots */
    r = load_str("# a comment\n\n   \n\t \n  # indented comment.test\n"
                 "   g-seven.test   \t\n"
                 " .h-eight.test.  \n"
                 "i-nine.test # trailing comment j-ten.test\n"
                 "#k-eleven.test\n");
    EXPECT(r == 3);
    EXPECT(host("g-seven.test") && host("h-eight.test") && host("i-nine.test"));
    EXPECT(!host("j-ten.test") && !host("k-eleven.test") && !host("indented.test"));
    EXPECT(!host("# a comment") && !host("comment"));
    /* duplicates (also of built-in names) and one-character lines are not added */
    EXPECT(load_str("paypal.com\nA-ONE.test\nx\n\n") == 0);
    EXPECT(load_str("paypal.com\nl-twelve.test\n") == 1);
    /* CRLF file with a blank CRLF line and a comment */
    EXPECT(load_str("\r\n# c\r\nm-thirteen.test\r\n\r\n") == 1);
    EXPECT(host("m-thirteen.test"));

    /* UTF-8 byte order mark (Notepad) must not swallow the first name */
    write_file("\xEF\xBB\xBF" "bom-first.test\r\nbom-second.test\r\n", 3 + 14 + 2 + 15 + 2);
    r = exclude_load_list(LISTFILE); track(r);
    EXPECT(r == 2);
    EXPECT(host("bom-first.test") && host("bom-second.test"));
    write_file("\xEF\xBB\xBF", 3);                                           /* BOM only */
    EXPECT(exclude_load_list(LISTFILE) == 0);

    /* over-long lines: the 1023-byte window of fgets must not turn the rest of a
     * long comment (or of a long line) into names, and the next line must be read again */
    big = (char *)malloc(5000);
    n = 0;
    big[n++] = '#'; big[n++] = ' ';
    while (n < 1023) big[n++] = 'x';
    memcpy(big + n, "tail-of-comment.test\nnext-line.test\n", 36); n += 36;
    write_file(big, n);
    r = exclude_load_list(LISTFILE); track(r);
    EXPECT(r == 1);
    EXPECT(!host("tail-of-comment.test") && host("next-line.test"));

    n = 0;
    memcpy(big, "long-head.test", 14); n = 14;
    while (n < 1023) big[n++] = ' ';
    memcpy(big + n, "long-tail.test\r\nafter-long.test\r\n", 33); n += 33;
    write_file(big, n);
    r = exclude_load_list(LISTFILE); track(r);
    /* current behaviour: trailing blanks count towards the 253-character limit, so
     * the head is refused too; what matters is that the tail is dropped and the next
     * line is read normally */
    EXPECT(r == 1);
    EXPECT(!host("long-head.test") && !host("long-tail.test") && host("after-long.test"));

    /* a line of 5000 name characters is refused, a line of exactly 253/254 characters */
    for (n = 0; n < 4999; n++) big[n] = (char)('a' + n % 20);
    big[4999] = '\n';
    write_file(big, 5000);
    EXPECT(exclude_load_list(LISTFILE) == 0);
    memset(big, 'a', 253); big[253] = '\n'; big[254] = 0;
    r = load_str(big);
    EXPECT(r == 1);
    EXPECT(host_n(big, 253));
    memset(big, 'b', 254); big[254] = '\n'; big[255] = 0;
    r = load_str(big);
    EXPECT(r == 0);
    /* long line without any newline at all */
    memset(big, 'c', 3000);
    write_file(big, 3000);
    EXPECT(exclude_load_list(LISTFILE) == 0);
    /* exactly 1023 characters then newline / EOF */
    memset(big, ' ', 1022); big[1022] = 'z'; big[1023] = '\n';
    memcpy(big + 1024, "after-1023.test\n", 16);
    write_file(big, 1024 + 16);
    r = exclude_load_list(LISTFILE); track(r);
    EXPECT(r == 1);                                                          /* "z" is one char: refused */
    EXPECT(host("after-1023.test"));
    free(big);

    /* capacity: the set holds EXCL_SLOTS/2 = 512 names; further names are refused (and
     * silently dropped, the call still succeeds); nothing already stored is lost */
    before = known;
    EXPECT(before > 0 && before < 512);
    big = (char *)malloc(700 * 16 + 1);
    n = 0;
    for (i = 0; i < 700; i++) n += (size_t)sprintf(big + n, "cap%03d.test\n", i);
    write_file(big, n);
    free(big);
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

int main(void) {
    test_hostname();
    test_learn();
    test_capacity();
    test_list();
    remove(LISTFILE);
    printf("cases: hostname=%d learn=%d capacity=%d list=%d\n",
           cases[G_HOST], cases[G_LEARN], cases[G_CAP], cases[G_LIST]);
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
