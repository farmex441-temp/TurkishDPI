/* Manual connectivity / latency check of the auto-detector's probes (no admin needed). */
#include <winsock2.h>
#include <stdio.h>
#include "probe.h"
#include "presets.h"

int main(void) {
    WSADATA w;
    const wchar_t *h[] = { L"www.google.com", L"www.microsoft.com", L"discord.com",
                           L"www.roblox.com", L"www.wattpad.com" };
    int r[5], ms[5];
    char blk[4][16];
    int nb;
    DWORD t;

    WSAStartup(MAKEWORD(2, 2), &w);
    for (int i = 0; i < DNS_SERVER_COUNT; i++) {
        int best = 1 << 30;
        for (int k = 0; k < 3; k++) {          /* best of three = RTT without noise */
            int m = probe_dns_ms(DNS_SERVERS[i].v4, DNS_SERVERS[i].port, "www.google.com", 1500, NULL, NULL);
            if (m >= 0 && m < best) best = m;
        }
        printf("dns %-15s:%-5u -> %s", DNS_SERVERS[i].v4, DNS_SERVERS[i].port, best < (1 << 30) ? "OK" : "fail");
        if (best < (1 << 30)) printf(" %d ms", best);
        printf("\n");
    }
    nb = probe_learn_block_ips(DNS_SERVERS[0].v4, DNS_SERVERS[0].port, blk, 4);
    printf("learned block-page IPs: %d", nb);
    for (int i = 0; i < nb; i++) printf(" %s", blk[i]);
    printf("\n");

    probe_flush_dns_cache();
    t = GetTickCount();
    probe_https_many(h, 5, 5000, r, ms);
    for (int i = 0; i < 5; i++)
        printf("https %-18ls -> %s %d ms (TCP+TLS)\n", h[i], r[i] ? "OK" : "BLOCKED/fail", ms[i]);
    printf("took %lu ms\n", GetTickCount() - t);
    return 0;
}
