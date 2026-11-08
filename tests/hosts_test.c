/*
 * Manual check: system DNS answer and HTTPS (TCP+TLS) result for each host
 * given on the command line. Run once without and once with TurkishDPI to see
 * what the engine changes, e.g. for Discord's web, gateway, CDN, updater and
 * voice hosts. No admin needed.
 *
 * Build: x86_64-w64-mingw32-gcc -O1 -Isrc/app -Isrc/engine tests/hosts_test.c src/app/probe.c
 *        src/app/sysdll.c -lws2_32 -o build/hosts_test.exe
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "probe.h"
#include "sysdll.h"

#define MAXH 32

int wmain(int argc, wchar_t **argv) {
    WSADATA w;
    const wchar_t *h[MAXH];
    int r[MAXH], ms[MAXH], n = 0;
    if (!sysdll_init()) return 1;
    WSAStartup(MAKEWORD(2, 2), &w);
    for (int i = 1; i < argc && n < MAXH; i++) h[n++] = argv[i];
    probe_flush_dns_cache();
    for (int i = 0; i < n; i++) {
        ADDRINFOW hints, *res = NULL;
        wchar_t ip[64] = L"-";
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        if (GetAddrInfoW(h[i], L"443", &hints, &res) == 0 && res) {
            DWORD len = 64;
            WSAAddressToStringW(res->ai_addr, (DWORD)res->ai_addrlen, NULL, ip, &len);
            FreeAddrInfoW(res);
        }
        printf("dns   %-34ls %ls\n", h[i], ip);
    }
    probe_https_many(h, n, 6000, r, ms);
    for (int i = 0; i < n; i++)
        printf("https %-34ls %s %d ms\n", h[i], r[i] ? "OK" : "BLOCKED/fail", ms[i]);
    return 0;
}
