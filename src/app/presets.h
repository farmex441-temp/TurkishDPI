/*
 * TurkishDPI - strategy presets.
 *
 * The non-SNI presets are the exact command lines shipped by GoodbyeDPI-Turkey
 * (cagritaskn/GoodbyeDPI-Turkey, windows/turkey_dnsredir scripts). The "SNI" presets add the
 * split-before-SNI + decoy-ClientHello technique described by Shroudly
 * (umutxyp/Shroudly, core/src/desync.c) using GoodbyeDPI's --frag-by-sni and
 * --fake-with-sni options.
 *
 * "{DNS}" is replaced at run time with the DNS redirection arguments of the
 * resolver that the auto-detector found reachable (see probe.c).
 * Order matters: the auto-detector tries them top to bottom, so the most
 * widely working / lightest presets come first.
 */
#ifndef TURKISHDPI_PRESETS_H
#define TURKISHDPI_PRESETS_H

typedef struct {
    const wchar_t *name;   /* shown in the tray menu */
    const char    *args;   /* engine arguments, may contain {DNS} */
} preset_t;

static const preset_t PRESETS[] = {
    { L"Türkiye Standart (-5, TTL 5, DNS)",      "-5 --set-ttl 5 {DNS}" },
    { L"Modern (-9, DNS)",                        "-9 {DNS}" },
    { L"Auto-TTL (-5, DNS)",                      "-5 {DNS}" },
    { L"Sabit TTL 3 (DNS)",                       "--set-ttl 3 {DNS}" },
    { L"SNI Parçalama + Sahte SNI (DNS)",         "-9 --frag-by-sni --fake-with-sni www.google.com {DNS}" },
    { L"Agresif SNI (DNS, 2x sahte paket)",       "-9 --frag-by-sni --fake-with-sni www.google.com --fake-resend 2 --auto-ttl {DNS}" },
    { L"Superonline (-5)",                        "-5" },
    { L"Superonline (-9)",                        "-9" },
    { L"Superonline (TTL 3)",                     "--set-ttl 3" },
    { L"Superonline SNI Parçalama",               "-9 --frag-by-sni --fake-with-sni www.google.com" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

/* DNS resolvers on non-standard ports (Turkish ISPs hijack port 53). */
typedef struct {
    const char *v4;
    const char *v6;
    unsigned short port;
} dns_server_t;

static const dns_server_t DNS_SERVERS[] = {
    { "77.88.8.8",     "2a02:6b8::feed:0ff",        1253 },  /* Yandex (GoodbyeDPI-Turkey default) */
    { "77.88.8.1",     "2a02:6b8:0:1::feed:0ff",    1253 },  /* Yandex secondary */
    { "94.140.14.140", "2a10:50c0::1:ff",           5353 },  /* AdGuard unfiltered */
};
#define DNS_SERVER_COUNT ((int)(sizeof(DNS_SERVERS) / sizeof(DNS_SERVERS[0])))

#endif
