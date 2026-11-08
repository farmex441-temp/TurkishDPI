/* Manual connectivity / latency check of the auto-detector's probes (no admin needed). */
#include <winsock2.h>
#include <stdio.h>
#include "probe.h"
#include "presets.h"
#include "sysdll.h"

int main(void) {
    WSADATA w;
    const wchar_t *h[] = { L"www.google.com", L"www.microsoft.com", L"discord.com",
                           L"www.roblox.com", L"www.wattpad.com" };
    int r[5], ms[5];
    char blk[4][16];
    int nb;
    DWORD t;

    if (!sysdll_init()) return 1;
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

    /* site diagnosis (what "Test et" reports when a site does not open) */
    {
        static const wchar_t *dh[] = { L"discord.com", L"www.wattpad.com", L"no-such-host.invalid" };
        for (int i = 0; i < 3; i++) {
            diag_facts_t f;
            probe_collect_facts(dh[i], DNS_SERVERS[0].v4, DNS_SERVERS[0].port,
                                (const char (*)[16])blk, nb, 4000, &f);
            printf("diag %-22ls -> %-10s (sys dns %d/%d forged, clean %d, tcp %d/%d, https %d)\n",
                   dh[i], diag_code(diag_classify(&f)), f.sys_bogus, f.sys_n, f.clean_n,
                   f.tcp_ok, f.tcp_tried, f.https_ok);
        }
    }
    return 0;
}
