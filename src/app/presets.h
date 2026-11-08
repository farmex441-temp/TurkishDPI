/*
 * TurkishDPI - strategy presets for the packet engine (src/engine/engine.c).
 *
 * Every preset sends a decoy and/or cuts the first request so the ISP's
 * filter never reads the whole site name, drops the resets it forges, and
 * blocks QUIC (the engine's default) so browsers use TCP, where this works.
 * The techniques are those of Shroudly's packet backend (umutxyp/Shroudly,
 * MIT): low-TTL / bad-checksum decoy, split, disorder, fakedsplit, TLS record
 * split. "{DNS}" is replaced at run time with the clean-resolver arguments
 * (and the hybrid race) chosen by the auto-detector.
 * Order matters: the auto-detector tries them top to bottom when a network
 * is new, so the most widely working, lightest presets come first.
 */
#ifndef TURKISHDPI_PRESETS_H
#define TURKISHDPI_PRESETS_H

typedef struct {
    const wchar_t *name;   /* shown in the menu and the window */
    const char    *args;   /* engine arguments, may contain {DNS} */
} preset_t;

static const preset_t PRESETS[] = {
    /* Measured on a Turk Telekom line, 5 runs x 4 sites per technique
     * (2026-11-08): cutting the first request after its 2nd byte and sending
     * the pieces in reverse order opens every blocked site, every time, at no
     * extra latency (~150 ms) - the filter gets a piece too short to classify
     * and a piece without a TLS record header. Cutting inside the site name
     * was unreliable (roblox 1/5); a decoy with the hop-distance TTL followed
     * by the real data costs ~300 ms (the filter drops the real first segment
     * and Windows resends it after its minimum RTO); a cut after byte 1 does
     * not work at all. The decoy variants stay for networks that need one. */
    { L"Ters bölme (önerilen)",
      "--desync none --split disorder --split-pos 2 --drop-rst 1 {DNS}" },
    { L"Ters bölme + sahte paket (TTL 5)",
      "--desync fake --split disorder --split-pos 2 --autottl 0 --fake-ttl 5 --drop-rst 1 {DNS}" },
    { L"Sahte + ters bölünmüş (ölçülen TTL)",
      "--desync fakeddisorder --split-pos 2 --autottl 1 --fake-ttl 5 --drop-rst 1 {DNS}" },
    { L"Sıralı bölme + sahte paket (TTL 5)",
      "--desync fake --split on --split-pos 2 --autottl 0 --fake-ttl 5 --drop-rst 1 {DNS}" },
    { L"TLS kaydı bölme (Superonline)",
      "--desync fake --split on --tlsrec 1 --autottl 1 --fake-ttl 4 --drop-rst 1 {DNS}" },
    { L"Ad içinden bölme + sahte paket",
      "--desync fake --split on --autottl 1 --fake-ttl 4 --drop-rst 1 {DNS}" },
    { L"Ad içinden ters bölme + sahte",
      "--desync fakeddisorder --autottl 1 --fake-ttl 4 --drop-rst 1 {DNS}" },
    { L"Ters bölme (DNS yönlendirmesiz)",
      "--desync none --split disorder --split-pos 2 --drop-rst 1" },
    { L"Ters bölme + sahte (DNS yönlendirmesiz)",
      "--desync fake --split disorder --split-pos 2 --autottl 0 --fake-ttl 5 --drop-rst 1" },
    { L"TLS kaydı bölme (DNS yönlendirmesiz)",
      "--desync fake --split on --tlsrec 1 --autottl 1 --fake-ttl 4 --drop-rst 1" },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

/* DNS resolvers on non-standard ports (Turkish ISPs hijack port 53). */
typedef struct {
    const char *v4;
    const char *v6;
    unsigned short port;
} dns_server_t;

static const dns_server_t DNS_SERVERS[] = {
    { "77.88.8.8",      "2a02:6b8::feed:0ff",        1253 },  /* Yandex */
    { "77.88.8.1",      "2a02:6b8:0:1::feed:0ff",    1253 },  /* Yandex secondary */
    { "94.140.14.140",  "2a10:50c0::1:ff",           5353 },  /* AdGuard unfiltered */
    { "94.140.14.141",  "2a10:50c0::2:ff",           5353 },  /* AdGuard unfiltered secondary */
    { "208.67.222.222", "2620:119:35::35",           5353 },  /* OpenDNS */
    { "208.67.220.220", "2620:119:53::53",           443  },  /* OpenDNS on 443 */
};
#define DNS_SERVER_COUNT ((int)(sizeof(DNS_SERVERS) / sizeof(DNS_SERVERS[0])))

#endif
