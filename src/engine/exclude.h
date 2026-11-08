#ifndef TURKISHDPI_EXCLUDE_H
#define TURKISHDPI_EXCLUDE_H
#include <stddef.h>

/* Domains whose traffic is never modified (payments, banks, 3-D Secure). */
int exclude_add(const char *host);
int exclude_load_builtin(void);
int exclude_load_list(const char *filename);   /* returns added count, -1 on error */
int exclude_enabled(void);
int exclude_check_hostname(const char *host_addr, size_t host_len);
/* Learns the addresses of excluded names from DNS answers seen on the wire,
 * so a connection is left alone even when its ClientHello spans several TCP
 * segments and the SNI is not in the first one. */
void exclude_learn_dns(const char *data, unsigned int len);
int  exclude_check_ip(const void *addr, int is_ipv6);   /* network byte order */

#endif
