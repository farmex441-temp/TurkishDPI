/*
 * TurkishDPI - why does a site not open? (platform independent, no system calls)
 *
 * The probes (probe_collect_facts in probe.c) gather plain facts about one
 * host; diag_classify turns them into a verdict. Kept free of Win32 so it can
 * be unit tested anywhere (tests/diag_test.c) and reused on other platforms.
 */
#ifndef TURKISHDPI_DIAG_H
#define TURKISHDPI_DIAG_H

typedef struct {
    int sys_n;       /* A records from the network's own resolver            */
    int sys_bogus;   /* ...of which block-page / sinkhole / private addresses */
    int clean_n;     /* A records from the clean resolver                     */
    int tcp_tried;   /* addresses a TCP connection to port 443 was tried on   */
    int tcp_ok;      /* 1 if any of them accepted the connection              */
    int https_ok;    /* full HTTPS request (TLS handshake + answer) worked     */
} diag_facts_t;

typedef enum {
    DIAG_OK,          /* opens                                                    */
    DIAG_NO_DNS,      /* no resolver knows the name: typo or site gone            */
    DIAG_DNS_POISON,  /* the network's resolver lies; TCP works: DPI tools help   */
    DIAG_TLS_BLOCK,   /* TCP works, TLS is cut (SNI filtering): DPI tools help    */
    DIAG_IP_BLOCK     /* no TCP connection at all while the internet works:
                         an IP-level block or the server is down - DPI cannot help */
} diag_t;

diag_t diag_classify(const diag_facts_t *f);
/* 1 if changing the DPI strategy can possibly fix this verdict. */
int    diag_bypassable(diag_t d);
const char *diag_code(diag_t d);   /* stable short name for the log */

#endif
