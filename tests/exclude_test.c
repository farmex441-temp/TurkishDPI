/* Checks the payment/bank "never touch" matcher. */
#include <stdio.h>
#include <string.h>
#include "exclude.h"
static int fails;
static void expect(const char *h, int want) {
    int got = exclude_check_hostname(h, strlen(h));
    if (got != want) { printf("FAIL %s -> %d (want %d)\n", h, got, want); fails++; }
}
int main(void) {
    exclude_load_builtin();
    expect("www.paypal.com", 1);
    expect("paypal.com", 1);
    expect("sanalpos.garanti.com.tr", 1);
    expect("ISBANK.com.tr", 1);
    expect("www.iyzico.com:443", 1);
    expect("js.stripe.com", 1);
    expect("pay.google.com", 1);
    expect("www.google.com", 0);
    expect("discord.com", 0);
    expect("notpaypal.com", 0);
    expect("paypal.com.evil.net", 0);
    printf(fails ? "%d failures\n" : "all passed\n", fails);
    return fails != 0;
}
