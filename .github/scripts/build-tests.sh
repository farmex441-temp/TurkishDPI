#!/usr/bin/env bash
# Builds the unit tests with one compiler into one folder.
#   bash .github/scripts/build-tests.sh <compiler> <outdir> [extra compiler flags...]
# Used by the Windows "test" job (native gcc) and by the Ubuntu "build" job
# (x86_64 and i686 MinGW cross compilers, -static), so every test is built
# the same way everywhere. To add a test, add one line to TESTS:
#   <exe name>|<sources>|<include flags>
# Tests that need special set-up (windivert_hash_test.c) or are manual tools
# (probe_test.c, hosts_test.c, uia_probe.c) are deliberately not in this list.
set -euo pipefail

CC="$1"; OUT="$2"; shift 2
CORE="src/engine/core/proto.c src/engine/core/tcpdpi.c src/engine/core/udpdpi.c src/engine/core/rawscope.c"
# Same warning policy as the exe's Makefile (-Wextra with these three off).
WARN="-Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"

TESTS="
exclude_test|tests/exclude_test.c src/engine/exclude.c|-Isrc/engine
dns_test|tests/dns_test.c src/engine/dns.c|-Isrc/engine
diag_test|tests/diag_test.c src/app/diag.c|-Isrc/app
updpkg_test|tests/updpkg_test.c src/app/updpkg.c|-Isrc/app -Itests
test_proto|tests/core/test_proto.c $CORE|-Isrc/engine/core
test_tcpdpi|tests/core/test_tcpdpi.c $CORE|-Isrc/engine/core
test_udpdpi|tests/core/test_udpdpi.c $CORE|-Isrc/engine/core
test_rawscope|tests/core/test_rawscope.c $CORE|-Isrc/engine/core
xlate_test|tests/xlate_test.c src/engine/core/tcpdpi.c|-Isrc/engine/core
"

mkdir -p "$OUT"
while IFS='|' read -r name srcs flags; do
  [ -n "$name" ] || continue
  echo "== $name"
  # word splitting of the source and flag lists is intended
  # shellcheck disable=SC2086
  "$CC" -O2 $WARN $flags "$@" $srcs -lws2_32 -o "$OUT/$name.exe"
done <<< "$TESTS"
