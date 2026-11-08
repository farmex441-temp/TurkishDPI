/* TurkishDPI: the TLS-record-split sequence translation through a connection's
 * close and on four-tuple reuse (tcpdpi_xlate_* with tcpdpi_xlate_fin). */
#include <stdio.h>
#include <stdint.h>
#include "tcpdpi.h"

static int fails;
#define EXPECT(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } } while (0)

int main(void) {
    const uint32_t sa = 0x0A000001, da = 0x5DB8D822;
    const uint16_t sp = 50000, dp = 443;
    const uint64_t T = 1000000;
    uint32_t s, a;

    tcpdpi_xlate_reset();
    EXPECT(tcpdpi_xlate_begin(sa, da, sp, dp, 1000, 5, T), "begin");

    /* client closes first: its FIN is shifted, and so must be everything after */
    tcpdpi_xlate_fin(sa, da, sp, dp, 0, T + 10);
    s = 3000; EXPECT(tcpdpi_xlate_out(sa, da, sp, dp, &s, T + 10) && s == 3005, "FIN shifted");
    a = 3006; EXPECT(tcpdpi_xlate_in(da, sa, dp, sp, &a, T + 20) && a == 3001, "ACK of FIN brought back");
    tcpdpi_xlate_fin(sa, da, sp, dp, 1, T + 20);                  /* the server's FIN */
    s = 3001; EXPECT(tcpdpi_xlate_out(sa, da, sp, dp, &s, T + 30) && s == 3006, "last ACK still shifted");

    /* retired only after the grace period: a later begin reclaims the slot */
    EXPECT(tcpdpi_xlate_live(T + 40) == 1, "still remembered after both FINs");
    EXPECT(tcpdpi_xlate_begin(0x0A000001, 0x01010101, 1, 443, 7, 5, T + 20 + TCPDPI_XLATE_CLOSE_MS + 1),
           "other connection begins");
    s = 3001; EXPECT(!tcpdpi_xlate_out(sa, da, sp, dp, &s, T + 70000) && s == 3001, "closed one forgotten");

    /* a reset ends it at once; a new SYN on the same tuple clears it */
    EXPECT(tcpdpi_xlate_begin(sa, da, sp, dp, 1000, 5, T), "begin again");
    tcpdpi_xlate_end(sa, da, sp, dp);
    s = 5000; EXPECT(!tcpdpi_xlate_out(sa, da, sp, dp, &s, T) && s == 5000, "reset ends it");

    /* many entries, one removed from the middle: the others are still found */
    tcpdpi_xlate_reset();
    for (uint16_t i = 0; i < 50; i++) EXPECT(tcpdpi_xlate_begin(sa, da, (uint16_t)(1000 + i), dp, 100, 5, T), "fill");
    tcpdpi_xlate_end(sa, da, 1025, dp);
    s = 200; EXPECT(tcpdpi_xlate_out(sa, da, 1049, dp, &s, T) && s == 205, "last entry found");
    s = 200; EXPECT(tcpdpi_xlate_out(sa, da, 1000, dp, &s, T) && s == 205, "first entry found");
    s = 200; EXPECT(!tcpdpi_xlate_out(sa, da, 1025, dp, &s, T), "removed entry gone");
    EXPECT(tcpdpi_xlate_live(T) == 49, "49 live");
    EXPECT(tcpdpi_xlate_begin(sa, da, 2000, dp, 100, 5, T), "slot reused");
    EXPECT(tcpdpi_xlate_live(T) == 50, "50 live");

    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
