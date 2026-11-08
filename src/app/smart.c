/*
 * TurkishDPI - adaptive strategy selection ("akıllı motor").
 *
 * Every network the computer joins is identified by its default gateway's
 * MAC address (falls back to the gateway IP). For each network we keep, per
 * preset, how often it was tried, how often it fully worked and an
 * exponentially-weighted average of its TLS handshake time. The next scan on
 * that network tries presets in order of their Laplace-smoothed success rate,
 * then by speed, so a known network is fixed on the first try, while a new
 * network still starts from the hand-tuned default order.
 *
 * Stored in TurkishDPI.ini, section [net_<id>], key pN = trials,ok,avg_ms.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "sysdll.h"
#include "app.h"
#include "presets.h"

typedef struct { int trials, ok, ms; } pstat_t;

static wchar_t g_net_id[40] = L"unknown";
static wchar_t g_net_name[64] = L"Ağ";
static pstat_t g_stats[PRESET_COUNT];

static void section(wchar_t *out, size_t cap) {
    swprintf(out, cap, L"net_%ls", g_net_id);
}

static void load_stats(void) {
    wchar_t sec[64], key[8], val[64];
    section(sec, 64);
    for (int i = 0; i < PRESET_COUNT; i++) {
        swprintf(key, 8, L"e%d", i);   /* "e": the engine-2 presets; old "p" records are ignored */
        GetPrivateProfileStringW(sec, key, L"", val, 64, g_ini);
        g_stats[i].trials = g_stats[i].ok = 0;
        g_stats[i].ms = 0;
        if (val[0])
            swscanf(val, L"%d,%d,%d", &g_stats[i].trials, &g_stats[i].ok, &g_stats[i].ms);
    }
}

static void save_stat(int i) {
    wchar_t sec[64], key[8], val[64];
    section(sec, 64);
    swprintf(key, 8, L"e%d", i);   /* "e": the engine-2 presets; old "p" records are ignored */
    swprintf(val, 64, L"%d,%d,%d", g_stats[i].trials, g_stats[i].ok, g_stats[i].ms);
    WritePrivateProfileStringW(sec, key, val, g_ini);
    WritePrivateProfileStringW(sec, L"Name", g_net_name, g_ini);
}

void smart_network_changed(void) {
    MIB_IPFORWARD_ROW2 route;
    SOCKADDR_INET dst, best_src;
    NET_IFINDEX ifidx = 0;
    wchar_t id[40] = L"";
    wchar_t name[64] = L"Ağ";

    ZeroMemory(&dst, sizeof(dst));
    dst.Ipv4.sin_family = AF_INET;
    dst.Ipv4.sin_addr.s_addr = htonl(0x08080808);   /* any public address */
    if (GetBestRoute2(NULL, 0, NULL, &dst, 0, &route, &best_src) == NO_ERROR) {
        ifidx = route.InterfaceIndex;
        if (route.NextHop.si_family == AF_INET && route.NextHop.Ipv4.sin_addr.s_addr) {
            MIB_IPNET_ROW2 row;
            ZeroMemory(&row, sizeof(row));
            row.Address = route.NextHop;
            row.InterfaceIndex = ifidx;
            if (ResolveIpNetEntry2(&row, NULL) == NO_ERROR && row.PhysicalAddressLength == 6) {
                swprintf(id, 40, L"%02x%02x%02x%02x%02x%02x",
                         row.PhysicalAddress[0], row.PhysicalAddress[1], row.PhysicalAddress[2],
                         row.PhysicalAddress[3], row.PhysicalAddress[4], row.PhysicalAddress[5]);
            } else {
                unsigned long a = ntohl(route.NextHop.Ipv4.sin_addr.s_addr);
                swprintf(id, 40, L"gw%08lx", a);
            }
        }
    }
    if (ifidx) {
        MIB_IF_ROW2 ifr;
        ZeroMemory(&ifr, sizeof(ifr));
        ifr.InterfaceIndex = ifidx;
        if (GetIfEntry2(&ifr) == NO_ERROR && ifr.Alias[0]) {
            wcsncpy(name, ifr.Alias, 63);
            name[63] = 0;
        }
    }
    if (!id[0]) wcscpy(id, L"unknown");

    EnterCriticalSection(&g_lock);
    wcscpy(g_net_id, id);
    wcscpy(g_net_name, name);
    wcscpy(g_ui.network, name);
    LeaveCriticalSection(&g_lock);
    load_stats();
    g_ui.learned = smart_learned_count();
}

const wchar_t *smart_network_id(void) {
    return g_net_id;
}

const wchar_t *smart_network_name(void) {
    return g_net_name;
}

/* Higher is better. Unknown presets get a neutral prior and keep their
 * hand-tuned order through a tiny index penalty. */
static double rank(int i) {
    const pstat_t *s = &g_stats[i];
    double rate = (s->ok + 1.0) / (s->trials + 2.0);
    double speed = (s->ok > 0 && s->ms > 0) ? 0.05 * (300.0 / (300.0 + s->ms)) : 0.0;
    return rate + speed - i * 0.0005;
}

void smart_order(int *order, int n) {
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {           /* insertion sort, n is tiny */
        int v = order[i], j = i - 1;
        while (j >= 0 && rank(order[j]) < rank(v)) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
}

void smart_record(int preset, int ok, int avg_ms) {
    pstat_t *s;
    if (preset < 0 || preset >= PRESET_COUNT) return;
    s = &g_stats[preset];
    /* Cap history so the network can "change its mind" (ISP updates DPI). */
    if (s->trials >= 20) {
        s->trials = s->trials / 2;
        s->ok = s->ok / 2;
    }
    s->trials++;
    if (ok) {
        s->ok++;
        if (avg_ms > 0)
            s->ms = s->ms ? (s->ms * 7 + avg_ms * 3) / 10 : avg_ms;
    }
    save_stat(preset);
    g_ui.learned = smart_learned_count();
}

int smart_known(int preset) {
    return preset >= 0 && preset < PRESET_COUNT && g_stats[preset].trials > 0;
}

int smart_learned_count(void) {
    int n = 0;
    for (int i = 0; i < PRESET_COUNT; i++) n += g_stats[i].trials > 0;
    return n;
}

int smart_best(void) {
    int order[PRESET_COUNT];
    smart_order(order, PRESET_COUNT);
    return g_stats[order[0]].ok > 0 ? order[0] : -1;
}

int smart_get_dns(int def) {
    wchar_t sec[64];
    section(sec, 64);
    return (int)GetPrivateProfileIntW(sec, L"Dns", def, g_ini);
}

void smart_set_dns(int dns) {
    wchar_t sec[64], v[8];
    section(sec, 64);
    swprintf(v, 8, L"%d", dns);
    WritePrivateProfileStringW(sec, L"Dns", v, g_ini);
}
