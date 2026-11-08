/* Tests the site-diagnosis classifier in src/app/diag.c (pure logic). */
#include <stdio.h>
#include "diag.h"

static int fails;

static void expect(int sys_n, int sys_bogus, int clean_n, int tried, int tcp, int https, diag_t want) {
    diag_facts_t f = { sys_n, sys_bogus, clean_n, tried, tcp, https };
    diag_t got = diag_classify(&f);
    if (got != want) {
        printf("FAIL sys=%d/%d clean=%d tcp=%d/%d https=%d -> %s (want %s)\n",
               sys_n, sys_bogus, clean_n, tcp, tried, https, diag_code(got), diag_code(want));
        fails++;
    }
}

int main(void) {
    expect(2, 0, 2, 0, 0, 1, DIAG_OK);           /* opens, nothing else matters      */
    expect(1, 1, 1, 0, 0, 1, DIAG_OK);           /* opens thanks to TurkishDPI       */
    expect(0, 0, 0, 0, 0, 0, DIAG_NO_DNS);       /* nobody knows the name            */
    expect(1, 1, 2, 2, 0, 0, DIAG_IP_BLOCK);     /* forged DNS and the real IP dead  */
    expect(2, 0, 2, 2, 0, 0, DIAG_IP_BLOCK);     /* real IPs, no TCP                 */
    expect(1, 1, 2, 1, 1, 0, DIAG_DNS_POISON);   /* block page from ISP, TCP fine    */
    expect(0, 0, 2, 1, 1, 0, DIAG_DNS_POISON);   /* ISP returns nothing              */
    expect(2, 0, 2, 1, 1, 0, DIAG_TLS_BLOCK);    /* DNS fine, TCP fine, TLS cut: SNI */
    expect(2, 1, 2, 1, 1, 0, DIAG_TLS_BLOCK);    /* one bogus among real answers     */
    expect(2, 0, 0, 0, 0, 0, DIAG_TLS_BLOCK);    /* nothing tried: no IP verdict     */
    if (!diag_bypassable(DIAG_TLS_BLOCK) || !diag_bypassable(DIAG_DNS_POISON) ||
        diag_bypassable(DIAG_IP_BLOCK) || diag_bypassable(DIAG_NO_DNS) || diag_bypassable(DIAG_OK)) {
        printf("FAIL diag_bypassable\n");
        fails++;
    }
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
