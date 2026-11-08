/*
 * Checks the WinDivert.dll SHA-256 pinning in src/engine/windivert_dyn.c
 * without touching the WinDivert driver.
 *
 * Build it with -DWINDIVERT_DLL_SHA256="<hex>" next to a copy of any harmless
 * DLL renamed to WinDivert.dll (the test script uses System32\version.dll):
 *   - hash of that copy  -> the DLL is loaded, but it has no WinDivert
 *                           exports, so WinDivertOpen fails with 126;
 *   - any other hash     -> the DLL is never loaded: error 1006.
 * Usage: windivert_hash_test.exe <expected error code>
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "windivert.h"

int main(int argc, char **argv) {
    DWORD want = argc > 1 ? strtoul(argv[1], NULL, 10) : 0, got;
    HANDLE h = WinDivertOpen("false", WINDIVERT_LAYER_NETWORK, 0, 0);
    got = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (h != INVALID_HANDLE_VALUE) WinDivertClose(h);
    if (got != want) {
        printf("FAIL: WinDivertOpen error %lu, expected %lu\n", got, want);
        return 1;
    }
    printf("all passed (error %lu)\n", got);
    return 0;
}
