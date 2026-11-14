/*
 * TurkishDPI - reading an update package: the release's SHA256SUMS.txt, the
 * zip (stored or deflated entries, CRC-32 checked) and small sanity checks.
 * Pure C without Win32 calls so it is unit tested (tests/updpkg_test.c).
 *
 * The zip is only opened after its SHA-256 matched SHA256SUMS.txt, but the
 * parser still treats it as hostile input: every offset and length is
 * bounds-checked and a broken file is rejected, never trusted.
 */
#ifndef TURKISHDPI_UPDPKG_H
#define TURKISHDPI_UPDPKG_H
#include <stddef.h>
#include <stdint.h>

#define UPD_MAX_ENTRY (32u * 1024u * 1024u)   /* largest file taken from a package */

uint32_t upd_crc32(const unsigned char *p, size_t n);

/* Raw DEFLATE (RFC 1951) into out[0..out_len); 0 only when the stream ends
 * exactly there. */
int upd_inflate(const unsigned char *in, size_t in_len, unsigned char *out, size_t out_len);

/*
 * Strict release tag "vMAJOR.MINOR.PATCH": lowercase 'v', exactly three
 * numeric parts of 1-5 digits without leading zeros, each <= 65535, nothing
 * else (pre-release tags like "v2.0.1-rc1" are refused). On success *ver
 * (may be NULL) = MAJOR<<48 | MINOR<<32 | PATCH<<16, the packing of a
 * Windows file version with build 0.
 */
int upd_tag_parse(const char *tag, uint64_t *ver);
/* upd_tag_parse without the value: a tag that is safe to put into a URL path. */
int upd_tag_ok(const char *tag);

/* Finds "<64 hex>  <name>" (sha256sum format, also "*<name>") in SHA256SUMS.txt. */
int upd_sums_find(const char *txt, size_t n, const char *name, unsigned char digest[32]);

/*
 * Extracts the entry whose last path part is name and that sits at the top
 * of the zip or in one folder ("TurkishDPI-v2.0.1-x64/TurkishDPI.exe").
 * Returns 1 and a malloc'ed buffer (caller frees), 0 when there is no such
 * entry, -1 when the zip is broken, ambiguous (two matches), encrypted, uses
 * another compression method, is too large or fails its CRC.
 */
int upd_zip_extract(const unsigned char *zip, size_t n, const char *name,
                    unsigned char **out, size_t *out_len);

/* IMAGE_FILE_HEADER.Machine of a PE image (0x8664 x64, 0x14c x86), 0 if not a PE. */
unsigned upd_pe_machine(const unsigned char *p, size_t n);

/*
 * File version of a PE image from its VERSIONINFO resource
 * (VS_FIXEDFILEINFO dwFileVersionMS << 32 | dwFileVersionLS, i.e.
 * major<<48 | minor<<32 | patch<<16 | build), 0 when there is none or the
 * image is malformed. Walks the section table and the resource directory
 * (RT_VERSION, first name, first language) with every offset bounds-checked;
 * it does not scan for the 0xFEEF04BD signature, which also occurs as an
 * immediate in code (our own exe compares against it).
 */
uint64_t upd_pe_file_version(const unsigned char *p, size_t n);

#endif
