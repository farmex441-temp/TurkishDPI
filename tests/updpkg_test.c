/* TurkishDPI: the update package reader (src/app/updpkg.c) - CRC-32, inflate,
 * SHA256SUMS.txt lookup, zip extraction and its refusals, tag and PE checks.
 *
 *   gcc -Isrc/app tests/updpkg_test.c src/app/updpkg.c -o updpkg_test
 *   updpkg_test                       built-in cases
 *   updpkg_test pkg.zip name file     also: extract name from pkg.zip and
 *                                     compare it with file (real releases)
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

int main(int argc, char **argv) {
    unsigned char out[2048], z[4096];
    size_t zl;
    make_text();
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
    EXPECT(upd_tag_ok("v2.0.1") && upd_tag_ok("V10.20.30"), "good tags");
    EXPECT(!upd_tag_ok("v2.0.1-rc1") && !upd_tag_ok("v") && !upd_tag_ok("2.0.1") && !upd_tag_ok("v2/../x") &&
           !upd_tag_ok("v.1") && !upd_tag_ok("v2.0.1%2f"), "bad tags");
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

    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
