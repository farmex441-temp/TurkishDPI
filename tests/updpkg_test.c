/* TurkishDPI: the update package reader (src/app/updpkg.c) - CRC-32, inflate,
 * SHA256SUMS.txt lookup, zip extraction and its refusals, tag and PE checks.
 *
 *   gcc -Isrc/app tests/updpkg_test.c src/app/updpkg.c -o updpkg_test
 *   updpkg_test                       built-in cases
 *   updpkg_test pkg.zip name file     also: extract name from pkg.zip and
 *                                     compare it with file (real releases)
 *   updpkg_test --pe-version exe [2.0.1]
 *                                     print the exe's file version (and check it)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "updpkg.h"
#include "updpkg_vectors.h"

static int fails;
#define EXPECT(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } } while (0)

static unsigned char TEXT[24 * 20 + 256];

static void make_text(void) {
    for (int i = 0; i < 20; i++) memcpy(TEXT + 24 * i, "TurkishDPI update test. ", 24);
    for (int i = 0; i < 256; i++) TEXT[480 + i] = (unsigned char)i;
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

/* A zip with stored entries (names[i] -> datas[i]); returns its length. */
static size_t make_zip(unsigned char *z, int n, const char **names, const char **datas) {
    size_t off[8], p = 0, cd;
    for (int i = 0; i < n; i++) {
        size_t nl = strlen(names[i]), dl = strlen(datas[i]);
        off[i] = p;
        memset(z + p, 0, 30);
        put32(z + p, 0x04034b50u);
        put32(z + p + 14, upd_crc32((const unsigned char *)datas[i], dl));
        put32(z + p + 18, (uint32_t)dl);
        put32(z + p + 22, (uint32_t)dl);
        put16(z + p + 26, (unsigned)nl);
        memcpy(z + p + 30, names[i], nl);
        memcpy(z + p + 30 + nl, datas[i], dl);
        p += 30 + nl + dl;
    }
    cd = p;
    for (int i = 0; i < n; i++) {
        size_t nl = strlen(names[i]), dl = strlen(datas[i]);
        memset(z + p, 0, 46);
        put32(z + p, 0x02014b50u);
        put32(z + p + 16, upd_crc32((const unsigned char *)datas[i], dl));
        put32(z + p + 20, (uint32_t)dl);
        put32(z + p + 24, (uint32_t)dl);
        put16(z + p + 28, (unsigned)nl);
        put32(z + p + 42, (uint32_t)off[i]);
        memcpy(z + p + 46, names[i], nl);
        p += 46 + nl;
    }
    memset(z + p, 0, 22);
    put32(z + p, 0x06054b50u);
    put16(z + p + 8, (unsigned)n);
    put16(z + p + 10, (unsigned)n);
    put32(z + p + 12, (uint32_t)(p - cd));
    put32(z + p + 16, (uint32_t)cd);
    return p + 22;
}

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/*
 * A minimal PE image: headers, a ".text" section holding a decoy
 * VS_FIXEDFILEINFO (version 9.9.9.9, must never be picked up) and a ".rsrc"
 * section at raw offset rsrc_raw holding RT_VERSION -> 1 -> 0x409 -> a
 * VS_VERSIONINFO with the given file version. The version data is the last
 * thing in the file. Returns the file length.
 */
#define PE_CAP 0x2000
#define PE_OPT 0x58                                       /* optional header offset */
#define PE_OPTSZ(b) ((size_t)((b)[0x54] | (b)[0x55] << 8))
#define PE_DATADIR(b, i) (PE_OPT + ((b)[PE_OPT + 1] == 0x02 ? 112 : 96) + (size_t)(i) * 8)
#define PE_SECT(b, i) (PE_OPT + PE_OPTSZ(b) + (size_t)(i) * 40)
#define PE_RSRC_DIR(b) ((size_t)get32((b) + PE_SECT(b, 1) + 20))
static size_t make_pe(unsigned char *b, int plus, size_t rsrc_raw, uint64_t version) {
    static const char key[] = "VS_VERSION_INFO";
    const size_t text_raw = 0x1A0, rsz = 0x58 + 92;
    size_t s, r, v, opt_size = plus ? 0xF0 : 0xE0, dd = PE_OPT + (plus ? 112 : 96);
    memset(b, 0, PE_CAP);
    b[0] = 'M'; b[1] = 'Z';
    put32(b + 0x3C, 0x40);
    memcpy(b + 0x40, "PE\0\0", 4);
    put16(b + 0x44, plus ? 0x8664 : 0x14c);
    put16(b + 0x46, 2);
    put16(b + 0x54, (unsigned)opt_size);
    put16(b + PE_OPT, plus ? 0x20b : 0x10b);
    put32(b + dd - 4, 16);                                 /* NumberOfRvaAndSizes */
    put32(b + dd + 16, 0x3000);                            /* resources: RVA, size */
    put32(b + dd + 20, (uint32_t)rsz);
    s = PE_OPT + opt_size;
    memcpy(b + s, ".text", 5);
    put32(b + s + 8, 0x40); put32(b + s + 12, 0x1000); put32(b + s + 16, 0x40); put32(b + s + 20, (uint32_t)text_raw);
    s += 40;
    memcpy(b + s, ".rsrc", 5);
    put32(b + s + 8, (uint32_t)rsz); put32(b + s + 12, 0x3000); put32(b + s + 16, (uint32_t)rsz);
    put32(b + s + 20, (uint32_t)rsrc_raw);
    /* decoy in .text: signature, struct version, file version 9.9.9.9 */
    put32(b + text_raw + 4, 0xFEEF04BDu); put32(b + text_raw + 8, 0x10000);
    put32(b + text_raw + 12, 0x00090009u); put32(b + text_raw + 16, 0x00090009u);
    /* resource tree */
    r = rsrc_raw;
    put16(b + r + 14, 1);         put32(b + r + 0x10, 16);    put32(b + r + 0x14, 0x80000000u | 0x18);
    put16(b + r + 0x18 + 14, 1);  put32(b + r + 0x28, 1);     put32(b + r + 0x2C, 0x80000000u | 0x30);
    put16(b + r + 0x30 + 14, 1);  put32(b + r + 0x40, 0x409); put32(b + r + 0x44, 0x48);
    put32(b + r + 0x48, 0x3000 + 0x58);
    put32(b + r + 0x4C, 92);
    v = r + 0x58;
    put16(b + v, 92);
    put16(b + v + 2, 52);
    for (size_t i = 0; i < sizeof(key); i++) b[v + 6 + 2 * i] = (unsigned char)key[i];
    put32(b + v + 40, 0xFEEF04BDu);
    put32(b + v + 44, 0x10000);
    put32(b + v + 48, (uint32_t)(version >> 32));
    put32(b + v + 52, (uint32_t)version);
    return v + 92;
}

static int extract_is(const unsigned char *z, size_t n, const char *name, const char *want) {
    unsigned char *out;
    size_t len;
    int r = upd_zip_extract(z, n, name, &out, &len), ok;
    if (r != 1) return r;
    ok = len == strlen(want) && !memcmp(out, want, len);
    free(out);
    return ok ? 1 : 99;
}

static int file_mode(const char *zip_path, const char *name, const char *file_path) {
    FILE *fz = fopen(zip_path, "rb"), *ff = fopen(file_path, "rb");
    unsigned char *z, *f, *out;
    long zl, fl;
    size_t len;
    int r;
    if (!fz || !ff) { printf("cannot open files\n"); return 1; }
    fseek(fz, 0, SEEK_END); zl = ftell(fz); fseek(fz, 0, SEEK_SET);
    fseek(ff, 0, SEEK_END); fl = ftell(ff); fseek(ff, 0, SEEK_SET);
    z = malloc((size_t)zl); f = malloc((size_t)fl + 1);
    if (fread(z, 1, (size_t)zl, fz) != (size_t)zl || fread(f, 1, (size_t)fl, ff) != (size_t)fl) return 1;
    r = upd_zip_extract(z, (size_t)zl, name, &out, &len);
    printf("%s: extract %s -> %d, %zu bytes, %s\n", zip_path, name, r, len,
           r == 1 && len == (size_t)fl && !memcmp(out, f, len) ? "identical" : "DIFFERENT");
    return !(r == 1 && len == (size_t)fl && !memcmp(out, f, len));
}

/* Prints the file version of a real PE file; with want ("2.0.1") also checks it. */
static int version_mode(const char *path, const char *want) {
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    uint64_t v;
    int ok = 1;
    if (!f) { printf("cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || !(b = (unsigned char *)malloc((size_t)n + 1)) || fread(b, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 1;
    }
    fclose(f);
    v = upd_pe_file_version(b, (size_t)n);
    printf("%s: file version %u.%u.%u.%u\n", path, (unsigned)(v >> 48), (unsigned)(v >> 32 & 0xFFFF),
           (unsigned)(v >> 16 & 0xFFFF), (unsigned)(v & 0xFFFF));
    if (want) {
        char tag[40];
        uint64_t t;
        snprintf(tag, sizeof(tag), "v%s", want);
        ok = upd_tag_parse(tag, &t) && (v & ~0xFFFFull) == t;
        printf("%s\n", ok ? "matches" : "DIFFERENT");
    }
    free(b);
    return !(v && ok);
}

int main(int argc, char **argv) {
    unsigned char out[2048], z[4096];
    size_t zl;
    make_text();
    if (argc >= 3 && !strcmp(argv[1], "--pe-version")) return version_mode(argv[2], argc > 3 ? argv[3] : NULL);
    if (argc == 4) return file_mode(argv[1], argv[2], argv[3]);

    EXPECT(upd_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u, "crc32 check value");
    EXPECT(upd_crc32(TEXT, sizeof(TEXT)) == 0xa574c7deu, "crc32 of TEXT (zlib)");

    /* inflate: every block type, exact output size only */
    EXPECT(!upd_inflate(V_lvl0, sizeof(V_lvl0), out, sizeof(TEXT)) && !memcmp(out, TEXT, sizeof(TEXT)), "stored blocks");
    EXPECT(!upd_inflate(V_dyn, sizeof(V_dyn), out, sizeof(TEXT)) && !memcmp(out, TEXT, sizeof(TEXT)), "dynamic Huffman");
    EXPECT(!upd_inflate(V_huff, sizeof(V_huff), out, sizeof(TEXT)) && !memcmp(out, TEXT, sizeof(TEXT)), "Huffman only");
    EXPECT(!upd_inflate(V_fixed, sizeof(V_fixed), out, 23) && !memcmp(out, "hello hello hello world", 23), "fixed Huffman");
    EXPECT(upd_inflate(V_dyn, sizeof(V_dyn), out, sizeof(TEXT) - 1) != 0, "output larger than announced");
    EXPECT(upd_inflate(V_dyn, sizeof(V_dyn), out, sizeof(TEXT) + 1) != 0, "output shorter than announced");
    for (size_t cut = 0; cut < sizeof(V_dyn); cut += 7)
        EXPECT(upd_inflate(V_dyn, cut, out, sizeof(TEXT)) != 0, "truncated stream refused");
    {   /* bit flips: never a crash, never more than the buffer (ASan watches) */
        unsigned char bad[sizeof(V_dyn)];
        int accepted = 0;
        for (size_t i = 0; i < sizeof(V_dyn) * 8; i++) {
            memcpy(bad, V_dyn, sizeof(bad));
            bad[i / 8] ^= (unsigned char)(1u << (i % 8));
            if (!upd_inflate(bad, sizeof(bad), out, sizeof(TEXT)) && !memcmp(out, TEXT, sizeof(TEXT))) accepted++;
        }
        (void)accepted;   /* a flip may still decode; the zip CRC catches it */
    }
    {   /* a back-reference before the start of the output */
        static const unsigned char far_ref[] = { 0x03, 0x02, 0x00 };   /* fixed block, distance > 0 bytes */
        EXPECT(upd_inflate(far_ref, sizeof(far_ref), out, 3) != 0, "distance before start refused");
    }

    /* the deflated zip made by Python's zipfile */
    {
        unsigned char *o;
        size_t ol;
        EXPECT(upd_zip_extract(V_zip, sizeof(V_zip), "TurkishDPI.exe", &o, &ol) == 1 &&
               ol == sizeof(TEXT) && !memcmp(o, TEXT, ol), "deflated zip entry");
        free(o);
        EXPECT(extract_is(V_zip, sizeof(V_zip), "NOTICE", "notice text\n") == 1, "second entry");
        EXPECT(extract_is(V_zip, sizeof(V_zip), "LICENSE", "") == 0, "missing entry is 0");
        {
            unsigned char bad[sizeof(V_zip)];
            memcpy(bad, V_zip, sizeof(bad));
            /* inside the first entry's data (after its local header) */
            bad[30 + (bad[26] | bad[27] << 8) + (bad[28] | bad[29] << 8) + 10] ^= 0x40;
            EXPECT(upd_zip_extract(bad, sizeof(bad), "TurkishDPI.exe", &o, &ol) == -1, "damaged data refused");
        }
        for (size_t cut = 0; cut < sizeof(V_zip); cut += 5)
            EXPECT(upd_zip_extract(V_zip, cut, "TurkishDPI.exe", &o, &ol) == -1, "truncated zip refused");
    }

    /* stored zips: names, one folder level, refusals */
    {
        const char *n1[] = { "TurkishDPI-v2.0.2-x64/TurkishDPI.exe", "TurkishDPI-v2.0.2-x64/NOTICE" };
        const char *d1[] = { "EXE", "N" };
        zl = make_zip(z, 2, n1, d1);
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "EXE") == 1, "stored entry in a folder");
        EXPECT(extract_is(z, zl, "NOTICE", "N") == 1, "stored second entry");
    }
    {
        const char *n[] = { "a/b/TurkishDPI.exe", "../TurkishDPI.exe", "/TurkishDPI.exe", "x\\TurkishDPI.exe",
                            "TurkishDPI.exe.bak", "NOTTurkishDPI.exe" };
        const char *d[] = { "1", "2", "3", "4", "5", "6" };
        zl = make_zip(z, 6, n, d);
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == 0, "deep, parent, absolute, backslash, near names ignored");
    }
    {
        const char *n[] = { "TurkishDPI.exe", "pkg/TurkishDPI.exe" };
        const char *d[] = { "A", "B" };
        zl = make_zip(z, 2, n, d);
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "two candidates refused");
    }
    {
        const char *n[] = { "TurkishDPI.exe" };
        const char *d[] = { "ABC" };
        zl = make_zip(z, 1, n, d);
        z[30 + 14 + 1] ^= 1;   /* data byte -> CRC mismatch */
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "CRC mismatch refused");
        z[30 + 14 + 1] ^= 1;
        put16(z + zl - 22 + 20, 5);   /* comment length that does not fit */
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "bad end record refused");
        put16(z + zl - 22 + 20, 0);
        put16(z + 30 + 14 + 3 + 8, 1);   /* central entry: encrypted flag */
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "encrypted refused");
        put16(z + 30 + 14 + 3 + 8, 0);
        put16(z + 30 + 14 + 3 + 10, 12);  /* bzip2 */
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "other method refused");
        put16(z + 30 + 14 + 3 + 10, 0);
        put32(z + 30 + 14 + 3 + 42, 9999);  /* local header beyond the file */
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "bad local offset refused");
        put32(z + 30 + 14 + 3 + 42, 0);
        put32(z + 30 + 14 + 3 + 24, 0x7FFFFFFF);  /* huge size */
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "") == -1, "oversized entry refused");
        put32(z + 30 + 14 + 3 + 24, 3);
        EXPECT(extract_is(z, zl, "TurkishDPI.exe", "ABC") == 1, "restored zip is fine again");
    }

    /* SHA256SUMS.txt */
    {
        const char *sums =
            "0000000000000000000000000000000000000000000000000000000000000000  TurkishDPI-v2.0.2-x86.zip\n"
            "00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF  TurkishDPI-v2.0.2-x64.zip\r\n"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff  TurkishDPI-v2.0.2-x64.zip.bak\n";
        unsigned char d[32];
        EXPECT(upd_sums_find(sums, strlen(sums), "TurkishDPI-v2.0.2-x64.zip", d) &&
               d[0] == 0x00 && d[1] == 0x11 && d[15] == 0xff && d[31] == 0xff, "sums line found");
        EXPECT(!upd_sums_find(sums, strlen(sums), "TurkishDPI-v2.0.2-arm.zip", d), "absent asset");
        EXPECT(!upd_sums_find("zz  a.zip\n", 10, "a.zip", d), "short hash refused");
    }

    /* tags and PE headers */
    {
        static const char *good[] = { "v2.0.1", "v10.20.30", "v0.0.0", "v65535.65535.65535", "v1.0.10" };
        static const char *bad[] = {
            "v2.0.1-rc1", "v", "2.0.1", "v2/../x", "v.1", "v2.0.1%2f", "V2.0.1", "v2.0", "v2", "v2.0.1.4",
            "v2..1", "v2.0.", "v.0.1", "v2.0.1.", "v-1.0.0", "v+1.0.0", "v 1.0.0", "v1.0.1 ", "v1.0.1\n",
            "v01.0.1", "v1.00.1", "v1.0.01", "v65536.0.0", "v0.65536.0", "v0.0.65536", "v99999.0.0",
            "v123456.0.0", "v1.0.123456", "v99999999999999999999999.0.0", "v2.0.1x", "x2.0.1", "",
            "v1,0,1", "v\xd9\xa1.0.0",
        };
        uint64_t ver = 1;
        for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
            EXPECT(upd_tag_ok(good[i]), good[i]);
            EXPECT(upd_tag_parse(good[i], NULL), good[i]);
        }
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            ver = 77;
            EXPECT(!upd_tag_ok(bad[i]) && !upd_tag_parse(bad[i], &ver) && ver == 77, bad[i]);
        }
        EXPECT(upd_tag_parse("v2.0.1", &ver) && ver == 0x0002000000010000ull, "v2.0.1 packed");
        EXPECT(upd_tag_parse("v65535.65535.65535", &ver) && ver == 0xFFFFFFFFFFFF0000ull, "max packed");
        EXPECT(upd_tag_parse("v0.0.0", &ver) && ver == 0, "zero packed");
        EXPECT(upd_tag_parse("v1.10.2", &ver) && ver == (1ull << 48 | 10ull << 32 | 2ull << 16), "v1.10.2 packed");
        EXPECT(!upd_tag_parse(NULL, &ver), "NULL tag");
    }
    {
        unsigned char pe[256];
        memset(pe, 0, sizeof(pe));
        pe[0] = 'M'; pe[1] = 'Z';
        put32(pe + 0x3C, 0x80);
        memcpy(pe + 0x80, "PE\0\0", 4);
        put16(pe + 0x84, 0x8664);
        EXPECT(upd_pe_machine(pe, sizeof(pe)) == 0x8664, "x64 PE");
        put32(pe + 0x3C, 250);
        EXPECT(upd_pe_machine(pe, sizeof(pe)) == 0, "PE header past the end");
        EXPECT(upd_pe_machine((const unsigned char *)"MZ", 2) == 0, "tiny file");
    }

    /* file version from the VERSIONINFO resource */
    {
        static unsigned char pe[PE_CAP], bad[PE_CAP];
        static const size_t raws[] = { 0x200, 0x400, 0x203, 0x1000, 0x1801 };
        const uint64_t want = 2ull << 48 | 0ull << 32 | 1ull << 16 | 7;
        size_t n;
        for (int plus = 0; plus < 2; plus++) {
            for (size_t k = 0; k < sizeof(raws) / sizeof(raws[0]); k++) {
                char what[96];
                n = make_pe(pe, plus, raws[k], 0x0002000000010007ull);
                snprintf(what, sizeof(what), "pe version (%s, .rsrc at 0x%zx)", plus ? "PE32+" : "PE32", raws[k]);
                EXPECT(upd_pe_file_version(pe, n) == want, what);
                /* the version data is the last thing in the file: every
                 * shorter buffer must be refused, never read past */
                for (size_t cut = 0; cut < n; cut++) {
                    unsigned char *t = (unsigned char *)malloc(cut ? cut : 1);
                    memcpy(t, pe, cut);
                    if (upd_pe_file_version(t, cut) != 0) {
                        snprintf(what, sizeof(what), "truncated pe at %zu (raw 0x%zx, %d)", cut, raws[k], plus);
                        EXPECT(0, what);
                    }
                    free(t);
                }
            }
        }
        n = make_pe(pe, 0, 0x400, 0x0002000000010007ull);
        EXPECT(upd_pe_machine(pe, n) == 0x14c, "synthetic PE32 machine");
        /* no VERSIONINFO, but the decoy signature in .text: still 0 */
        memcpy(bad, pe, n);
        put32(bad + PE_RSRC_DIR(bad) + 16, 0x80000000u | 0x10);   /* type entry is now named, not RT_VERSION */
        EXPECT(upd_pe_file_version(bad, n) == 0, "no RT_VERSION entry");
        memcpy(bad, pe, n);
        put32(bad + n - 52, 0xFEEF04BCu);                        /* signature broken */
        EXPECT(upd_pe_file_version(bad, n) == 0, "wrong signature");
        memcpy(bad, pe, n);
        bad[n - 92 + 6] = 'W';                                    /* key "WS_VERSION_INFO" */
        EXPECT(upd_pe_file_version(bad, n) == 0, "wrong key");
        memcpy(bad, pe, n);
        put32(bad + PE_DATADIR(bad, 0) + 16 + 4, 0);              /* resource directory size 0 */
        EXPECT(upd_pe_file_version(bad, n) == 0, "no resource directory");
        memcpy(bad, pe, n);
        put32(bad + PE_RSRC_DIR(bad) + 0x48 + 4, 93);             /* data entry 1 byte past the file */
        EXPECT(upd_pe_file_version(bad, n) == 0, "data past the end");
        memcpy(bad, pe, n);
        put32(bad + PE_RSRC_DIR(bad) + 0x18 + 16 + 4, 0x80000000u);   /* level 2 points at the root again */
        EXPECT(upd_pe_file_version(bad, n) == 0, "directory loop");
        memcpy(bad, pe, n);
        put16(bad + PE_RSRC_DIR(bad) + 14, 0xFFFF);               /* more entries than fit */
        EXPECT(upd_pe_file_version(bad, n) == 0, "entry count too large");
        memcpy(bad, pe, n);
        put16(bad + 0x40 + 6, 0xFFFF);                            /* more sections than the file holds */
        EXPECT(upd_pe_file_version(bad, n) == 0, "section count too large");
        memcpy(bad, pe, n);
        put32(bad + PE_SECT(bad, 1) + 20, 0xFFFFFF00u);           /* .rsrc raw pointer near 4 GB */
        EXPECT(upd_pe_file_version(bad, n) == 0, "raw pointer wraps");
        memcpy(bad, pe, n);
        put32(bad + PE_SECT(bad, 1) + 12, 0xFFFFF000u);           /* .rsrc VA near 4 GB */
        EXPECT(upd_pe_file_version(bad, n) == 0, "virtual address wraps");
        memcpy(bad, pe, n);
        put16(bad + 0x40 + 24, 0x10c);                            /* unknown optional header magic */
        EXPECT(upd_pe_file_version(bad, n) == 0, "unknown optional header");
        EXPECT(upd_pe_file_version((const unsigned char *)"MZ", 2) == 0, "tiny file version");
    }

    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
