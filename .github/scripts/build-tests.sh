#!/usr/bin/env bash
# Builds the unit tests with one compiler into one folder.
#   bash .github/scripts/build-tests.sh <compiler> <outdir> [extra compiler flags...]
# Used by the Windows "test" job (native gcc), by the Ubuntu "build" job
# (x86_64 and i686 MinGW cross compilers, -static) and by the Ubuntu sanitizer
# job (native gcc with -fsanitize), so every test is built the same way
# everywhere. To add a test, add one line to TESTS:
#
#   <exe name>|<sources>|<flags>|<libs>
#
#   sources  files after the test's own .c file (space separated)
#   flags    per-test compiler flags: -I..., -D... (may be empty)
#   libs     OPTIONAL fourth column, linked after the sources:
#              missing  -> the default libs (-lws2_32; TEST_LIBS overrides it)
#              "-"      -> no libs at all
#              other    -> exactly those, e.g. "-ladvapi32 -lshell32 -lole32 -luuid"
#   Example (a test that needs a define and Windows libs, not ws2_32):
#     update_install_test|tests/update_install_test.c src/app/updpkg.c|-DUPD_TEST -Isrc/app -Isrc/engine -Itests|-ladvapi32 -lshell32 -lole32 -luuid
#
# Environment:
#   WARN_FLAGS  warning policy (the workflow sets it; default below = the exe's)
#   TEST_ONLY   space separated exe names: build only those (the Linux
#               sanitizer job cannot build tests that include Windows headers)
#   TEST_LIBS   default libs for lines without a fourth column (set it empty
#               on Linux); TEST_EXT  output suffix (default .exe, empty on Linux)
#
# Tests that need special set-up (windivert_hash_test.c) or are manual tools
# (probe_test.c, hosts_test.c, uia_probe.c) are deliberately not in this list.
set -euo pipefail

CC="$1"; OUT="$2"; shift 2
CORE="src/engine/core/proto.c src/engine/core/tcpdpi.c src/engine/core/udpdpi.c src/engine/core/rawscope.c"
WARN="${WARN_FLAGS:--Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers}"
DEFLIBS="${TEST_LIBS--lws2_32}"
EXT="${TEST_EXT-.exe}"
ONLY="${TEST_ONLY:-}"

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
pieces_test|tests/pieces_test.c src/engine/core/tcpdpi.c src/engine/core/proto.c|-Isrc/engine/core|-
policy_test|tests/policy_test.c src/engine/policy.c src/engine/core/udpdpi.c|-Isrc/engine -Isrc/engine/core|-
desync_test|tests/desync_test.c src/engine/desync.c src/engine/core/tcpdpi.c src/engine/core/proto.c|-Isrc/engine -Isrc/engine/core|-
"

mkdir -p "$OUT"
built=0
while IFS='|' read -r name srcs flags libs; do
  [ -n "$name" ] || continue
  if [ -n "$ONLY" ]; then
    case " $ONLY " in *" $name "*) ;; *) continue ;; esac
  fi
  if [ -z "${libs+x}" ] || [ -z "$libs" ]; then libs="$DEFLIBS"; fi
  [ "$libs" = "-" ] && libs=""
  echo "== $name"
  # word splitting of the source, flag and library lists is intended
  # shellcheck disable=SC2086
  "$CC" -O2 $WARN $flags "$@" $srcs $libs -o "$OUT/$name$EXT"
  built=$((built + 1))
done <<< "$TESTS"
echo "built $built test(s) into $OUT"
if [ -n "$ONLY" ]; then
  for n in $ONLY; do
    case "$TESTS" in *"
$n|"*) ;; *) echo "TEST_ONLY names an unknown test: $n" >&2; exit 1 ;; esac
  done
fi
[ "$built" -gt 0 ]
