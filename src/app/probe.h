#ifndef TURKISHDPI_PROBE_H
#define TURKISHDPI_PROBE_H
#include <windows.h>

#define PROBE_MAX_HOSTS 8

/* Parallel HTTPS reachability test. Returns how many hosts answered. */
/* results[i]=1/0, lat_ms[i]=ms or -1 (both optional). */
int  probe_https_many(const wchar_t **hosts, int count, DWORD timeout_ms, int *results, int *lat_ms);
int  probe_https(const wchar_t *host, DWORD timeout_ms);
/* 1 if server_v4:port answers an A query for name. Needs WSAStartup. */
int  probe_dns(const char *server_v4, unsigned short port, const char *name, DWORD timeout_ms);
void probe_flush_dns_cache(void);

#endif
