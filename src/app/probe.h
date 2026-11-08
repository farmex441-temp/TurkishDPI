#ifndef TURKISHDPI_PROBE_H
#define TURKISHDPI_PROBE_H
#include <windows.h>
#include "diag.h"

#define PROBE_MAX_HOSTS 8

/* Parallel HTTPS reachability test. Returns how many hosts answered. */
/* results[i]=1/0, lat_ms[i]=ms or -1 (both optional). */
int  probe_https_many(const wchar_t **hosts, int count, DWORD timeout_ms, int *results, int *lat_ms);
int  probe_https(const wchar_t *host, DWORD timeout_ms);
/* 1 if server_v4:port answers an A query for name. Needs WSAStartup. */
int  probe_dns(const char *server_v4, unsigned short port, const char *name, DWORD timeout_ms);
/* Round-trip time in ms of a clean answer, -1 if none; optionally returns its A records. */
int  probe_dns_ms(const char *server_v4, unsigned short port, const char *name, DWORD timeout_ms,
                  unsigned long *addrs, int *naddrs);
/* Block-page IPv4 addresses the network's resolver answers with (see probe.c). */
int  probe_learn_block_ips(const char *clean_v4, unsigned short clean_port, char out[][16], int max);
void probe_flush_dns_cache(void);
/* Facts for diag_classify about one host: the network's and the clean
 * resolver's answers (block_ips: learned block pages), TCP to port 443 of the
 * real addresses, and a full HTTPS request. */
void probe_collect_facts(const wchar_t *host, const char *clean_v4, unsigned short clean_port,
                         const char (*block_ips)[16], int nblock, DWORD timeout_ms,
                         diag_facts_t *out);

#endif
