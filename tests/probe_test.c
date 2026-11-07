/* Manual connectivity check of the auto-detector's probes (no admin needed). */
#include <winsock2.h>
#include <stdio.h>
#include "probe.h"
#include "presets.h"
int main(void) {
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
    for (int i = 0; i < DNS_SERVER_COUNT; i++)
        printf("dns %s:%u -> %s\n", DNS_SERVERS[i].v4, DNS_SERVERS[i].port,
               probe_dns(DNS_SERVERS[i].v4, DNS_SERVERS[i].port, "discord.com", 1500) ? "OK" : "fail");
    const wchar_t *h[] = { L"www.google.com", L"www.microsoft.com", L"discord.com", L"www.roblox.com", L"www.wattpad.com" };
    int r[5]; DWORD t = GetTickCount();
    probe_https_many(h, 5, 5000, r, NULL);
    for (int i = 0; i < 5; i++) printf("https %ls -> %s\n", h[i], r[i] ? "OK" : "BLOCKED/fail");
    printf("took %lu ms\n", GetTickCount() - t);
    return 0;
}
