#!/bin/sh
# Build the loot mod's progs.dat, or run the loot generator tests.
#
#   ./build.sh          compile qcsrc/ into progs.dat
#   ./build.sh test     compile and run the headless loot tests
#
# Needs gmqcc (https://github.com/graphitemaster/gmqcc). Set GMQCC and
# QCVM if they are not on your PATH, e.g.
#   GMQCC=~/gmqcc/gmqcc QCVM=~/gmqcc/qcvm ./build.sh test
set -e

cd "$(dirname "$0")"
GMQCC="${GMQCC:-gmqcc}"
QCVM="${QCVM:-qcvm}"

if [ "$1" = "test" ]; then
    cd tests
    "$GMQCC" -std=qcc -Werror -Wno-unused-variable > /dev/null
    status=0
    # qcvm stops a run after 1,000,000 jumps, so each group runs on its own
    for group in 0 1 2 3 4 5 6 7 8 9 10 11; do
        out="$("$QCVM" loot_test.dat -float "$group" 2>&1)" || status=1
        echo "$out"
        echo "$out" | grep -q "RESULT: PASS" || status=1
    done
    if [ "$status" -ne 0 ]; then
        echo "=== SOME TESTS FAILED ==="
    else
        echo "=== ALL TESTS PASSED ==="
    fi
    exit "$status"
fi

cd qcsrc
"$GMQCC" -std=qcc
echo "Built mod/progs.dat"
