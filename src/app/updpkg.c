/*
 * TurkishDPI - update package reader (see updpkg.h).
 *
 * The inflater follows the structure of Mark Adler's reference decoder
 * "puff" (canonical Huffman decoding, the same validity rules for the code
 * tables), written for this program with explicit bounds checks instead of
 * longjmp.
 */
#include <stdlib.h>
#include <string.h>
#include "updpkg.h"

/* ---------- CRC-32 (zip, IEEE 802.3) ---------- */
uint32_t upd_crc32(const unsigned char *p, size_t n) {
    static uint32_t table[256];
    static int ready;
    uint32_t c = 0xFFFFFFFFu;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        ready = 1;
    }
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---------- inflate ---------- */
#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const unsigned char *in;
    size_t in_len, in_pos;
    uint32_t bitbuf;
    int bitcnt;
    unsigned char *out;
    size_t out_len, out_pos;
    int err;
} inf_t;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} huff_t;

static int getbits(inf_t *s, int need) {
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->in_pos >= s->in_len) { s->err = 1; return 0; }
        v |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}

static int stored(inf_t *s) {
    unsigned len;
    s->bitbuf = 0;               /* to the next byte boundary */
    s->bitcnt = 0;
    if (s->in_len - s->in_pos < 4) return 1;
    len = s->in[s->in_pos] | (unsigned)s->in[s->in_pos + 1] << 8;
    if (s->in[s->in_pos + 2] != (unsigned char)~len || s->in[s->in_pos + 3] != (unsigned char)(~len >> 8))
        return 1;
    s->in_pos += 4;
    if (s->in_len - s->in_pos < len || s->out_len - s->out_pos < len) return 1;
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

static int decode(inf_t *s, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        int count;
        code |= getbits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;   /* ran out of codes */
}

/* Builds a canonical code from lengths; returns 0 complete, >0 incomplete,
 * <0 over-subscribed. */
static int construct(huff_t *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    int left = 1;
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;          /* no codes: complete, but decoding fails */
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static int codes(inf_t *s, const huff_t *lencode, const huff_t *distcode) {
    static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                     35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                     257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                     8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    for (;;) {
        int symbol = decode(s, lencode);
        if (symbol < 0 || s->err) return 1;
        if (symbol < 256) {
            if (s->out_pos >= s->out_len) return 1;
            s->out[s->out_pos++] = (unsigned char)symbol;
        } else if (symbol == 256) {
            return 0;
        } else {
            size_t len, dist;
            symbol -= 257;
            if (symbol >= 29) return 1;
            len = (size_t)lbase[symbol] + (size_t)getbits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0 || symbol >= 30 || s->err) return 1;
            dist = (size_t)dbase[symbol] + (size_t)getbits(s, dext[symbol]);
            if (s->err || dist > s->out_pos || len > s->out_len - s->out_pos) return 1;
            for (size_t i = 0; i < len; i++, s->out_pos++) s->out[s->out_pos] = s->out[s->out_pos - dist];
        }
    }
}

static int fixed(inf_t *s) {
    static huff_t lencode, distcode;
    static int ready;
    if (!ready) {
        short lengths[FIXLCODES];
        int sym = 0;
        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&distcode, lengths, MAXDCODES);
        ready = 1;
    }
    return codes(s, &lencode, &distcode);
}

static int dynamic(inf_t *s) {
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXLCODES + MAXDCODES];
    huff_t lencode, distcode;
    int nlen, ndist, ncode, index, err;
    nlen = getbits(s, 5) + 257;
    ndist = getbits(s, 5) + 1;
    ncode = getbits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) return 1;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)getbits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return 1;
    if (construct(&lencode, lengths, 19) != 0) return 1;   /* must be complete */

    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode), len;
        if (symbol < 0 || s->err) return 1;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) return 1;
                len = lengths[index - 1];
                symbol = 3 + getbits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + getbits(s, 3);
            } else {
                symbol = 11 + getbits(s, 7);
            }
            if (s->err || index + symbol > nlen + ndist) return 1;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return 1;   /* no end-of-block code */
    err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return 1;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return 1;
    return codes(s, &lencode, &distcode);
}

int upd_inflate(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_len) {
    inf_t s;
    int last;
    memset(&s, 0, sizeof(s));
    s.in = in; s.in_len = in_len; s.out = out; s.out_len = out_len;
    do {
        int type, r;
        last = getbits(&s, 1);
        type = getbits(&s, 2);
        if (s.err) return 1;
        if (type == 0) r = stored(&s);
        else if (type == 1) r = fixed(&s);
        else if (type == 2) r = dynamic(&s);
        else r = 1;
        if (r || s.err) return 1;
    } while (!last);
    return s.out_pos == out_len ? 0 : 1;
}

/* ---------- small checks ---------- */
/* "v<a>.<b>.<c>": exactly three parts, 1-5 digits each, no leading zero
 * (except "0" itself), every value <= 65535 so it fits the 16-bit fields of
 * a Windows file version. Digits are accumulated in an unsigned long that
 * never exceeds 99999, so there is no overflow on any input. */
int upd_tag_parse(const char *tag, uint64_t *ver) {
    const char *p = tag;
    uint64_t v = 0;
    if (!p || *p++ != 'v') return 0;
    for (int part = 0; part < 3; part++) {
        unsigned long x = 0;
        int digits = 0;
        const char *start = p;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 5) return 0;
            x = x * 10 + (unsigned long)(*p - '0');
            p++;
        }
        if (digits == 0 || x > 0xFFFFu || (digits > 1 && *start == '0')) return 0;
        v = v << 16 | x;
        if (part < 2) {
            if (*p != '.') return 0;
            p++;
        }
    }
    if (*p) return 0;
    if (ver) *ver = v << 16;
    return 1;
}

int upd_tag_ok(const char *tag) {
    return upd_tag_parse(tag, NULL);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int upd_sums_find(const char *txt, size_t n, const char *name, unsigned char digest[32]) {
    size_t nl = strlen(name), i = 0;
    while (i < n) {
        size_t start = i, end;
        while (i < n && txt[i] != '\n') i++;
        end = i;
        if (i < n) i++;
        if (end > start && txt[end - 1] == '\r') end--;
        /* <64 hex><space><space or *><name> */
        if (end - start == 66 + nl && txt[start + 64] == ' ' &&
            (txt[start + 65] == ' ' || txt[start + 65] == '*') &&
            !memcmp(txt + start + 66, name, nl)) {
            int ok = 1;
            for (int k = 0; k < 32 && ok; k++) {
                int hi = hexval(txt[start + 2 * k]), lo = hexval(txt[start + 2 * k + 1]);
                if (hi < 0 || lo < 0) ok = 0;
                else digest[k] = (unsigned char)(hi << 4 | lo);
            }
            if (ok) return 1;
        }
    }
    return 0;
}

static uint32_t rd16(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

unsigned upd_pe_machine(const unsigned char *p, size_t n) {
    uint32_t pe;
    if (n < 0x40 || p[0] != 'M' || p[1] != 'Z') return 0;
    pe = rd32(p + 0x3C);
    if (pe > n || n - pe < 24 || memcmp(p + pe, "PE\0\0", 4) != 0) return 0;
    return (unsigned)rd16(p + pe + 4);
}

/*
 * File offset of [rva, rva+len) through the section table, or 0 when the
 * range is not wholly inside one section's raw data and the file. All sums
 * in 64 bits: none of the 32-bit fields can wrap.
 */
static int rva_to_off(const unsigned char *p, size_t n, size_t sect, unsigned nsect,
                      uint32_t rva, uint32_t len, size_t *off) {
    for (unsigned i = 0; i < nsect; i++) {
        const unsigned char *s = p + sect + (size_t)i * 40;
        uint64_t vsize = rd32(s + 8), va = rd32(s + 12), raw = rd32(s + 16), ptr = rd32(s + 20);
        uint64_t span = vsize && vsize < raw ? vsize : raw;   /* the part that is both mapped and in the file */
        if (rva >= va && (uint64_t)rva + len <= va + span) {
            uint64_t o = ptr + (rva - va);
            if (o > n || n - o < len) return 0;
            *off = (size_t)o;
            return 1;
        }
    }
    return 0;
}

/* The first entry of the resource directory at dir (offset inside the
 * resource section r[0..rlen)) whose ID is want, or simply the first entry
 * when want < 0. *child = its OffsetToData. */
static int res_entry(const unsigned char *r, uint32_t rlen, uint32_t dir, long want, uint32_t *child) {
    uint32_t total;
    if (dir > rlen || rlen - dir < 16) return 0;
    total = rd16(r + dir + 12) + rd16(r + dir + 14);   /* named + ID entries */
    if ((rlen - dir - 16) / 8 < total) return 0;
    for (uint32_t i = 0; i < total; i++) {
        const unsigned char *e = r + dir + 16 + (size_t)i * 8;
        uint32_t name = rd32(e);
        if (want >= 0 && ((name & 0x80000000u) || name != (uint32_t)want)) continue;
        *child = rd32(e + 4);
        return 1;
    }
    return 0;
}

uint64_t upd_pe_file_version(const unsigned char *p, size_t n) {
    static const char key[] = "VS_VERSION_INFO";
    uint32_t pe, optsz, nrva, ddoff, res_rva, res_size, c1, c2, c3, data_rva, data_size;
    unsigned nsect;
    size_t opt, sect, roff, voff;
    const unsigned char *r, *v;
    if (n < 0x40 || p[0] != 'M' || p[1] != 'Z') return 0;
    pe = rd32(p + 0x3C);
    if (pe > n || n - pe < 24 || memcmp(p + pe, "PE\0\0", 4) != 0) return 0;
    nsect = (unsigned)rd16(p + pe + 6);
    optsz = rd16(p + pe + 20);
    opt = (size_t)pe + 24;
    if (n - opt < optsz) return 0;
    sect = opt + optsz;
    if ((n - sect) / 40 < nsect) return 0;
    /* data directory 2 (resources) in a PE32 or PE32+ optional header */
    if (optsz < 2) return 0;
    if (rd16(p + opt) == 0x10b) ddoff = 96;
    else if (rd16(p + opt) == 0x20b) ddoff = 112;
    else return 0;
    if (optsz < ddoff + 3 * 8) return 0;
    nrva = rd32(p + opt + ddoff - 4);
    if (nrva < 3) return 0;
    res_rva = rd32(p + opt + ddoff + 2 * 8);
    res_size = rd32(p + opt + ddoff + 2 * 8 + 4);
    if (!res_rva || !res_size || !rva_to_off(p, n, sect, nsect, res_rva, res_size, &roff)) return 0;
    r = p + roff;
    /* type RT_VERSION (16) -> first name -> first language -> data entry;
     * a fixed depth of three, so a directory pointing at itself cannot loop */
    if (!res_entry(r, res_size, 0, 16, &c1) || !(c1 & 0x80000000u)) return 0;
    if (!res_entry(r, res_size, c1 & 0x7FFFFFFFu, -1, &c2) || !(c2 & 0x80000000u)) return 0;
    if (!res_entry(r, res_size, c2 & 0x7FFFFFFFu, -1, &c3) || (c3 & 0x80000000u)) return 0;
    if (c3 > res_size || res_size - c3 < 16) return 0;
    data_rva = rd32(r + c3);
    data_size = rd32(r + c3 + 4);
    /* VS_VERSIONINFO: wLength, wValueLength, wType, L"VS_VERSION_INFO\0",
     * padding to 40, then VS_FIXEDFILEINFO (52 bytes) */
    if (data_size < 40 + 52 || !rva_to_off(p, n, sect, nsect, data_rva, data_size, &voff)) return 0;
    v = p + voff;
    if (rd16(v) < 40 + 52 || rd16(v) > data_size || rd16(v + 2) < 52) return 0;
    for (size_t i = 0; i < sizeof(key); i++)   /* UTF-16LE, terminator included */
        if (v[6 + 2 * i] != (unsigned char)key[i] || v[7 + 2 * i] != 0) return 0;
    if (rd32(v + 40) != 0xFEEF04BDu) return 0;
    return (uint64_t)rd32(v + 48) << 32 | rd32(v + 52);
}

/* 1 if the entry name is "name" or "<folder>/name" (one level, no "..",
 * no backslashes, no absolute path). */
static int name_matches(const unsigned char *s, size_t len, const char *name) {
    size_t nl = strlen(name), slash = (size_t)-1;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\\' || s[i] == 0) return 0;
        if (s[i] == '/') {
            if (slash != (size_t)-1) return 0;   /* deeper than one folder */
            slash = i;
        }
    }
    if (slash == (size_t)-1) return len == nl && !memcmp(s, name, nl);
    if (slash == 0 || (slash == 2 && s[0] == '.' && s[1] == '.') || (slash == 1 && s[0] == '.')) return 0;
    return len - slash - 1 == nl && !memcmp(s + slash + 1, name, nl);
}

int upd_zip_extract(const unsigned char *zip, size_t n, const char *name,
                    unsigned char **out, size_t *out_len) {
    size_t eocd = (size_t)-1, cd, cd_end, p;
    uint32_t entries, found = 0;
    size_t f_off = 0;
    uint32_t f_csize = 0, f_usize = 0, f_crc = 0, f_method = 0;
    unsigned char *buf;

    *out = NULL;
    *out_len = 0;
    if (n < 22) return -1;
    /* end of central directory: last 22 bytes + a comment of up to 64 KB */
    for (size_t i = n - 22 + 1; i-- > 0 && n - i <= 22 + 65535;) {
        if (rd32(zip + i) == 0x06054b50u && i + 22 + rd16(zip + i + 20) == n) { eocd = i; break; }
    }
    if (eocd == (size_t)-1) return -1;
    if (rd16(zip + eocd + 4) != 0 || rd16(zip + eocd + 6) != 0) return -1;   /* multi-disk */
    entries = rd16(zip + eocd + 10);
    cd = rd32(zip + eocd + 16);
    cd_end = cd + (size_t)rd32(zip + eocd + 12);
    if (entries != rd16(zip + eocd + 8) || cd > eocd || cd_end > eocd || cd_end < cd) return -1;

    p = cd;
    for (uint32_t e = 0; e < entries; e++) {
        uint32_t flags, method, crc, csize, usize, nlen, xlen, clen;
        size_t loff;
        if (cd_end - p < 46 || rd32(zip + p) != 0x02014b50u) return -1;
        flags = rd16(zip + p + 8);
        method = rd16(zip + p + 10);
        crc = rd32(zip + p + 16);
        csize = rd32(zip + p + 20);
        usize = rd32(zip + p + 24);
        nlen = rd16(zip + p + 28);
        xlen = rd16(zip + p + 30);
        clen = rd16(zip + p + 32);
        loff = rd32(zip + p + 42);
        if (cd_end - p - 46 < (size_t)nlen + xlen + clen) return -1;
        if (name_matches(zip + p + 46, nlen, name)) {
            if (found) return -1;                     /* two candidates: refuse */
            if (flags & 1) return -1;                 /* encrypted */
            if (method != 0 && method != 8) return -1;
            found = 1;
            f_off = loff; f_csize = csize; f_usize = usize; f_crc = crc; f_method = method;
        }
        p += 46 + (size_t)nlen + xlen + clen;
    }
    if (!found) return 0;
    if (f_usize > UPD_MAX_ENTRY || f_csize > UPD_MAX_ENTRY) return -1;
    if (f_method == 0 && f_csize != f_usize) return -1;
    /* local header: its own name/extra lengths; data must end before the
     * central directory */
    if (f_off > cd || cd - f_off < 30 || rd32(zip + f_off) != 0x04034b50u) return -1;
    {
        size_t data = f_off + 30 + rd16(zip + f_off + 26) + rd16(zip + f_off + 28);
        if (data > cd || cd - data < f_csize) return -1;
        buf = (unsigned char *)malloc(f_usize ? f_usize : 1);
        if (!buf) return -1;
        if (f_method == 0) memcpy(buf, zip + data, f_usize);
        else if (upd_inflate(zip + data, f_csize, buf, f_usize) != 0) { free(buf); return -1; }
    }
    if (upd_crc32(buf, f_usize) != f_crc) { free(buf); return -1; }
    *out = buf;
    *out_len = f_usize;
    return 1;
}
